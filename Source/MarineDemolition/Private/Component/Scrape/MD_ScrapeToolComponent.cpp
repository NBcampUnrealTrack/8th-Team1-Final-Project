// Copyright Epic Games, Inc. All Rights Reserved.

#include "Component/Scrape/MD_ScrapeToolComponent.h"

#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"
#include "Character/MD_Character.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Camera/CameraTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/Scrape/MD_ScrapeEvaluator.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Sound/SoundBase.h"
#include "Net/UnrealNetwork.h"
#include "World/Contamination/MD_Barnacle.h"
#include "World/Wall/MD_CuttableWall.h"

UMD_ScrapeToolComponent::UMD_ScrapeToolComponent()
{
	SetIsReplicatedByDefault(true);

	TraceDistanceCm = 220.0f;
	TraceChannel = ECC_Visibility;
	ContaminationScrapeSound = nullptr;
	BareSurfaceScrapeSound = nullptr;
	ContaminationScrapeEffect = nullptr;
	BareSurfaceScrapeEffect = nullptr;
	MinimumFeedbackIntervalSeconds = 0.08f;
	bShowDebugFeedback = true;
	ScrapeFocusPointerSensitivity = 0.0025f;
	ScrapeFocusPointerDirectionLockThresholdNormalized = 0.01f;
	ScrapeFocusPointerSafeMargin = 0.05f;
	ScrapeFocusAspectRatioAbsoluteTolerance = 0.0045f;
	ScrapeFocusViewOriginDriftToleranceCm = 5.0f;
	TransientMissGraceSeconds = 0.10f;
	ReconnectCandidateMaxCm = 15.0f;
	ReconnectBridgeMaxCm = 2.5f;
	MaxMissingAimSequences = 2;
	PersistentInvalidExitSeconds = 0.50f;
	BeginBasisRotationToleranceDegrees = 0.5f;
	BeginFOVToleranceDegrees = 0.1f;
	AimViewConeMarginDegrees = 0.25f;
	BeginAckTimeoutSeconds = 1.0f;
	MaxSampleAgeSeconds = 0.15f;
	AimSilenceTimeoutSeconds = 0.50f;
	ScrapingAimSendRateHz = 30.0f;
	ReadyAimHeartbeatRateHz = 5.0f;
	DirectionLockThresholdCm = 1.0f;
	PathDirectionResampleSpacingCm = 0.5f;
	BladeDirectionWindowCm = 2.5f;
	MaximumBladeAxisTurnDegreesPerCm = 10.0f;
	LargeCornerSplitDegrees = 60.0f;
	MaximumPerpendicularDriftCm = 2.0f;
	BreakStrokePerpendicularDistanceCm = 8.0f;
	bUseAimCorrection = true;
	AimCorrectionInterpolationSpeed = 6.0f;
	AimCorrectionSpeedDegreesPerSecond = 90.0f;
	AimCorrectionMaximumErrorDegrees = 8.0f;
	AimCorrectionVisibilityToleranceCm = 2.0f;
	ScrapeLoad = 0.0f;
	ScrapeLoadPerRemovedPercent = 0.6f;
	ScrapeLoadPerRemovedBarnacle = 8.0f;
	SaturatedContaminationStampRatio = 0.4f;
	SaturatedRemovalMultiplier = 0.05f;
	SaturatedContaminationStampCoverage = 0.5f;
	SaturatedContaminationStampDepthCm = 4.0f;
	SaturatedContaminationStampSpacingCm = 6.0f;
	bIsScraping = false;
	ScrapeFocusState = EMD_ScrapeFocusState::Free;
	ScrapeFocusGeneration = 0;
	ScrapeFocusPointerNormalized = FVector2D(0.5, 0.5);
	bHasScrapeFocusView = false;
	bHasPreviousSurfacePosition = false;
	bHasLockedStrokeAxis = false;
	SaturatedContaminationStampPatternSeed = 7319;
	PreviousSurfacePositionCm = FVector2D::ZeroVector;
	PreviousSurfaceFace = EMD_ScrapeSurfaceFace::Front;
	StrokeAnchorPositionCm = FVector2D::ZeroVector;
	FurthestSurfacePositionCm = FVector2D::ZeroVector;
	LockedStrokeAxis = FVector2D::ZeroVector;
	PendingScrapePathArcLengthCm = 0.0;
	ScrapePathArcLengthCm = 0.0;
	LastEmittedScrapePathArcLengthCm = 0.0;
	PendingResolvedScrapePathPointsCm.Reset();
	PendingResolvedScrapePathTravelCm = 0.0;
	PendingScrapePathSampleTimeSeconds = 0.0;
	LastEmittedScrapePathPositionCm = FVector2D::ZeroVector;
	LastEmittedBladeDepthAxis = FVector2D::ZeroVector;
	StableBladeDepthAxis = FVector2D::ZeroVector;
	LastRawTravelTangent = FVector2D::ZeroVector;
	bHasStableBladeDepthAxis = false;
	bHasLastRawTravelTangent = false;
	bHasLastScrapeSurfaceHit = false;
	LastFeedbackWorldSeconds = -DBL_MAX;

	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UMD_ScrapeToolComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(
		UMD_ScrapeToolComponent,
		ScrapeToolSpec,
		COND_OwnerOnly);
	DOREPLIFETIME(
		UMD_ScrapeToolComponent,
		ScrapeLoad);
	DOREPLIFETIME_CONDITION(
		UMD_ScrapeToolComponent,
		bIsScraping,
		COND_SkipOwner);
	DOREPLIFETIME_CONDITION(
		UMD_ScrapeToolComponent,
		LastContactWorldLocation,
		COND_SkipOwner);
	DOREPLIFETIME_CONDITION(
		UMD_ScrapeToolComponent,
		bHasLastContact,
		COND_SkipOwner);
	DOREPLIFETIME_CONDITION(
		UMD_ScrapeToolComponent,
		LastContactBladeWorldDirection,
		COND_SkipOwner);
	DOREPLIFETIME_CONDITION(
		UMD_ScrapeToolComponent,
		bHasLastContactBladeDirection,
		COND_SkipOwner);
}

const TCHAR* UMD_ScrapeToolComponent::GetContactTypeLabel(
	const EMD_ScrapeContactType ContactType)
{
	switch (ContactType)
	{
	case EMD_ScrapeContactType::ContaminationOnly:
		return TEXT("오염막 제거");

	case EMD_ScrapeContactType::Mixed:
		return TEXT("오염막 + 맨 벽면");

	case EMD_ScrapeContactType::BareSurfaceOnly:
		return TEXT("맨 벽면");

	case EMD_ScrapeContactType::None:
	default:
		return TEXT("무접촉");
	}
}

void UMD_ScrapeToolComponent::BeginPlay()
{
	Super::BeginPlay();

	SetComponentTickEnabled(false);
}

float UMD_ScrapeToolComponent::GetEffectiveScrapeReachCm() const
{
	return FMath::IsFinite(ScrapeToolSpec.ReachCm)
		? FMath::Max(ScrapeToolSpec.ReachCm, 0.0f)
		: 0.0f;
}

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
		return false;
	}
	if (!CaptureScrapeFocusView())
	{
		return false;
	}

	if (bIsScraping)
	{
		// 과도기 호환 경로로 시작된 LMB 긁기가 있다면 유효한 Focus가 이를 인계한다.
		StopScraping();
	}

	CurrentScrapeAim = PreflightAim;
	ResetInvalidScrapeAim();
	++ScrapeFocusGeneration;
	if (ScrapeFocusGeneration == 0)
	{
		// 0은 아직 Focus를 시작하지 않은 상태로 예약한다.
		++ScrapeFocusGeneration;
	}

	SetScrapeFocusState(EMD_ScrapeFocusState::FocusPending);
	FocusPendingElapsedSeconds = 0.0;
	LocalAimSendAccumulatorSeconds = 0.0;
	LocalFocusMonotonicStartSeconds = GetScrapeMonotonicSeconds();
	NextLocalAimSequence = 0;
	const uint64 OwnerIdentity = GetOwner()
		? static_cast<uint64>(GetOwner()->GetUniqueID())
		: 1ull;
	ActiveScrapeFocusSessionId =
		(FPlatformTime::Cycles64() << 1)
		^ (OwnerIdentity << 32)
		^ static_cast<uint64>(ScrapeFocusGeneration);
	if (ActiveScrapeFocusSessionId == 0)
	{
		ActiveScrapeFocusSessionId = 1;
	}
	ScrapeFocusAspectRatio = ScrapeFocusViewportSize.Y > 0
		? static_cast<float>(ScrapeFocusViewportSize.X)
			/ static_cast<float>(ScrapeFocusViewportSize.Y)
		: 16.0f / 9.0f;
	SetComponentTickEnabled(true);
	ServerBeginScrapeFocus(
		ActiveScrapeFocusSessionId,
		NextLocalAimSequence,
		ScrapeFocusViewRotation,
		ScrapeFocusViewFOV,
		ScrapeFocusAspectRatio,
		ScrapeFocusHorizontalHalfTangent,
		ScrapeFocusVerticalHalfTangent);
	return true;
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

	const bool bIsSameAcceptedSession = bAuthoritativeScrapeFocusAccepted
		&& FocusSessionId != 0
		&& FocusSessionId == ActiveScrapeFocusSessionId;
	const auto RejectBegin = [this, FocusSessionId, bIsSameAcceptedSession](
		const EMD_ScrapeFocusTerminationReason SameSessionReason,
		const FRotator& ReplyViewRotation,
		const float ReplyFOV)
	{
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
	};

	if (FocusSessionId == 0
		|| RequestedViewRotation.ContainsNaN()
		|| !FMath::IsFinite(RequestedFOV)
		|| !FMath::IsFinite(RequestedAspectRatio)
		|| !FMath::IsFinite(RequestedHorizontalHalfTangent)
		|| !FMath::IsFinite(RequestedVerticalHalfTangent)
		|| RequestedFOV <= UE_SMALL_NUMBER
		|| RequestedHorizontalHalfTangent <= UE_SMALL_NUMBER
		|| RequestedVerticalHalfTangent <= UE_SMALL_NUMBER
		|| RequestedAspectRatio < MinimumSupportedFocusAspectRatio
		|| RequestedAspectRatio > MaximumSupportedFocusAspectRatio)
	{
		RejectBegin(
			EMD_ScrapeFocusTerminationReason::ProtocolViolation,
			FRotator::ZeroRotator,
			90.0f);
		return;
	}
	if (!IsScrapeToolCurrentlyEligible())
	{
		RejectBegin(
			EMD_ScrapeFocusTerminationReason::EligibilityLost,
			FRotator::ZeroRotator,
			90.0f);
		return;
	}

	const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
	const AController* const OwnerController = OwnerPawn
		? OwnerPawn->GetController()
		: nullptr;
	if (!OwnerController)
	{
		RejectBegin(
			EMD_ScrapeFocusTerminationReason::ViewInvalid,
			FRotator::ZeroRotator,
			90.0f);
		return;
	}

	FVector ServerEyeLocation = FVector::ZeroVector;
	FRotator ServerViewRotation = FRotator::ZeroRotator;
	OwnerController->GetPlayerViewPoint(ServerEyeLocation, ServerViewRotation);
	const APlayerController* const PlayerController = Cast<APlayerController>(
		OwnerController);
	const APlayerCameraManager* const PlayerCameraManager = PlayerController
		? PlayerController->PlayerCameraManager
		: nullptr;
	if (!PlayerCameraManager)
	{
		RejectBegin(
			EMD_ScrapeFocusTerminationReason::ViewInvalid,
			ServerViewRotation,
			90.0f);
		return;
	}

	const float ServerFOV = PlayerCameraManager->GetFOVAngle();
	const ULocalPlayer* const LocalPlayerDefaults = GetDefault<ULocalPlayer>();
	const EAspectRatioAxisConstraint DefaultAxisConstraint =
		LocalPlayerDefaults
			? static_cast<EAspectRatioAxisConstraint>(
				LocalPlayerDefaults->AspectRatioAxisConstraint.GetValue())
			: AspectRatio_MaintainYFOV;
	FMinimalViewInfo RequestedProjectionView =
		PlayerCameraManager->GetCameraCacheView();
	RequestedProjectionView.FOV = RequestedFOV;
	float RequestedProjectionHorizontalHalfTangent = 0.0f;
	float RequestedProjectionVerticalHalfTangent = 0.0f;
	const bool bProjectionContractMatches =
		CalculateScrapeFocusProjectionHalfTangents(
			RequestedProjectionView,
			DefaultAxisConstraint,
			RequestedAspectRatio,
			IsLegacyMaintainYFOVProjectionEnabled(),
			RequestedProjectionHorizontalHalfTangent,
			RequestedProjectionVerticalHalfTangent)
		&& FMath::Abs(RequestedHorizontalHalfTangent
				- RequestedProjectionHorizontalHalfTangent)
			<= ProjectionHalfTangentTolerance
		&& FMath::Abs(RequestedVerticalHalfTangent
				- RequestedProjectionVerticalHalfTangent)
			<= ProjectionHalfTangentTolerance;

	float LiveHorizontalHalfTangent = 0.0f;
	float LiveVerticalHalfTangent = 0.0f;
	FMinimalViewInfo LiveProjectionView =
		PlayerCameraManager->GetCameraCacheView();
	LiveProjectionView.FOV = ServerFOV;
	float AuthorityPolicyHorizontalHalfTangent = 0.0f;
	float AuthorityPolicyVerticalHalfTangent = 0.0f;
	const bool bAuthorityProjectionPolicyValid =
		CalculateScrapeFocusProjectionHalfTangents(
			LiveProjectionView,
			DefaultAxisConstraint,
			AuthoritativeFocusAspectRatio,
			IsLegacyMaintainYFOVProjectionEnabled(),
			AuthorityPolicyHorizontalHalfTangent,
			AuthorityPolicyVerticalHalfTangent);
	// The requested projection remains the visual/reticle contract. Only the
	// separate authority cone is clamped, so a remote (or dedicated) server never
	// grants additional aim authority from a client-claimed viewport aspect.
	const float ApprovedHorizontalHalfTangent = FMath::Min(
		RequestedProjectionHorizontalHalfTangent,
		AuthorityPolicyHorizontalHalfTangent);
	const float ApprovedVerticalHalfTangent = FMath::Min(
		RequestedProjectionVerticalHalfTangent,
		AuthorityPolicyVerticalHalfTangent);
	const bool bLiveProjectionMatchesAgreedSession =
		!bIsSameAcceptedSession
		|| (CalculateScrapeFocusProjectionHalfTangents(
				LiveProjectionView,
				DefaultAxisConstraint,
				ScrapeFocusAspectRatio,
				IsLegacyMaintainYFOVProjectionEnabled(),
				LiveHorizontalHalfTangent,
				LiveVerticalHalfTangent)
			&& FMath::Abs(LiveHorizontalHalfTangent
					- ScrapeFocusHorizontalHalfTangent)
				<= ProjectionHalfTangentTolerance
			&& FMath::Abs(LiveVerticalHalfTangent
					- ScrapeFocusVerticalHalfTangent)
				<= ProjectionHalfTangentTolerance);
	const bool bAuthorityProjectionMatchesAgreedSession =
		!bIsSameAcceptedSession
		|| (bAuthorityProjectionPolicyValid
			&& FMath::Abs(FMath::Min(
					ScrapeFocusHorizontalHalfTangent,
					AuthorityPolicyHorizontalHalfTangent)
					- ScrapeFocusAuthorityHorizontalHalfTangent)
				<= ProjectionHalfTangentTolerance
			&& FMath::Abs(FMath::Min(
					ScrapeFocusVerticalHalfTangent,
					AuthorityPolicyVerticalHalfTangent)
					- ScrapeFocusAuthorityVerticalHalfTangent)
				<= ProjectionHalfTangentTolerance);
	const bool bRequestedContractMatchesAgreedSession =
		!bIsSameAcceptedSession
		|| (FMath::Abs(RequestedAspectRatio - ScrapeFocusAspectRatio)
				<= FMath::Max(ScrapeFocusAspectRatioAbsoluteTolerance, 0.0f)
			&& FMath::Abs(RequestedHorizontalHalfTangent
					- ScrapeFocusHorizontalHalfTangent)
				<= ProjectionHalfTangentTolerance
			&& FMath::Abs(RequestedVerticalHalfTangent
					- ScrapeFocusVerticalHalfTangent)
				<= ProjectionHalfTangentTolerance);
	const bool bLiveViewMatchesAgreedSession = !bIsSameAcceptedSession
		|| (bHasScrapeFocusView
			&& FVector::Distance(ServerEyeLocation, ScrapeFocusViewOrigin)
				<= FMath::Max(ScrapeFocusViewOriginDriftToleranceCm, 0.0f)
			&& CalculateRotatorErrorDegrees(
				ServerViewRotation,
				ScrapeFocusViewRotation)
				<= FMath::Max(BeginBasisRotationToleranceDegrees, 0.0f)
			&& FMath::Abs(ServerFOV - ScrapeFocusViewFOV)
				<= FMath::Max(BeginFOVToleranceDegrees, 0.0f)
			&& bLiveProjectionMatchesAgreedSession);
	const bool bBasisMatches = !ServerEyeLocation.ContainsNaN()
		&& !ServerViewRotation.ContainsNaN()
		&& FMath::IsFinite(ServerFOV)
		&& bProjectionContractMatches
		&& bAuthorityProjectionPolicyValid
		&& bAuthorityProjectionMatchesAgreedSession
		&& bRequestedContractMatchesAgreedSession
		&& bLiveViewMatchesAgreedSession
		&& CalculateRotatorErrorDegrees(
			RequestedViewRotation,
			ServerViewRotation)
			<= FMath::Max(BeginBasisRotationToleranceDegrees, 0.0f)
		&& FMath::Abs(RequestedFOV - ServerFOV)
			<= FMath::Max(BeginFOVToleranceDegrees, 0.0f);
	const FMD_ScrapeAimQueryResult Preflight = bBasisMatches
		? QueryScrapeAim(ServerEyeLocation, ServerViewRotation.Vector())
		: FMD_ScrapeAimQueryResult();
	if (!bBasisMatches || !Preflight.IsActionable())
	{
		RejectBegin(
			EMD_ScrapeFocusTerminationReason::ViewInvalid,
			ServerViewRotation,
			ServerFOV);
		return;
	}

	if (bIsSameAcceptedSession)
	{
		ClientScrapeFocusBeginResult(
			FocusSessionId,
			EMD_ScrapeFocusBeginResult::Accepted,
			ScrapeFocusViewRotation,
			ScrapeFocusViewFOV,
			ScrapeFocusAuthorityHorizontalHalfTangent,
			ScrapeFocusAuthorityVerticalHalfTangent);
		return;
	}

	// 새 Begin은 기존 accepted session과 그 경로를 먼저 멱등 정리한다.
	if (bAuthoritativeScrapeFocusAccepted
		&& ActiveScrapeFocusSessionId != FocusSessionId)
	{
		if (AMD_Character* const Character = Cast<AMD_Character>(GetOwner()))
		{
			Character->CancelScrapeAbilityForSession(
				ActiveScrapeFocusSessionId);
		}
		StopScraping();
		ResetStrokeTracking();
	}

	ActiveScrapeFocusSessionId = FocusSessionId;
	LastAuthoritativeAimSequence = InitialAimSequence;
	NextLocalAimSequence = InitialAimSequence;
	bAuthoritativeScrapeFocusAccepted = true;
	bAuthoritativeClientClockInitialized = false;
	bAuthoritativeFirstScrapeAimApproved = false;
	AuthoritativeFirstAimSessionId = 0;
	AuthoritativeFirstAimSequence = 0;
	AuthoritativeFirstAimClientSampleSeconds = 0.0;
	BufferedPreFirstAuthoritativeAims.Reset();
	HighestDroppedPreFirstAimSequence = 0;
	bBufferedPreFirstAimReplayDiscontinuous = false;
	AuthoritativeAimRateTokens = AuthoritativeAimBurstCapacity;
	LastAuthoritativeAimRateRefillSeconds = GetScrapeMonotonicSeconds();
	ScrapeFocusViewOrigin = ServerEyeLocation;
	ScrapeFocusViewRotation = ServerViewRotation;
	ScrapeFocusViewFOV = ServerFOV;
	ScrapeFocusAspectRatio = RequestedAspectRatio;
	ScrapeFocusHorizontalHalfTangent =
		RequestedProjectionHorizontalHalfTangent;
	ScrapeFocusVerticalHalfTangent = RequestedProjectionVerticalHalfTangent;
	ScrapeFocusAuthorityHorizontalHalfTangent =
		ApprovedHorizontalHalfTangent;
	ScrapeFocusAuthorityVerticalHalfTangent = ApprovedVerticalHalfTangent;
	bHasScrapeFocusView = true;
	CurrentScrapeAim = Preflight;
	LastAuthoritativeAimReceiveSeconds =
		LastAuthoritativeAimRateRefillSeconds;
	Stage3FocusSessionIdentityOverride = FocusSessionId;
	SetScrapeFocusState(EMD_ScrapeFocusState::FocusedReady);
	SetComponentTickEnabled(true);
	ProcessReadyScrapeAimResult(Preflight, 0.0f);
	ClientScrapeFocusBeginResult(
		FocusSessionId,
		EMD_ScrapeFocusBeginResult::Accepted,
		ServerViewRotation,
		ServerFOV,
		ApprovedHorizontalHalfTangent,
		ApprovedVerticalHalfTangent);
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
		|| FocusSessionId != ActiveScrapeFocusSessionId)
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
			> ScrapeFocusHorizontalHalfTangent
				+ ProjectionHalfTangentTolerance
		|| AgreedVerticalHalfTangent
			> ScrapeFocusVerticalHalfTangent
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
	ScrapeFocusAuthorityHorizontalHalfTangent = FMath::Min(
		AgreedHorizontalHalfTangent,
		ScrapeFocusHorizontalHalfTangent);
	ScrapeFocusAuthorityVerticalHalfTangent = FMath::Min(
		AgreedVerticalHalfTangent,
		ScrapeFocusVerticalHalfTangent);
	ClampScrapeFocusPointerToAuthorityCone();
	FocusPendingElapsedSeconds = 0.0;
	LocalAimSendAccumulatorSeconds = 0.0;
	SetScrapeFocusState(EMD_ScrapeFocusState::FocusedReady);
	RefreshScrapeFocusAim();
}

void UMD_ScrapeToolComponent::ServerEndScrapeFocus_Implementation(
	const uint64 FocusSessionId)
{
	if (!bAuthoritativeScrapeFocusAccepted
		|| FocusSessionId == 0
		|| FocusSessionId != ActiveScrapeFocusSessionId)
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
	if (FocusSessionId == 0 || FocusSessionId != ActiveScrapeFocusSessionId)
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
		|| !bAuthoritativeScrapeFocusAccepted
		|| ActiveScrapeFocusSessionId == 0)
	{
		return;
	}
	const uint64 EndingSessionId = ActiveScrapeFocusSessionId;
	// 먼저 적용 불가로 만들고 Ability/경로를 정리한 뒤 종료 통지를 보낸다.
	bAuthoritativeScrapeFocusAccepted = false;
	bAuthoritativeFirstScrapeAimApproved = false;
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
	if (!GetOwner()
		|| !GetOwner()->HasAuthority()
		|| !bAuthoritativeScrapeFocusAccepted
		|| Sample.FocusSessionId == 0
		|| Sample.FocusSessionId != ActiveScrapeFocusSessionId
		|| !ConsumeAuthoritativeAimRateToken(GetScrapeMonotonicSeconds()))
	{
		// Wrong-session과 초과-rate 표본은 session lease도 갱신하지 않는다.
		return;
	}

	FMD_ScrapeAimQueryResult ValidatedAim;
	float SampleDeltaTime = 0.0f;
	int32 MissingSequences = 0;
	if (!ValidateAndCacheAuthoritativeAim(
			Sample,
			false,
			ValidatedAim,
			SampleDeltaTime,
			MissingSequences))
	{
		return;
	}

	CurrentScrapeAim = ValidatedAim;
	const bool bCanMutateWorld = bIsScraping
		&& IsAuthoritativeFirstScrapeAimApproved(Sample.FocusSessionId);
	if (bCanMutateWorld)
	{
		ProcessScrapeAimResult(
			ValidatedAim,
			SampleDeltaTime,
			true,
			MissingSequences);
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
	}
}

void UMD_ScrapeToolComponent::EndScrapeFocus()
{
	EndScrapeFocusInternal(true);
}

void UMD_ScrapeToolComponent::EndScrapeFocusInternal(
	const bool bSendServerEnd)
{
	const uint64 EndingSessionId = ActiveScrapeFocusSessionId;
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
	CurrentScrapeAim = FMD_ScrapeAimQueryResult();
	SetScrapeFocusState(EMD_ScrapeFocusState::Free);
	bHasScrapeFocusView = false;
	ScrapeFocusViewOrigin = FVector::ZeroVector;
	ScrapeFocusViewRotation = FRotator::ZeroRotator;
	ScrapeFocusViewFOV = 90.0f;
	ScrapeFocusAspectRatio = 16.0f / 9.0f;
	ScrapeFocusHorizontalHalfTangent = 1.0f;
	ScrapeFocusVerticalHalfTangent = 9.0f / 16.0f;
	ScrapeFocusAuthorityHorizontalHalfTangent = 1.0f;
	ScrapeFocusAuthorityVerticalHalfTangent = 9.0f / 16.0f;
	ScrapeFocusViewportSize = FIntPoint::ZeroValue;
	ScrapeFocusPointerNormalized = FVector2D(0.5, 0.5);
	ResetScrapeFocusPointerStrokeConstraint();
	ResetScrapeFocusSessionState();
	SetComponentTickEnabled(false);
}

void UMD_ScrapeToolComponent::ResetScrapeFocusSessionState()
{
	ActiveScrapeFocusSessionId = 0;
	NextLocalAimSequence = 0;
	LastAuthoritativeAimSequence = 0;
	FocusPendingElapsedSeconds = 0.0;
	LocalAimSendAccumulatorSeconds = 0.0;
	LocalFocusMonotonicStartSeconds = 0.0;
	LastAuthoritativeAimReceiveSeconds = 0.0;
	LastAuthoritativeClientSampleSeconds = 0.0;
	AuthoritativeClientClockOffsetSeconds = 0.0;
	bAuthoritativeScrapeFocusAccepted = false;
	bAuthoritativeClientClockInitialized = false;
	bAuthoritativeFirstScrapeAimApproved = false;
	AuthoritativeFirstAimSessionId = 0;
	AuthoritativeFirstAimSequence = 0;
	AuthoritativeFirstAimClientSampleSeconds = 0.0;
	bLastAuthoritativeAimWasActionable = false;
	AuthoritativeAimRateTokens = AuthoritativeAimBurstCapacity;
	LastAuthoritativeAimRateRefillSeconds = -1.0;
	BufferedPreFirstAuthoritativeAims.Reset();
	HighestDroppedPreFirstAimSequence = 0;
	bBufferedPreFirstAimReplayDiscontinuous = false;
	Stage3FocusSessionIdentityOverride = 0;
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
	if (!IsCurrentScrapeFocusViewportAspectCompatible())
	{
		// 서버 session cone은 Begin 때 합의한 비율을 사용한다. 창 크기 변경으로
		// 실제 비율이 달라졌다면 다른 ray를 protocol violation으로 보내기 전에 정리한다.
		TerminateScrapeFocusFromComponent();
		return false;
	}
	if (!IsScrapeFocusViewOriginValid())
	{
		TerminateScrapeFocusFromComponent();
		return false;
	}

	CurrentScrapeAim = QueryCurrentScrapeAim();
	return CurrentScrapeAim.IsActionable();
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
	FRotator CurrentViewRotation = FRotator::ZeroRotator;
	OwnerController->GetPlayerViewPoint(CurrentViewLocation, CurrentViewRotation);
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
		&& bAuthoritativeScrapeFocusAccepted
		&& ActiveScrapeFocusSessionId != 0)
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

bool UMD_ScrapeToolComponent::CaptureScrapeFocusView()
{
	const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
	APlayerController* const PlayerController = OwnerPawn
		? Cast<APlayerController>(OwnerPawn->GetController())
		: nullptr;
	if (!PlayerController)
	{
		return false;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	PlayerController->GetViewportSize(ViewportWidth, ViewportHeight);
	const APlayerCameraManager* const PlayerCameraManager =
		PlayerController->PlayerCameraManager;
	const ULocalPlayer* const LocalPlayer = PlayerController->GetLocalPlayer();
	const float ViewFOV = PlayerCameraManager
		? PlayerCameraManager->GetFOVAngle()
		: 0.0f;
	if (ViewLocation.ContainsNaN()
		|| ViewRotation.ContainsNaN()
		|| !FMath::IsFinite(ViewFOV)
		|| ViewFOV <= UE_SMALL_NUMBER
		|| !PlayerCameraManager
		|| !LocalPlayer
		|| ViewportWidth <= 0
		|| ViewportHeight <= 0)
	{
		return false;
	}

	const float ViewportAspectRatio = static_cast<float>(ViewportWidth)
		/ static_cast<float>(ViewportHeight);
	FMinimalViewInfo ProjectionView =
		PlayerCameraManager->GetCameraCacheView();
	ProjectionView.FOV = ViewFOV;
	float HorizontalHalfTangent = 0.0f;
	float VerticalHalfTangent = 0.0f;
	if (!CalculateScrapeFocusProjectionHalfTangents(
		ProjectionView,
		static_cast<EAspectRatioAxisConstraint>(
			LocalPlayer->AspectRatioAxisConstraint.GetValue()),
		ViewportAspectRatio,
		IsLegacyMaintainYFOVProjectionEnabled(),
		HorizontalHalfTangent,
		VerticalHalfTangent))
	{
		return false;
	}

	ScrapeFocusViewOrigin = ViewLocation;
	ScrapeFocusViewRotation = ViewRotation;
	ScrapeFocusViewFOV = ViewFOV;
	ScrapeFocusAspectRatio = ViewportAspectRatio;
	ScrapeFocusHorizontalHalfTangent = HorizontalHalfTangent;
	ScrapeFocusVerticalHalfTangent = VerticalHalfTangent;
	ScrapeFocusAuthorityHorizontalHalfTangent = HorizontalHalfTangent;
	ScrapeFocusAuthorityVerticalHalfTangent = VerticalHalfTangent;
	ScrapeFocusViewportSize = FIntPoint(ViewportWidth, ViewportHeight);
	ScrapeFocusPointerNormalized = FVector2D(0.5, 0.5);
	ResetScrapeFocusPointerStrokeConstraint();
	bHasScrapeFocusView = true;
	return true;
}

bool UMD_ScrapeToolComponent::ConsumeScrapeFocusLookInput(
	const FVector2D& LookInput)
{
	if (!IsScrapeFocusActive())
	{
		return false;
	}
	if (!LookInput.ContainsNaN())
	{
		const float Sensitivity = FMath::Max(
			ScrapeFocusPointerSensitivity,
			0.0f);
		FVector2D GestureDelta = LookInput * Sensitivity;
		// 이 프로젝트의 Look 매핑은 마우스를 위로 움직일 때 Y가 음수다.
		// 화면 normalized Y도 위쪽이 작으므로 부호를 다시 뒤집지 않는다.
		if (bIsScraping && bHasScrapeFocusPointerStrokeAxis)
		{
			GestureDelta = ScrapeFocusPointerGestureAxis
				* FVector2D::DotProduct(
					GestureDelta,
					ScrapeFocusPointerGestureAxis);
		}
		else if (bIsScraping)
		{
			const FVector2D OriginalGestureDelta = GestureDelta;
			const FVector2D PreviousGestureDisplacement =
				ScrapeFocusPointerGestureDisplacement;
			const FVector2D RequestedGestureDisplacement =
				PreviousGestureDisplacement + OriginalGestureDelta;
			const double Threshold = FMath::Max(
				static_cast<double>(
					ScrapeFocusPointerDirectionLockThresholdNormalized),
				static_cast<double>(UE_KINDA_SMALL_NUMBER));
			if (RequestedGestureDisplacement.SizeSquared()
				>= Threshold * Threshold)
			{
				// 임계점을 건넌 정확한 위치에서 축을 잡고, 같은 입력의 남은
				// 부분부터 투영해 입력 표본 밀도에 따른 각도 차이를 없앤다.
				const double A = OriginalGestureDelta.SizeSquared();
				const double B = 2.0 * FVector2D::DotProduct(
					PreviousGestureDisplacement,
					OriginalGestureDelta);
				const double C = PreviousGestureDisplacement.SizeSquared()
					- Threshold * Threshold;
				double CrossingAlpha = 1.0;
				const double Discriminant = B * B - 4.0 * A * C;
				if (A > UE_DOUBLE_SMALL_NUMBER && Discriminant >= 0.0)
				{
					const double Root = FMath::Sqrt(Discriminant);
					const double First = (-B - Root) / (2.0 * A);
					const double Second = (-B + Root) / (2.0 * A);
					if (First >= 0.0 && First <= 1.0)
					{
						CrossingAlpha = First;
					}
					else if (Second >= 0.0 && Second <= 1.0)
					{
						CrossingAlpha = Second;
					}
				}
				ScrapeFocusPointerGestureDisplacement =
					PreviousGestureDisplacement
					+ OriginalGestureDelta * CrossingAlpha;
				TryLatchScrapeFocusPointerStrokeAxis();
				if (bHasScrapeFocusPointerStrokeAxis)
				{
					const FVector2D RemainingGestureDelta =
						OriginalGestureDelta * (1.0 - CrossingAlpha);
					GestureDelta = OriginalGestureDelta * CrossingAlpha
						+ ScrapeFocusPointerGestureAxis
							* FVector2D::DotProduct(
								RemainingGestureDelta,
								ScrapeFocusPointerGestureAxis);
				}
			}
			else
			{
				ScrapeFocusPointerGestureDisplacement =
					RequestedGestureDisplacement;
			}
		}
		const float AspectRatio = GetScrapeFocusPointerAspectRatio();
		const FVector2D PointerDelta(
			GestureDelta.X,
			GestureDelta.Y * static_cast<double>(AspectRatio));
		ScrapeFocusPointerNormalized += PointerDelta;
		ClampScrapeFocusPointerToAuthorityCone();
		if (bIsScraping && !bHasScrapeFocusPointerStrokeAxis)
		{
			TryLatchScrapeFocusPointerStrokeAxis();
			ClampScrapeFocusPointerToAuthorityCone();
		}

		// Look과 LMB Started가 같은 프레임에 들어와도 이전 Tick의 aim을 사용하지 않는다.
		RefreshScrapeFocusAim();
	}
	return true;
}

void UMD_ScrapeToolComponent::SetScrapeFocusPointerSensitivity(
	const float NewSensitivity)
{
	if (!FMath::IsFinite(NewSensitivity))
	{
		return;
	}
	ScrapeFocusPointerSensitivity = FMath::Clamp(
		NewSensitivity,
		0.0001f,
		0.1f);
}

float UMD_ScrapeToolComponent::
	CalculateScrapeFocusPointerNormalizedHalfRange(
		const float ProjectionHalfTangent,
		const float AuthorityHalfTangent,
		const float PointerSafeMargin)
{
	if (!FMath::IsFinite(ProjectionHalfTangent)
		|| !FMath::IsFinite(AuthorityHalfTangent)
		|| ProjectionHalfTangent <= UE_SMALL_NUMBER
		|| AuthorityHalfTangent <= UE_SMALL_NUMBER)
	{
		return 0.0f;
	}

	const float MaxScreenCoordinate = 1.0f - 2.0f * FMath::Clamp(
		PointerSafeMargin,
		0.0f,
		0.49f);
	const float AuthorityToProjectionRatio = FMath::Clamp(
		AuthorityHalfTangent / ProjectionHalfTangent,
		0.0f,
		1.0f);
	return 0.5f * MaxScreenCoordinate * AuthorityToProjectionRatio;
}

void UMD_ScrapeToolComponent::ClampScrapeFocusPointerToAuthorityCone()
{
	const float HorizontalHalfRange =
		CalculateScrapeFocusPointerNormalizedHalfRange(
			ScrapeFocusHorizontalHalfTangent,
			ScrapeFocusAuthorityHorizontalHalfTangent,
			ScrapeFocusPointerSafeMargin);
	const float VerticalHalfRange =
		CalculateScrapeFocusPointerNormalizedHalfRange(
			ScrapeFocusVerticalHalfTangent,
			ScrapeFocusAuthorityVerticalHalfTangent,
			ScrapeFocusPointerSafeMargin);
	const double MinimumX = 0.5 - static_cast<double>(HorizontalHalfRange);
	const double MaximumX = 0.5 + static_cast<double>(HorizontalHalfRange);
	const double MinimumY = 0.5 - static_cast<double>(VerticalHalfRange);
	const double MaximumY = 0.5 + static_cast<double>(VerticalHalfRange);
	if (!bIsScraping || !bHasScrapeFocusPointerStrokeAxis)
	{
		ScrapeFocusPointerNormalized.X = FMath::Clamp(
			ScrapeFocusPointerNormalized.X,
			MinimumX,
			MaximumX);
		ScrapeFocusPointerNormalized.Y = FMath::Clamp(
			ScrapeFocusPointerNormalized.Y,
			MinimumY,
			MaximumY);
		return;
	}

	// X/Y를 따로 clamp하면 대각선 끝점에서 잠긴 직선 밖으로 꺾인다.
	// 직선과 허용 사각형의 교차 구간을 구해 scalar travel만 제한한다.
	double MinimumTravel = -TNumericLimits<double>::Max();
	double MaximumTravel = TNumericLimits<double>::Max();
	const auto IntersectAxisInterval = [&MinimumTravel, &MaximumTravel](
		const double Anchor,
		const double Axis,
		const double Minimum,
		const double Maximum)
	{
		if (FMath::Abs(Axis) <= UE_DOUBLE_SMALL_NUMBER)
		{
			return Anchor >= Minimum && Anchor <= Maximum;
		}
		double First = (Minimum - Anchor) / Axis;
		double Second = (Maximum - Anchor) / Axis;
		if (First > Second)
		{
			Swap(First, Second);
		}
		MinimumTravel = FMath::Max(MinimumTravel, First);
		MaximumTravel = FMath::Min(MaximumTravel, Second);
		return MinimumTravel <= MaximumTravel;
	};
	if (!IntersectAxisInterval(
			ScrapeFocusPointerStrokeAnchorNormalized.X,
			ScrapeFocusPointerStrokeAxis.X,
			MinimumX,
			MaximumX)
		|| !IntersectAxisInterval(
			ScrapeFocusPointerStrokeAnchorNormalized.Y,
			ScrapeFocusPointerStrokeAxis.Y,
			MinimumY,
			MaximumY))
	{
		ScrapeFocusPointerNormalized = ScrapeFocusPointerStrokeAnchorNormalized;
		return;
	}

	const double RequestedTravel = FVector2D::DotProduct(
		ScrapeFocusPointerNormalized - ScrapeFocusPointerStrokeAnchorNormalized,
		ScrapeFocusPointerStrokeAxis);
	const double ClampedTravel = FMath::Clamp(
		RequestedTravel,
		MinimumTravel,
		MaximumTravel);
	ScrapeFocusPointerNormalized = ScrapeFocusPointerStrokeAnchorNormalized
		+ ScrapeFocusPointerStrokeAxis * ClampedTravel;
}

void UMD_ScrapeToolComponent::ResetScrapeFocusPointerStrokeConstraint()
{
	ScrapeFocusPointerStrokeAnchorNormalized = ScrapeFocusPointerNormalized;
	ScrapeFocusPointerGestureDisplacement = FVector2D::ZeroVector;
	ScrapeFocusPointerGestureAxis = FVector2D::ZeroVector;
	ScrapeFocusPointerStrokeAxis = FVector2D::ZeroVector;
	bHasScrapeFocusPointerStrokeAxis = false;
}

void UMD_ScrapeToolComponent::TryLatchScrapeFocusPointerStrokeAxis()
{
	if (!bIsScraping || bHasScrapeFocusPointerStrokeAxis)
	{
		return;
	}

	const FVector2D Displacement = ScrapeFocusPointerGestureDisplacement;
	const double Threshold = FMath::Max(
		static_cast<double>(
			ScrapeFocusPointerDirectionLockThresholdNormalized),
		static_cast<double>(UE_KINDA_SMALL_NUMBER));
	if (!FMath::IsFinite(Displacement.SizeSquared())
		|| Displacement.SizeSquared() < Threshold * Threshold)
	{
		return;
	}

	ScrapeFocusPointerGestureAxis = Displacement.GetSafeNormal();
	const float AspectRatio = GetScrapeFocusPointerAspectRatio();
	ScrapeFocusPointerStrokeAxis = FVector2D(
		ScrapeFocusPointerGestureAxis.X,
		ScrapeFocusPointerGestureAxis.Y * static_cast<double>(AspectRatio))
		.GetSafeNormal();
	bHasScrapeFocusPointerStrokeAxis =
		!ScrapeFocusPointerGestureAxis.IsNearlyZero()
		&& !ScrapeFocusPointerStrokeAxis.IsNearlyZero();
}

float UMD_ScrapeToolComponent::GetScrapeFocusPointerAspectRatio() const
{
	if (ScrapeFocusViewportSize.X > 0 && ScrapeFocusViewportSize.Y > 0)
	{
		const float ViewportAspectRatio =
			static_cast<float>(ScrapeFocusViewportSize.X)
			/ static_cast<float>(ScrapeFocusViewportSize.Y);
		if (FMath::IsFinite(ViewportAspectRatio)
			&& ViewportAspectRatio > UE_SMALL_NUMBER)
		{
			return ViewportAspectRatio;
		}
	}
	return FMath::IsFinite(ScrapeFocusAspectRatio)
		&& ScrapeFocusAspectRatio > UE_SMALL_NUMBER
		? ScrapeFocusAspectRatio
		: 1.0f;
}

bool UMD_ScrapeToolComponent::ApplyScrapeFocusCameraView(
	FMinimalViewInfo& InOutViewInfo) const
{
	if (!IsScrapeFocusActive() || !bHasScrapeFocusView)
	{
		return false;
	}

	InOutViewInfo.Rotation = ScrapeFocusViewRotation;
	InOutViewInfo.FOV = ScrapeFocusViewFOV;
	InOutViewInfo.DesiredFOV = ScrapeFocusViewFOV;
	return true;
}

FVector UMD_ScrapeToolComponent::GetScrapeFocusAimDirection() const
{
	if (!bHasScrapeFocusView
		|| ScrapeFocusViewportSize.X <= 0
		|| ScrapeFocusViewportSize.Y <= 0
		|| !FMath::IsFinite(ScrapeFocusHorizontalHalfTangent)
		|| !FMath::IsFinite(ScrapeFocusVerticalHalfTangent)
		|| ScrapeFocusHorizontalHalfTangent <= UE_SMALL_NUMBER
		|| ScrapeFocusVerticalHalfTangent <= UE_SMALL_NUMBER)
	{
		return FVector::ZeroVector;
	}

	// 방향 판정 구간에는 reticle만 움직이고 실제 판정 ray는 LMB 시작점에 둔다.
	const FVector2D AimPointerNormalized =
		bIsScraping && !bHasScrapeFocusPointerStrokeAxis
		? ScrapeFocusPointerStrokeAnchorNormalized
		: ScrapeFocusPointerNormalized;
	const double ScreenX = (AimPointerNormalized.X - 0.5) * 2.0;
	const double ScreenY = (0.5 - AimPointerNormalized.Y) * 2.0;
	const FRotationMatrix ViewAxes(ScrapeFocusViewRotation);
	return (ViewAxes.GetScaledAxis(EAxis::X)
		+ ViewAxes.GetScaledAxis(EAxis::Y) * ScreenX
			* static_cast<double>(ScrapeFocusHorizontalHalfTangent)
		+ ViewAxes.GetScaledAxis(EAxis::Z) * ScreenY
			* static_cast<double>(ScrapeFocusVerticalHalfTangent)).GetSafeNormal();
}

bool UMD_ScrapeToolComponent::IsCurrentScrapeFocusViewportAspectCompatible() const
{
	const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled())
	{
		// 원격 authority session에는 로컬 viewport가 없다. 해당 session은
		// Begin에서 합의한 aspect와 전달받은 camera-relative ray만 검증한다.
		return true;
	}

	const APlayerController* const PlayerController = Cast<APlayerController>(
		OwnerPawn->GetController());
	if (!PlayerController)
	{
		return false;
	}

	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	PlayerController->GetViewportSize(ViewportWidth, ViewportHeight);
	if (!IsScrapeFocusViewportAspectCompatible(
		ScrapeFocusAspectRatio,
		FIntPoint(ViewportWidth, ViewportHeight),
		ScrapeFocusAspectRatioAbsoluteTolerance))
	{
		return false;
	}

	const APlayerCameraManager* const PlayerCameraManager =
		PlayerController->PlayerCameraManager;
	const ULocalPlayer* const LocalPlayer = PlayerController->GetLocalPlayer();
	if (!PlayerCameraManager || !LocalPlayer)
	{
		return false;
	}
	FMinimalViewInfo ProjectionView =
		PlayerCameraManager->GetCameraCacheView();
	ProjectionView.FOV = ScrapeFocusViewFOV;
	float CurrentHorizontalHalfTangent = 0.0f;
	float CurrentVerticalHalfTangent = 0.0f;
	const float CurrentAspectRatio = static_cast<float>(ViewportWidth)
		/ static_cast<float>(ViewportHeight);
	return CalculateScrapeFocusProjectionHalfTangents(
			ProjectionView,
			static_cast<EAspectRatioAxisConstraint>(
				LocalPlayer->AspectRatioAxisConstraint.GetValue()),
			CurrentAspectRatio,
			IsLegacyMaintainYFOVProjectionEnabled(),
			CurrentHorizontalHalfTangent,
			CurrentVerticalHalfTangent)
		&& FMath::Abs(CurrentHorizontalHalfTangent
				- ScrapeFocusHorizontalHalfTangent)
			<= ProjectionHalfTangentTolerance
		&& FMath::Abs(CurrentVerticalHalfTangent
				- ScrapeFocusVerticalHalfTangent)
			<= ProjectionHalfTangentTolerance;
}

bool UMD_ScrapeToolComponent::IsScrapeFocusViewportAspectCompatible(
	const float AgreedAspectRatio,
	const FIntPoint& CurrentViewportSize,
	const float AbsoluteTolerance)
{
	if (!FMath::IsFinite(AgreedAspectRatio)
		|| AgreedAspectRatio <= UE_SMALL_NUMBER
		|| CurrentViewportSize.X <= 0
		|| CurrentViewportSize.Y <= 0)
	{
		return false;
	}

	const float CurrentAspectRatio = static_cast<float>(CurrentViewportSize.X)
		/ static_cast<float>(CurrentViewportSize.Y);
	return FMath::IsNearlyEqual(
		CurrentAspectRatio,
		AgreedAspectRatio,
		FMath::Max(AbsoluteTolerance, 0.0f));
}

bool UMD_ScrapeToolComponent::CalculateScrapeFocusProjectionHalfTangents(
	const FMinimalViewInfo& ViewInfo,
	const EAspectRatioAxisConstraint DefaultAxisConstraint,
	const float ViewportAspectRatio,
	const bool bUseLegacyMaintainYFOV,
	float& OutHorizontalHalfTangent,
	float& OutVerticalHalfTangent)
{
	OutHorizontalHalfTangent = 0.0f;
	OutVerticalHalfTangent = 0.0f;
	if (!FMath::IsFinite(ViewInfo.FOV)
		|| ViewInfo.FOV <= UE_SMALL_NUMBER
		|| ViewInfo.FOV >= 179.0f
		|| !FMath::IsFinite(ViewInfo.AspectRatio)
		|| ViewInfo.AspectRatio <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(ViewportAspectRatio)
		|| ViewportAspectRatio <= UE_SMALL_NUMBER
		|| ViewInfo.bConstrainAspectRatio
		|| ViewInfo.ProjectionMode != ECameraProjectionMode::Perspective
		|| !ViewInfo.OffCenterProjectionOffset.IsNearlyZero(UE_SMALL_NUMBER))
	{
		return false;
	}

	const EAspectRatioAxisConstraint AxisConstraint =
		ViewInfo.AspectRatioAxisConstraint.Get(DefaultAxisConstraint);
	const bool bMaintainXFOV =
		AxisConstraint == AspectRatio_MaintainXFOV
		|| (AxisConstraint == AspectRatio_MajorAxisFOV
			&& ViewportAspectRatio > 1.0f);
	if (!bMaintainXFOV
		&& AxisConstraint != AspectRatio_MaintainYFOV
		&& AxisConstraint != AspectRatio_MajorAxisFOV)
	{
		return false;
	}

	const double RawHorizontalHalfTangent = FMath::Tan(
		FMath::DegreesToRadians(static_cast<double>(ViewInfo.FOV) * 0.5));
	double HorizontalHalfTangent = 0.0;
	double VerticalHalfTangent = 0.0;
	if (bMaintainXFOV)
	{
		HorizontalHalfTangent = RawHorizontalHalfTangent;
		VerticalHalfTangent = RawHorizontalHalfTangent
			/ static_cast<double>(ViewportAspectRatio);
	}
	else if (bUseLegacyMaintainYFOV)
	{
		VerticalHalfTangent = RawHorizontalHalfTangent;
		HorizontalHalfTangent = RawHorizontalHalfTangent
			* static_cast<double>(ViewportAspectRatio);
	}
	else
	{
		VerticalHalfTangent = RawHorizontalHalfTangent
			/ static_cast<double>(ViewInfo.AspectRatio);
		HorizontalHalfTangent = VerticalHalfTangent
			* static_cast<double>(ViewportAspectRatio);
	}

	if (!FMath::IsFinite(HorizontalHalfTangent)
		|| !FMath::IsFinite(VerticalHalfTangent)
		|| HorizontalHalfTangent <= UE_DOUBLE_SMALL_NUMBER
		|| VerticalHalfTangent <= UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}
	OutHorizontalHalfTangent = static_cast<float>(HorizontalHalfTangent);
	OutVerticalHalfTangent = static_cast<float>(VerticalHalfTangent);
	return FMath::IsFinite(OutHorizontalHalfTangent)
		&& FMath::IsFinite(OutVerticalHalfTangent);
}

bool UMD_ScrapeToolComponent::IsLegacyMaintainYFOVProjectionEnabled()
{
	const IConsoleVariable* const LegacyMaintainYFOV =
		IConsoleManager::Get().FindConsoleVariable(
			TEXT("r.UseLegacyMaintainYFOVViewMatrix"));
	return LegacyMaintainYFOV && LegacyMaintainYFOV->GetBool();
}

double UMD_ScrapeToolComponent::GetScrapeMonotonicSeconds()
{
	return FPlatformTime::Seconds();
}

float UMD_ScrapeToolComponent::CalculateRotatorErrorDegrees(
	const FRotator& A,
	const FRotator& B)
{
	if (A.ContainsNaN() || B.ContainsNaN())
	{
		return TNumericLimits<float>::Max();
	}
	const double Dot = FMath::Clamp(
		FMath::Abs(A.Quaternion() | B.Quaternion()), 0.0, 1.0);
	return FMath::RadiansToDegrees(
		static_cast<float>(2.0 * FMath::Acos(Dot)));
}

bool UMD_ScrapeToolComponent::IsScrapeToolCurrentlyEligible() const
{
	const AMD_Character* const Character = Cast<AMD_Character>(GetOwner());
	const UMD_EquipHotbarComponent* const Hotbar = Character
		? Character->GetHotbarComponent()
		: nullptr;
	const UMD_ToolDefinition* const CurrentTool = Hotbar
		? Hotbar->GetCurrentTool()
		: nullptr;
	return IsValid(CurrentTool)
		&& CurrentTool->ToolAbilityTag.MatchesTagExact(
			MDGameplayTags::Ability_Scrape);
}

bool UMD_ScrapeToolComponent::BuildLocalAimSample(
	FMD_ScrapeAimNetSample& OutSample)
{
	const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn
		|| !OwnerPawn->IsLocallyControlled()
		|| !IsScrapeFocusReady()
		|| ActiveScrapeFocusSessionId == 0
		|| !bHasScrapeFocusView)
	{
		return false;
	}

	const FVector WorldDirection = GetScrapeFocusAimDirection().GetSafeNormal();
	if (WorldDirection.IsNearlyZero() || WorldDirection.ContainsNaN())
	{
		return false;
	}
	const FRotationMatrix ViewAxes(ScrapeFocusViewRotation);
	const FVector RelativeDirection(
		FVector::DotProduct(WorldDirection, ViewAxes.GetScaledAxis(EAxis::X)),
		FVector::DotProduct(WorldDirection, ViewAxes.GetScaledAxis(EAxis::Y)),
		FVector::DotProduct(WorldDirection, ViewAxes.GetScaledAxis(EAxis::Z)));
	if (RelativeDirection.ContainsNaN() || RelativeDirection.IsNearlyZero())
	{
		return false;
	}

	++NextLocalAimSequence;
	if (NextLocalAimSequence == 0)
	{
		++NextLocalAimSequence;
	}
	OutSample.FocusSessionId = ActiveScrapeFocusSessionId;
	OutSample.Sequence = NextLocalAimSequence;
	OutSample.ClientMonotonicSeconds = static_cast<float>(FMath::Max(
		GetScrapeMonotonicSeconds() - LocalFocusMonotonicStartSeconds,
		0.0));
	OutSample.CameraRelativeAimDirection = RelativeDirection.GetSafeNormal();
	return true;
}

bool UMD_ScrapeToolComponent::BuildScrapeAbilityFirstAim(
	uint64& OutSessionId,
	uint32& OutAimSequence,
	double& OutSampleTimeSeconds,
	FVector& OutCameraRelativeAimDirection)
{
	CurrentScrapeAim = QueryCurrentScrapeAim();
	if (!CurrentScrapeAim.IsActionable())
	{
		return false;
	}
	FMD_ScrapeAimNetSample Sample;
	if (!BuildLocalAimSample(Sample))
	{
		return false;
	}
	OutSessionId = Sample.FocusSessionId;
	OutAimSequence = Sample.Sequence;
	OutSampleTimeSeconds = static_cast<double>(Sample.ClientMonotonicSeconds);
	OutCameraRelativeAimDirection = FVector(Sample.CameraRelativeAimDirection);
	return true;
}

bool UMD_ScrapeToolComponent::CanActivateAuthoritativeScrape(
	const uint64 FocusSessionId) const
{
	return GetOwner()
		&& GetOwner()->HasAuthority()
		&& bAuthoritativeScrapeFocusAccepted
		&& FocusSessionId != 0
		&& FocusSessionId == ActiveScrapeFocusSessionId;
}

bool UMD_ScrapeToolComponent::IsAuthoritativeFirstScrapeAimApproved(
	const uint64 FocusSessionId) const
{
	return CanActivateAuthoritativeScrape(FocusSessionId)
		&& bAuthoritativeFirstScrapeAimApproved
		&& AuthoritativeFirstAimSessionId == FocusSessionId;
}

void UMD_ScrapeToolComponent::ClearScrapeAbilityFirstAimApproval(
	const uint64 FocusSessionId)
{
	if (AuthoritativeFirstAimSessionId != FocusSessionId)
	{
		return;
	}
	bAuthoritativeFirstScrapeAimApproved = false;
	AuthoritativeFirstAimSessionId = 0;
	AuthoritativeFirstAimSequence = 0;
	AuthoritativeFirstAimClientSampleSeconds = 0.0;
	BufferedPreFirstAuthoritativeAims.Reset();
	HighestDroppedPreFirstAimSequence = 0;
	bBufferedPreFirstAimReplayDiscontinuous = false;
}

void UMD_ScrapeToolComponent::RejectScrapeAbilityFirstAimProtocol(
	const uint64 FocusSessionId)
{
	if (!CanActivateAuthoritativeScrape(FocusSessionId))
	{
		return;
	}
	TerminateAuthoritativeScrapeFocus(
		EMD_ScrapeFocusTerminationReason::ProtocolViolation);
}

bool UMD_ScrapeToolComponent::AuthorizeScrapeAbilityFirstAim(
	const uint64 FocusSessionId,
	const uint32 AimSequence,
	const double SampleTimeSeconds,
	const FVector& CameraRelativeAimDirection)
{
	if (!CanActivateAuthoritativeScrape(FocusSessionId))
	{
		return false;
	}
	const double DirectionSizeSquared =
		CameraRelativeAimDirection.SizeSquared();
	if (AimSequence == 0
		|| !FMath::IsFinite(SampleTimeSeconds)
		|| SampleTimeSeconds < 0.0
		|| CameraRelativeAimDirection.ContainsNaN()
		|| !FMath::IsFinite(DirectionSizeSquared)
		|| FMath::Abs(DirectionSizeSquared - 1.0) > 0.02)
	{
		TerminateAuthoritativeScrapeFocus(
			EMD_ScrapeFocusTerminationReason::ProtocolViolation);
		return false;
	}

	FMD_ScrapeAimNetSample Sample;
	Sample.FocusSessionId = FocusSessionId;
	Sample.Sequence = AimSequence;
	Sample.ClientMonotonicSeconds = static_cast<float>(SampleTimeSeconds);
	Sample.CameraRelativeAimDirection = CameraRelativeAimDirection.GetSafeNormal();
	FMD_ScrapeAimQueryResult ValidatedAim;
	float SampleDeltaTime = 0.0f;
	int32 MissingSequences = 0;
	if (!ValidateAndCacheAuthoritativeAim(
			Sample,
			true,
			ValidatedAim,
			SampleDeltaTime,
			MissingSequences)
		|| !ValidatedAim.IsActionable())
	{
		return false;
	}

	CurrentScrapeAim = ValidatedAim;
	bAuthoritativeFirstScrapeAimApproved = true;
	AuthoritativeFirstAimSessionId = FocusSessionId;
	AuthoritativeFirstAimSequence = AimSequence;
	AuthoritativeFirstAimClientSampleSeconds = SampleTimeSeconds;
	PrepareBufferedAuthoritativeAimReplay(AimSequence);
	return true;
}

void UMD_ScrapeToolComponent::SendLocalAimIfDue(const float DeltaTime)
{
	if (!IsScrapeFocusReady())
	{
		return;
	}
	LocalAimSendAccumulatorSeconds += FMath::IsFinite(DeltaTime)
		? FMath::Max(static_cast<double>(DeltaTime), 0.0)
		: 0.0;
	const float RateHz = bIsScraping
		? FMath::Clamp(ScrapingAimSendRateHz, 1.0f, 30.0f)
		: FMath::Clamp(ReadyAimHeartbeatRateHz, 1.0f, 5.0f);
	const double IntervalSeconds = 1.0 / static_cast<double>(RateHz);
	if (LocalAimSendAccumulatorSeconds + UE_DOUBLE_SMALL_NUMBER
		< IntervalSeconds)
	{
		return;
	}
	LocalAimSendAccumulatorSeconds = FMath::Fmod(
		LocalAimSendAccumulatorSeconds,
		IntervalSeconds);

	FMD_ScrapeAimNetSample Sample;
	if (BuildLocalAimSample(Sample))
	{
		ServerUpdateScrapeAim(Sample);
	}
}

bool UMD_ScrapeToolComponent::ConsumeAuthoritativeAimRateToken(
	const double ServerNowSeconds)
{
	if (!FMath::IsFinite(ServerNowSeconds))
	{
		return false;
	}
	if (!FMath::IsFinite(LastAuthoritativeAimRateRefillSeconds)
		|| LastAuthoritativeAimRateRefillSeconds < 0.0)
	{
		LastAuthoritativeAimRateRefillSeconds = ServerNowSeconds;
		AuthoritativeAimRateTokens = AuthoritativeAimBurstCapacity;
	}
	const double RefillElapsedSeconds = FMath::Max(
		ServerNowSeconds - LastAuthoritativeAimRateRefillSeconds,
		0.0);
	AuthoritativeAimRateTokens = FMath::Min(
		AuthoritativeAimBurstCapacity,
		AuthoritativeAimRateTokens
			+ RefillElapsedSeconds * MaximumAuthoritativeAimRateHz);
	LastAuthoritativeAimRateRefillSeconds = ServerNowSeconds;
	if (AuthoritativeAimRateTokens + UE_DOUBLE_SMALL_NUMBER < 1.0)
	{
		return false;
	}
	AuthoritativeAimRateTokens -= 1.0;
	return true;
}

void UMD_ScrapeToolComponent::BufferPreFirstAuthoritativeAim(
	const FMD_ScrapeAimNetSample& Sample,
	const FMD_ScrapeAimQueryResult& ValidatedAim)
{
	if (Sample.FocusSessionId != ActiveScrapeFocusSessionId
		|| Sample.Sequence == 0)
	{
		return;
	}
	if (BufferedPreFirstAuthoritativeAims.Num()
		>= MaximumBufferedPreFirstAimSamples)
	{
		HighestDroppedPreFirstAimSequence = FMath::Max(
			HighestDroppedPreFirstAimSequence,
			BufferedPreFirstAuthoritativeAims[0].Sample.Sequence);
		BufferedPreFirstAuthoritativeAims.RemoveAt(
			0,
			1,
			EAllowShrinking::No);
	}
	FMD_BufferedAuthoritativeScrapeAim& Buffered =
		BufferedPreFirstAuthoritativeAims.AddDefaulted_GetRef();
	Buffered.Sample = Sample;
	Buffered.ValidatedAim = ValidatedAim;
}

void UMD_ScrapeToolComponent::PrepareBufferedAuthoritativeAimReplay(
	const uint32 FirstAimSequence)
{
	BufferedPreFirstAuthoritativeAims.RemoveAll(
		[FirstAimSequence](
			const FMD_BufferedAuthoritativeScrapeAim& Buffered)
		{
			return Buffered.Sample.Sequence <= FirstAimSequence;
		});
	BufferedPreFirstAuthoritativeAims.Sort(
		[](const FMD_BufferedAuthoritativeScrapeAim& A,
			const FMD_BufferedAuthoritativeScrapeAim& B)
		{
			return A.Sample.Sequence < B.Sample.Sequence;
		});

	bBufferedPreFirstAimReplayDiscontinuous =
		HighestDroppedPreFirstAimSequence > FirstAimSequence;
	uint32 ExpectedSequence = FirstAimSequence + 1;
	if (ExpectedSequence == 0)
	{
		bBufferedPreFirstAimReplayDiscontinuous = true;
	}
	for (const FMD_BufferedAuthoritativeScrapeAim& Buffered
		: BufferedPreFirstAuthoritativeAims)
	{
		if (ExpectedSequence == 0
			|| Buffered.Sample.Sequence != ExpectedSequence)
		{
			bBufferedPreFirstAimReplayDiscontinuous = true;
		}
		ExpectedSequence = Buffered.Sample.Sequence + 1;
	}
	HighestDroppedPreFirstAimSequence = 0;
}

float UMD_ScrapeToolComponent::CalculateAuthoritativeSampleDeltaTime(
	const double PreviousClientSeconds,
	const double CurrentClientSeconds,
	bool& bOutIsDiscontinuous) const
{
	bOutIsDiscontinuous = !FMath::IsFinite(PreviousClientSeconds)
		|| !FMath::IsFinite(CurrentClientSeconds)
		|| CurrentClientSeconds <= PreviousClientSeconds;
	if (bOutIsDiscontinuous)
	{
		return 0.0f;
	}
	const double RawDeltaSeconds =
		CurrentClientSeconds - PreviousClientSeconds;
	const double MaximumContinuousSeconds = static_cast<double>(
		FMath::Max(MaxSampleAgeSeconds, 0.01f));
	if (RawDeltaSeconds > MaximumContinuousSeconds)
	{
		bOutIsDiscontinuous = true;
		return 0.0f;
	}
	return static_cast<float>(FMath::Clamp(
		RawDeltaSeconds,
		1.0 / MaximumAuthoritativeAimRateHz,
		MaximumContinuousSeconds));
}

bool UMD_ScrapeToolComponent::IsAuthoritativeAimPositionDiscontinuous(
	const FMD_ScrapeAimQueryResult& PreviousAim,
	const FMD_ScrapeAimQueryResult& CurrentAim,
	const double MaximumDistanceCm)
{
	if (!PreviousAim.IsActionable() || !CurrentAim.IsActionable())
	{
		return false;
	}
	const double SafeMaximumDistanceCm = FMath::IsFinite(MaximumDistanceCm)
		? FMath::Max(MaximumDistanceCm, 0.0)
		: 0.0;
	return PreviousAim.WorldContactLocation.ContainsNaN()
		|| CurrentAim.WorldContactLocation.ContainsNaN()
		|| FVector::Distance(
			PreviousAim.WorldContactLocation,
			CurrentAim.WorldContactLocation) > SafeMaximumDistanceCm;
}

void UMD_ScrapeToolComponent::ReplayBufferedAuthoritativeAims()
{
	if (!bIsScraping
		|| !bAuthoritativeFirstScrapeAimApproved
		|| AuthoritativeFirstAimSequence == 0)
	{
		return;
	}

	uint32 PreviousSequence = AuthoritativeFirstAimSequence;
	double PreviousClientSeconds =
		AuthoritativeFirstAimClientSampleSeconds;
	FMD_ScrapeAimQueryResult PreviousAim = CurrentScrapeAim;
	bool bForceNewAnchor = bBufferedPreFirstAimReplayDiscontinuous;
	for (const FMD_BufferedAuthoritativeScrapeAim& Buffered
		: BufferedPreFirstAuthoritativeAims)
	{
		if (Buffered.Sample.Sequence <= PreviousSequence)
		{
			continue;
		}
		bool bTimeDiscontinuous = false;
		const float SampleDeltaTime = CalculateAuthoritativeSampleDeltaTime(
			PreviousClientSeconds,
			static_cast<double>(Buffered.Sample.ClientMonotonicSeconds),
			bTimeDiscontinuous);
		const bool bSequenceDiscontinuous = PreviousSequence == MAX_uint32
			|| Buffered.Sample.Sequence != PreviousSequence + 1;
		const bool bPositionDiscontinuous =
			IsAuthoritativeAimPositionDiscontinuous(
				PreviousAim,
				Buffered.ValidatedAim,
				ReconnectCandidateMaxCm);
		const bool bStartsNewAnchor = bForceNewAnchor
			|| bSequenceDiscontinuous
			|| bTimeDiscontinuous
			|| bPositionDiscontinuous;
		if (bStartsNewAnchor)
		{
			ResetStrokeTracking();
			ResetInvalidScrapeAim();
			SetTrackedBarnacle(nullptr);
		}
		CurrentScrapeAim = Buffered.ValidatedAim;
		ProcessScrapeAimResult(
			Buffered.ValidatedAim,
			bStartsNewAnchor ? 0.0f : SampleDeltaTime,
			true,
			bStartsNewAnchor
				? FMath::Max(MaxMissingAimSequences, 0) + 1
				: 0);
		PreviousSequence = Buffered.Sample.Sequence;
		PreviousClientSeconds = static_cast<double>(
			Buffered.Sample.ClientMonotonicSeconds);
		PreviousAim = Buffered.ValidatedAim;
		bForceNewAnchor = false;
	}
	BufferedPreFirstAuthoritativeAims.Reset();
	bBufferedPreFirstAimReplayDiscontinuous = false;
}

bool UMD_ScrapeToolComponent::IsCameraRelativeAimInsideSessionCone(
	const FVector& CameraRelativeDirection) const
{
	if (CameraRelativeDirection.ContainsNaN())
	{
		return false;
	}
	const FVector Direction = CameraRelativeDirection.GetSafeNormal();
	if (Direction.IsNearlyZero() || Direction.X <= UE_SMALL_NUMBER)
	{
		return false;
	}
	const double SafeMargin = FMath::Clamp(
		static_cast<double>(ScrapeFocusPointerSafeMargin), 0.0, 0.49);
	const double MaxScreenCoordinate = 1.0 - (2.0 * SafeMargin);
	if (!FMath::IsFinite(ScrapeFocusAuthorityHorizontalHalfTangent)
		|| !FMath::IsFinite(ScrapeFocusAuthorityVerticalHalfTangent)
		|| ScrapeFocusAuthorityHorizontalHalfTangent <= UE_SMALL_NUMBER
		|| ScrapeFocusAuthorityVerticalHalfTangent <= UE_SMALL_NUMBER)
	{
		return false;
	}
	const double HorizontalLimit =
		static_cast<double>(ScrapeFocusAuthorityHorizontalHalfTangent)
		* MaxScreenCoordinate;
	const double VerticalLimit =
		static_cast<double>(ScrapeFocusAuthorityVerticalHalfTangent)
		* MaxScreenCoordinate;
	const double MarginTangent = FMath::Tan(FMath::DegreesToRadians(
		static_cast<double>(FMath::Max(AimViewConeMarginDegrees, 0.0f))));
	return FMath::Abs(Direction.Y / Direction.X)
		<= HorizontalLimit + MarginTangent
		&& FMath::Abs(Direction.Z / Direction.X)
			<= VerticalLimit + MarginTangent;
}

bool UMD_ScrapeToolComponent::ValidateAndCacheAuthoritativeAim(
	const FMD_ScrapeAimNetSample& Sample,
	const bool bIsReliableFirstAim,
	FMD_ScrapeAimQueryResult& OutAim,
	float& OutSampleDeltaTime,
	int32& OutMissingSequences)
{
	OutAim = FMD_ScrapeAimQueryResult();
	OutSampleDeltaTime = 0.0f;
	OutMissingSequences = 0;
	if (!GetOwner()
		|| !GetOwner()->HasAuthority()
		|| !bAuthoritativeScrapeFocusAccepted
		|| Sample.FocusSessionId == 0
		|| Sample.FocusSessionId != ActiveScrapeFocusSessionId
		|| Sample.Sequence == 0)
	{
		return false;
	}

	const FVector RelativeDirection = FVector(
		Sample.CameraRelativeAimDirection);
	const double DirectionSizeSquared = RelativeDirection.SizeSquared();
	if (RelativeDirection.ContainsNaN()
		|| !FMath::IsFinite(DirectionSizeSquared)
		|| FMath::Abs(DirectionSizeSquared - 1.0) > 0.02
		|| !IsCameraRelativeAimInsideSessionCone(RelativeDirection))
	{
		TerminateAuthoritativeScrapeFocus(
			EMD_ScrapeFocusTerminationReason::ProtocolViolation);
		return false;
	}

	// ASC TargetData와 Pawn RPC는 서로 다른 actor channel이다. 첫 Reliable
	// sample은 아직 승인 전인 현재 session에 한해 newer Unreliable보다 늦게
	// 도착해도 검증한다. Continuous duplicate/old sample은 lease도 갱신하지 않는다.
	const bool bAllowReorderedFirstAim = bIsReliableFirstAim
		&& !bAuthoritativeFirstScrapeAimApproved;
	if (!bAllowReorderedFirstAim
		&& LastAuthoritativeAimSequence != 0
		&& Sample.Sequence <= LastAuthoritativeAimSequence)
	{
		return false;
	}

	const double ServerNow = GetScrapeMonotonicSeconds();
	const double ClientSampleSeconds = static_cast<double>(
		Sample.ClientMonotonicSeconds);
	if (!FMath::IsFinite(ClientSampleSeconds) || ClientSampleSeconds < 0.0)
	{
		TerminateAuthoritativeScrapeFocus(
			EMD_ScrapeFocusTerminationReason::ProtocolViolation);
		return false;
	}
	if (!bAuthoritativeClientClockInitialized)
	{
		AuthoritativeClientClockOffsetSeconds = ServerNow - ClientSampleSeconds;
		bAuthoritativeClientClockInitialized = true;
	}
	const double SampleAgeSeconds = ServerNow
		- (ClientSampleSeconds + AuthoritativeClientClockOffsetSeconds);
	if (SampleAgeSeconds > static_cast<double>(FMath::Max(
			MaxSampleAgeSeconds, 0.01f))
		|| SampleAgeSeconds < -0.05)
	{
		// 구조는 정상이나 너무 오래되거나 미래인 표본은 anchor만 끊고 lease를 갱신하지 않는다.
		ResetStrokeTracking();
		return false;
	}

	const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
	const AController* const OwnerController = OwnerPawn
		? OwnerPawn->GetController()
		: nullptr;
	if (!OwnerController || !IsScrapeToolCurrentlyEligible())
	{
		TerminateAuthoritativeScrapeFocus(
			EMD_ScrapeFocusTerminationReason::EligibilityLost);
		return false;
	}
	FVector ServerEyeLocation = FVector::ZeroVector;
	FRotator ServerViewRotation = FRotator::ZeroRotator;
	OwnerController->GetPlayerViewPoint(ServerEyeLocation, ServerViewRotation);
	if (ServerEyeLocation.ContainsNaN()
		|| ServerViewRotation.ContainsNaN()
		|| FVector::Distance(ServerEyeLocation, ScrapeFocusViewOrigin)
			> FMath::Max(ScrapeFocusViewOriginDriftToleranceCm, 0.0f)
		|| CalculateRotatorErrorDegrees(
			ServerViewRotation,
			ScrapeFocusViewRotation)
			> FMath::Max(BeginBasisRotationToleranceDegrees, 0.0f))
	{
		TerminateAuthoritativeScrapeFocus(
			EMD_ScrapeFocusTerminationReason::ViewInvalid);
		return false;
	}

	const FRotationMatrix Basis(ScrapeFocusViewRotation);
	const FVector WorldDirection =
		(Basis.GetScaledAxis(EAxis::X) * RelativeDirection.X
			+ Basis.GetScaledAxis(EAxis::Y) * RelativeDirection.Y
			+ Basis.GetScaledAxis(EAxis::Z) * RelativeDirection.Z)
		.GetSafeNormal();
	if (WorldDirection.IsNearlyZero())
	{
		TerminateAuthoritativeScrapeFocus(
			EMD_ScrapeFocusTerminationReason::ProtocolViolation);
		return false;
	}

	OutAim = QueryScrapeAim(ServerEyeLocation, WorldDirection);
	const uint32 PreviousSequence = LastAuthoritativeAimSequence;
	const double PreviousClientSeconds = LastAuthoritativeClientSampleSeconds;
	if (!bAllowReorderedFirstAim
		&& PreviousSequence != 0
		&& ClientSampleSeconds <= PreviousClientSeconds)
	{
		ResetStrokeTracking();
		return false;
	}
	if (!bAllowReorderedFirstAim || Sample.Sequence > LastAuthoritativeAimSequence)
	{
		LastAuthoritativeAimSequence = Sample.Sequence;
		LastAuthoritativeClientSampleSeconds = ClientSampleSeconds;
	}
	LastAuthoritativeAimReceiveSeconds = ServerNow;
	bLastAuthoritativeAimWasActionable = OutAim.IsActionable();
	if (PreviousSequence != 0 && Sample.Sequence > PreviousSequence)
	{
		const uint64 SequenceGap = static_cast<uint64>(Sample.Sequence)
			- static_cast<uint64>(PreviousSequence) - 1ull;
		OutMissingSequences = static_cast<int32>(FMath::Min<uint64>(
			SequenceGap, static_cast<uint64>(MAX_int32)));
	}
	if (!bAllowReorderedFirstAim && PreviousSequence != 0)
	{
		bool bTimeDiscontinuous = false;
		OutSampleDeltaTime = CalculateAuthoritativeSampleDeltaTime(
			PreviousClientSeconds,
			ClientSampleSeconds,
			bTimeDiscontinuous);
		if (bTimeDiscontinuous
			|| IsAuthoritativeAimPositionDiscontinuous(
				CurrentScrapeAim,
				OutAim,
				ReconnectCandidateMaxCm))
		{
			OutMissingSequences = FMath::Max(MaxMissingAimSequences, 0) + 1;
		}
	}
	return true;
}

FVector2D UMD_ScrapeToolComponent::ConstrainLookInput(
	const FVector2D& LookInput) const
{
	if (IsScrapeFocusActive())
	{
		return FVector2D::ZeroVector;
	}
	if (!bIsScraping || !bHasLockedStrokeAxis)
	{
		return LookInput;
	}

	const FVector2D SafeAxis = LockedStrokeAxis.GetSafeNormal();
	return SafeAxis.IsNearlyZero()
		? LookInput
		: SafeAxis * FVector2D::DotProduct(LookInput, SafeAxis);
}

bool UMD_ScrapeToolComponent::SetScrapeToolSpec(
	const FMD_ScrapeToolSpec& NewToolSpec)
{
	if (!GetOwner()
		|| !GetOwner()->HasAuthority()
		|| !NewToolSpec.IsValid())
	{
		return false;
	}

	// 성능이 바뀐 프레임에 이전 위치부터 새 범위를 소급 적용하지 않도록 경로를 끊는다.
	ResetStrokeTracking();
	ScrapeToolSpec = NewToolSpec;
	++ScrapeToolSpecGeneration;
	if (ScrapeToolSpecGeneration == 0)
	{
		++ScrapeToolSpecGeneration;
	}
	GetOwner()->ForceNetUpdate();
	return true;
}

void UMD_ScrapeToolComponent::OnRep_ScrapeToolSpec()
{
	// 서버에서 새 스펙을 받은 프레임에 이전 로컬 위치부터 새 범위를 잇지 않는다.
	ResetStrokeTracking();
	++ScrapeToolSpecGeneration;
	if (ScrapeToolSpecGeneration == 0)
	{
		++ScrapeToolSpecGeneration;
	}
}

bool UMD_ScrapeToolComponent::SetScrapeBladeWidthCm(
	const float NewBladeWidthCm)
{
	FMD_ScrapeToolSpec NewToolSpec = ScrapeToolSpec;
	NewToolSpec.BladeWidthCm = NewBladeWidthCm;
	return SetScrapeToolSpec(NewToolSpec);
}

EMD_ScrapeLoadStage UMD_ScrapeToolComponent::CalculateScrapeLoadStage(
	const float Load)
{
	if (!FMath::IsFinite(Load) || Load <= 0.0f)
	{
		return EMD_ScrapeLoadStage::Empty;
	}

	if (Load < ScrapeLoadStage2Threshold)
	{
		return EMD_ScrapeLoadStage::Stage1;
	}

	if (Load < ScrapeLoadStage3Threshold)
	{
		return EMD_ScrapeLoadStage::Stage2;
	}

	return EMD_ScrapeLoadStage::Stage3;
}

float UMD_ScrapeToolComponent::CalculateScrapeLoadGainFromCleanRatioDelta(
	const float CleanRatioDelta,
	const float LoadPerRemovedPercent)
{
	if (!FMath::IsFinite(CleanRatioDelta)
		|| CleanRatioDelta <= 0.0f
		|| !FMath::IsFinite(LoadPerRemovedPercent)
		|| LoadPerRemovedPercent <= 0.0f)
	{
		return 0.0f;
	}

	return FMath::Clamp(CleanRatioDelta, 0.0f, 1.0f)
		* 100.0f
		* LoadPerRemovedPercent;
}

bool UMD_ScrapeToolComponent::SetScrapeLoad(const float NewLoad)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !FMath::IsFinite(NewLoad))
	{
		return false;
	}

	const float PreviousLoad = GetScrapeLoad();
	const float ClampedNewLoad = FMath::Clamp(
		NewLoad,
		0.0f,
		MaximumScrapeLoad);
	if (CalculateScrapeLoadStage(PreviousLoad) == EMD_ScrapeLoadStage::Stage3
		&& CalculateScrapeLoadStage(ClampedNewLoad)
			!= EMD_ScrapeLoadStage::Stage3)
	{
		// 이전 spec/generation으로 생성한 tail을 먼저 확정한다.
		FlushStage3PathTail();
		AdvanceStage3SubStrokeGeneration();
	}
	ScrapeLoad = ClampedNewLoad;
	if (!FMath::IsNearlyEqual(PreviousLoad, ScrapeLoad))
	{
		NotifyScrapeLoadChanged();
		GetOwner()->ForceNetUpdate();
	}
	return true;
}

float UMD_ScrapeToolComponent::AddScrapeLoad(
	const float RequestedAmount)
{
	if (!GetOwner()
		|| !GetOwner()->HasAuthority()
		|| !FMath::IsFinite(RequestedAmount)
		|| RequestedAmount <= 0.0f)
	{
		return 0.0f;
	}

	const float PreviousLoad = GetScrapeLoad();
	ScrapeLoad = FMath::Clamp(
		PreviousLoad + RequestedAmount,
		0.0f,
		MaximumScrapeLoad);
	const float AddedAmount = ScrapeLoad - PreviousLoad;
	if (AddedAmount > 0.0f)
	{
		NotifyScrapeLoadChanged();
		GetOwner()->ForceNetUpdate();
	}

	return AddedAmount;
}

void UMD_ScrapeToolComponent::OnRep_ScrapeLoad()
{
	ScrapeLoad = GetScrapeLoad();
	NotifyScrapeLoadChanged();
}

void UMD_ScrapeToolComponent::NotifyScrapeLoadChanged()
{
	OnScrapeLoadChanged.Broadcast(ScrapeLoad);
}

bool UMD_ScrapeToolComponent::GetCurrentScrapeContact(
	FVector& OutWorldLocation) const
{
	if (!bHasLastContact)
	{
		return false;
	}

	OutWorldLocation = LastContactWorldLocation;
	return true;
}

bool UMD_ScrapeToolComponent::GetCurrentScrapeContactPose(
	FVector& OutWorldLocation,
	FVector& OutBladeWorldDirection) const
{
	if (!bHasLastContact || !bHasLastContactBladeDirection)
	{
		return false;
	}
	OutWorldLocation = LastContactWorldLocation;
	OutBladeWorldDirection = FVector(LastContactBladeWorldDirection);
	return true;
}

void UMD_ScrapeToolComponent::SetCurrentScrapeContact(
	const FVector& WorldLocation)
{
	const bool bHadContact = bHasLastContact;
	LastContactWorldLocation = WorldLocation;
	// 위치를 먼저 기록해야 같은 복제 묶음에서 유효 플래그가 켜질 때 완성된 값을 읽는다.
	bHasLastContact = true;

	if (!bHadContact && GetOwner() && GetOwner()->HasAuthority())
	{
		GetOwner()->ForceNetUpdate();
	}
}

void UMD_ScrapeToolComponent::SetCurrentScrapeBladeDirection(
	UMD_ScrapeSurfaceComponent* const Surface,
	const EMD_ScrapeSurfaceFace SurfaceFace,
	const FVector2D& SurfacePositionCm,
	const FVector2D& BladeDepthAxis,
	const FVector& ReferenceWorldPosition)
{
	const FVector2D SafeAxis = BladeDepthAxis.GetSafeNormal();
	if (!IsValid(Surface) || SafeAxis.IsNearlyZero())
	{
		return;
	}
	FVector StartWorld = FVector::ZeroVector;
	FVector EndWorld = FVector::ZeroVector;
	if (!Surface->TryGetScrapeSurfaceWorldPositionAtDepth(
			SurfacePositionCm,
			SurfaceFace,
			ReferenceWorldPosition,
			StartWorld)
		|| !Surface->TryGetScrapeSurfaceWorldPositionAtDepth(
			SurfacePositionCm + SafeAxis,
			SurfaceFace,
			ReferenceWorldPosition,
			EndWorld))
	{
		return;
	}
	const FVector WorldDirection = (EndWorld - StartWorld).GetSafeNormal();
	if (WorldDirection.IsNearlyZero() || WorldDirection.ContainsNaN())
	{
		return;
	}
	LastContactBladeWorldDirection = WorldDirection;
	bHasLastContactBladeDirection = true;
}

void UMD_ScrapeToolComponent::ClearCurrentScrapeContact()
{
	if (!bHasLastContact)
	{
		return;
	}

	bHasLastContact = false;
	bHasLastContactBladeDirection = false;
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		// Tick이 멈춘 뒤에도 시뮬레이티드 프록시에 해제 상태가 바로 전달되어야 한다.
		GetOwner()->ForceNetUpdate();
	}
}

void UMD_ScrapeToolComponent::StartScraping()
{
	if (bIsScraping)
	{
		return;
	}

	const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
	if (OwnerPawn && OwnerPawn->IsLocallyControlled() && IsScrapeFocusActive()
		&& !(GetOwner() && GetOwner()->HasAuthority()
			&& bAuthoritativeScrapeFocusAccepted))
	{
		// Ability gate와 Commit 직전의 최신 포인터 ray를 동기적으로 확정한다.
		CurrentScrapeAim = QueryCurrentScrapeAim();
	}
	if (GetOwner() && GetOwner()->HasAuthority()
		&& bAuthoritativeScrapeFocusAccepted
		&& !IsAuthoritativeFirstScrapeAimApproved(
			ActiveScrapeFocusSessionId))
	{
		return;
	}
	if (!IsScrapeFocusReady() || !CurrentScrapeAim.IsActionable())
	{
		// 승인된 Ready session과 actionable first aim이 없는 LMB는 저장하지 않는다.
		return;
	}

	bIsScraping = true;
	ResetScrapeFocusPointerStrokeConstraint();
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		// Remote pose state must not wait for the next ordinary actor update.
		GetOwner()->ForceNetUpdate();
	}
	ResetInvalidScrapeAim();
	++LmbStrokeGeneration;
	if (LmbStrokeGeneration == 0)
	{
		++LmbStrokeGeneration;
	}
	Stage3SubStrokeGeneration = 0;
	AdvanceStage3SubStrokeGeneration();
	Stage3SampleSequence = 0;
	CommittedStage3Dabs.Reset();
	if (ScrapeFocusState == EMD_ScrapeFocusState::FocusedReady)
	{
		SetScrapeFocusState(EMD_ScrapeFocusState::FocusedScraping);
	}
	ResetStrokeTracking();
	SetComponentTickEnabled(true);

	// 서버 권위 session은 Reliable first TargetData로 검증한 cache를 정확히 한 번
	// 첫 anchor로 사용한다. 서버 중앙 ControlRotation을 다시 query하지 않는다.
	if (GetOwner() && GetOwner()->HasAuthority()
		&& bAuthoritativeScrapeFocusAccepted)
	{
		if (bAuthoritativeFirstScrapeAimApproved)
		{
			ProcessScrapeAimResult(CurrentScrapeAim, 0.0f, true, 0);
			ReplayBufferedAuthoritativeAims();
		}
		return;
	}
	ProcessScrapeTrace(0.0f);
}

void UMD_ScrapeToolComponent::StopScraping()
{
	const bool bWasScraping = bIsScraping;
	bIsScraping = false;
	ResetScrapeFocusPointerStrokeConstraint();
	ResetStrokeTracking();
	ResetInvalidScrapeAim();
	if (ScrapeFocusState == EMD_ScrapeFocusState::FocusedScraping)
	{
		SetScrapeFocusState(EMD_ScrapeFocusState::FocusedReady);
	}
	if (bWasScraping && GetOwner() && GetOwner()->HasAuthority())
	{
		// Replicate the authoritative pose-off edge together with cleared contact.
		GetOwner()->ForceNetUpdate();
	}
	SetComponentTickEnabled(IsScrapeFocusActive());
}

void UMD_ScrapeToolComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!IsScrapeFocusActive() && !bIsScraping)
	{
		return;
	}

	const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
	const bool bLocallyControlled = OwnerPawn && OwnerPawn->IsLocallyControlled();
	if (bLocallyControlled
		&& IsScrapeFocusActive()
		&& !IsCurrentScrapeFocusViewportAspectCompatible())
	{
		// 비율이 달라진 첫 Tick에서 aim heartbeat/판정을 만들기 전에 정리한다.
		TerminateScrapeFocusFromComponent();
		return;
	}
	if (ScrapeFocusState == EMD_ScrapeFocusState::FocusPending
		&& bLocallyControlled)
	{
		FocusPendingElapsedSeconds += FMath::IsFinite(DeltaTime)
			? FMath::Max(static_cast<double>(DeltaTime), 0.0)
			: 0.0;
		if (FocusPendingElapsedSeconds
			>= static_cast<double>(FMath::Max(BeginAckTimeoutSeconds, 0.01f)))
		{
			EndScrapeFocusInternal(true);
			return;
		}
	}

	if (GetOwner() && GetOwner()->HasAuthority()
		&& bAuthoritativeScrapeFocusAccepted
		&& ActiveScrapeFocusSessionId != 0)
	{
		const double SilenceSeconds = GetScrapeMonotonicSeconds()
			- LastAuthoritativeAimReceiveSeconds;
		if (!IsScrapeToolCurrentlyEligible())
		{
			TerminateAuthoritativeScrapeFocus(
				EMD_ScrapeFocusTerminationReason::EligibilityLost);
			return;
		}
		if (SilenceSeconds >= static_cast<double>(
				FMath::Max(AimSilenceTimeoutSeconds, 0.01f)))
		{
			TerminateAuthoritativeScrapeFocus(
				EMD_ScrapeFocusTerminationReason::SessionTimeout);
			return;
		}
	}

	if (IsScrapeFocusActive() && bHasScrapeFocusView)
	{
		if (!IsScrapeFocusViewOriginValid())
		{
			// 허용치를 넘는 물리 밀림·서버 보정·텔레포트는 이전 점과 연결하지 않는다.
			TerminateScrapeFocusFromComponent();
			return;
		}
	}

	if (bLocallyControlled && IsScrapeFocusReady())
	{
		SendLocalAimIfDue(DeltaTime);
	}

	// authority session은 host/remote 모두 RPC가 검증한 표본을 그 RPC에서 정확히
	// 한 번 처리한다. 중앙/local query로 같은 이동을 Tick에서 다시 적용하지 않는다.
	if (GetOwner() && GetOwner()->HasAuthority()
		&& bAuthoritativeScrapeFocusAccepted)
	{
		return;
	}

	if (bIsScraping)
	{
		ProcessScrapeTrace(DeltaTime);
	}
	else
	{
		ProcessReadyScrapeAim(DeltaTime);
	}
}

void UMD_ScrapeToolComponent::ProcessReadyScrapeAim(const float DeltaTime)
{
	ProcessReadyScrapeAimResult(QueryCurrentScrapeAim(), DeltaTime);
}

void UMD_ScrapeToolComponent::ProcessReadyScrapeAimResult(
	const FMD_ScrapeAimQueryResult& AimResult,
	const float SampleDeltaTime)
{
	CurrentScrapeAim = AimResult;
	if (AimResult.IsActionable())
	{
		SetCurrentScrapeContact(AimResult.WorldContactLocation);
		ResetInvalidScrapeAim();
		return;
	}

	ClearCurrentScrapeContact();
	SetTrackedBarnacle(nullptr);
	HandleInvalidScrapeAim(SampleDeltaTime, false);
}

void UMD_ScrapeToolComponent::BeginInvalidScrapeAim()
{
	if (bHasInvalidScrapeAim)
	{
		return;
	}

	bHasInvalidScrapeAim = true;
	InvalidScrapeAimElapsedSeconds = 0.0;
	bClosedStrokeForInvalidScrapeAim = false;
	bHasScrapeReconnectAnchor = bHasPreviousSurfacePosition
		&& PreviousSurface.IsValid();
	ScrapeReconnectSurface = bHasScrapeReconnectAnchor
		? PreviousSurface
		: TWeakObjectPtr<UMD_ScrapeSurfaceComponent>();
	ScrapeReconnectSurfaceFace = PreviousSurfaceFace;
	ScrapeReconnectPositionCm = bHasScrapeReconnectAnchor
		? PreviousSurfacePositionCm
		: FVector2D::ZeroVector;
	ScrapeReconnectSurfaceHit = bHasLastScrapeSurfaceHit
		? LastScrapeSurfaceHit
		: FHitResult();
}

void UMD_ScrapeToolComponent::ResetInvalidScrapeAim()
{
	bHasInvalidScrapeAim = false;
	InvalidScrapeAimElapsedSeconds = 0.0;
	bClosedStrokeForInvalidScrapeAim = false;
	bHasScrapeReconnectAnchor = false;
	ScrapeReconnectSurface.Reset();
	ScrapeReconnectSurfaceFace = EMD_ScrapeSurfaceFace::Front;
	ScrapeReconnectPositionCm = FVector2D::ZeroVector;
	ScrapeReconnectSurfaceHit = FHitResult();
}

FMD_ScrapeReacquireEvaluation
UMD_ScrapeToolComponent::EvaluateScrapeReacquisition(
	const bool bHasValidSurface,
	const bool bHasPriorStroke,
	const bool bSameSurfaceAndFace,
	const double InvalidElapsedSeconds,
	const double ReconnectDistanceCm,
	const double CandidateMaxCm,
	const double EffectiveBridgeMaxCm,
	const bool bBridgeVisibilityValid,
	const bool bSequenceGapKnown,
	const int32 MissingAimSequences,
	const int32 MaximumMissingAimSequences,
	const double GraceSeconds,
	const double PersistentExitSeconds)
{
	FMD_ScrapeReacquireEvaluation Evaluation;
	const double SafeElapsedSeconds = FMath::IsFinite(InvalidElapsedSeconds)
		? FMath::Max(InvalidElapsedSeconds, 0.0)
		: TNumericLimits<double>::Max();
	const double SafePersistentExitSeconds =
		FMath::IsFinite(PersistentExitSeconds)
		? FMath::Max(PersistentExitSeconds, 0.01)
		: 0.01;
	if (SafeElapsedSeconds >= SafePersistentExitSeconds)
	{
		Evaluation.Decision = EMD_ScrapeReacquireDecision::TerminateFocus;
		return Evaluation;
	}

	if (!bHasValidSurface)
	{
		Evaluation.Decision = EMD_ScrapeReacquireDecision::KeepWaiting;
		return Evaluation;
	}

	if (!bHasPriorStroke)
	{
		Evaluation.Decision = EMD_ScrapeReacquireDecision::StartNewStroke;
		return Evaluation;
	}

	const double SafeDistanceCm = FMath::IsFinite(ReconnectDistanceCm)
		? FMath::Max(ReconnectDistanceCm, 0.0)
		: TNumericLimits<double>::Max();
	const double SafeCandidateMaxCm = FMath::IsFinite(CandidateMaxCm)
		? FMath::Max(CandidateMaxCm, 0.0)
		: 0.0;
	const double SafeBridgeMaxCm = FMath::IsFinite(EffectiveBridgeMaxCm)
		? FMath::Max(EffectiveBridgeMaxCm, 0.0)
		: 0.0;
	const double SafeGraceSeconds = FMath::IsFinite(GraceSeconds)
		? FMath::Max(GraceSeconds, 0.0)
		: 0.0;
	const bool bSequenceGapAllowed = !bSequenceGapKnown
		|| (MissingAimSequences >= 0
			&& MissingAimSequences <= FMath::Max(MaximumMissingAimSequences, 0));
	Evaluation.bIsReconnectCandidate = bSameSurfaceAndFace
		&& bSequenceGapAllowed
		&& SafeDistanceCm <= SafeCandidateMaxCm;
	Evaluation.bCanBridge = Evaluation.bIsReconnectCandidate
		&& bBridgeVisibilityValid
		&& SafeElapsedSeconds <= SafeGraceSeconds
		&& SafeDistanceCm <= SafeBridgeMaxCm;
	Evaluation.Decision = Evaluation.bCanBridge
		? EMD_ScrapeReacquireDecision::ContinueBridge
		: EMD_ScrapeReacquireDecision::StartNewStroke;
	return Evaluation;
}

bool UMD_ScrapeToolComponent::HandleInvalidScrapeAim(
	const float DeltaTime,
	const bool bMayCloseStroke)
{
	BeginInvalidScrapeAim();
	if (FMath::IsFinite(DeltaTime) && DeltaTime > 0.0f)
	{
		InvalidScrapeAimElapsedSeconds += static_cast<double>(DeltaTime);
	}

	const double GraceSeconds = FMath::IsFinite(TransientMissGraceSeconds)
		? FMath::Max(static_cast<double>(TransientMissGraceSeconds), 0.0)
		: 0.0;
	if (bMayCloseStroke
		&& !bClosedStrokeForInvalidScrapeAim
		&& InvalidScrapeAimElapsedSeconds > GraceSeconds)
	{
		ResetStrokeTracking();
		bClosedStrokeForInvalidScrapeAim = true;
	}

	const FMD_ScrapeReacquireEvaluation Evaluation =
		EvaluateScrapeReacquisition(
			false,
			bHasScrapeReconnectAnchor,
			false,
			InvalidScrapeAimElapsedSeconds,
			0.0,
			ReconnectCandidateMaxCm,
			0.0,
			false,
			false,
			0,
			MaxMissingAimSequences,
			TransientMissGraceSeconds,
			PersistentInvalidExitSeconds);
	if (Evaluation.Decision == EMD_ScrapeReacquireDecision::TerminateFocus)
	{
		TerminateScrapeFocusFromComponent();
		return true;
	}

	return false;
}

bool UMD_ScrapeToolComponent::ValidateReconnectBridge(
	const FMD_ScrapeAimQueryResult& AimResult,
	const double EffectiveBridgeMaxCm) const
{
	UMD_ScrapeSurfaceComponent* const Surface = AimResult.Surface;
	if (!bHasScrapeReconnectAnchor
		|| !IsValid(Surface)
		|| ScrapeReconnectSurface.Get() != Surface
		|| ScrapeReconnectSurfaceFace != AimResult.SurfaceFace
		|| IsValid(AimResult.Barnacle)
		|| !FMath::IsFinite(EffectiveBridgeMaxCm)
		|| EffectiveBridgeMaxCm <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const FVector2D MidpointCm =
		(ScrapeReconnectPositionCm + AimResult.SurfacePositionCm) * 0.5;
	const FVector ReferenceWorldPosition =
		(ScrapeReconnectSurfaceHit.ImpactPoint + AimResult.Hit.ImpactPoint) * 0.5;
	FVector MidpointWorldPosition = FVector::ZeroVector;
	if (!Surface->TryGetScrapeSurfaceWorldPositionAtDepth(
			MidpointCm,
			AimResult.SurfaceFace,
			ReferenceWorldPosition,
			MidpointWorldPosition))
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

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	OwnerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector AimDirection = MidpointWorldPosition - ViewLocation;
	if (ViewLocation.ContainsNaN()
		|| AimDirection.ContainsNaN()
		|| AimDirection.IsNearlyZero())
	{
		return false;
	}

	const FMD_ScrapeAimQueryResult MidpointAim =
		QueryScrapeAim(ViewLocation, AimDirection.GetSafeNormal());
	const double PositionToleranceCm = FMath::Max(
		EffectiveBridgeMaxCm,
		0.25) + 0.01;
	return MidpointAim.Type == EMD_ScrapeAimQueryType::SurfaceHit
		&& MidpointAim.Surface == Surface
		&& MidpointAim.SurfaceFace == AimResult.SurfaceFace
		&& !IsValid(MidpointAim.Barnacle)
		&& FVector2D::Distance(
			MidpointAim.SurfacePositionCm,
			MidpointCm) <= PositionToleranceCm;
}

bool UMD_ScrapeToolComponent::PrepareReacquiredSurface(
	const FMD_ScrapeAimQueryResult& AimResult,
	const bool bSequenceGapKnown,
	const int32 MissingAimSequences)
{
	if (!bHasInvalidScrapeAim)
	{
		return true;
	}

	UMD_ScrapeSurfaceComponent* const Surface = AimResult.Surface;
	const bool bSameSurfaceAndFace = bHasScrapeReconnectAnchor
		&& ScrapeReconnectSurface.Get() == Surface
		&& ScrapeReconnectSurfaceFace == AimResult.SurfaceFace;
	const double ReconnectDistanceCm = bHasScrapeReconnectAnchor
		? FVector2D::Distance(
			ScrapeReconnectPositionCm,
			AimResult.SurfacePositionCm)
		: TNumericLimits<double>::Max();
	const double EvaluatorSpacingCm = IsValid(Surface)
		? Surface->GetScrapeSampleSpacingCm(ScrapeToolSpec)
		: 0.0;
	const double UserBridgeMaxCm = FMath::IsFinite(ReconnectBridgeMaxCm)
		? FMath::Max(static_cast<double>(ReconnectBridgeMaxCm), 0.0)
		: 0.0;
	const double EffectiveBridgeMaxCm =
		FMath::IsFinite(EvaluatorSpacingCm) && EvaluatorSpacingCm > 0.0
		? FMath::Min(EvaluatorSpacingCm, UserBridgeMaxCm)
		: 0.0;
	const bool bCouldBridge = bSameSurfaceAndFace
		&& InvalidScrapeAimElapsedSeconds
			<= static_cast<double>(FMath::Max(TransientMissGraceSeconds, 0.0f))
		&& ReconnectDistanceCm
			<= static_cast<double>(FMath::Max(ReconnectCandidateMaxCm, 0.0f))
		&& ReconnectDistanceCm <= EffectiveBridgeMaxCm;
	const bool bBridgeVisibilityValid = bCouldBridge
		&& ValidateReconnectBridge(AimResult, EffectiveBridgeMaxCm);

	const FMD_ScrapeReacquireEvaluation Evaluation =
		EvaluateScrapeReacquisition(
			IsValid(Surface),
			bHasScrapeReconnectAnchor,
			bSameSurfaceAndFace,
			InvalidScrapeAimElapsedSeconds,
			ReconnectDistanceCm,
			ReconnectCandidateMaxCm,
			EffectiveBridgeMaxCm,
			bBridgeVisibilityValid,
			bSequenceGapKnown,
			MissingAimSequences,
			MaxMissingAimSequences,
			TransientMissGraceSeconds,
			PersistentInvalidExitSeconds);

	if (Evaluation.Decision == EMD_ScrapeReacquireDecision::ContinueBridge)
	{
		ResetInvalidScrapeAim();
		return true;
	}
	if (Evaluation.Decision == EMD_ScrapeReacquireDecision::TerminateFocus)
	{
		TerminateScrapeFocusFromComponent();
		return false;
	}

	if (!bClosedStrokeForInvalidScrapeAim)
	{
		ResetStrokeTracking();
	}
	ResetInvalidScrapeAim();
	return true;
}

void UMD_ScrapeToolComponent::ProcessScrapeTrace(const float DeltaTime)
{
	ProcessScrapeAimResult(
		QueryCurrentScrapeAim(),
		DeltaTime,
		false,
		0);
}

void UMD_ScrapeToolComponent::ProcessScrapeAimResult(
	const FMD_ScrapeAimQueryResult& AimResult,
	const float SampleDeltaTime,
	const bool bSequenceGapKnown,
	const int32 MissingAimSequences)
{
	if (bSequenceGapKnown
		&& MissingAimSequences > FMath::Max(MaxMissingAimSequences, 0))
	{
		// 유실 구간을 작은 공간 jump라는 이유로 연결하지 않는다.
		ResetStrokeTracking();
		ResetInvalidScrapeAim();
		SetTrackedBarnacle(nullptr);
	}
	else if (bSequenceGapKnown
		&& MissingAimSequences > 0
		&& bHasPreviousSurfacePosition)
	{
		// 명시 NoContact가 오지 않았어도 유실 sequence 자체를 transient miss로
		// 취급해 sample-spacing/중간 Trace bridge 조건을 동일하게 적용한다.
		BeginInvalidScrapeAim();
		InvalidScrapeAimElapsedSeconds = FMath::Max(
			InvalidScrapeAimElapsedSeconds,
			static_cast<double>(FMath::Max(SampleDeltaTime, 0.0f)));
	}
	if (IsScrapeFocusActive())
	{
		CurrentScrapeAim = AimResult;
	}

	if (AimResult.Type == EMD_ScrapeAimQueryType::DirectBarnacleOnly
		&& IsValid(AimResult.Barnacle))
	{
		// A direct barnacle remains actionable, but it cannot stay connected to
		// the previous surface path.
		ResetStrokeTracking();
		ResetInvalidScrapeAim();
		ProcessDirectBarnacleContact(
			AimResult.Barnacle,
			AimResult.BarnacleHit,
			SampleDeltaTime);
		return;
	}

	if (AimResult.Type != EMD_ScrapeAimQueryType::SurfaceHit)
	{
		ClearCurrentScrapeContact();
		SetTrackedBarnacle(nullptr);
		HandleInvalidScrapeAim(SampleDeltaTime, true);
		return;

		// 오염 표면이 아니어도 따개비일 수 있다. 둘은 성격이 달라 경로를 갈라 둔다.
	}

	FHitResult CurrentHit = AimResult.Hit;
	UMD_ScrapeSurfaceComponent* const CurrentSurface = AimResult.Surface;
	AMD_Barnacle* const OccludingBarnacle = AimResult.Barnacle;
	if (!IsValid(CurrentSurface))
	{
		ClearCurrentScrapeContact();
		SetTrackedBarnacle(nullptr);
		HandleInvalidScrapeAim(SampleDeltaTime, true);
		return;
	}

	if (!PrepareReacquiredSurface(
			AimResult,
			bSequenceGapKnown,
			MissingAimSequences))
	{
		return;
	}

	const bool bHasContinuousSurfaceTarget = bHasPreviousSurfacePosition
		&& PreviousSurface.Get() == CurrentSurface
		&& PreviousSurfaceFace == AimResult.SurfaceFace;
	const double ValidSampleJumpCm = bHasContinuousSurfaceTarget
		? FVector2D::Distance(
			PreviousSurfacePositionCm,
			AimResult.SurfacePositionCm)
		: 0.0;
	const double MaximumContinuousJumpCm =
		FMath::IsFinite(ReconnectCandidateMaxCm)
		? FMath::Max(static_cast<double>(ReconnectCandidateMaxCm), 0.0)
		: 0.0;
	if (bHasContinuousSurfaceTarget
		&& (!FMath::IsFinite(ValidSampleJumpCm)
			|| ValidSampleJumpCm > MaximumContinuousJumpCm))
	{
		// A large valid-to-valid jump is also a discontinuity. Never fill it with
		// a long segment merely because no intermediate miss sample was observed.
		ResetStrokeTracking();
	}

	SetCurrentScrapeContact(CurrentHit.ImpactPoint);

	if (IsValid(OccludingBarnacle))
	{
		// 클라이언트에서 부착 복제가 BeginPlay보다 늦어도 직접 맞은 순간 복구한다.
		// 장애물 등록이 있어야 아래 경로 차단과 조준 보정이 함께 작동한다.
		OccludingBarnacle->RegisterWithScrapeSurface(CurrentSurface);
	}

	const FVector2D RawSurfacePositionCm = AimResult.SurfacePositionCm;
	const EMD_ScrapeSurfaceFace CurrentSurfaceFace = AimResult.SurfaceFace;

	if (!bHasPreviousSurfacePosition
		|| !PreviousSurface.IsValid()
		|| PreviousSurface.Get() != CurrentSurface
		|| PreviousSurfaceFace != CurrentSurfaceFace)
	{
		if (bHasPreviousSurfacePosition || PreviousSurface.IsValid())
		{
			ResetStrokeTracking();
		}
		BeginStrokeTracking(
			CurrentSurface,
			RawSurfacePositionCm,
			CurrentSurfaceFace);
		LastScrapeSurfaceHit = CurrentHit;
		bHasLastScrapeSurfaceHit = true;
		return;
	}
	LastScrapeSurfaceHit = CurrentHit;
	bHasLastScrapeSurfaceHit = true;

	TArray<FMD_ScrapeToolPathSegment> PathSegments;
	if (FMath::IsFinite(SampleDeltaTime) && SampleDeltaTime > 0.0f)
	{
		PendingScrapePathSampleTimeSeconds +=
			static_cast<double>(SampleDeltaTime);
	}
	if (!ConsumeRawScrapePathPoint(RawSurfacePositionCm, PathSegments))
	{
		PreviousSurfacePositionCm = RawSurfacePositionCm;
		return;
	}

	double EmittedTravelCm = 0.0;
	for (const FMD_ScrapeToolPathSegment& PathSegment : PathSegments)
	{
		if (!PathSegment.bStartsNewSubStroke)
		{
			EmittedTravelCm += FMath::Max(
				static_cast<double>(PathSegment.TravelDistanceCm), 0.0);
		}
	}
	const double RemainingTravelCm = bHasStableBladeDepthAxis
		? FMath::Max(
			ScrapePathArcLengthCm - LastEmittedScrapePathArcLengthCm,
			0.0)
		: FMath::Max(PendingScrapePathArcLengthCm, 0.0);
	const double TimedTravelCm = EmittedTravelCm + RemainingTravelCm;
	const double AvailablePathTimeSeconds = FMath::Max(
		PendingScrapePathSampleTimeSeconds, 0.0);
	const double EmittedTimeSeconds = TimedTravelCm > UE_DOUBLE_SMALL_NUMBER
		? AvailablePathTimeSeconds * (EmittedTravelCm / TimedTravelCm)
		: 0.0;
	PendingScrapePathSampleTimeSeconds = FMath::Max(
		AvailablePathTimeSeconds - EmittedTimeSeconds, 0.0);

	for (const FMD_ScrapeToolPathSegment& PathSegment : PathSegments)
	{
		if (PathSegment.bStartsNewSubStroke)
		{
			SplitScrapeSubStrokeAtCorner(PathSegment.StartPositionCm);
			continue;
		}

		FVector2D AppliedEndPositionCm = PathSegment.EndPositionCm;
		AMD_Barnacle* BlockingBarnacle = nullptr;
		const float SegmentDeltaTime = EmittedTravelCm > UE_DOUBLE_SMALL_NUMBER
			? static_cast<float>(EmittedTimeSeconds
				* (static_cast<double>(PathSegment.TravelDistanceCm)
					/ EmittedTravelCm))
			: 0.0f;
		SetCurrentScrapeBladeDirection(
			CurrentSurface,
			CurrentSurfaceFace,
			PathSegment.EndPositionCm,
			PathSegment.EndBladeDepthAxis,
			CurrentHit.ImpactPoint);
		if (ApplyScrapePathSegment(
				CurrentSurface,
				CurrentSurfaceFace,
				CurrentHit,
				PathSegment,
				SegmentDeltaTime,
				AppliedEndPositionCm,
				BlockingBarnacle))
		{
			RebaseScrapePathAfterObstacle(AppliedEndPositionCm);
			return;
		}
	}

	SetTrackedBarnacle(nullptr);
	PreviousSurfacePositionCm = RawSurfacePositionCm;
	return;

}

void UMD_ScrapeToolComponent::DispatchAuthoritativeScrapeResult(
	UMD_ScrapeSurfaceComponent* Surface,
	const FMD_ScrapeResult& Result,
	const FVector& ContactLocation)
{
	// 서버 게임 로직은 권한 인스턴스의 공통 표면 이벤트를 구독할 수 있다.
	OnScrapeSurfaceApplied.Broadcast(Surface, Result);
	if (AMD_CuttableWall* const Wall = Surface
		? Cast<AMD_CuttableWall>(Surface->GetOwner())
		: nullptr)
	{
		OnScrapeApplied.Broadcast(Wall, Result);
	}

	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (OwnerPawn
		&& OwnerPawn->IsLocallyControlled()
		&& GetNetMode() != NM_DedicatedServer)
	{
		EmitFeedback(Surface, Result, ContactLocation);
		return;
	}

	// 원격 소유자의 사운드/이펙트와 Blueprint 피드백은 해당 클라이언트에서 실행한다.
	ClientReceiveScrapeResult(Surface, Result, ContactLocation);
}

void UMD_ScrapeToolComponent::ClientReceiveScrapeResult_Implementation(
	UMD_ScrapeSurfaceComponent* Surface,
	const FMD_ScrapeResult& Result,
	const FVector_NetQuantize10 ContactLocation)
{
	OnScrapeSurfaceApplied.Broadcast(Surface, Result);
	if (AMD_CuttableWall* const Wall = Surface
		? Cast<AMD_CuttableWall>(Surface->GetOwner())
		: nullptr)
	{
		OnScrapeApplied.Broadcast(Wall, Result);
	}
	EmitFeedback(Surface, Result, ContactLocation);
}

FMD_ScrapeAimQueryResult UMD_ScrapeToolComponent::QueryCurrentScrapeAim() const
{
	const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->GetController() || !GetWorld())
	{
		return FMD_ScrapeAimQueryResult();
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	OwnerPawn->GetController()->GetPlayerViewPoint(ViewLocation, ViewRotation);
	if (IsScrapeFocusActive() && bHasScrapeFocusView)
	{
		return QueryScrapeAim(ViewLocation, GetScrapeFocusAimDirection());
	}
	return QueryScrapeAim(ViewLocation, ViewRotation.Vector());
}

FMD_ScrapeAimQueryResult UMD_ScrapeToolComponent::QueryScrapeAim(
	const FVector& TraceStart,
	const FVector& AimDirection) const
{
	FMD_ScrapeAimQueryResult Result;
	const float EffectiveReachCm = GetEffectiveScrapeReachCm();
	if (!GetWorld()
		|| TraceStart.ContainsNaN()
		|| AimDirection.ContainsNaN()
		|| EffectiveReachCm <= UE_SMALL_NUMBER)
	{
		return Result;
	}

	const FVector SafeAimDirection = AimDirection.GetSafeNormal();
	if (SafeAimDirection.IsNearlyZero())
	{
		return Result;
	}

	FHitResult SurfaceHit;
	FHitResult BarnacleHit;
	UMD_ScrapeSurfaceComponent* Surface = nullptr;
	AMD_Barnacle* Barnacle = nullptr;
	const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
	const bool bHasSurface = TryTraceScrapeTarget(
		TraceStart,
		TraceStart + (SafeAimDirection * EffectiveReachCm),
		OwnerPawn,
		SurfaceHit,
		Surface,
		Barnacle,
		BarnacleHit);

	Result.Surface = Surface;
	Result.Barnacle = Barnacle;
	Result.BarnacleHit = BarnacleHit;
	if (bHasSurface && IsValid(Surface))
	{
		Result.Hit = SurfaceHit;
		if (!Surface->TryGetScrapeSurfacePositionFromHit(
				SurfaceHit,
				Result.SurfacePositionCm,
				Result.SurfaceFace))
		{
			// 충돌은 작업면이었지만 같은 표면 좌표계로 해석할 수 없으므로 진입 불가다.
			Result.Surface = nullptr;
			Result.bWasBlocked = true;
			return Result;
		}

		Result.Type = EMD_ScrapeAimQueryType::SurfaceHit;
		Result.WorldContactLocation = SurfaceHit.ImpactPoint;
		Result.WorldContactNormal = SurfaceHit.ImpactNormal;
		Result.TraceDistanceCm = static_cast<float>(FVector::Distance(
			TraceStart,
			SurfaceHit.ImpactPoint));
		return Result;
	}

	if (IsValid(Barnacle) && BarnacleHit.bBlockingHit)
	{
		// 뒤 trace가 빗나가거나 일반 물체에 다시 막혀도 첫 따개비 직접 접촉은 유효하다.
		Result.Type = EMD_ScrapeAimQueryType::DirectBarnacleOnly;
		Result.Hit = BarnacleHit;
		Result.WorldContactLocation = BarnacleHit.ImpactPoint;
		Result.WorldContactNormal = BarnacleHit.ImpactNormal;
		Result.TraceDistanceCm = static_cast<float>(FVector::Distance(
			TraceStart,
			BarnacleHit.ImpactPoint));
		return Result;
	}

	// TryTraceScrapeTarget는 일반 물체를 첫 충돌로 보존해 NoTarget과 가림을 구분한다.
	Result.Hit = SurfaceHit;
	Result.bWasBlocked = SurfaceHit.bBlockingHit;
	if (Result.bWasBlocked)
	{
		Result.WorldContactLocation = SurfaceHit.ImpactPoint;
		Result.WorldContactNormal = SurfaceHit.ImpactNormal;
		Result.TraceDistanceCm = static_cast<float>(FVector::Distance(
			TraceStart,
			SurfaceHit.ImpactPoint));
	}
	return Result;
}

bool UMD_ScrapeToolComponent::TryGetCurrentSurfaceHit(
	FHitResult& OutHit,
	UMD_ScrapeSurfaceComponent*& OutSurface,
	AMD_Barnacle*& OutOccludingBarnacle,
	FHitResult& OutBarnacleHit) const
{
	OutHit = FHitResult();
	OutSurface = nullptr;
	OutOccludingBarnacle = nullptr;
	OutBarnacleHit = FHitResult();

	const FMD_ScrapeAimQueryResult Result = QueryCurrentScrapeAim();
	OutHit = Result.Hit;
	OutSurface = Result.Surface;
	OutOccludingBarnacle = Result.Barnacle;
	OutBarnacleHit = Result.BarnacleHit;
	return Result.Type == EMD_ScrapeAimQueryType::SurfaceHit;
}

bool UMD_ScrapeToolComponent::TryTraceScrapeTarget(
	const FVector& TraceStart,
	const FVector& TraceEnd,
	const AActor* const IgnoredActor,
	FHitResult& OutHit,
	UMD_ScrapeSurfaceComponent*& OutSurface,
	AMD_Barnacle*& OutOccludingBarnacle,
	FHitResult& OutBarnacleHit) const
{
	OutHit = FHitResult();
	OutSurface = nullptr;
	OutOccludingBarnacle = nullptr;
	OutBarnacleHit = FHitResult();
	if (!GetWorld() || TraceStart.ContainsNaN() || TraceEnd.ContainsNaN())
	{
		return false;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MD_ScrapeTrace), true);
	QueryParams.AddIgnoredActor(IgnoredActor);

	FHitResult FirstHit;
	if (!GetWorld()->LineTraceSingleByChannel(
			FirstHit,
			TraceStart,
			TraceEnd,
			TraceChannel,
			QueryParams))
	{
		return false;
	}

	OutHit = FirstHit;
	OutSurface = UMD_ScrapeSurfaceComponent::FindForPrimitiveComponent(
		FirstHit.GetComponent());
	if (IsValid(OutSurface))
	{
		return true;
	}

	OutOccludingBarnacle = Cast<AMD_Barnacle>(FirstHit.GetActor());
	if (!IsValid(OutOccludingBarnacle))
	{
		return false;
	}
	OutBarnacleHit = FirstHit;

	// 따개비가 Visibility를 막아도 그 뒤의 작업면 좌표가 있어야 헤라 경로를
	// 따개비 앞에서 자를 수 있다. 첫 따개비만 무시하고 같은 선을 다시 쏜다.
	QueryParams.AddIgnoredActor(OutOccludingBarnacle);
	FHitResult SurfaceHit;
	if (!GetWorld()->LineTraceSingleByChannel(
			SurfaceHit,
			TraceStart,
			TraceEnd,
			TraceChannel,
			QueryParams))
	{
		return false;
	}

	OutSurface = UMD_ScrapeSurfaceComponent::FindForPrimitiveComponent(
		SurfaceHit.GetComponent());
	if (!IsValid(OutSurface))
	{
		return false;
	}

	OutHit = SurfaceHit;
	return true;
}

void UMD_ScrapeToolComponent::ProcessDirectBarnacleContact(
	AMD_Barnacle* Barnacle,
	const FHitResult& Hit,
	const float DeltaTime)
{
	if (!IsValid(Barnacle) || Barnacle->IsFullyRemoved())
	{
		SetTrackedBarnacle(nullptr);
		return;
	}

	SetTrackedBarnacle(Barnacle);

	// 처음 맞췄으면 기준점만 잡는다. 옮겨오는 동안의 시선 이동을 속도로 세면
	// 겨냥하는 것만으로 첫 타격이 들어간다.
	if (!bHasPreviousBarnacleHitLocation)
	{
		PreviousBarnacleHitLocation = Hit.ImpactPoint;
		bHasPreviousBarnacleHitLocation = true;
		return;
	}

	const FVector TravelDelta = Hit.ImpactPoint - PreviousBarnacleHitLocation;
	const float TravelCentimeters = static_cast<float>(TravelDelta.Size());

	// 속도는 프레임 간 변화량이므로 문턱 미만이어도 기준점을 매 프레임 갱신한다.
	// 갱신을 미루면 여러 프레임이 한 프레임의 이동으로 뭉쳐 속도가 부풀려진다.
	PreviousBarnacleHitLocation = Hit.ImpactPoint;

	DeliverBarnacleImpact(
		Barnacle,
		CalculateImpactSpeedCmPerSecond(TravelCentimeters, DeltaTime),
		TravelDelta);
}

void UMD_ScrapeToolComponent::DeliverBarnacleImpact(
	AMD_Barnacle* Barnacle,
	const float ImpactSpeedCmPerSecond,
	const FVector& ImpactWorldDirection)
{
	const float MinimumImpactSpeed = FMath::Max(
		0.0f,
		ScrapeToolSpec.BarnacleMinimumImpactSpeedCmPerSecond);

	// 스윙이 끝나 속도가 충분히 떨어지면 다음 타격을 준비한다. 최소 속도를 그대로
	// 빗장으로 쓰면 그 근처에서 진동할 때 준비와 발사가 매 프레임 번갈아 일어난다.
	if (ImpactSpeedCmPerSecond <= MinimumImpactSpeed * 0.5f)
	{
		bBarnacleImpactArmed = true;
	}

	if (!IsValid(Barnacle)
		|| !bBarnacleImpactArmed
		|| ImpactSpeedCmPerSecond < MinimumImpactSpeed)
	{
		return;
	}

	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		// 클라이언트에서도 빗장은 걸어 둔다. 서버로 옮겨도 리듬이 같아야 한다.
		bBarnacleImpactArmed = false;
		return;
	}

	bBarnacleImpactArmed = false;

	if (Barnacle->ApplyScrapeImpact(
			ScrapeToolSpec.CalculateBarnacleImpactWork(ImpactSpeedCmPerSecond),
			ImpactWorldDirection))
	{
		AddScrapeLoad(ScrapeLoadPerRemovedBarnacle);

		// 떨어져 나가면 액터가 파괴되므로 추적을 끊는다.
		SetTrackedBarnacle(nullptr);
	}
}

void UMD_ScrapeToolComponent::SetTrackedBarnacle(AMD_Barnacle* Barnacle)
{
	if (TrackedBarnacle.Get() == Barnacle)
	{
		return;
	}

	TrackedBarnacle = Barnacle;
	PreviousBarnacleHitLocation = FVector::ZeroVector;
	bHasPreviousBarnacleHitLocation = false;
	bBarnacleImpactArmed = true;
}

float UMD_ScrapeToolComponent::CalculateImpactSpeedCmPerSecond(
	const float TravelCentimeters,
	const float DeltaTime)
{
	// StartScraping이 첫 프레임을 DeltaTime 0으로 부른다. 여기서 막지 않으면
	// 입력을 누른 순간 무한대 속도로 따개비가 떨어진다.
	if (!FMath::IsFinite(TravelCentimeters)
		|| TravelCentimeters <= 0.0f
		|| !FMath::IsFinite(DeltaTime)
		|| DeltaTime <= UE_SMALL_NUMBER)
	{
		return 0.0f;
	}

	return TravelCentimeters / DeltaTime;
}

void UMD_ScrapeToolComponent::BeginStrokeTracking(
	UMD_ScrapeSurfaceComponent* Surface,
	const FVector2D& SurfacePositionCm,
	const EMD_ScrapeSurfaceFace SurfaceFace)
{
	FlushPendingScrapePathApplication();
	FlushPendingContaminationPaintTail();
	bHasPreviousSurfacePosition = true;
	bHasLockedStrokeAxis = false;
	PreviousSurfacePositionCm = SurfacePositionCm;
	PreviousSurfaceFace = SurfaceFace;
	StrokeAnchorPositionCm = SurfacePositionCm;
	FurthestSurfacePositionCm = SurfacePositionCm;
	LockedStrokeAxis = FVector2D::ZeroVector;
	PreviousSurface = Surface;
	// 최초 anchor는 현재 generation을 그대로 쓴다. 표면·면 hard 전환은
	// 이전 tracking을 정리하는 경로에서 새 generation을 이미 발급한다.
	ResetScrapePathAccumulator(SurfacePositionCm);
}

bool UMD_ScrapeToolComponent::ConsumeRawScrapePathPoint(
	const FVector2D& RawSurfacePositionCm,
	TArray<FMD_ScrapeToolPathSegment>& OutSegments)
{
	OutSegments.Reset();
	if (!bHasPreviousSurfacePosition || RawSurfacePositionCm.ContainsNaN())
	{
		return false;
	}

	const FVector2D RawDelta =
		RawSurfacePositionCm - PreviousSurfacePositionCm;
	const double RawTravelCm = RawDelta.Size();
	if (!FMath::IsFinite(RawTravelCm) || RawTravelCm <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const FVector2D RawTravelTangent = RawDelta / RawTravelCm;
	if (bHasLastRawTravelTangent)
	{
		const double SignedDirectionDot = FMath::Clamp(
			FVector2D::DotProduct(
				LastRawTravelTangent,
				RawTravelTangent),
			-1.0,
			1.0);
		const double EquivalentDirectionDot = FMath::Clamp(
			FMath::Abs(SignedDirectionDot),
			0.0,
			1.0);
		const double CornerDegrees = FMath::RadiansToDegrees(
			FMath::Acos(EquivalentDirectionDot));
		if (CornerDegrees >= FMath::Clamp(
				static_cast<double>(LargeCornerSplitDegrees),
				0.0,
				90.0))
		{
			if (!bHasStableBladeDepthAxis
				&& PendingScrapePolylineCm.Num() >= 2)
			{
				// threshold보다 짧아도 명시 basis 계약으로 이전 raw 선분을 먼저 보존한다.
				ResolvePendingScrapePath(OutSegments);
			}
			FlushResolvedScrapePathTail(
				PreviousSurfacePositionCm,
				OutSegments);
			// 공통 꼭짓점에서 상태만 나눈다. 이전/다음 점을 잇는 대각선은 만들지 않는다.
			FMD_ScrapeToolPathSegment& Boundary =
				OutSegments.AddDefaulted_GetRef();
			Boundary.StartPositionCm = PreviousSurfacePositionCm;
			Boundary.EndPositionCm = PreviousSurfacePositionCm;
			Boundary.StartBladeDepthAxis = StableBladeDepthAxis;
			Boundary.EndBladeDepthAxis = StableBladeDepthAxis;
			Boundary.bStartsNewSubStroke = true;
			ResetScrapePathAccumulator(PreviousSurfacePositionCm);
		}
		else if (SignedDirectionDot < -UE_KINDA_SMALL_NUMBER)
		{
			// 180도 왕복은 같은 sub-stroke지만 꼭짓점을 건너 chord로 펴면 안 된다.
			FlushResolvedScrapePathTail(
				PreviousSurfacePositionCm,
				OutSegments);
		}
	}

	LastRawTravelTangent = RawTravelTangent;
	bHasLastRawTravelTangent = true;
	if (!bHasStableBladeDepthAxis)
	{
		if (PendingScrapePolylineCm.IsEmpty())
		{
			PendingScrapePolylineCm.Add(PreviousSurfacePositionCm);
		}
		PendingScrapePolylineCm.Add(RawSurfacePositionCm);
		PendingScrapePathArcLengthCm += RawTravelCm;
		if (PendingScrapePathArcLengthCm < FMath::Max(
				static_cast<double>(DirectionLockThresholdCm),
				static_cast<double>(UE_SMALL_NUMBER)))
		{
			return !OutSegments.IsEmpty();
		}

		ResolvePendingScrapePath(OutSegments);
		return !OutSegments.IsEmpty();
	}

	AppendResolvedScrapePathSegment(
		PreviousSurfacePositionCm,
		RawSurfacePositionCm,
		OutSegments);
	return !OutSegments.IsEmpty();
}

void UMD_ScrapeToolComponent::ResolvePendingScrapePath(
	TArray<FMD_ScrapeToolPathSegment>& OutSegments)
{
	if (PendingScrapePolylineCm.Num() < 2)
	{
		return;
	}

	const FVector2D InitialAxis =
		(PendingScrapePolylineCm.Last() - PendingScrapePolylineCm[0])
		.GetSafeNormal();
	StableBladeDepthAxis = InitialAxis.IsNearlyZero()
		? LastRawTravelTangent.GetSafeNormal()
		: InitialAxis;
	if (StableBladeDepthAxis.IsNearlyZero())
	{
		return;
	}

	bHasStableBladeDepthAxis = true;
	LockedStrokeAxis = StableBladeDepthAxis;
	bHasLockedStrokeAxis = true;
	StrokeAnchorPositionCm = PendingScrapePolylineCm[0];
	FurthestSurfacePositionCm = StrokeAnchorPositionCm;
	ScrapeDirectionSamples.Reset();
	ScrapePathArcLengthCm = 0.0;
	AddDirectionSample(PendingScrapePolylineCm[0], 0.0);
	LastEmittedScrapePathArcLengthCm = 0.0;
	LastEmittedScrapePathPositionCm = PendingScrapePolylineCm[0];
	PendingResolvedScrapePathPointsCm.Reset();
	PendingResolvedScrapePathPointsCm.Add(PendingScrapePolylineCm[0]);
	PendingResolvedScrapePathTravelCm = 0.0;
	LastEmittedBladeDepthAxis = StableBladeDepthAxis;

	const TArray<FVector2D> BufferedPolyline = PendingScrapePolylineCm;
	PendingScrapePolylineCm.Reset();
	PendingScrapePathArcLengthCm = 0.0;
	for (int32 PointIndex = 1; PointIndex < BufferedPolyline.Num(); ++PointIndex)
	{
		AppendResolvedScrapePathSegment(
			BufferedPolyline[PointIndex - 1],
			BufferedPolyline[PointIndex],
			OutSegments);
	}
}

void UMD_ScrapeToolComponent::AppendResolvedScrapePathSegment(
	const FVector2D& StartPositionCm,
	const FVector2D& EndPositionCm,
	TArray<FMD_ScrapeToolPathSegment>& OutSegments)
{
	const FVector2D Delta = EndPositionCm - StartPositionCm;
	const double SegmentTravelCm = Delta.Size();
	if (!FMath::IsFinite(SegmentTravelCm) || SegmentTravelCm <= UE_SMALL_NUMBER)
	{
		return;
	}

	const double SegmentStartArcCm = ScrapePathArcLengthCm;
	const double SegmentEndArcCm = SegmentStartArcCm + SegmentTravelCm;
	const double SafeSpacingCm = FMath::Max(
		static_cast<double>(PathDirectionResampleSpacingCm),
		0.01);

	if (PendingResolvedScrapePathPointsCm.IsEmpty())
	{
		PendingResolvedScrapePathPointsCm.Add(StartPositionCm);
	}
	else if (!PendingResolvedScrapePathPointsCm.Last().Equals(
		StartPositionCm,
		UE_SMALL_NUMBER))
	{
		// A rebase or future direct caller must never turn a discontinuity into a
		// scrape chord. Drop only the un-emitted tail and restart at this point.
		PendingResolvedScrapePathPointsCm.Reset();
		PendingResolvedScrapePathPointsCm.Add(StartPositionCm);
		PendingResolvedScrapePathTravelCm = 0.0;
		LastEmittedScrapePathPositionCm = StartPositionCm;
		LastEmittedScrapePathArcLengthCm = SegmentStartArcCm;
		LastEmittedBladeDepthAxis = StableBladeDepthAxis;
	}

	// Raw aim frequency must not become an evaluator boundary. Extend a pending
	// leg while travel continues in the same direction, but keep turnarounds as
	// explicit points so sub-spacing A-B-A movement is not chorded away.
	bool bExtendedPendingLeg = false;
	if (PendingResolvedScrapePathPointsCm.Num() >= 2
		&& PendingResolvedScrapePathPointsCm.Last().Equals(
			StartPositionCm,
			UE_SMALL_NUMBER))
	{
		const FVector2D PendingLeg =
			PendingResolvedScrapePathPointsCm.Last()
			- PendingResolvedScrapePathPointsCm[
				PendingResolvedScrapePathPointsCm.Num() - 2];
		if (FVector2D::DotProduct(PendingLeg, Delta)
			> UE_DOUBLE_SMALL_NUMBER)
		{
			PendingResolvedScrapePathPointsCm.Last() = EndPositionCm;
			bExtendedPendingLeg = true;
		}
	}
	if (!bExtendedPendingLeg)
	{
		PendingResolvedScrapePathPointsCm.Add(EndPositionCm);
	}
	PendingResolvedScrapePathTravelCm += SegmentTravelCm;

	while (PendingResolvedScrapePathTravelCm
		>= SafeSpacingCm - UE_SMALL_NUMBER
		&& PendingResolvedScrapePathPointsCm.Num() >= 2)
	{
		double RemainingSampleTravelCm = SafeSpacingCm;
		FVector2D SamplePositionCm = PendingResolvedScrapePathPointsCm[0];
		TArray<FVector2D, TInlineAllocator<8>> ConsumedChunkPointsCm;
		ConsumedChunkPointsCm.Add(SamplePositionCm);
		while (RemainingSampleTravelCm > UE_SMALL_NUMBER
			&& PendingResolvedScrapePathPointsCm.Num() >= 2)
		{
			const FVector2D LegStartCm = PendingResolvedScrapePathPointsCm[0];
			const FVector2D LegEndCm = PendingResolvedScrapePathPointsCm[1];
			const double LegTravelCm = FVector2D::Distance(
				LegStartCm,
				LegEndCm);
			if (LegTravelCm <= UE_SMALL_NUMBER)
			{
				PendingResolvedScrapePathPointsCm.RemoveAt(
					0,
					1,
					EAllowShrinking::No);
				continue;
			}
			if (LegTravelCm <= RemainingSampleTravelCm + UE_SMALL_NUMBER)
			{
				SamplePositionCm = LegEndCm;
				ConsumedChunkPointsCm.Add(SamplePositionCm);
				RemainingSampleTravelCm = FMath::Max(
					RemainingSampleTravelCm - LegTravelCm,
					0.0);
				PendingResolvedScrapePathPointsCm.RemoveAt(
					0,
					1,
					EAllowShrinking::No);
			}
			else
			{
				const double Alpha = RemainingSampleTravelCm / LegTravelCm;
				SamplePositionCm = FMath::Lerp(LegStartCm, LegEndCm, Alpha);
				ConsumedChunkPointsCm.Add(SamplePositionCm);
				PendingResolvedScrapePathPointsCm[0] = SamplePositionCm;
				RemainingSampleTravelCm = 0.0;
			}
		}

		const double SampleArcCm = LastEmittedScrapePathArcLengthCm
			+ SafeSpacingCm;
		AddDirectionSample(SamplePositionCm, SampleArcCm);
		UpdateStableBladeAxis(SafeSpacingCm);

		FVector2D PartStartPositionCm = LastEmittedScrapePathPositionCm;
		for (int32 ConsumedIndex = 1;
			ConsumedIndex < ConsumedChunkPointsCm.Num();
			++ConsumedIndex)
		{
			const FVector2D PartEndPositionCm =
				ConsumedChunkPointsCm[ConsumedIndex];
			const double PartTravelCm = FVector2D::Distance(
				PartStartPositionCm,
				PartEndPositionCm);
			if (PartTravelCm > UE_SMALL_NUMBER)
			{
				FMD_ScrapeToolPathSegment& Part =
					OutSegments.AddDefaulted_GetRef();
				Part.StartPositionCm = PartStartPositionCm;
				Part.EndPositionCm = PartEndPositionCm;
				Part.StartBladeDepthAxis = LastEmittedBladeDepthAxis;
				Part.EndBladeDepthAxis = StableBladeDepthAxis;
				Part.TravelDistanceCm = static_cast<float>(PartTravelCm);
			}
			PartStartPositionCm = PartEndPositionCm;
		}
		LastEmittedScrapePathPositionCm = SamplePositionCm;
		LastEmittedScrapePathArcLengthCm = SampleArcCm;
		LastEmittedBladeDepthAxis = StableBladeDepthAxis;
		PendingResolvedScrapePathTravelCm = FMath::Max(
			PendingResolvedScrapePathTravelCm - SafeSpacingCm,
			0.0);
	}
	ScrapePathArcLengthCm = SegmentEndArcCm;
}

void UMD_ScrapeToolComponent::FlushResolvedScrapePathTail(
	const FVector2D& EndPositionCm,
	TArray<FMD_ScrapeToolPathSegment>& OutSegments)
{
	if (!bHasStableBladeDepthAxis || EndPositionCm.ContainsNaN())
	{
		return;
	}

	const double TailTravelCm =
		ScrapePathArcLengthCm - LastEmittedScrapePathArcLengthCm;
	if (TailTravelCm <= UE_SMALL_NUMBER)
	{
		return;
	}

	const int32 InitialSegmentCount = OutSegments.Num();
	FVector2D TailStartPositionCm = LastEmittedScrapePathPositionCm;
	FVector2D TailStartBladeAxis = LastEmittedBladeDepthAxis;
	for (int32 PointIndex = 1;
		PointIndex < PendingResolvedScrapePathPointsCm.Num();
		++PointIndex)
	{
		const FVector2D TailEndPositionCm =
			PendingResolvedScrapePathPointsCm[PointIndex];
		const double LegTravelCm = FVector2D::Distance(
			TailStartPositionCm,
			TailEndPositionCm);
		if (LegTravelCm <= UE_SMALL_NUMBER)
		{
			continue;
		}

		FMD_ScrapeToolPathSegment& Tail = OutSegments.AddDefaulted_GetRef();
		Tail.StartPositionCm = TailStartPositionCm;
		Tail.EndPositionCm = TailEndPositionCm;
		Tail.StartBladeDepthAxis = TailStartBladeAxis;
		Tail.EndBladeDepthAxis = StableBladeDepthAxis;
		Tail.TravelDistanceCm = static_cast<float>(LegTravelCm);
		TailStartPositionCm = TailEndPositionCm;
		TailStartBladeAxis = StableBladeDepthAxis;
	}
	if (!TailStartPositionCm.Equals(EndPositionCm, UE_SMALL_NUMBER))
	{
		const double FinalLegTravelCm = FVector2D::Distance(
			TailStartPositionCm,
			EndPositionCm);
		if (FinalLegTravelCm > UE_SMALL_NUMBER)
		{
			FMD_ScrapeToolPathSegment& Tail = OutSegments.AddDefaulted_GetRef();
			Tail.StartPositionCm = TailStartPositionCm;
			Tail.EndPositionCm = EndPositionCm;
			Tail.StartBladeDepthAxis = TailStartBladeAxis;
			Tail.EndBladeDepthAxis = StableBladeDepthAxis;
			Tail.TravelDistanceCm = static_cast<float>(FinalLegTravelCm);
		}
	}
	if (OutSegments.Num() == InitialSegmentCount)
	{
		return;
	}

	LastEmittedScrapePathPositionCm = EndPositionCm;
	LastEmittedScrapePathArcLengthCm = ScrapePathArcLengthCm;
	LastEmittedBladeDepthAxis = StableBladeDepthAxis;
	PendingResolvedScrapePathPointsCm.Reset();
	PendingResolvedScrapePathPointsCm.Add(EndPositionCm);
	PendingResolvedScrapePathTravelCm = 0.0;
}

void UMD_ScrapeToolComponent::FlushPendingScrapePathApplication()
{
	UMD_ScrapeSurfaceComponent* const Surface = PreviousSurface.Get();
	if (!bHasPreviousSurfacePosition
		|| !bHasLastScrapeSurfaceHit
		|| !IsValid(Surface))
	{
		return;
	}

	TArray<FMD_ScrapeToolPathSegment> TailSegments;
	FlushResolvedScrapePathTail(PreviousSurfacePositionCm, TailSegments);
	double TailTravelCm = 0.0;
	for (const FMD_ScrapeToolPathSegment& TailSegment : TailSegments)
	{
		TailTravelCm += FMath::Max(
			static_cast<double>(TailSegment.TravelDistanceCm), 0.0);
	}
	const double TailTimeSeconds = FMath::Max(
		PendingScrapePathSampleTimeSeconds, 0.0);
	PendingScrapePathSampleTimeSeconds = 0.0;
	for (const FMD_ScrapeToolPathSegment& TailSegment : TailSegments)
	{
		FVector2D AppliedEndPositionCm = TailSegment.EndPositionCm;
		AMD_Barnacle* BlockingBarnacle = nullptr;
		const float TailSegmentDeltaTime = TailTravelCm > UE_DOUBLE_SMALL_NUMBER
			? static_cast<float>(TailTimeSeconds
				* (static_cast<double>(TailSegment.TravelDistanceCm)
					/ TailTravelCm))
			: 0.0f;
		if (ApplyScrapePathSegment(
				Surface,
				PreviousSurfaceFace,
				LastScrapeSurfaceHit,
				TailSegment,
				TailSegmentDeltaTime,
				AppliedEndPositionCm,
				BlockingBarnacle))
		{
			RebaseScrapePathAfterObstacle(AppliedEndPositionCm);
			break;
		}
	}
}

void UMD_ScrapeToolComponent::SplitScrapeSubStrokeAtCorner(
	const FVector2D& CornerPositionCm)
{
	FlushPendingContaminationPaintTail();
	AdvanceStage3SubStrokeGeneration();
	StrokeAnchorPositionCm = CornerPositionCm;
	FurthestSurfacePositionCm = CornerPositionCm;
}

void UMD_ScrapeToolComponent::ResetScrapePathAccumulator(
	const FVector2D& StartPositionCm)
{
	PendingScrapePolylineCm.Reset();
	PendingScrapePolylineCm.Add(StartPositionCm);
	PendingScrapePathArcLengthCm = 0.0;
	ScrapeDirectionSamples.Reset();
	ScrapePathArcLengthCm = 0.0;
	LastEmittedScrapePathArcLengthCm = 0.0;
	PendingResolvedScrapePathPointsCm.Reset();
	PendingResolvedScrapePathPointsCm.Add(StartPositionCm);
	PendingResolvedScrapePathTravelCm = 0.0;
	LastEmittedScrapePathPositionCm = StartPositionCm;
	LastEmittedBladeDepthAxis = FVector2D::ZeroVector;
	StableBladeDepthAxis = FVector2D::ZeroVector;
	LastRawTravelTangent = FVector2D::ZeroVector;
	bHasStableBladeDepthAxis = false;
	bHasLastRawTravelTangent = false;
}

void UMD_ScrapeToolComponent::RebaseScrapePathAfterObstacle(
	const FVector2D& BlockedPositionCm)
{
	FlushPendingContaminationPaintTail();
	AdvanceStage3SubStrokeGeneration();
	PreviousSurfacePositionCm = BlockedPositionCm;
	StrokeAnchorPositionCm = BlockedPositionCm;
	FurthestSurfacePositionCm = BlockedPositionCm;
	LockedStrokeAxis = FVector2D::ZeroVector;
	bHasLockedStrokeAxis = false;
	PendingScrapePathSampleTimeSeconds = 0.0;
	ResetScrapePathAccumulator(BlockedPositionCm);
}

void UMD_ScrapeToolComponent::AddDirectionSample(
	const FVector2D& PositionCm,
	const double ArcLengthCm)
{
	FMD_ScrapeToolDirectionSample& Sample =
		ScrapeDirectionSamples.AddDefaulted_GetRef();
	Sample.PositionCm = PositionCm;
	Sample.ArcLengthCm = ArcLengthCm;

	const double KeepAfterArcCm = ArcLengthCm
		- FMath::Max(static_cast<double>(BladeDirectionWindowCm), 0.1)
		- FMath::Max(static_cast<double>(PathDirectionResampleSpacingCm), 0.01);
	while (ScrapeDirectionSamples.Num() > 2
		&& ScrapeDirectionSamples[1].ArcLengthCm < KeepAfterArcCm)
	{
		ScrapeDirectionSamples.RemoveAt(0, 1, EAllowShrinking::No);
	}
}

FVector2D UMD_ScrapeToolComponent::CalculateWindowedBladeAxis() const
{
	if (ScrapeDirectionSamples.Num() < 2)
	{
		return StableBladeDepthAxis;
	}

	const FMD_ScrapeToolDirectionSample& Latest = ScrapeDirectionSamples.Last();
	const double TargetArcCm = Latest.ArcLengthCm
		- FMath::Max(static_cast<double>(BladeDirectionWindowCm), 0.1);
	FVector2D WindowStartCm = ScrapeDirectionSamples[0].PositionCm;
	for (int32 Index = 1; Index < ScrapeDirectionSamples.Num(); ++Index)
	{
		const FMD_ScrapeToolDirectionSample& Previous =
			ScrapeDirectionSamples[Index - 1];
		const FMD_ScrapeToolDirectionSample& Current =
			ScrapeDirectionSamples[Index];
		if (TargetArcCm <= Current.ArcLengthCm)
		{
			const double SpanCm = Current.ArcLengthCm - Previous.ArcLengthCm;
			const double Alpha = SpanCm > UE_SMALL_NUMBER
				? FMath::Clamp((TargetArcCm - Previous.ArcLengthCm) / SpanCm, 0.0, 1.0)
				: 0.0;
			WindowStartCm = FMath::Lerp(
				Previous.PositionCm,
				Current.PositionCm,
				Alpha);
			break;
		}
	}

	const FVector2D Candidate =
		(Latest.PositionCm - WindowStartCm).GetSafeNormal();
	return Candidate.IsNearlyZero() ? StableBladeDepthAxis : Candidate;
}

void UMD_ScrapeToolComponent::UpdateStableBladeAxis(const double ArcStepCm)
{
	FVector2D Candidate = CalculateWindowedBladeAxis().GetSafeNormal();
	if (Candidate.IsNearlyZero())
	{
		return;
	}
	if (FVector2D::DotProduct(Candidate, StableBladeDepthAxis) < 0.0)
	{
		Candidate *= -1.0;
	}

	const double Dot = FMath::Clamp(
		FVector2D::DotProduct(StableBladeDepthAxis, Candidate),
		0.0,
		1.0);
	const double AngleRadians = FMath::Acos(Dot);
	const double MaximumRadians = FMath::DegreesToRadians(
		FMath::Max(static_cast<double>(MaximumBladeAxisTurnDegreesPerCm), 0.0)
		* FMath::Max(ArcStepCm, 0.0));
	if (AngleRadians > UE_SMALL_NUMBER)
	{
		const double Cross = StableBladeDepthAxis.X * Candidate.Y
			- StableBladeDepthAxis.Y * Candidate.X;
		const double AppliedRadians = FMath::Min(AngleRadians, MaximumRadians)
			* (Cross < 0.0 ? -1.0 : 1.0);
		const double CosAngle = FMath::Cos(AppliedRadians);
		const double SinAngle = FMath::Sin(AppliedRadians);
		StableBladeDepthAxis = FVector2D(
			StableBladeDepthAxis.X * CosAngle - StableBladeDepthAxis.Y * SinAngle,
			StableBladeDepthAxis.X * SinAngle + StableBladeDepthAxis.Y * CosAngle)
			.GetSafeNormal();
	}
	LockedStrokeAxis = StableBladeDepthAxis;
	bHasLockedStrokeAxis = true;
}

FMD_ScrapeBladeBasis UMD_ScrapeToolComponent::MakeBladeBasis(
	const FVector2D& BladeDepthAxis)
{
	const FVector2D SafeDepthAxis = BladeDepthAxis.GetSafeNormal();
	return FMD_ScrapeBladeBasis(
		SafeDepthAxis,
		FVector2D(-SafeDepthAxis.Y, SafeDepthAxis.X));
}

FVector2D UMD_ScrapeToolComponent::InterpolateBladeAxis(
	const FVector2D& StartAxis,
	const FVector2D& EndAxis,
	const double Alpha)
{
	const FVector2D SafeStart = StartAxis.GetSafeNormal();
	FVector2D SafeEnd = EndAxis.GetSafeNormal();
	if (SafeStart.IsNearlyZero() || SafeEnd.IsNearlyZero())
	{
		return SafeStart.IsNearlyZero() ? SafeEnd : SafeStart;
	}
	if (FVector2D::DotProduct(SafeStart, SafeEnd) < 0.0)
	{
		SafeEnd *= -1.0;
	}
	const double Dot = FMath::Clamp(
		FVector2D::DotProduct(SafeStart, SafeEnd), -1.0, 1.0);
	const double Cross = SafeStart.X * SafeEnd.Y - SafeStart.Y * SafeEnd.X;
	const double AngleRadians = FMath::Atan2(Cross, Dot)
		* FMath::Clamp(Alpha, 0.0, 1.0);
	const double CosAngle = FMath::Cos(AngleRadians);
	const double SinAngle = FMath::Sin(AngleRadians);
	return FVector2D(
		SafeStart.X * CosAngle - SafeStart.Y * SinAngle,
		SafeStart.X * SinAngle + SafeStart.Y * CosAngle).GetSafeNormal();
}

bool UMD_ScrapeToolComponent::ApplyScrapePathSegment(
	UMD_ScrapeSurfaceComponent* const Surface,
	const EMD_ScrapeSurfaceFace SurfaceFace,
	const FHitResult& CurrentHit,
	const FMD_ScrapeToolPathSegment& PathSegment,
	const float DeltaTime,
	FVector2D& OutAppliedEndPositionCm,
	AMD_Barnacle*& OutBlockingBarnacle)
{
	OutAppliedEndPositionCm = PathSegment.EndPositionCm;
	OutBlockingBarnacle = nullptr;
	if (!IsValid(Surface))
	{
		return false;
	}

	FMD_ScrapeStroke AttemptedStroke;
	AttemptedStroke.SurfaceFace = SurfaceFace;
	AttemptedStroke.StartPositionCm = PathSegment.StartPositionCm;
	AttemptedStroke.EndPositionCm = PathSegment.EndPositionCm;
	AttemptedStroke.TravelDistanceCm = PathSegment.TravelDistanceCm;
	AttemptedStroke.StartBladeBasis = MakeBladeBasis(
		PathSegment.StartBladeDepthAxis);
	AttemptedStroke.EndBladeBasis = MakeBladeBasis(
		PathSegment.EndBladeDepthAxis);
	const bool bBlocked = Surface->TryClampScrapeStrokeAgainstObstacles(
		AttemptedStroke,
		ScrapeToolSpec,
		OutAppliedEndPositionCm,
		OutBlockingBarnacle);
	const double AttemptedChordCm = FVector2D::Distance(
		PathSegment.StartPositionCm,
		PathSegment.EndPositionCm);
	const double AppliedChordCm = FVector2D::Distance(
		PathSegment.StartPositionCm,
		OutAppliedEndPositionCm);
	const double AppliedRatio = AttemptedChordCm > UE_SMALL_NUMBER
		? FMath::Clamp(AppliedChordCm / AttemptedChordCm, 0.0, 1.0)
		: 0.0;
	const FVector2D AppliedEndBladeAxis = InterpolateBladeAxis(
		PathSegment.StartBladeDepthAxis,
		PathSegment.EndBladeDepthAxis,
		AppliedRatio);
	LockedStrokeAxis = AppliedEndBladeAxis;
	bHasLockedStrokeAxis = !LockedStrokeAxis.IsNearlyZero();

	FVector AppliedWorldPosition = CurrentHit.ImpactPoint;
	const bool bHasAppliedWorldPosition =
		Surface->TryGetScrapeSurfaceWorldPositionAtDepth(
			OutAppliedEndPositionCm,
			SurfaceFace,
			CurrentHit.ImpactPoint,
			AppliedWorldPosition);

	if (AppliedChordCm > UE_SMALL_NUMBER)
	{
		if (bHasLockedStrokeAxis
			&& FVector2D::DotProduct(
				OutAppliedEndPositionCm - StrokeAnchorPositionCm,
				LockedStrokeAxis)
				> FVector2D::DotProduct(
					FurthestSurfacePositionCm - StrokeAnchorPositionCm,
					LockedStrokeAxis))
		{
			FurthestSurfacePositionCm = OutAppliedEndPositionCm;
		}

		FMD_ScrapeStroke Stroke = AttemptedStroke;
		Stroke.EndPositionCm = OutAppliedEndPositionCm;
		Stroke.TravelDistanceCm = PathSegment.TravelDistanceCm
			* static_cast<float>(AppliedRatio);
		Stroke.EndBladeBasis = MakeBladeBasis(AppliedEndBladeAxis);
		if (GetOwner() && GetOwner()->HasAuthority())
		{
			const bool bWasSaturated =
				GetScrapeLoadStage() == EMD_ScrapeLoadStage::Stage3;
			FMD_ScrapeToolSpec EffectiveToolSpec = ScrapeToolSpec;
			if (bWasSaturated)
			{
				EffectiveToolSpec.RemovalPerTravelCentimeter *=
					GetSaturatedRemovalMultiplier();
			}
			const FMD_ScrapeResult Result =
				bWasSaturated
				? Surface->ApplyLocalScrapeStrokeWithDeferredCompletion(
					Stroke,
					EffectiveToolSpec)
				: Surface->ApplyLocalScrapeStrokeWithToolSpec(
					Stroke,
					EffectiveToolSpec);
			AddScrapeLoad(CalculateScrapeLoadGainFromCleanRatioDelta(
				Result.CleanRatioDelta,
				ScrapeLoadPerRemovedPercent));

			if (bWasSaturated)
			{
				// Surface의 제거가 완료된 뒤 candidate를 enqueue/commit한다.
				AccumulateStage3PathSegment(
					Surface,
					SurfaceFace,
					PathSegment,
					AppliedRatio);
			}
			else if (GetScrapeLoadStage() == EMD_ScrapeLoadStage::Stage3)
			{
				// Stage2→3 전환 segment는 소급 도포하지 않고 endpoint에서 시작한다.
				StartStage3PathAccumulator(
					Surface,
					SurfaceFace,
					OutAppliedEndPositionCm,
					AppliedEndBladeAxis);
			}
			if (Result.bIsInputValid && Result.bWasStrokeLongEnough)
			{
				const FVector FeedbackLocation = bHasAppliedWorldPosition
					? AppliedWorldPosition
					: FVector(CurrentHit.ImpactPoint);
				DispatchAuthoritativeScrapeResult(
					Surface,
					Result,
					FeedbackLocation);
			}
		}
	}

	if (!bBlocked)
	{
		return false;
	}

	FVector StrokeStartWorldPosition;
	FVector StrokeEndWorldPosition;
	FVector ImpactWorldDirection = FVector::ZeroVector;
	if (Surface->TryGetScrapeSurfaceWorldPosition(
			PathSegment.StartPositionCm,
			SurfaceFace,
			StrokeStartWorldPosition)
		&& Surface->TryGetScrapeSurfaceWorldPosition(
			PathSegment.EndPositionCm,
			SurfaceFace,
			StrokeEndWorldPosition))
	{
		ImpactWorldDirection = StrokeEndWorldPosition - StrokeStartWorldPosition;
	}
	SetTrackedBarnacle(OutBlockingBarnacle);
	DeliverBarnacleImpact(
		OutBlockingBarnacle,
		CalculateImpactSpeedCmPerSecond(PathSegment.TravelDistanceCm, DeltaTime),
		ImpactWorldDirection);
	return true;
}

FRotator UMD_ScrapeToolComponent::CalculateAimCorrectionRotation(
	const FRotator& CurrentControlRotation,
	const FVector& ViewToCurrentAim,
	const FVector& ViewToTarget,
	const float CorrectionRatio,
	const float MaximumStepDegrees,
	const float MaximumErrorDegrees)
{
	if (CurrentControlRotation.ContainsNaN()
		|| ViewToCurrentAim.ContainsNaN()
		|| ViewToTarget.ContainsNaN())
	{
		return CurrentControlRotation;
	}

	const FVector CurrentDirection = ViewToCurrentAim.GetSafeNormal();
	const FVector TargetDirection = ViewToTarget.GetSafeNormal();
	if (CurrentDirection.IsNearlyZero() || TargetDirection.IsNearlyZero())
	{
		return CurrentControlRotation;
	}

	const double DirectionDot = FMath::Clamp(
		FVector::DotProduct(CurrentDirection, TargetDirection),
		-1.0,
		1.0);
	const float ErrorDegrees = FMath::RadiansToDegrees(
		static_cast<float>(FMath::Acos(DirectionDot)));
	if (!FMath::IsFinite(ErrorDegrees)
		|| ErrorDegrees <= UE_KINDA_SMALL_NUMBER)
	{
		return CurrentControlRotation;
	}

	// 시선이 벽을 너무 스치는 각도면 이 프레임은 건너뛴다.
	if (FMath::IsFinite(MaximumErrorDegrees)
		&& MaximumErrorDegrees > 0.0f
		&& ErrorDegrees > MaximumErrorDegrees)
	{
		return CurrentControlRotation;
	}

	// 오차의 일정 비율만 없앤다. 항상 오차보다 작으므로 목표를 지나치지 않는다.
	const float SafeCorrectionRatio = FMath::IsFinite(CorrectionRatio)
		? FMath::Clamp(CorrectionRatio, 0.0f, 1.0f)
		: 0.0f;
	float AppliedDegrees = ErrorDegrees * SafeCorrectionRatio;
	if (FMath::IsFinite(MaximumStepDegrees) && MaximumStepDegrees > 0.0f)
	{
		AppliedDegrees = FMath::Min(AppliedDegrees, MaximumStepDegrees);
	}
	if (AppliedDegrees <= UE_KINDA_SMALL_NUMBER)
	{
		return CurrentControlRotation;
	}

	const FVector RotationAxis = FVector::CrossProduct(
		CurrentDirection,
		TargetDirection).GetSafeNormal();
	if (RotationAxis.IsNearlyZero())
	{
		return CurrentControlRotation;
	}

	// 두 시선 방향의 각도 차이를 그대로 현재 회전에 더한다.
	const FQuat DeltaQuat(
		RotationAxis,
		FMath::DegreesToRadians(AppliedDegrees));
	FRotator CorrectedRotation =
		(DeltaQuat * CurrentControlRotation.Quaternion()).Rotator();
	if (CorrectedRotation.ContainsNaN())
	{
		return CurrentControlRotation;
	}

	// Roll은 보정 대상이 아니므로 현재 값을 유지한다.
	CorrectedRotation.Roll = CurrentControlRotation.Roll;
	return CorrectedRotation;
}

bool UMD_ScrapeToolComponent::IsAimCorrectionTargetVisible(
	const FVector& ViewLocation,
	const FVector& WorldPosition,
	const AActor* IgnoredActor) const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const UWorld* World = GetWorld();
	if (!OwnerPawn
		|| !World
		|| ViewLocation.ContainsNaN()
		|| WorldPosition.ContainsNaN())
	{
		return false;
	}

	const FVector ViewToTarget = WorldPosition - ViewLocation;
	const double DistanceCm = ViewToTarget.Size();
	const double SafeToleranceCm = FMath::Max(
		static_cast<double>(AimCorrectionVisibilityToleranceCm),
		static_cast<double>(UE_KINDA_SMALL_NUMBER));
	if (DistanceCm <= SafeToleranceCm)
	{
		return true;
	}

	// 목표 표면 자신에 맞는 것은 가림이 아니므로 조금 앞까지만 검사한다.
	FCollisionQueryParams QueryParams(
		SCENE_QUERY_STAT(MD_ScrapeAimVisibility),
		true);
	QueryParams.AddIgnoredActor(OwnerPawn);
	if (IsValid(IgnoredActor))
	{
		QueryParams.AddIgnoredActor(IgnoredActor);
	}

	FHitResult BlockingHit;
	return !World->LineTraceSingleByChannel(
		BlockingHit,
		ViewLocation,
		ViewLocation
			+ ViewToTarget.GetSafeNormal()
				* (DistanceCm - SafeToleranceCm),
		TraceChannel,
		QueryParams);
}

void UMD_ScrapeToolComponent::KeepAimOnScrapePosition(
	const FVector& ViewLocation,
	const FVector& CurrentAimWorldPosition,
	const FVector& TargetWorldPosition,
	const float DeltaTime) const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (IsScrapeFocusActive()
		|| !bUseAimCorrection
		|| !OwnerPawn
		|| !OwnerPawn->IsLocallyControlled()
		|| ViewLocation.ContainsNaN()
		|| CurrentAimWorldPosition.ContainsNaN()
		|| TargetWorldPosition.ContainsNaN())
	{
		return;
	}

	AController* Controller = OwnerPawn->GetController();
	if (!Controller)
	{
		return;
	}

	const float SafeDeltaTime = FMath::IsFinite(DeltaTime)
		? FMath::Max(DeltaTime, 0.0f)
		: 0.0f;

	// 프레임률이 달라도 같은 속도로 수렴하도록 지수 비율을 쓴다.
	const float CorrectionRatio = 1.0f - FMath::Exp(
		-FMath::Max(AimCorrectionInterpolationSpeed, 0.0f) * SafeDeltaTime);
	const FRotator CurrentControlRotation = Controller->GetControlRotation();
	const FRotator CorrectedControlRotation = CalculateAimCorrectionRotation(
		CurrentControlRotation,
		CurrentAimWorldPosition - ViewLocation,
		TargetWorldPosition - ViewLocation,
		CorrectionRatio,
		AimCorrectionSpeedDegreesPerSecond * SafeDeltaTime,
		AimCorrectionMaximumErrorDegrees);
	if (CorrectedControlRotation.Equals(
			CurrentControlRotation,
			UE_KINDA_SMALL_NUMBER))
	{
		return;
	}

	Controller->SetControlRotation(CorrectedControlRotation);
}

void UMD_ScrapeToolComponent::StartStage3PathAccumulator(
	UMD_ScrapeSurfaceComponent* const Surface,
	const EMD_ScrapeSurfaceFace SurfaceFace,
	const FVector2D& StartPositionCm,
	const FVector2D& StartBladeDepthAxis)
{
	if (!IsValid(Surface)
		|| StartPositionCm.ContainsNaN()
		|| StartBladeDepthAxis.ContainsNaN())
	{
		return;
	}
	if (bStage3PathActive)
	{
		FlushStage3PathTail();
	}
	if (LmbStrokeGeneration == 0)
	{
		LmbStrokeGeneration = 1;
	}
	if (Stage3SubStrokeGeneration == 0)
	{
		Stage3SubStrokeGeneration = 1;
	}
	if (ScrapeToolSpecGeneration == 0)
	{
		ScrapeToolSpecGeneration = 1;
	}

	bStage3PathActive = true;
	Stage3PathArcLengthCm = 0.0;
	Stage3NextDistanceBin = 0;
	Stage3PathSurface = Surface;
	Stage3PathSurfaceFace = SurfaceFace;
	Stage3PathToolSpec = ScrapeToolSpec;
	Stage3PathStartPositionCm = StartPositionCm;
	Stage3PathStartBladeBasis = MakeBladeBasis(StartBladeDepthAxis);
	Stage3ProtectionDistanceCm =
		Surface->GetContaminationPaintProtectionDistanceCm(
			Stage3PathToolSpec,
			GetSaturatedContaminationStampDepthCm());
	PendingStage3Dabs.Reset();
}

void UMD_ScrapeToolComponent::AccumulateStage3PathSegment(
	UMD_ScrapeSurfaceComponent* const Surface,
	const EMD_ScrapeSurfaceFace SurfaceFace,
	const FMD_ScrapeToolPathSegment& PathSegment,
	const double AppliedRatio)
{
	if (!IsValid(Surface) || PathSegment.bStartsNewSubStroke)
	{
		return;
	}
	if (bStage3PathActive
		&& (Stage3PathSurface.Get() != Surface
			|| Stage3PathSurfaceFace != SurfaceFace))
	{
		FlushStage3PathTail();
		AdvanceStage3SubStrokeGeneration();
	}
	if (!bStage3PathActive)
	{
		StartStage3PathAccumulator(
			Surface,
			SurfaceFace,
			PathSegment.StartPositionCm,
			PathSegment.StartBladeDepthAxis);
	}
	if (!bStage3PathActive)
	{
		return;
	}

	const double SafeAppliedRatio = FMath::Clamp(AppliedRatio, 0.0, 1.0);
	const double AppliedTravelCm =
		static_cast<double>(PathSegment.TravelDistanceCm) * SafeAppliedRatio;
	if (!FMath::IsFinite(AppliedTravelCm)
		|| AppliedTravelCm <= UE_SMALL_NUMBER)
	{
		return;
	}

	FMD_ScrapeToolPathSegment AppliedSegment = PathSegment;
	AppliedSegment.EndPositionCm = FMath::Lerp(
		PathSegment.StartPositionCm,
		PathSegment.EndPositionCm,
		SafeAppliedRatio);
	AppliedSegment.EndBladeDepthAxis = InterpolateBladeAxis(
		PathSegment.StartBladeDepthAxis,
		PathSegment.EndBladeDepthAxis,
		SafeAppliedRatio);
	AppliedSegment.TravelDistanceCm = static_cast<float>(AppliedTravelCm);

	const double SegmentStartArcCm = Stage3PathArcLengthCm;
	const double SegmentEndArcCm = SegmentStartArcCm + AppliedTravelCm;
	++Stage3SampleSequence;
	if (Stage3SampleSequence == 0)
	{
		++Stage3SampleSequence;
	}
	const int32 ResolvedPatternSeed =
		Surface->ResolveContaminationPaintPatternSeed(
			SurfaceFace,
			SaturatedContaminationStampPatternSeed);
	const float StampDepthCm = GetSaturatedContaminationStampDepthCm();
	const float StampSpacingCm = GetSaturatedContaminationStampSpacingCm();

	while (Stage3NextDistanceBin < MAX_int32)
	{
		FMD_ScrapeContaminationStampIdentity Identity;
		Identity.FocusSessionGeneration = GetStage3FocusSessionIdentity();
		Identity.LmbStrokeGeneration = LmbStrokeGeneration;
		Identity.SubStrokeGeneration = Stage3SubStrokeGeneration;
		Identity.DistanceBin = Stage3NextDistanceBin;
		const double CandidateArcCm =
			FMD_ScrapeEvaluator::CalculateContaminationDabArcLengthCm(
				Identity,
				StampDepthCm,
				StampSpacingCm,
				ResolvedPatternSeed);
		if (!FMath::IsFinite(CandidateArcCm)
			|| CandidateArcCm > SegmentEndArcCm + UE_KINDA_SMALL_NUMBER)
		{
			break;
		}

		const int32 CandidateDistanceBin = Stage3NextDistanceBin;
		++Stage3NextDistanceBin;
		// 시작점 제외/끝점 포함으로 frame 경계의 중복을 막는다.
		if (CandidateArcCm
			<= SegmentStartArcCm + UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}
		Identity.DistanceBin = CandidateDistanceBin;
		if (!FMD_ScrapeEvaluator::ShouldSelectContaminationDab(
				Identity,
				GetSaturatedContaminationStampCoverage(),
				ResolvedPatternSeed))
		{
			continue;
		}

		FMD_PendingScrapeContaminationDab& Pending =
			PendingStage3Dabs.AddDefaulted_GetRef();
		Pending.Dab = BuildStage3DabAtArcLength(
			AppliedSegment,
			SegmentStartArcCm,
			AppliedTravelCm,
			CandidateArcCm,
			CandidateDistanceBin,
			SurfaceFace);
		Pending.Surface = Surface;
		Pending.ToolSpec = Stage3PathToolSpec;
		Pending.StampAmount = GetSaturatedContaminationStampRatio();
		Pending.StampDepthCm = StampDepthCm;
	}

	Stage3PathArcLengthCm = SegmentEndArcCm;
	CommitProtectedStage3Dabs();
}

FMD_ScrapeContaminationDab
UMD_ScrapeToolComponent::BuildStage3DabAtArcLength(
	const FMD_ScrapeToolPathSegment& PathSegment,
	const double SegmentStartArcCm,
	const double SegmentTravelCm,
	const double CandidateArcCm,
	const int32 DistanceBin,
	const EMD_ScrapeSurfaceFace SurfaceFace)
{
	const double Alpha = SegmentTravelCm > UE_SMALL_NUMBER
		? FMath::Clamp(
			(CandidateArcCm - SegmentStartArcCm) / SegmentTravelCm,
			0.0,
			1.0)
		: 0.0;
	const FVector2D BladeDepthAxis = InterpolateBladeAxis(
		PathSegment.StartBladeDepthAxis,
		PathSegment.EndBladeDepthAxis,
		Alpha);

	FMD_ScrapeContaminationDab Dab;
	Dab.Identity.FocusSessionGeneration = GetStage3FocusSessionIdentity();
	Dab.Identity.LmbStrokeGeneration = LmbStrokeGeneration;
	Dab.Identity.SubStrokeGeneration = Stage3SubStrokeGeneration;
	Dab.Identity.DistanceBin = DistanceBin;
	Dab.PositionCm = FMath::Lerp(
		PathSegment.StartPositionCm,
		PathSegment.EndPositionCm,
		Alpha);
	Dab.BladeBasis = MakeBladeBasis(BladeDepthAxis);
	Dab.SurfaceFace = SurfaceFace;
	Dab.ArcLengthCm = CandidateArcCm;
	Dab.SampleSequence = Stage3SampleSequence;
	Dab.ToolSpecGeneration = ScrapeToolSpecGeneration;
	return Dab;
}

void UMD_ScrapeToolComponent::CommitProtectedStage3Dabs()
{
	const double CommitThroughArcCm =
		Stage3PathArcLengthCm - FMath::Max(Stage3ProtectionDistanceCm, 0.0);
	int32 CommitCount = 0;
	while (CommitCount < PendingStage3Dabs.Num()
		&& PendingStage3Dabs[CommitCount].Dab.ArcLengthCm
			<= CommitThroughArcCm + UE_KINDA_SMALL_NUMBER)
	{
		FMD_PendingScrapeContaminationDab& Pending =
			PendingStage3Dabs[CommitCount];
		if (UMD_ScrapeSurfaceComponent* const Surface = Pending.Surface.Get())
		{
			Surface->ApplyLocalContaminationDab(
				Pending.Dab,
				Pending.ToolSpec,
				Pending.StampAmount,
				Pending.StampDepthCm);
		}
		CommittedStage3Dabs.Add(Pending.Dab);
		++CommitCount;
	}
	if (CommitCount > 0)
	{
		PendingStage3Dabs.RemoveAt(
			0,
			CommitCount,
			EAllowShrinking::No);
	}
}

void UMD_ScrapeToolComponent::FlushStage3PathTail()
{
	if (!bStage3PathActive)
	{
		return;
	}
	UMD_ScrapeSurfaceComponent* const FinalizedSurface =
		Stage3PathSurface.Get();
	for (FMD_PendingScrapeContaminationDab& Pending : PendingStage3Dabs)
	{
		if (UMD_ScrapeSurfaceComponent* const Surface = Pending.Surface.Get())
		{
			Surface->ApplyLocalContaminationDab(
				Pending.Dab,
				Pending.ToolSpec,
				Pending.StampAmount,
				Pending.StampDepthCm);
		}
		CommittedStage3Dabs.Add(Pending.Dab);
	}
	if (IsValid(FinalizedSurface))
	{
		FinalizedSurface->FinalizeLocalContaminationPath();
	}
	ResetStage3PathAccumulator();
}

void UMD_ScrapeToolComponent::ResetStage3PathAccumulator()
{
	bStage3PathActive = false;
	Stage3PathArcLengthCm = 0.0;
	Stage3NextDistanceBin = 0;
	Stage3ProtectionDistanceCm = 0.0;
	Stage3PathSurface.Reset();
	Stage3PathSurfaceFace = EMD_ScrapeSurfaceFace::Front;
	Stage3PathToolSpec = FMD_ScrapeToolSpec();
	Stage3PathStartPositionCm = FVector2D::ZeroVector;
	Stage3PathStartBladeBasis = FMD_ScrapeBladeBasis();
	PendingStage3Dabs.Reset();
}

void UMD_ScrapeToolComponent::AdvanceStage3SubStrokeGeneration()
{
	++Stage3SubStrokeGeneration;
	if (Stage3SubStrokeGeneration == 0)
	{
		++Stage3SubStrokeGeneration;
	}
}

void UMD_ScrapeToolComponent::BreakStage3SubStroke()
{
	if (!bStage3PathActive)
	{
		return;
	}
	FlushStage3PathTail();
	AdvanceStage3SubStrokeGeneration();
}

uint64 UMD_ScrapeToolComponent::GetStage3FocusSessionIdentity() const
{
	if (Stage3FocusSessionIdentityOverride != 0)
	{
		return Stage3FocusSessionIdentityOverride;
	}

	// TASK 7의 서버 권위 session id가 붙기 전에도 다른 플레이어의 로컬
	// generation이 같은 Surface dedupe key로 충돌하지 않도록 source를 상위 32bit에 싣는다.
	const UObject* const IdentitySource = GetOwner()
		? static_cast<const UObject*>(GetOwner())
		: static_cast<const UObject*>(this);
	const uint32 SourceIdentity = IdentitySource
		? static_cast<uint32>(IdentitySource->GetUniqueID())
		: 1u;
	const uint32 LocalGeneration = ScrapeFocusGeneration != 0
		? ScrapeFocusGeneration
		: 1u;
	return (static_cast<uint64>(SourceIdentity) << 32)
		| static_cast<uint64>(LocalGeneration);
}

void UMD_ScrapeToolComponent::FlushPendingContaminationPaintTail()
{
	FlushStage3PathTail();
}

void UMD_ScrapeToolComponent::ResetStrokeTracking()
{
	const bool bHadTrackedSubStroke =
		bHasPreviousSurfacePosition || bStage3PathActive;
	const uint32 GenerationBeforeFlush = Stage3SubStrokeGeneration;
	FlushPendingScrapePathApplication();
	const bool bHadOrBuiltStage3Path = bStage3PathActive;
	FlushPendingContaminationPaintTail();
	if ((bHadTrackedSubStroke || bHadOrBuiltStage3Path)
		&& Stage3SubStrokeGeneration == GenerationBeforeFlush)
	{
		AdvanceStage3SubStrokeGeneration();
	}
	ClearCurrentScrapeContact();
	bHasPreviousSurfacePosition = false;
	bHasLockedStrokeAxis = false;
	PreviousSurfacePositionCm = FVector2D::ZeroVector;
	PreviousSurfaceFace = EMD_ScrapeSurfaceFace::Front;
	StrokeAnchorPositionCm = FVector2D::ZeroVector;
	FurthestSurfacePositionCm = FVector2D::ZeroVector;
	LockedStrokeAxis = FVector2D::ZeroVector;
	PreviousSurface.Reset();
	bHasLastScrapeSurfaceHit = false;
	PendingScrapePathSampleTimeSeconds = 0.0;
	ResetScrapePathAccumulator(FVector2D::ZeroVector);
}

void UMD_ScrapeToolComponent::EmitFeedback(
	UMD_ScrapeSurfaceComponent* Surface,
	const FMD_ScrapeResult& Result,
	const FVector& ContactLocation)
{
	if (!Surface || !GetWorld() || !Result.bHadSurfaceContact)
	{
		return;
	}

	if (bShowDebugFeedback && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			reinterpret_cast<uint64>(this),
			0.15f,
			Result.ContactType == EMD_ScrapeContactType::BareSurfaceOnly
				? FColor::Silver
				: FColor::Purple,
			FString::Printf(
				TEXT("헤라: %s | 오염 제거 %.1f%% | 오염 스택 %.1f / 100"),
				GetContactTypeLabel(Result.ContactType),
				FMath::Clamp(Result.CleanRatioAfter, 0.0f, 1.0f) * 100.0f,
				GetScrapeLoad()));
	}

	const double WorldSeconds = GetWorld()->GetTimeSeconds();
	if (WorldSeconds - LastFeedbackWorldSeconds
		< static_cast<double>(MinimumFeedbackIntervalSeconds))
	{
		return;
	}

	const bool bBareSurfaceFeedback =
		Result.ContactType == EMD_ScrapeContactType::BareSurfaceOnly
		|| Result.ContactType == EMD_ScrapeContactType::Mixed;

	USoundBase* FeedbackSound = bBareSurfaceFeedback
		? BareSurfaceScrapeSound
		: ContaminationScrapeSound;
	UParticleSystem* FeedbackEffect = bBareSurfaceFeedback
		? BareSurfaceScrapeEffect
		: ContaminationScrapeEffect;

	if (FeedbackSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			this,
			FeedbackSound,
			ContactLocation);
	}

	if (FeedbackEffect)
	{
		UGameplayStatics::SpawnEmitterAtLocation(
			GetWorld(),
			FeedbackEffect,
			FTransform(ContactLocation));
	}

	if (FeedbackSound || FeedbackEffect)
	{
		LastFeedbackWorldSeconds = WorldSeconds;
	}
}
