// Copyright Epic Games, Inc. All Rights Reserved.

#include "Component/Scrape/MD_ScrapeToolComponent.h"

#include "Gameplay/Scrape/MD_ScrapeFocusProjection.h"
#include "Gameplay/Scrape/MD_ScrapeAimValidation.h"
#include "Character/MD_Character.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraTypes.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"

bool UMD_ScrapeToolComponent::TryBeginScrapeFocus()
{
	if (IsScrapeFocusActive())
	{
		return true;
	}

	if (!IsScrapeToolCurrentlyEligible())
	{
		return false;
	}

	// 진입 실패는 기존 긁기와 카메라 상태를 포함해 아무 상태도 바꾸지 않는다.
	const FMD_ScrapeAimQueryResult PreflightAim = QueryCurrentScrapeAim();
	if (!PreflightAim.IsActionable())
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[ScrapeFocus][Local][%s] Begin skipped: no actionable target (Blocked=%s, Component=%s)"),
			*GetNameSafe(GetOwner()),
			*GetNameSafe(PreflightAim.Hit.GetActor()),
			*GetNameSafe(PreflightAim.Hit.GetComponent()));
		return false;
	}
	if (!CaptureScrapeFocusView())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ScrapeFocus][Local][%s] Begin skipped: camera view unavailable"),
			*GetNameSafe(GetOwner()));
		return false;
	}

	if (bIsScraping)
	{
		// 과도기 호환 경로로 시작된 LMB 긁기가 있다면 유효한 Focus가 이를 인계한다.
		StopScraping();
	}

	CurrentScrapeAim = PreflightAim;
	ResetInvalidScrapeAim();
	++ScrapeFocusSession.ScrapeFocusGeneration;
	if (ScrapeFocusSession.ScrapeFocusGeneration == 0)
	{
		// 0은 아직 Focus를 시작하지 않은 상태로 예약한다.
		++ScrapeFocusSession.ScrapeFocusGeneration;
	}

	SetScrapeFocusState(EMD_ScrapeFocusState::FocusPending);
	ScrapeFocusSession.FocusPendingElapsedSeconds = 0.0;
	AuthoritativeAim.LocalAimSendAccumulatorSeconds = 0.0;
	ScrapeFocusSession.LocalFocusMonotonicStartSeconds = GetScrapeMonotonicSeconds();
	AuthoritativeAim.NextLocalAimSequence = 0;
	const uint64 OwnerIdentity = GetOwner()
		? static_cast<uint64>(GetOwner()->GetUniqueID())
		: 1ull;
	ScrapeFocusSession.ActiveScrapeFocusSessionId =
		(FPlatformTime::Cycles64() << 1)
		^ (OwnerIdentity << 32)
		^ static_cast<uint64>(ScrapeFocusSession.ScrapeFocusGeneration);
	if (ScrapeFocusSession.ActiveScrapeFocusSessionId == 0)
	{
		ScrapeFocusSession.ActiveScrapeFocusSessionId = 1;
	}
	ScrapeFocusSession.ScrapeFocusAspectRatio = ScrapeFocusViewportSize.Y > 0
		? static_cast<float>(ScrapeFocusViewportSize.X)
			/ static_cast<float>(ScrapeFocusViewportSize.Y)
		: 16.0f / 9.0f;
	SetComponentTickEnabled(true);
	ServerBeginScrapeFocus(
		ScrapeFocusSession.ActiveScrapeFocusSessionId,
		AuthoritativeAim.NextLocalAimSequence,
		ScrapeFocusViewRotation,
		ScrapeFocusViewFOV,
		ScrapeFocusSession.ScrapeFocusAspectRatio,
		ScrapeFocusSession.ScrapeFocusHorizontalHalfTangent,
		ScrapeFocusSession.ScrapeFocusVerticalHalfTangent);
	return true;
}

bool UMD_ScrapeToolComponent::IsScrapeFocusBeginPayloadValid(
	const uint64 FocusSessionId,
	const FRotator& RequestedViewRotation,
	const float RequestedFOV,
	const float RequestedAspectRatio,
	const float RequestedHorizontalHalfTangent,
	const float RequestedVerticalHalfTangent) const
{
	if (FocusSessionId != 0
		&& !RequestedViewRotation.ContainsNaN()
		&& FMath::IsFinite(RequestedFOV)
		&& FMath::IsFinite(RequestedAspectRatio)
		&& FMath::IsFinite(RequestedHorizontalHalfTangent)
		&& FMath::IsFinite(RequestedVerticalHalfTangent)
		&& RequestedFOV > UE_SMALL_NUMBER
		&& RequestedHorizontalHalfTangent > UE_SMALL_NUMBER
		&& RequestedVerticalHalfTangent > UE_SMALL_NUMBER
		&& FMD_ScrapeFocusProjection::IsSupportedAspectRatio(RequestedAspectRatio))
	{
		return true;
	}

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[ScrapeFocus][Server][%s] Invalid Begin payload: Session=%llu RotationNaN=%d FOV=%.3f Aspect=%.4f HalfTangents=(%.4f, %.4f) SupportedAspect=(%.4f, %.4f)"),
		*GetNameSafe(GetOwner()),
		static_cast<unsigned long long>(FocusSessionId),
		RequestedViewRotation.ContainsNaN() ? 1 : 0,
		RequestedFOV,
		RequestedAspectRatio,
		RequestedHorizontalHalfTangent,
		RequestedVerticalHalfTangent,
		FMD_ScrapeFocusProjection::MinimumSupportedAspectRatio,
		FMD_ScrapeFocusProjection::MaximumSupportedAspectRatio);
	return false;
}

FMD_ScrapeFocusBeginApproval UMD_ScrapeToolComponent::EvaluateScrapeFocusBeginRequest(
	const FMinimalViewInfo& ServerViewInfo,
	const FRotator& RequestedViewRotation,
	const float RequestedFOV,
	const float RequestedAspectRatio,
	const float RequestedHorizontalHalfTangent,
	const float RequestedVerticalHalfTangent,
	const bool bIsSameAcceptedSession) const
{
	const FVector ServerEyeLocation = ServerViewInfo.Location;
	const FRotator ServerViewRotation = ServerViewInfo.Rotation;
	const float ServerFOV = ServerViewInfo.FOV;

	FMD_ScrapeFocusBeginApproval Approval;
	Approval.ApprovedViewRotation = RequestedViewRotation.GetNormalized();

	const ULocalPlayer* const LocalPlayerDefaults = GetDefault<ULocalPlayer>();
	const EAspectRatioAxisConstraint DefaultAxisConstraint =
		LocalPlayerDefaults
			? static_cast<EAspectRatioAxisConstraint>(
				LocalPlayerDefaults->AspectRatioAxisConstraint.GetValue())
			: AspectRatio_MaintainYFOV;
	FMinimalViewInfo RequestedProjectionView = ServerViewInfo;
	RequestedProjectionView.FOV = RequestedFOV;
	const bool bProjectionContractMatches =
		FMD_ScrapeFocusProjection::TryCalculateHalfTangents(
			RequestedProjectionView,
			DefaultAxisConstraint,
			RequestedAspectRatio,
			IsLegacyMaintainYFOVProjectionEnabled(),
			Approval.RequestedProjectionHorizontalHalfTangent,
			Approval.RequestedProjectionVerticalHalfTangent)
		&& FMath::Abs(RequestedHorizontalHalfTangent
				- Approval.RequestedProjectionHorizontalHalfTangent)
			<= ProjectionHalfTangentTolerance
		&& FMath::Abs(RequestedVerticalHalfTangent
				- Approval.RequestedProjectionVerticalHalfTangent)
			<= ProjectionHalfTangentTolerance;

	float LiveHorizontalHalfTangent = 0.0f;
	float LiveVerticalHalfTangent = 0.0f;
	FMinimalViewInfo LiveProjectionView = ServerViewInfo;
	LiveProjectionView.FOV = ServerFOV;
	float AuthorityPolicyHorizontalHalfTangent = 0.0f;
	float AuthorityPolicyVerticalHalfTangent = 0.0f;
	const bool bAuthorityProjectionPolicyValid =
		FMD_ScrapeFocusProjection::TryCalculateHalfTangents(
			LiveProjectionView,
			DefaultAxisConstraint,
			AuthoritativeFocusAspectRatio,
			IsLegacyMaintainYFOVProjectionEnabled(),
			AuthorityPolicyHorizontalHalfTangent,
			AuthorityPolicyVerticalHalfTangent);
	// The requested projection remains the visual/reticle contract. Only the
	// separate authority cone is clamped, so a remote (or dedicated) server never
	// grants additional aim authority from a client-claimed viewport aspect.
	Approval.ApprovedAuthorityHorizontalHalfTangent = FMath::Min(
		Approval.RequestedProjectionHorizontalHalfTangent,
		AuthorityPolicyHorizontalHalfTangent);
	Approval.ApprovedAuthorityVerticalHalfTangent = FMath::Min(
		Approval.RequestedProjectionVerticalHalfTangent,
		AuthorityPolicyVerticalHalfTangent);
	const bool bLiveProjectionMatchesAgreedSession =
		!bIsSameAcceptedSession
		|| (FMD_ScrapeFocusProjection::TryCalculateHalfTangents(
				LiveProjectionView,
				DefaultAxisConstraint,
				ScrapeFocusSession.ScrapeFocusAspectRatio,
				IsLegacyMaintainYFOVProjectionEnabled(),
				LiveHorizontalHalfTangent,
				LiveVerticalHalfTangent)
			&& FMath::Abs(LiveHorizontalHalfTangent
					- ScrapeFocusSession.ScrapeFocusHorizontalHalfTangent)
				<= ProjectionHalfTangentTolerance
			&& FMath::Abs(LiveVerticalHalfTangent
					- ScrapeFocusSession.ScrapeFocusVerticalHalfTangent)
				<= ProjectionHalfTangentTolerance);
	const bool bAuthorityProjectionMatchesAgreedSession =
		!bIsSameAcceptedSession
		|| (bAuthorityProjectionPolicyValid
			&& FMath::Abs(FMath::Min(
					ScrapeFocusSession.ScrapeFocusHorizontalHalfTangent,
					AuthorityPolicyHorizontalHalfTangent)
					- ScrapeFocusSession.ScrapeFocusAuthorityHorizontalHalfTangent)
				<= ProjectionHalfTangentTolerance
			&& FMath::Abs(FMath::Min(
					ScrapeFocusSession.ScrapeFocusVerticalHalfTangent,
					AuthorityPolicyVerticalHalfTangent)
					- ScrapeFocusSession.ScrapeFocusAuthorityVerticalHalfTangent)
				<= ProjectionHalfTangentTolerance);
	const bool bRequestedContractMatchesAgreedSession =
		!bIsSameAcceptedSession
		|| (FMath::Abs(RequestedAspectRatio - ScrapeFocusSession.ScrapeFocusAspectRatio)
				<= FMath::Max(ScrapeFocusAspectRatioAbsoluteTolerance, 0.0f)
			&& FMath::Abs(RequestedHorizontalHalfTangent
					- ScrapeFocusSession.ScrapeFocusHorizontalHalfTangent)
				<= ProjectionHalfTangentTolerance
			&& FMath::Abs(RequestedVerticalHalfTangent
					- ScrapeFocusSession.ScrapeFocusVerticalHalfTangent)
				<= ProjectionHalfTangentTolerance);
	const bool bRequestedViewMatchesAgreedSession =
		!bIsSameAcceptedSession
		|| FMD_ScrapeAimValidation::CalculateRotatorErrorDegrees(
			Approval.ApprovedViewRotation,
			ScrapeFocusViewRotation)
			<= FMath::Max(BeginBasisRotationToleranceDegrees, 0.0f);
	const bool bLiveViewMatchesAgreedSession = !bIsSameAcceptedSession
		|| (bHasScrapeFocusView
			&& FVector::Distance(ServerEyeLocation, ScrapeFocusViewOrigin)
				<= FMath::Max(ScrapeFocusViewOriginDriftToleranceCm, 0.0f)
			&& FMath::Abs(ServerFOV - ScrapeFocusViewFOV)
				<= FMath::Max(BeginFOVToleranceDegrees, 0.0f)
			&& bLiveProjectionMatchesAgreedSession);
	Approval.bBasisMatches = !ServerEyeLocation.ContainsNaN()
		&& !ServerViewRotation.ContainsNaN()
		&& FMath::IsFinite(ServerFOV)
		&& bProjectionContractMatches
		&& bAuthorityProjectionPolicyValid
		&& bAuthorityProjectionMatchesAgreedSession
		&& bRequestedContractMatchesAgreedSession
		&& bRequestedViewMatchesAgreedSession
		&& bLiveViewMatchesAgreedSession
		&& FMath::Abs(RequestedFOV - ServerFOV)
			<= FMath::Max(BeginFOVToleranceDegrees, 0.0f);
	return Approval;
}

void UMD_ScrapeToolComponent::AdoptApprovedScrapeFocusSession(
	const uint64 FocusSessionId,
	const uint32 InitialAimSequence,
	AController& OwnerController,
	const FMinimalViewInfo& ServerViewInfo,
	const float RequestedAspectRatio,
	const FMD_ScrapeFocusBeginApproval& Approval,
	const FMD_ScrapeAimQueryResult& Preflight)
{
	// 새 Begin은 기존 accepted session과 그 경로를 먼저 멱등 정리한다.
	if (ScrapeFocusSession.bAuthoritativeScrapeFocusAccepted
		&& ScrapeFocusSession.ActiveScrapeFocusSessionId != FocusSessionId)
	{
		if (AMD_Character* const Character = Cast<AMD_Character>(GetOwner()))
		{
			Character->CancelScrapeAbilityForSession(
				ScrapeFocusSession.ActiveScrapeFocusSessionId);
		}
		StopScraping();
		ResetStrokeTracking();
	}

	ScrapeFocusSession.ActiveScrapeFocusSessionId = FocusSessionId;
	AuthoritativeAim.LastAuthoritativeAimSequence = InitialAimSequence;
	AuthoritativeAim.NextLocalAimSequence = InitialAimSequence;
	ScrapeFocusSession.bAuthoritativeScrapeFocusAccepted = true;
	AuthoritativeAim.bAuthoritativeClientClockInitialized = false;
	AuthoritativeAim.bAuthoritativeFirstScrapeAimApproved = false;
	AuthoritativeAim.AuthoritativeFirstAimSessionId = 0;
	AuthoritativeAim.AuthoritativeFirstAimSequence = 0;
	AuthoritativeAim.AuthoritativeFirstAimClientSampleSeconds = 0.0;
	AuthoritativeAim.BufferedPreFirstAuthoritativeAims.Reset();
	AuthoritativeAim.HighestDroppedPreFirstAimSequence = 0;
	AuthoritativeAim.bBufferedPreFirstAimReplayDiscontinuous = false;
	AuthoritativeAim.AuthoritativeAimRateTokens =
		FMD_ScrapeAuthoritativeAimState::BurstCapacity;
	AuthoritativeAim.LastAuthoritativeAimRateRefillSeconds =
		GetScrapeMonotonicSeconds();
	// The owning client already controls this rotation through CharacterMovement.
	// The Reliable Begin carries the newest sample and can legitimately arrive
	// before its Unreliable movement packet. Adopt it so remote clients use the
	// same frozen Focus basis as a listen host, while origin/FOV/trace remain
	// server authoritative.
	OwnerController.SetControlRotation(Approval.ApprovedViewRotation);
	ScrapeFocusViewOrigin = ServerViewInfo.Location;
	ScrapeFocusViewRotation = Approval.ApprovedViewRotation;
	ScrapeFocusViewFOV = ServerViewInfo.FOV;
	ScrapeFocusSession.ScrapeFocusAspectRatio = RequestedAspectRatio;
	ScrapeFocusSession.ScrapeFocusHorizontalHalfTangent =
		Approval.RequestedProjectionHorizontalHalfTangent;
	ScrapeFocusSession.ScrapeFocusVerticalHalfTangent =
		Approval.RequestedProjectionVerticalHalfTangent;
	ScrapeFocusSession.ScrapeFocusAuthorityHorizontalHalfTangent =
		Approval.ApprovedAuthorityHorizontalHalfTangent;
	ScrapeFocusSession.ScrapeFocusAuthorityVerticalHalfTangent =
		Approval.ApprovedAuthorityVerticalHalfTangent;
	bHasScrapeFocusView = true;
	CurrentScrapeAim = Preflight;
	AuthoritativeAim.LastAuthoritativeAimReceiveSeconds =
		AuthoritativeAim.LastAuthoritativeAimRateRefillSeconds;
	ContaminationPaint.Stage3FocusSessionIdentityOverride = FocusSessionId;
	SetScrapeFocusState(EMD_ScrapeFocusState::FocusedReady);
	SetComponentTickEnabled(true);
	ProcessReadyScrapeAimResult(Preflight, 0.0f);
	ClientScrapeFocusBeginResult(
		FocusSessionId,
		EMD_ScrapeFocusBeginResult::Accepted,
		Approval.ApprovedViewRotation,
		ServerViewInfo.FOV,
		Approval.ApprovedAuthorityHorizontalHalfTangent,
		Approval.ApprovedAuthorityVerticalHalfTangent);
}

void UMD_ScrapeToolComponent::RejectScrapeFocusBegin(
	const uint64 FocusSessionId,
	const bool bIsSameAcceptedSession,
	const EMD_ScrapeFocusTerminationReason SameSessionReason,
	const FRotator& ReplyViewRotation,
	const float ReplyFOV)
{
	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[ScrapeFocus][Server][%s] Begin rejected (Session=%llu, Reason=%d, Existing=%d)"),
		*GetNameSafe(GetOwner()),
		static_cast<unsigned long long>(FocusSessionId),
		static_cast<int32>(SameSessionReason),
		bIsSameAcceptedSession ? 1 : 0);
	// 이미 승인한 session이 같은 이유로 거절되면 세션 자체를 정리한다.
	if (bIsSameAcceptedSession)
	{
		TerminateAuthoritativeScrapeFocus(SameSessionReason);
		return;
	}
	ClientScrapeFocusBeginResult(
		FocusSessionId,
		EMD_ScrapeFocusBeginResult::Denied,
		ReplyViewRotation,
		ReplyFOV,
		0.0f,
		0.0f);
}

void UMD_ScrapeToolComponent::ServerBeginScrapeFocus_Implementation(
	const uint64 FocusSessionId,
	const uint32 InitialAimSequence,
	const FRotator RequestedViewRotation,
	const float RequestedFOV,
	const float RequestedAspectRatio,
	const float RequestedHorizontalHalfTangent,
	const float RequestedVerticalHalfTangent)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	const bool bIsSameAcceptedSession =
		ScrapeFocusSession.bAuthoritativeScrapeFocusAccepted
		&& FocusSessionId != 0
		&& FocusSessionId == ScrapeFocusSession.ActiveScrapeFocusSessionId;
	if (!IsScrapeFocusBeginPayloadValid(
			FocusSessionId,
			RequestedViewRotation,
			RequestedFOV,
			RequestedAspectRatio,
			RequestedHorizontalHalfTangent,
			RequestedVerticalHalfTangent))
	{
		RejectScrapeFocusBegin(
			FocusSessionId,
			bIsSameAcceptedSession,
			EMD_ScrapeFocusTerminationReason::ProtocolViolation,
			FRotator::ZeroRotator,
			90.0f);
		return;
	}
	if (!IsScrapeToolCurrentlyEligible())
	{
		RejectScrapeFocusBegin(
			FocusSessionId,
			bIsSameAcceptedSession,
			EMD_ScrapeFocusTerminationReason::EligibilityLost,
			FRotator::ZeroRotator,
			90.0f);
		return;
	}

	APawn* const OwnerPawn = Cast<APawn>(GetOwner());
	AController* const OwnerController = OwnerPawn
		? OwnerPawn->GetController()
		: nullptr;
	FMinimalViewInfo ServerViewInfo;
	if (!TryGetAuthoritativeScrapeView(ServerViewInfo))
	{
		RejectScrapeFocusBegin(
			FocusSessionId,
			bIsSameAcceptedSession,
			EMD_ScrapeFocusTerminationReason::ViewInvalid,
			FRotator::ZeroRotator,
			90.0f);
		return;
	}

	const FMD_ScrapeFocusBeginApproval Approval = EvaluateScrapeFocusBeginRequest(
		ServerViewInfo,
		RequestedViewRotation,
		RequestedFOV,
		RequestedAspectRatio,
		RequestedHorizontalHalfTangent,
		RequestedVerticalHalfTangent,
		bIsSameAcceptedSession);
	const FMD_ScrapeAimQueryResult Preflight = Approval.bBasisMatches
		? QueryScrapeAim(
			ServerViewInfo.Location,
			Approval.ApprovedViewRotation.Vector())
		: FMD_ScrapeAimQueryResult();
	if (!Approval.bBasisMatches || !Preflight.IsActionable())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ScrapeFocus][Server][%s] Begin denied: Basis=%d Target=%d RotationError=%.2f FOV(client/server)=%.2f/%.2f Aspect=%.4f"),
			*GetNameSafe(GetOwner()),
			Approval.bBasisMatches ? 1 : 0,
			Preflight.IsActionable() ? 1 : 0,
			FMD_ScrapeAimValidation::CalculateRotatorErrorDegrees(
				Approval.ApprovedViewRotation,
				ServerViewInfo.Rotation),
			RequestedFOV,
			ServerViewInfo.FOV,
			RequestedAspectRatio);
		RejectScrapeFocusBegin(
			FocusSessionId,
			bIsSameAcceptedSession,
			EMD_ScrapeFocusTerminationReason::ViewInvalid,
			ServerViewInfo.Rotation,
			ServerViewInfo.FOV);
		return;
	}

	// 같은 session을 다시 요청받으면 이미 승인한 basis를 그대로 재확인해 준다.
	if (bIsSameAcceptedSession)
	{
		ClientScrapeFocusBeginResult(
			FocusSessionId,
			EMD_ScrapeFocusBeginResult::Accepted,
			ScrapeFocusViewRotation,
			ScrapeFocusViewFOV,
			ScrapeFocusSession.ScrapeFocusAuthorityHorizontalHalfTangent,
			ScrapeFocusSession.ScrapeFocusAuthorityVerticalHalfTangent);
		return;
	}
	if (!OwnerController)
	{
		RejectScrapeFocusBegin(
			FocusSessionId,
			bIsSameAcceptedSession,
			EMD_ScrapeFocusTerminationReason::ViewInvalid,
			ServerViewInfo.Rotation,
			ServerViewInfo.FOV);
		return;
	}

	AdoptApprovedScrapeFocusSession(
		FocusSessionId,
		InitialAimSequence,
		*OwnerController,
		ServerViewInfo,
		RequestedAspectRatio,
		Approval,
		Preflight);
}

void UMD_ScrapeToolComponent::ClientScrapeFocusBeginResult_Implementation(
	const uint64 FocusSessionId,
	const EMD_ScrapeFocusBeginResult Result,
	const FRotator AgreedViewRotation,
	const float AgreedFOV,
	const float AgreedHorizontalHalfTangent,
	const float AgreedVerticalHalfTangent)
{
	if (ScrapeFocusState != EMD_ScrapeFocusState::FocusPending
		|| FocusSessionId == 0
		|| FocusSessionId != ScrapeFocusSession.ActiveScrapeFocusSessionId)
	{
		return;
	}
	if (Result != EMD_ScrapeFocusBeginResult::Accepted
		|| AgreedViewRotation.ContainsNaN()
		|| !FMath::IsFinite(AgreedFOV)
		|| !FMath::IsFinite(AgreedHorizontalHalfTangent)
		|| !FMath::IsFinite(AgreedVerticalHalfTangent)
		|| AgreedHorizontalHalfTangent <= UE_SMALL_NUMBER
		|| AgreedVerticalHalfTangent <= UE_SMALL_NUMBER
		|| AgreedHorizontalHalfTangent
			> ScrapeFocusSession.ScrapeFocusHorizontalHalfTangent
				+ ProjectionHalfTangentTolerance
		|| AgreedVerticalHalfTangent
			> ScrapeFocusSession.ScrapeFocusVerticalHalfTangent
				+ ProjectionHalfTangentTolerance)
	{
		EndScrapeFocusInternal(false);
		return;
	}
	// 서버는 허용 오차 밖 basis를 거부하므로 Ack 값 적용은 가시적 snap을 만들지 않는다.
	ScrapeFocusViewRotation = AgreedViewRotation;
	ScrapeFocusViewFOV = AgreedFOV;
	// Preserve the captured projection for exact reticle-to-ray alignment. The
	// server Ack carries only its equal-or-narrower authority cone.
	ScrapeFocusSession.ScrapeFocusAuthorityHorizontalHalfTangent = FMath::Min(
		AgreedHorizontalHalfTangent,
		ScrapeFocusSession.ScrapeFocusHorizontalHalfTangent);
	ScrapeFocusSession.ScrapeFocusAuthorityVerticalHalfTangent = FMath::Min(
		AgreedVerticalHalfTangent,
		ScrapeFocusSession.ScrapeFocusVerticalHalfTangent);
	ClampScrapeFocusPointerToAuthorityCone();
	ScrapeFocusSession.FocusPendingElapsedSeconds = 0.0;
	AuthoritativeAim.LocalAimSendAccumulatorSeconds = 0.0;
	SetScrapeFocusState(EMD_ScrapeFocusState::FocusedReady);
	RefreshScrapeFocusAim();
}

void UMD_ScrapeToolComponent::ServerEndScrapeFocus_Implementation(
	const uint64 FocusSessionId)
{
	if (!ScrapeFocusSession.bAuthoritativeScrapeFocusAccepted
		|| FocusSessionId == 0
		|| FocusSessionId != ScrapeFocusSession.ActiveScrapeFocusSessionId)
	{
		return;
	}
	if (AMD_Character* const Character = Cast<AMD_Character>(GetOwner()))
	{
		Character->CancelScrapeAbilityForSession(FocusSessionId);
	}
	EndScrapeFocusInternal(false);
}

void UMD_ScrapeToolComponent::ClientTerminateScrapeFocus_Implementation(
	const uint64 FocusSessionId,
	const EMD_ScrapeFocusTerminationReason Reason)
{
	(void)Reason;
	if (FocusSessionId == 0 || FocusSessionId != ScrapeFocusSession.ActiveScrapeFocusSessionId)
	{
		return;
	}
	if (AMD_Character* const Character = Cast<AMD_Character>(GetOwner()))
	{
		Character->CancelScrapeAbilityForSession(FocusSessionId);
	}
	EndScrapeFocusInternal(false);
}

void UMD_ScrapeToolComponent::TerminateAuthoritativeScrapeFocus(
	const EMD_ScrapeFocusTerminationReason Reason)
{
	if (!GetOwner()
		|| !GetOwner()->HasAuthority()
		|| !ScrapeFocusSession.bAuthoritativeScrapeFocusAccepted
		|| ScrapeFocusSession.ActiveScrapeFocusSessionId == 0)
	{
		return;
	}
	const uint64 EndingSessionId = ScrapeFocusSession.ActiveScrapeFocusSessionId;
	// 먼저 적용 불가로 만들고 Ability/경로를 정리한 뒤 종료 통지를 보낸다.
	ScrapeFocusSession.bAuthoritativeScrapeFocusAccepted = false;
	AuthoritativeAim.bAuthoritativeFirstScrapeAimApproved = false;
	if (AMD_Character* const Character = Cast<AMD_Character>(GetOwner()))
	{
		Character->CancelScrapeAbilityForSession(EndingSessionId);
	}
	StopScraping();
	ResetStrokeTracking();
	ClientTerminateScrapeFocus(EndingSessionId, Reason);
	EndScrapeFocusInternal(false);
}

void UMD_ScrapeToolComponent::ServerUpdateScrapeAim_Implementation(
	const FMD_ScrapeAimNetSample& Sample)
{
	const bool bCanResolvePrediction = GetOwner()
		&& GetOwner()->HasAuthority()
		&& ScrapeFocusSession.bAuthoritativeScrapeFocusAccepted
		&& Sample.FocusSessionId != 0
		&& Sample.Sequence != 0
		&& Sample.FocusSessionId
			== ScrapeFocusSession.ActiveScrapeFocusSessionId;
	FMD_ScrapeAimQueryResult ValidatedAim;
	float SampleDeltaTime = 0.0f;
	int32 MissingSequences = 0;
	if (!GetOwner()
		|| !GetOwner()->HasAuthority()
		|| !ScrapeFocusSession.bAuthoritativeScrapeFocusAccepted
		|| Sample.FocusSessionId == 0
		|| Sample.FocusSessionId != ScrapeFocusSession.ActiveScrapeFocusSessionId
		|| !ConsumeAuthoritativeAimRateToken(GetScrapeMonotonicSeconds())
		|| !ValidateAndCacheAuthoritativeAim(
			Sample,
			false,
			ValidatedAim,
			SampleDeltaTime,
			MissingSequences))
	{
		// 잘못된 session, 초과 rate, 유효하지 않은 aim은 거부한다.
		if (bCanResolvePrediction)
		{
			SendScrapePredictionResolution(
				Sample.FocusSessionId,
				Sample.Sequence);
		}
		return;
	}

	CurrentScrapeAim = ValidatedAim;
	const bool bCanMutateWorld = bIsScraping
		&& IsAuthoritativeFirstScrapeAimApproved(Sample.FocusSessionId);
	if (bCanMutateWorld)
	{
		ProcessAuthoritativeScrapeAimResult(
			ValidatedAim,
			SampleDeltaTime,
			true,
			MissingSequences,
			Sample.FocusSessionId,
			Sample.Sequence);
	}
	else
	{
		BufferPreFirstAuthoritativeAim(Sample, ValidatedAim);
		// Ready/first-waiting heartbeat updates contact and invalid lease only.
		ProcessReadyScrapeAimResult(ValidatedAim, SampleDeltaTime);
		if (MissingSequences > FMath::Max(MaxMissingAimSequences, 0))
		{
			ResetStrokeTracking();
		}
		if (!bIsScraping)
		{
			SendScrapePredictionResolution(
				Sample.FocusSessionId,
				Sample.Sequence);
		}
	}
}

void UMD_ScrapeToolComponent::EndScrapeFocus()
{
	EndScrapeFocusInternal(true);
}

void UMD_ScrapeToolComponent::EndScrapeFocusInternal(
	const bool bSendServerEnd)
{
	const uint64 EndingSessionId = ScrapeFocusSession.ActiveScrapeFocusSessionId;
	if (bSendServerEnd && EndingSessionId != 0)
	{
		const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
		if (OwnerPawn && OwnerPawn->IsLocallyControlled()
			&& (!GetOwner()->HasAuthority() || GetNetMode() != NM_Standalone))
		{
			ServerEndScrapeFocus(EndingSessionId);
		}
	}

	StopScraping();
	ClearLocalScrapePredictions();
	CurrentScrapeAim = FMD_ScrapeAimQueryResult();
	SetScrapeFocusState(EMD_ScrapeFocusState::Free);
	bHasScrapeFocusView = false;
	ScrapeFocusViewOrigin = FVector::ZeroVector;
	ScrapeFocusViewRotation = FRotator::ZeroRotator;
	ScrapeFocusViewFOV = 90.0f;
	ScrapeFocusSession.ScrapeFocusAspectRatio = 16.0f / 9.0f;
	ScrapeFocusSession.ScrapeFocusHorizontalHalfTangent = 1.0f;
	ScrapeFocusSession.ScrapeFocusVerticalHalfTangent = 9.0f / 16.0f;
	ScrapeFocusSession.ScrapeFocusAuthorityHorizontalHalfTangent = 1.0f;
	ScrapeFocusSession.ScrapeFocusAuthorityVerticalHalfTangent = 9.0f / 16.0f;
	ScrapeFocusViewportSize = FIntPoint::ZeroValue;
	ScrapeFocusPointerNormalized = FVector2D(0.5, 0.5);
	ResetScrapeFocusPointerStrokeConstraint();
	ResetScrapeFocusSessionState();
	SetComponentTickEnabled(false);
}

void UMD_ScrapeToolComponent::ResetScrapeFocusSessionState()
{
	ScrapeFocusSession.ActiveScrapeFocusSessionId = 0;
	// 조준 프로토콜 상태는 통째로 초기 상태(토큰 가득, 승인 없음)로 되돌린다.
	AuthoritativeAim = FMD_ScrapeAuthoritativeAimState();
	ScrapeFocusSession.FocusPendingElapsedSeconds = 0.0;
	ScrapeFocusSession.LocalFocusMonotonicStartSeconds = 0.0;
	ScrapeFocusSession.bAuthoritativeScrapeFocusAccepted = false;
	ContaminationPaint.Stage3FocusSessionIdentityOverride = 0;
}

void UMD_ScrapeToolComponent::SetScrapeFocusState(
	const EMD_ScrapeFocusState NewState)
{
	ScrapeFocusState = NewState;
}

bool UMD_ScrapeToolComponent::RefreshScrapeFocusAim()
{
	if (!IsScrapeFocusActive() || !bHasScrapeFocusView)
	{
		CurrentScrapeAim = FMD_ScrapeAimQueryResult();
		return false;
	}
	if (!IsCurrentScrapeFocusViewportAspectCompatible()
		|| !IsScrapeFocusViewOriginValid())
	{
		// 화면 비율이나 View 시작점이 바뀌면 새 ray를 보내기 전에 종료한다.
		TerminateScrapeFocusFromComponent();
		return false;
	}

	CurrentScrapeAim = QueryCurrentScrapeAim();
	return CurrentScrapeAim.IsActionable();
}

bool UMD_ScrapeToolComponent::TryGetAuthoritativeScrapeView(
	FMinimalViewInfo& OutViewInfo) const
{
	OutViewInfo = FMinimalViewInfo();
	APawn* const OwnerPawn = Cast<APawn>(GetOwner());
	const AController* const OwnerController = OwnerPawn
		? OwnerPawn->GetController()
		: nullptr;
	if (!OwnerPawn
		|| !OwnerController
		|| !GetOwner()->HasAuthority())
	{
		return false;
	}

	// A remote server PlayerCameraManager does not continuously evaluate the
	// owning client's camera. Its cache can therefore retain the default 4:3
	// projection (and a stale pose), while the client is using the Character's
	// actual CameraComponent. Read that raw component view directly; calling the
	// Pawn's CalcCamera here would feed the already-frozen scrape Focus view back
	// into this validation on a listen server.
	TInlineComponentArray<UCameraComponent*> CameraComponents;
	OwnerPawn->GetComponents(CameraComponents);
	UCameraComponent* ActiveCamera = nullptr;
	for (UCameraComponent* CameraComponent : CameraComponents)
	{
		if (IsValid(CameraComponent) && CameraComponent->IsActive())
		{
			ActiveCamera = CameraComponent;
			break;
		}
	}
	if (!ActiveCamera)
	{
		return false;
	}
	ActiveCamera->GetCameraView(0.0f, OutViewInfo);
	OutViewInfo.Rotation = OwnerController->GetControlRotation();
	return !OutViewInfo.Location.ContainsNaN()
		&& !OutViewInfo.Rotation.ContainsNaN()
		&& FMath::IsFinite(OutViewInfo.FOV)
		&& OutViewInfo.FOV > UE_SMALL_NUMBER
		&& FMath::IsFinite(OutViewInfo.AspectRatio)
		&& OutViewInfo.AspectRatio > UE_SMALL_NUMBER;
}

bool UMD_ScrapeToolComponent::IsScrapeFocusViewOriginValid() const
{
	if (!IsScrapeFocusActive() || !bHasScrapeFocusView)
	{
		return false;
	}

	const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
	const AController* const OwnerController = OwnerPawn
		? OwnerPawn->GetController()
		: nullptr;
	if (!OwnerController)
	{
		return false;
	}

	FVector CurrentViewLocation = FVector::ZeroVector;
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		FMinimalViewInfo ServerViewInfo;
		if (!TryGetAuthoritativeScrapeView(ServerViewInfo))
		{
			return false;
		}
		CurrentViewLocation = ServerViewInfo.Location;
	}
	else
	{
		FRotator CurrentViewRotation = FRotator::ZeroRotator;
		OwnerController->GetPlayerViewPoint(
			CurrentViewLocation,
			CurrentViewRotation);
	}
	const float DriftToleranceCm = FMath::Max(
		ScrapeFocusViewOriginDriftToleranceCm,
		0.0f);
	return !CurrentViewLocation.ContainsNaN()
		&& FVector::DistSquared(CurrentViewLocation, ScrapeFocusViewOrigin)
			<= FMath::Square(DriftToleranceCm);
}

void UMD_ScrapeToolComponent::TerminateScrapeFocusFromComponent()
{
	if (GetOwner() && GetOwner()->HasAuthority()
		&& ScrapeFocusSession.bAuthoritativeScrapeFocusAccepted
		&& ScrapeFocusSession.ActiveScrapeFocusSessionId != 0)
	{
		TerminateAuthoritativeScrapeFocus(
			EMD_ScrapeFocusTerminationReason::ViewInvalid);
		return;
	}

	if (AMD_Character* const Character = Cast<AMD_Character>(GetOwner()))
	{
		// 강제 종료도 Character의 저장된 GAS handle을 먼저 취소해
		// Focus=Free인데 Ability만 active로 남는 상태를 만들지 않는다.
		Character->CancelScrapeInteraction();
		return;
	}

	EndScrapeFocus();
}
