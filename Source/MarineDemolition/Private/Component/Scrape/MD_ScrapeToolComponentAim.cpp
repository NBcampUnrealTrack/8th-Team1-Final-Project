// Copyright Epic Games, Inc. All Rights Reserved.

#include "Component/Scrape/MD_ScrapeToolComponent.h"

#include "Gameplay/Scrape/MD_ScrapeAimValidation.h"
#include "Character/MD_Character.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Camera/CameraTypes.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameplayTags/MD_GameplayTags.h"

FMD_ScrapeAimAuthorityCone UMD_ScrapeToolComponent::MakeAimAuthorityCone() const
{
	FMD_ScrapeAimAuthorityCone Cone;
	// Continuous samples are validated against the server-owned cone frozen at
	// Begin. On a listen host the local display latch owns the similarly named
	// session fields and must never widen or otherwise overwrite authority.
	Cone.HorizontalHalfTangent =
		AuthoritativeScrapeFocusConeHorizontalHalfTangent;
	Cone.VerticalHalfTangent =
		AuthoritativeScrapeFocusConeVerticalHalfTangent;
	Cone.PointerSafeMargin = ScrapeFocusPointerSafeMargin;
	Cone.MarginDegrees = AimViewConeMarginDegrees;
	return Cone;
}

double UMD_ScrapeToolComponent::GetScrapeMonotonicSeconds()
{
	return FPlatformTime::Seconds();
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
		|| ScrapeFocusSession.ActiveScrapeFocusSessionId == 0
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

	++AuthoritativeAim.NextLocalAimSequence;
	if (AuthoritativeAim.NextLocalAimSequence == 0)
	{
		++AuthoritativeAim.NextLocalAimSequence;
	}
	OutSample.FocusSessionId = ScrapeFocusSession.ActiveScrapeFocusSessionId;
	OutSample.Sequence = AuthoritativeAim.NextLocalAimSequence;
	OutSample.ClientMonotonicSeconds = static_cast<float>(FMath::Max(
		GetScrapeMonotonicSeconds() - ScrapeFocusSession.LocalFocusMonotonicStartSeconds,
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
		&& ScrapeFocusSession.bAuthoritativeScrapeFocusAccepted
		&& FocusSessionId != 0
		&& FocusSessionId == ScrapeFocusSession.ActiveScrapeFocusSessionId;
}

bool UMD_ScrapeToolComponent::IsAuthoritativeFirstScrapeAimApproved(
	const uint64 FocusSessionId) const
{
	return CanActivateAuthoritativeScrape(FocusSessionId)
		&& AuthoritativeAim.bAuthoritativeFirstScrapeAimApproved
		&& AuthoritativeAim.AuthoritativeFirstAimSessionId == FocusSessionId;
}

void UMD_ScrapeToolComponent::ClearScrapeAbilityFirstAimApproval(
	const uint64 FocusSessionId)
{
	if (AuthoritativeAim.AuthoritativeFirstAimSessionId != FocusSessionId)
	{
		return;
	}
	AuthoritativeAim.bAuthoritativeFirstScrapeAimApproved = false;
	AuthoritativeAim.AuthoritativeFirstAimSessionId = 0;
	AuthoritativeAim.AuthoritativeFirstAimSequence = 0;
	AuthoritativeAim.AuthoritativeFirstAimClientSampleSeconds = 0.0;
	AuthoritativeAim.BufferedPreFirstAuthoritativeAims.Reset();
	AuthoritativeAim.HighestDroppedPreFirstAimSequence = 0;
	AuthoritativeAim.bBufferedPreFirstAimReplayDiscontinuous = false;
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
	AuthoritativeAim.bAuthoritativeFirstScrapeAimApproved = true;
	AuthoritativeAim.AuthoritativeFirstAimSessionId = FocusSessionId;
	AuthoritativeAim.AuthoritativeFirstAimSequence = AimSequence;
	AuthoritativeAim.AuthoritativeFirstAimClientSampleSeconds = SampleTimeSeconds;
	PrepareBufferedAuthoritativeAimReplay(AimSequence);
	return true;
}

void UMD_ScrapeToolComponent::SendLocalAimIfDue(const float DeltaTime)
{
	if (!IsScrapeFocusReady())
	{
		return;
	}
	AuthoritativeAim.LocalAimSendAccumulatorSeconds += FMath::IsFinite(DeltaTime)
		? FMath::Max(static_cast<double>(DeltaTime), 0.0)
		: 0.0;
	const float RateHz = bIsScraping
		? FMath::Clamp(ScrapingAimSendRateHz, 1.0f, 30.0f)
		: FMath::Clamp(ReadyAimHeartbeatRateHz, 1.0f, 5.0f);
	const double IntervalSeconds = 1.0 / static_cast<double>(RateHz);
	if (AuthoritativeAim.LocalAimSendAccumulatorSeconds + UE_DOUBLE_SMALL_NUMBER
		< IntervalSeconds)
	{
		return;
	}
	AuthoritativeAim.LocalAimSendAccumulatorSeconds = FMath::Fmod(
		AuthoritativeAim.LocalAimSendAccumulatorSeconds,
		IntervalSeconds);

	FMD_ScrapeAimNetSample Sample;
	if (BuildLocalAimSample(Sample))
	{
		const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
		if (bIsScraping
			&& OwnerPawn
			&& OwnerPawn->IsLocallyControlled()
			&& !OwnerPawn->HasAuthority())
		{
			ProcessLocalPredictedScrapeAim(
				Sample,
				QueryCurrentScrapeAim(),
				static_cast<float>(IntervalSeconds));
		}
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
	if (!FMath::IsFinite(AuthoritativeAim.LastAuthoritativeAimRateRefillSeconds)
		|| AuthoritativeAim.LastAuthoritativeAimRateRefillSeconds < 0.0)
	{
		AuthoritativeAim.LastAuthoritativeAimRateRefillSeconds = ServerNowSeconds;
		AuthoritativeAim.AuthoritativeAimRateTokens = FMD_ScrapeAuthoritativeAimState::BurstCapacity;
	}
	const double RefillElapsedSeconds = FMath::Max(
		ServerNowSeconds - AuthoritativeAim.LastAuthoritativeAimRateRefillSeconds,
		0.0);
	AuthoritativeAim.AuthoritativeAimRateTokens = FMath::Min(
		FMD_ScrapeAuthoritativeAimState::BurstCapacity,
		AuthoritativeAim.AuthoritativeAimRateTokens
			+ RefillElapsedSeconds * FMD_ScrapeAuthoritativeAimState::MaximumSampleRateHz);
	AuthoritativeAim.LastAuthoritativeAimRateRefillSeconds = ServerNowSeconds;
	if (AuthoritativeAim.AuthoritativeAimRateTokens + UE_DOUBLE_SMALL_NUMBER < 1.0)
	{
		return false;
	}
	AuthoritativeAim.AuthoritativeAimRateTokens -= 1.0;
	return true;
}

void UMD_ScrapeToolComponent::BufferPreFirstAuthoritativeAim(
	const FMD_ScrapeAimNetSample& Sample,
	const FMD_ScrapeAimQueryResult& ValidatedAim)
{
	if (Sample.FocusSessionId != ScrapeFocusSession.ActiveScrapeFocusSessionId
		|| Sample.Sequence == 0)
	{
		return;
	}
	if (AuthoritativeAim.BufferedPreFirstAuthoritativeAims.Num()
		>= FMD_ScrapeAuthoritativeAimState::MaximumBufferedSamples)
	{
		AuthoritativeAim.HighestDroppedPreFirstAimSequence = FMath::Max(
			AuthoritativeAim.HighestDroppedPreFirstAimSequence,
			AuthoritativeAim.BufferedPreFirstAuthoritativeAims[0].Sample.Sequence);
		AuthoritativeAim.BufferedPreFirstAuthoritativeAims.RemoveAt(
			0,
			1,
			EAllowShrinking::No);
	}
	FMD_BufferedAuthoritativeScrapeAim& Buffered =
		AuthoritativeAim.BufferedPreFirstAuthoritativeAims.AddDefaulted_GetRef();
	Buffered.Sample = Sample;
	Buffered.ValidatedAim = ValidatedAim;
}

void UMD_ScrapeToolComponent::PrepareBufferedAuthoritativeAimReplay(
	const uint32 FirstAimSequence)
{
	AuthoritativeAim.BufferedPreFirstAuthoritativeAims.RemoveAll(
		[FirstAimSequence](
			const FMD_BufferedAuthoritativeScrapeAim& Buffered)
		{
			return Buffered.Sample.Sequence <= FirstAimSequence;
		});
	AuthoritativeAim.BufferedPreFirstAuthoritativeAims.Sort(
		[](const FMD_BufferedAuthoritativeScrapeAim& A,
			const FMD_BufferedAuthoritativeScrapeAim& B)
		{
			return A.Sample.Sequence < B.Sample.Sequence;
		});

	AuthoritativeAim.bBufferedPreFirstAimReplayDiscontinuous =
		AuthoritativeAim.HighestDroppedPreFirstAimSequence > FirstAimSequence;
	uint32 ExpectedSequence = FirstAimSequence + 1;
	if (ExpectedSequence == 0)
	{
		AuthoritativeAim.bBufferedPreFirstAimReplayDiscontinuous = true;
	}
	for (const FMD_BufferedAuthoritativeScrapeAim& Buffered
		: AuthoritativeAim.BufferedPreFirstAuthoritativeAims)
	{
		if (ExpectedSequence == 0
			|| Buffered.Sample.Sequence != ExpectedSequence)
		{
			AuthoritativeAim.bBufferedPreFirstAimReplayDiscontinuous = true;
		}
		ExpectedSequence = Buffered.Sample.Sequence + 1;
	}
	AuthoritativeAim.HighestDroppedPreFirstAimSequence = 0;
}

void UMD_ScrapeToolComponent::ReplayBufferedAuthoritativeAims()
{
	const uint64 ReplayFocusSessionId =
		ScrapeFocusSession.ActiveScrapeFocusSessionId;
	if (!bIsScraping
		|| ReplayFocusSessionId == 0
		|| !IsAuthoritativeFirstScrapeAimApproved(ReplayFocusSessionId)
		|| AuthoritativeAim.AuthoritativeFirstAimSequence == 0)
	{
		return;
	}

	TArray<FMD_BufferedAuthoritativeScrapeAim> BufferedReplay =
		MoveTemp(AuthoritativeAim.BufferedPreFirstAuthoritativeAims);
	AuthoritativeAim.BufferedPreFirstAuthoritativeAims.Reset();
	AuthoritativeAim.HighestDroppedPreFirstAimSequence = 0;
	bool bForceNewAnchor =
		AuthoritativeAim.bBufferedPreFirstAimReplayDiscontinuous;
	AuthoritativeAim.bBufferedPreFirstAimReplayDiscontinuous = false;

	uint32 PreviousSequence = AuthoritativeAim.AuthoritativeFirstAimSequence;
	double PreviousClientSeconds =
		AuthoritativeAim.AuthoritativeFirstAimClientSampleSeconds;
	FMD_ScrapeAimQueryResult PreviousAim = CurrentScrapeAim;
	for (const FMD_BufferedAuthoritativeScrapeAim& Buffered
		: BufferedReplay)
	{
		if (Buffered.Sample.Sequence <= PreviousSequence)
		{
			continue;
		}
		bool bTimeDiscontinuous = false;
		const float SampleDeltaTime =
			FMD_ScrapeAimValidation::CalculateSampleDeltaTime(
				PreviousClientSeconds,
				static_cast<double>(Buffered.Sample.ClientMonotonicSeconds),
				MaxSampleAgeSeconds,
				FMD_ScrapeAuthoritativeAimState::MaximumSampleRateHz,
				bTimeDiscontinuous);
		const bool bSequenceDiscontinuous = PreviousSequence == MAX_uint32
			|| Buffered.Sample.Sequence != PreviousSequence + 1;
		const bool bPositionDiscontinuous =
			FMD_ScrapeAimValidation::IsPositionDiscontinuous(
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
		}
		CurrentScrapeAim = Buffered.ValidatedAim;
		ProcessAuthoritativeScrapeAimResult(
			Buffered.ValidatedAim,
			bStartsNewAnchor ? 0.0f : SampleDeltaTime,
			true,
			bStartsNewAnchor
				? FMath::Max(MaxMissingAimSequences, 0) + 1
				: 0,
			Buffered.Sample.FocusSessionId,
			Buffered.Sample.Sequence);
		if (!bIsScraping
			|| !IsAuthoritativeFirstScrapeAimApproved(ReplayFocusSessionId)
			|| ScrapeFocusSession.ActiveScrapeFocusSessionId
				!= ReplayFocusSessionId)
		{
			break;
		}

		PreviousSequence = Buffered.Sample.Sequence;
		PreviousClientSeconds = static_cast<double>(
			Buffered.Sample.ClientMonotonicSeconds);
		PreviousAim = Buffered.ValidatedAim;
		bForceNewAnchor = false;
	}
}

bool UMD_ScrapeToolComponent::TryAcceptAuthoritativeAimTiming(
	const FMD_ScrapeAimNetSample& Sample,
	const double ServerNowSeconds,
	double& OutClientSampleSeconds)
{
	OutClientSampleSeconds = static_cast<double>(Sample.ClientMonotonicSeconds);
	if (!FMath::IsFinite(OutClientSampleSeconds) || OutClientSampleSeconds < 0.0)
	{
		TerminateAuthoritativeScrapeFocus(
			EMD_ScrapeFocusTerminationReason::ProtocolViolation);
		return false;
	}
	if (!AuthoritativeAim.bAuthoritativeClientClockInitialized)
	{
		AuthoritativeAim.AuthoritativeClientClockOffsetSeconds =
			ServerNowSeconds - OutClientSampleSeconds;
		AuthoritativeAim.bAuthoritativeClientClockInitialized = true;
	}
	const double SampleAgeSeconds = ServerNowSeconds
		- (OutClientSampleSeconds
			+ AuthoritativeAim.AuthoritativeClientClockOffsetSeconds);
	if (SampleAgeSeconds > static_cast<double>(FMath::Max(
			MaxSampleAgeSeconds, 0.01f))
		|| SampleAgeSeconds < -0.05)
	{
		// 구조는 정상이나 너무 오래되거나 미래인 표본은 anchor만 끊고 lease를 갱신하지 않는다.
		ResetStrokeTracking();
		return false;
	}
	return true;
}

bool UMD_ScrapeToolComponent::TryResolveAuthoritativeAimDirection(
	const FVector& CameraRelativeDirection,
	FVector& OutServerEyeLocation,
	FVector& OutWorldDirection)
{
	OutServerEyeLocation = FVector::ZeroVector;
	OutWorldDirection = FVector::ZeroVector;
	if (!IsAuthoritativeScrapeFocusToolCurrent())
	{
		TerminateAuthoritativeScrapeFocus(
			EMD_ScrapeFocusTerminationReason::EligibilityLost);
		return false;
	}
	FMinimalViewInfo ServerViewInfo;
	if (!TryGetAuthoritativeScrapeView(ServerViewInfo))
	{
		TerminateAuthoritativeScrapeFocus(
			EMD_ScrapeFocusTerminationReason::ViewInvalid);
		return false;
	}
	OutServerEyeLocation = ServerViewInfo.Location;
	// The accepted Focus basis is frozen for the session. A delayed movement
	// packet may temporarily restore the pre-Begin ControlRotation, so continuous
	// samples validate their server-owned origin and session-relative direction
	// without comparing against that independently ordered live rotation.
	if (!bHasAuthoritativeScrapeFocusView
		|| OutServerEyeLocation.ContainsNaN()
		|| FVector::Distance(
			OutServerEyeLocation,
			AuthoritativeScrapeFocusViewOrigin)
			> FMath::Max(ScrapeFocusViewOriginDriftToleranceCm, 0.0f))
	{
		TerminateAuthoritativeScrapeFocus(
			EMD_ScrapeFocusTerminationReason::ViewInvalid);
		return false;
	}

	const FRotationMatrix Basis(AuthoritativeScrapeFocusViewRotation);
	OutWorldDirection =
		(Basis.GetScaledAxis(EAxis::X) * CameraRelativeDirection.X
			+ Basis.GetScaledAxis(EAxis::Y) * CameraRelativeDirection.Y
			+ Basis.GetScaledAxis(EAxis::Z) * CameraRelativeDirection.Z)
		.GetSafeNormal();
	if (OutWorldDirection.IsNearlyZero())
	{
		TerminateAuthoritativeScrapeFocus(
			EMD_ScrapeFocusTerminationReason::ProtocolViolation);
		return false;
	}
	return true;
}

bool UMD_ScrapeToolComponent::TryRecordAuthoritativeAimSample(
	const FMD_ScrapeAimNetSample& Sample,
	const bool bAllowReorderedFirstAim,
	const double ClientSampleSeconds,
	const double ServerNowSeconds,
	const FMD_ScrapeAimQueryResult& Aim,
	float& OutSampleDeltaTime,
	int32& OutMissingSequences)
{
	const uint32 PreviousSequence = AuthoritativeAim.LastAuthoritativeAimSequence;
	const double PreviousClientSeconds =
		AuthoritativeAim.LastAuthoritativeClientSampleSeconds;
	if (!bAllowReorderedFirstAim
		&& PreviousSequence != 0
		&& ClientSampleSeconds <= PreviousClientSeconds)
	{
		ResetStrokeTracking();
		return false;
	}
	if (!bAllowReorderedFirstAim
		|| Sample.Sequence > AuthoritativeAim.LastAuthoritativeAimSequence)
	{
		AuthoritativeAim.LastAuthoritativeAimSequence = Sample.Sequence;
		AuthoritativeAim.LastAuthoritativeClientSampleSeconds = ClientSampleSeconds;
	}
	AuthoritativeAim.LastAuthoritativeAimReceiveSeconds = ServerNowSeconds;
	AuthoritativeAim.bLastAuthoritativeAimWasActionable = Aim.IsActionable();
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
		OutSampleDeltaTime = FMD_ScrapeAimValidation::CalculateSampleDeltaTime(
			PreviousClientSeconds,
			ClientSampleSeconds,
			MaxSampleAgeSeconds,
			FMD_ScrapeAuthoritativeAimState::MaximumSampleRateHz,
			bTimeDiscontinuous);
		if (bTimeDiscontinuous
			|| FMD_ScrapeAimValidation::IsPositionDiscontinuous(
				CurrentScrapeAim,
				Aim,
				ReconnectCandidateMaxCm))
		{
			OutMissingSequences = FMath::Max(MaxMissingAimSequences, 0) + 1;
		}
	}
	return true;
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
		|| !ScrapeFocusSession.bAuthoritativeScrapeFocusAccepted
		|| Sample.FocusSessionId == 0
		|| Sample.FocusSessionId != ScrapeFocusSession.ActiveScrapeFocusSessionId
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
		|| !FMD_ScrapeAimValidation::IsDirectionInsideCone(
			RelativeDirection,
			MakeAimAuthorityCone()))
	{
		TerminateAuthoritativeScrapeFocus(
			EMD_ScrapeFocusTerminationReason::ProtocolViolation);
		return false;
	}

	// ASC TargetData와 Pawn RPC는 서로 다른 actor channel이다. 첫 Reliable
	// sample은 아직 승인 전인 현재 session에 한해 newer Unreliable보다 늦게
	// 도착해도 검증한다. Continuous duplicate/old sample은 lease도 갱신하지 않는다.
	const bool bAllowReorderedFirstAim = bIsReliableFirstAim
		&& !AuthoritativeAim.bAuthoritativeFirstScrapeAimApproved;
	if (!bAllowReorderedFirstAim
		&& AuthoritativeAim.LastAuthoritativeAimSequence != 0
		&& Sample.Sequence <= AuthoritativeAim.LastAuthoritativeAimSequence)
	{
		return false;
	}

	const double ServerNow = GetScrapeMonotonicSeconds();
	double ClientSampleSeconds = 0.0;
	if (!TryAcceptAuthoritativeAimTiming(
			Sample,
			ServerNow,
			ClientSampleSeconds))
	{
		return false;
	}

	FVector ServerEyeLocation = FVector::ZeroVector;
	FVector WorldDirection = FVector::ZeroVector;
	if (!TryResolveAuthoritativeAimDirection(
			RelativeDirection,
			ServerEyeLocation,
			WorldDirection))
	{
		return false;
	}

	OutAim = QueryScrapeAim(ServerEyeLocation, WorldDirection);
	return TryRecordAuthoritativeAimSample(
		Sample,
		bAllowReorderedFirstAim,
		ClientSampleSeconds,
		ServerNow,
		OutAim,
		OutSampleDeltaTime,
		OutMissingSequences);
}
