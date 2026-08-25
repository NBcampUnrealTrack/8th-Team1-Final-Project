// Copyright Epic Games, Inc. All Rights Reserved.

#include "Component/Scrape/MD_ScrapeToolComponent.h"

#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Audio/MD_AudioSubsystem.h"
#include "Net/UnrealNetwork.h"

UMD_ScrapeToolComponent::UMD_ScrapeToolComponent()
{
	SetIsReplicatedByDefault(true);

	TraceDistanceCm = 220.0f;
	TraceChannel = ECC_Visibility;
	// 긁는 소리는 지속음 하나뿐이다. 에셋 참조는 DA_MD_Audio_Clean 에 있다.
	ScrapeLoopSoundTag =
		FGameplayTag::RequestGameplayTag(TEXT("SFX.Clean.Hera.ScrapeSludge"), /*ErrorIfNotFound*/ false);
	ContaminationScrapeEffect = nullptr;
	BareSurfaceScrapeEffect = nullptr;
	ScrapeLoopIdleGraceSeconds = 0.25f;
	ScrapeLoopMinTravelCm = 0.0f;
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
	ScrapeFocusSession.ScrapeFocusGeneration = 0;
	ScrapeFocusPointerNormalized = FVector2D(0.5, 0.5);
	bHasScrapeFocusView = false;
	StrokePath.bHasPreviousSurfacePosition = false;
	StrokePath.bHasLockedStrokeAxis = false;
	ContaminationPaint.SaturatedContaminationStampPatternSeed = 7319;
	StrokePath.PreviousSurfacePositionCm = FVector2D::ZeroVector;
	StrokePath.PreviousSurfaceFace = EMD_ScrapeSurfaceFace::Front;
	StrokePath.StrokeAnchorPositionCm = FVector2D::ZeroVector;
	StrokePath.FurthestSurfacePositionCm = FVector2D::ZeroVector;
	StrokePath.LockedStrokeAxis = FVector2D::ZeroVector;
	StrokePath.PendingScrapePathArcLengthCm = 0.0;
	StrokePath.ScrapePathArcLengthCm = 0.0;
	StrokePath.LastEmittedScrapePathArcLengthCm = 0.0;
	StrokePath.PendingResolvedScrapePathPointsCm.Reset();
	StrokePath.PendingResolvedScrapePathTravelCm = 0.0;
	StrokePath.PendingScrapePathSampleTimeSeconds = 0.0;
	StrokePath.LastEmittedScrapePathPositionCm = FVector2D::ZeroVector;
	StrokePath.LastEmittedBladeDepthAxis = FVector2D::ZeroVector;
	StrokePath.StableBladeDepthAxis = FVector2D::ZeroVector;
	StrokePath.LastRawTravelTangent = FVector2D::ZeroVector;
	StrokePath.bHasStableBladeDepthAxis = false;
	StrokePath.bHasLastRawTravelTangent = false;
	StrokePath.bHasLastScrapeSurfaceHit = false;
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

void UMD_ScrapeToolComponent::StartScrapeLoopAudio()
{
	if (ScrapeLoopHandle.IsValid() || !ScrapeLoopSoundTag.IsValid())
	{
		return;
	}

	const UWorld* World = GetWorld();
	UMD_AudioSubsystem* Audio = World ? World->GetSubsystem<UMD_AudioSubsystem>() : nullptr;
	AActor* Owner = GetOwner();
	if (!Audio || !IsValid(Owner))
	{
		return;
	}

	// 헤라는 손에 있다. 벽이 아니라 캐릭터를 따라가야 한다
	ScrapeLoopHandle = Audio->PlayEventAttached(ScrapeLoopSoundTag, Owner->GetRootComponent());
}

void UMD_ScrapeToolComponent::StopScrapeLoopAudio()
{
	if (!ScrapeLoopHandle.IsValid())
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (UMD_AudioSubsystem* Audio = World ? World->GetSubsystem<UMD_AudioSubsystem>() : nullptr)
	{
		// 음수 = 데이터 에셋의 FadeOutSeconds 를 쓴다
		Audio->StopHandle(ScrapeLoopHandle, -1.0f);
	}

	ScrapeLoopHandle = FMD_AudioHandle();
}

void UMD_ScrapeToolComponent::UpdateScrapeLoopAudio()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (!bIsScraping)
	{
		StopScrapeLoopAudio();
		return;
	}

	const double Now = World->GetTimeSeconds();

	// 커밋된 경로만 보면 안 된다. 이동량은 Pending 에 모였다가 임계값을 넘을 때
	// 한 덩어리로 넘어오므로, 그것만 보면 스트로크 첫 구간에서 소리가 늦게 붙는다.
	// 두 값을 합치면 드래그하는 동안 연속으로 증가한다
	const double Arc =
		StrokePath.ScrapePathArcLengthCm + StrokePath.PendingScrapePathArcLengthCm;

	// 경로가 늘었다 = 날이 표면을 훑고 있다.
	// 버튼만 누르고 마우스를 멈추면 이 값이 그대로라 소리가 멎는다.
	//
	// 방향을 꺾으면 ResetScrapePathAccumulator 가 누적을 0 으로 되돌린다.
	// 그때 값이 거꾸로 떨어지므로 "늘었는가" 만 보면 이동으로 안 잡혀
	// 하필 방향 전환마다 소리가 끊긴다. 경로가 리셋됐다는 것 자체가
	// 플레이어가 날을 꺾었다는 뜻이라 이동으로 친다
	const double PreviousArc = LastObservedScrapePathArcLengthCm;
	const bool bPathReset = Arc < PreviousArc - UE_KINDA_SMALL_NUMBER;
	if (bPathReset || Arc - PreviousArc > ScrapeLoopMinTravelCm)
	{
		LastScrapeTravelWorldSeconds = Now;
	}
	LastObservedScrapePathArcLengthCm = Arc;

	const double ArcDelta = Arc - PreviousArc;
	const bool bDragging =
		(Now - LastScrapeTravelWorldSeconds) <= static_cast<double>(ScrapeLoopIdleGraceSeconds);
	DrawScrapeAudioDebug(Now, Arc, ArcDelta, bDragging);

	if (!bDragging)
	{
		StopScrapeLoopAudio();
		return;
	}

	// 소재는 하나뿐이다. 면 상태에 따라 갈아 끼우면 훑는 중에 소리가 바뀌어 산만해진다
	StartScrapeLoopAudio();
}

void UMD_ScrapeToolComponent::DrawScrapeAudioDebug(
	double Now, double Arc, double ArcDelta, bool bDragging) const
{
	if (!bShowDebugFeedback || !GEngine)
	{
		return;
	}

	const double SinceTravel = Now - LastScrapeTravelWorldSeconds;

	GEngine->AddOnScreenDebugMessage(
		reinterpret_cast<uint64>(this) + 1,
		0.15f,
		bDragging ? FColor::Green : FColor::Red,
		FString::Printf(
			TEXT("[헤라오디오] drag=%s | arc=%.2f d=%.3f | 이동후=%.2fs | handle=%s"),
			bDragging ? TEXT("O") : TEXT("X"),
			Arc, ArcDelta,
			SinceTravel > 1000.0 ? -1.0 : SinceTravel,
			ScrapeLoopHandle.IsValid() ? TEXT("O") : TEXT("X")));
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
			&& ScrapeFocusSession.bAuthoritativeScrapeFocusAccepted))
	{
		// Ability gate와 Commit 직전의 최신 포인터 ray를 동기적으로 확정한다.
		CurrentScrapeAim = QueryCurrentScrapeAim();
	}
	if (GetOwner() && GetOwner()->HasAuthority()
		&& ScrapeFocusSession.bAuthoritativeScrapeFocusAccepted
		&& !IsAuthoritativeFirstScrapeAimApproved(
			ScrapeFocusSession.ActiveScrapeFocusSessionId))
	{
		return;
	}
	if (!IsScrapeFocusReady() || !CurrentScrapeAim.IsActionable())
	{
		// 승인된 Ready session과 actionable first aim이 없는 LMB는 저장하지 않는다.
		return;
	}

	bIsScraping = true;
	LastObservedScrapePathArcLengthCm =
		StrokePath.ScrapePathArcLengthCm + StrokePath.PendingScrapePathArcLengthCm;
	LastScrapeTravelWorldSeconds = -DBL_MAX;
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
	ContaminationPaint.Stage3SubStrokeGeneration = 0;
	AdvanceStage3SubStrokeGeneration();
	ContaminationPaint.Stage3SampleSequence = 0;
	ContaminationPaint.CommittedStage3Dabs.Reset();
	if (ScrapeFocusState == EMD_ScrapeFocusState::FocusedReady)
	{
		SetScrapeFocusState(EMD_ScrapeFocusState::FocusedScraping);
	}
	ResetStrokeTracking();
	SetComponentTickEnabled(true);

	// 서버 권위 session은 Reliable first TargetData로 검증한 cache를 정확히 한 번
	// 첫 anchor로 사용한다. 서버 중앙 ControlRotation을 다시 query하지 않는다.
	if (GetOwner() && GetOwner()->HasAuthority()
		&& ScrapeFocusSession.bAuthoritativeScrapeFocusAccepted)
	{
		if (AuthoritativeAim.bAuthoritativeFirstScrapeAimApproved)
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
	StopScrapeLoopAudio();
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

	UpdateScrapeLoopAudio();

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
		ScrapeFocusSession.FocusPendingElapsedSeconds += FMath::IsFinite(DeltaTime)
			? FMath::Max(static_cast<double>(DeltaTime), 0.0)
			: 0.0;
		if (ScrapeFocusSession.FocusPendingElapsedSeconds
			>= static_cast<double>(FMath::Max(BeginAckTimeoutSeconds, 0.01f)))
		{
			EndScrapeFocusInternal(true);
			return;
		}
	}

	if (GetOwner() && GetOwner()->HasAuthority()
		&& ScrapeFocusSession.bAuthoritativeScrapeFocusAccepted
		&& ScrapeFocusSession.ActiveScrapeFocusSessionId != 0)
	{
		const double SilenceSeconds = GetScrapeMonotonicSeconds()
			- AuthoritativeAim.LastAuthoritativeAimReceiveSeconds;
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
		&& ScrapeFocusSession.bAuthoritativeScrapeFocusAccepted)
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

void UMD_ScrapeToolComponent::ResetStrokeTracking()
{
	const bool bHadTrackedSubStroke =
		StrokePath.bHasPreviousSurfacePosition || ContaminationPaint.bStage3PathActive;
	const uint32 GenerationBeforeFlush = ContaminationPaint.Stage3SubStrokeGeneration;
	FlushPendingScrapePathApplication();
	const bool bHadOrBuiltStage3Path = ContaminationPaint.bStage3PathActive;
	FlushPendingContaminationPaintTail();
	if ((bHadTrackedSubStroke || bHadOrBuiltStage3Path)
		&& ContaminationPaint.Stage3SubStrokeGeneration == GenerationBeforeFlush)
	{
		AdvanceStage3SubStrokeGeneration();
	}
	ClearCurrentScrapeContact();
	StrokePath.bHasPreviousSurfacePosition = false;
	StrokePath.bHasLockedStrokeAxis = false;
	StrokePath.PreviousSurfacePositionCm = FVector2D::ZeroVector;
	StrokePath.PreviousSurfaceFace = EMD_ScrapeSurfaceFace::Front;
	StrokePath.StrokeAnchorPositionCm = FVector2D::ZeroVector;
	StrokePath.FurthestSurfacePositionCm = FVector2D::ZeroVector;
	StrokePath.LockedStrokeAxis = FVector2D::ZeroVector;
	StrokePath.PreviousSurface.Reset();
	StrokePath.bHasLastScrapeSurfaceHit = false;
	StrokePath.PendingScrapePathSampleTimeSeconds = 0.0;
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

	// 긁는 소리는 지속음(ScrapeLoopSoundTag) 하나로 끝낸다.
	// 면에 따라 원샷을 덧대면 훑는 중에 소재가 바뀌어 들려 산만하다
	UNiagaraSystem* FeedbackEffect = bBareSurfaceFeedback
		? BareSurfaceScrapeEffect
		: ContaminationScrapeEffect;

	if (FeedbackEffect)
	{
		// 이펙트 로컬 +X가 벽 법선을 향하도록 정렬한다. 법선을 못 구하면 기존처럼 무회전.
		FVector ContactNormal = FVector::ZeroVector;
		const FRotator EffectRotation =
			Surface->TryGetScrapeSurfaceWorldNormal(Result.SurfaceFace, ContactNormal)
				? ContactNormal.Rotation()
				: FRotator::ZeroRotator;
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this,
			FeedbackEffect,
			ContactLocation,
			EffectRotation);
	}

	if (FeedbackEffect)
	{
		LastFeedbackWorldSeconds = WorldSeconds;
	}
}
