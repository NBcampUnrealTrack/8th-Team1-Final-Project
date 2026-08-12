// Copyright Epic Games, Inc. All Rights Reserved.

#include "Component/Scrape/MD_ScrapeToolComponent.h"

#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
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
	DirectionLockThresholdCm = 1.0f;
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
	bHasPreviousSurfacePosition = false;
	bHasLockedStrokeAxis = false;
	SaturatedContaminationStampPatternSeed = 7319;
	PreviousSurfacePositionCm = FVector2D::ZeroVector;
	PreviousSurfaceFace = EMD_ScrapeSurfaceFace::Front;
	StrokeAnchorPositionCm = FVector2D::ZeroVector;
	FurthestSurfacePositionCm = FVector2D::ZeroVector;
	LockedStrokeAxis = FVector2D::ZeroVector;
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
		LastContactWorldLocation,
		COND_SkipOwner);
	DOREPLIFETIME_CONDITION(
		UMD_ScrapeToolComponent,
		bHasLastContact,
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

FVector2D UMD_ScrapeToolComponent::ConstrainLookInput(
	const FVector2D& LookInput) const
{
	if (!bIsScraping || !bHasLockedStrokeAxis)
	{
		return LookInput;
	}

	FVector2D ConstrainedLookInput = LookInput;
	if (FMath::Abs(LockedStrokeAxis.X)
		>= FMath::Abs(LockedStrokeAxis.Y))
	{
		// 가로 긁기에서는 위아래 시선을 고정한다.
		ConstrainedLookInput.Y = 0.0;
	}
	else
	{
		// 세로 긁기에서는 좌우 시선을 고정한다.
		ConstrainedLookInput.X = 0.0;
	}

	return ConstrainedLookInput;
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
	GetOwner()->ForceNetUpdate();
	return true;
}

void UMD_ScrapeToolComponent::OnRep_ScrapeToolSpec()
{
	// 서버에서 새 스펙을 받은 프레임에 이전 로컬 위치부터 새 범위를 잇지 않는다.
	ResetStrokeTracking();
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
	ScrapeLoad = FMath::Clamp(NewLoad, 0.0f, MaximumScrapeLoad);
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

void UMD_ScrapeToolComponent::ClearCurrentScrapeContact()
{
	if (!bHasLastContact)
	{
		return;
	}

	bHasLastContact = false;
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

	bIsScraping = true;
	ResetStrokeTracking();
	SetComponentTickEnabled(true);

	// 입력을 누른 첫 프레임의 조준점을 즉시 기록해 다음 프레임부터 스트로크를 만들 수 있게 한다.
	// 이 호출은 기준점만 잡으므로 조준 보정에 쓸 프레임 시간은 없다.
	ProcessScrapeTrace(0.0f);
}

void UMD_ScrapeToolComponent::StopScraping()
{
	if (!bIsScraping)
	{
		ClearCurrentScrapeContact();
		return;
	}

	bIsScraping = false;
	SetComponentTickEnabled(false);
	ResetStrokeTracking();
}

void UMD_ScrapeToolComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bIsScraping)
	{
		return;
	}

	ProcessScrapeTrace(DeltaTime);
}

void UMD_ScrapeToolComponent::ProcessScrapeTrace(const float DeltaTime)
{
	FHitResult CurrentHit;
	FHitResult DirectBarnacleHit;
	UMD_ScrapeSurfaceComponent* CurrentSurface = nullptr;
	AMD_Barnacle* OccludingBarnacle = nullptr;
	if (!TryGetCurrentSurfaceHit(
			CurrentHit,
			CurrentSurface,
			OccludingBarnacle,
			DirectBarnacleHit))
	{
		ResetStrokeTracking();

		// 오염 표면이 아니어도 따개비일 수 있다. 둘은 성격이 달라 경로를 갈라 둔다.
		if (IsValid(OccludingBarnacle))
		{
			ProcessDirectBarnacleContact(
				OccludingBarnacle,
				DirectBarnacleHit,
				DeltaTime);
			return;
		}

		SetTrackedBarnacle(nullptr);
		return;
	}

	SetCurrentScrapeContact(CurrentHit.ImpactPoint);

	if (IsValid(OccludingBarnacle))
	{
		// 클라이언트에서 부착 복제가 BeginPlay보다 늦어도 직접 맞은 순간 복구한다.
		// 장애물 등록이 있어야 아래 경로 차단과 조준 보정이 함께 작동한다.
		OccludingBarnacle->RegisterWithScrapeSurface(CurrentSurface);
	}

	FVector2D RawSurfacePositionCm;
	EMD_ScrapeSurfaceFace CurrentSurfaceFace =
		EMD_ScrapeSurfaceFace::Front;
	if (!CurrentSurface->TryGetScrapeSurfacePositionFromHit(
		CurrentHit,
		RawSurfacePositionCm,
		CurrentSurfaceFace))
	{
		ResetStrokeTracking();
		return;
	}

	if (!bHasPreviousSurfacePosition
		|| !PreviousSurface.IsValid()
		|| PreviousSurface.Get() != CurrentSurface
		|| PreviousSurfaceFace != CurrentSurfaceFace)
	{
		BeginStrokeTracking(
			CurrentSurface,
			RawSurfacePositionCm,
			CurrentSurfaceFace);
		return;
	}

	FVector2D ConstrainedSurfacePositionCm;
	if (!TryConstrainStrokePosition(
		RawSurfacePositionCm,
		ConstrainedSurfacePositionCm))
	{
		return;
	}

	// 따개비는 벽에 붙어 자란 입체물이라 헤라가 그 위를 지나갈 수 없다.
	// 막히면 스트로크를 거기서 끊고, 가려던 속도를 그대로 따개비에 때린다.
	// 조준 보정보다 먼저 잘라야 시선까지 그 자리에 붙들린다. 뒤로 미루면 자국만
	// 멈추고 조준은 따개비를 넘어가 "막혔다"가 읽히지 않는다.
	const FVector2D AttemptedSurfacePositionCm = ConstrainedSurfacePositionCm;
	AMD_Barnacle* BlockingBarnacle = nullptr;
	FVector2D BlockedSurfacePositionCm = ConstrainedSurfacePositionCm;
	const bool bStrokeBlockedByBarnacle =
		CurrentSurface->TryClampScrapeStrokeAgainstObstacles(
			CurrentSurfaceFace,
			PreviousSurfacePositionCm,
			AttemptedSurfacePositionCm,
			ScrapeToolSpec.BladeWidthCm * 0.5f,
			BlockedSurfacePositionCm,
			BlockingBarnacle);

	if (bStrokeBlockedByBarnacle)
	{
		ConstrainedSurfacePositionCm = BlockedSurfacePositionCm;
	}

	// 앞면이 여러 깊이에 걸친 벽에서도 조준 보정이 실제 표면 위에 남도록
	// 지금 맞고 있는 히트 지점을 깊이 기준으로 넘긴다.
	FVector ConstrainedWorldPosition = CurrentHit.ImpactPoint;
	const bool bHasConstrainedWorldPosition =
		CurrentSurface->TryGetScrapeSurfaceWorldPositionAtDepth(
			ConstrainedSurfacePositionCm,
			CurrentSurfaceFace,
			CurrentHit.ImpactPoint,
			ConstrainedWorldPosition);
	if (bHasConstrainedWorldPosition
		&& !RawSurfacePositionCm.Equals(
			ConstrainedSurfacePositionCm,
			0.01))
	{
		// 조준점과 실제 자국을 한곳에 둔다.
		// 지금 맞고 있는 지점과 보정 목표의 각도 차이만큼만 시선을 돌린다.
		// 단, 보정 목표가 다른 표면에 가려져 있으면 보정하지 않는다.
		if (IsAimCorrectionTargetVisible(
				CurrentHit.TraceStart,
				ConstrainedWorldPosition,
				bStrokeBlockedByBarnacle
					? BlockingBarnacle
					: nullptr))
		{
			KeepAimOnScrapePosition(
				CurrentHit.TraceStart,
				CurrentHit.ImpactPoint,
				ConstrainedWorldPosition,
				DeltaTime);
		}
	}

	if (bHasLockedStrokeAxis
		&& FVector2D::DotProduct(
			ConstrainedSurfacePositionCm - StrokeAnchorPositionCm,
			LockedStrokeAxis)
			> FVector2D::DotProduct(
				FurthestSurfacePositionCm - StrokeAnchorPositionCm,
				LockedStrokeAxis))
	{
		FurthestSurfacePositionCm = ConstrainedSurfacePositionCm;
	}

	FMD_ScrapeStroke Stroke;
	Stroke.SurfaceFace = CurrentSurfaceFace;
	Stroke.StartPositionCm = PreviousSurfacePositionCm;
	Stroke.EndPositionCm = ConstrainedSurfacePositionCm;
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		const bool bIsSaturated =
			GetScrapeLoadStage() == EMD_ScrapeLoadStage::Stage3;
		FMD_ScrapeToolSpec EffectiveToolSpec = ScrapeToolSpec;
		if (bIsSaturated)
		{
			EffectiveToolSpec.RemovalPerTravelCentimeter *=
				GetSaturatedRemovalMultiplier();
		}
		float RemovedCleanRatioDelta = 0.0f;
		const FMD_ScrapeResult Result =
			CurrentSurface->ApplyLocalScrapeStrokeWithContaminationStamp(
				Stroke,
				EffectiveToolSpec,
				StrokeAnchorPositionCm,
				LockedStrokeAxis,
				bIsSaturated
					? GetSaturatedContaminationStampRatio()
					: 0.0f,
				bIsSaturated
					? GetSaturatedContaminationStampCoverage()
					: 0.0f,
				GetSaturatedContaminationStampDepthCm(),
				GetSaturatedContaminationStampSpacingCm(),
				SaturatedContaminationStampPatternSeed,
				RemovedCleanRatioDelta);

		AddScrapeLoad(CalculateScrapeLoadGainFromCleanRatioDelta(
			RemovedCleanRatioDelta,
			ScrapeLoadPerRemovedPercent));

		if (Result.bIsInputValid && Result.bWasStrokeLongEnough)
		{
			const FVector FeedbackLocation = bHasConstrainedWorldPosition
				? ConstrainedWorldPosition
				: CurrentHit.ImpactPoint;

			DispatchAuthoritativeScrapeResult(
				CurrentSurface,
				Result,
				FeedbackLocation);
		}
	}

	if (bStrokeBlockedByBarnacle)
	{
		// 힘은 실제로 지나간 거리가 아니라 "지나가려 했던" 거리에서 나온다.
		// 잘라 낸 거리로 재면 따개비에 바짝 붙을수록 약해져, 세게 그을수록
		// 오히려 안 떨어지는 거꾸로 된 손맛이 된다.
		const float AttemptedTravelCm = static_cast<float>(
			FVector2D::Distance(
				PreviousSurfacePositionCm,
				AttemptedSurfacePositionCm));

		FVector StrokeStartWorldPosition;
		FVector StrokeEndWorldPosition;
		FVector ImpactWorldDirection = FVector::ZeroVector;
		if (CurrentSurface->TryGetScrapeSurfaceWorldPosition(
				PreviousSurfacePositionCm,
				CurrentSurfaceFace,
				StrokeStartWorldPosition)
			&& CurrentSurface->TryGetScrapeSurfaceWorldPosition(
				AttemptedSurfacePositionCm,
				CurrentSurfaceFace,
				StrokeEndWorldPosition))
		{
			ImpactWorldDirection =
				StrokeEndWorldPosition - StrokeStartWorldPosition;
		}

		SetTrackedBarnacle(BlockingBarnacle);
		DeliverBarnacleImpact(
			BlockingBarnacle,
			CalculateImpactSpeedCmPerSecond(AttemptedTravelCm, DeltaTime),
			ImpactWorldDirection);
	}
	else
	{
		// 경로가 트였다. 다음에 다시 걸리면 새 스윙으로 센다.
		SetTrackedBarnacle(nullptr);
	}

	PreviousSurfacePositionCm = ConstrainedSurfacePositionCm;
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

	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->GetController() || !GetWorld())
	{
		return false;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	OwnerPawn->GetController()->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector TraceEnd =
		ViewLocation + (ViewRotation.Vector() * TraceDistanceCm);
	return TryTraceScrapeTarget(
		ViewLocation,
		TraceEnd,
		OwnerPawn,
		OutHit,
		OutSurface,
		OutOccludingBarnacle,
		OutBarnacleHit);
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
	FlushPendingContaminationPaintTail();
	bHasPreviousSurfacePosition = true;
	bHasLockedStrokeAxis = false;
	PreviousSurfacePositionCm = SurfacePositionCm;
	PreviousSurfaceFace = SurfaceFace;
	StrokeAnchorPositionCm = SurfacePositionCm;
	FurthestSurfacePositionCm = SurfacePositionCm;
	LockedStrokeAxis = FVector2D::ZeroVector;
	PreviousSurface = Surface;
}

bool UMD_ScrapeToolComponent::TryConstrainStrokePosition(
	const FVector2D& RawSurfacePositionCm,
	FVector2D& OutConstrainedPositionCm)
{
	OutConstrainedPositionCm = RawSurfacePositionCm;
	if (!bHasPreviousSurfacePosition)
	{
		return false;
	}

	const FVector2D AnchorToRawCm =
		RawSurfacePositionCm - StrokeAnchorPositionCm;
	if (!bHasLockedStrokeAxis)
	{
		const double SafeLockThresholdCm = FMath::Max(
			static_cast<double>(DirectionLockThresholdCm),
			static_cast<double>(UE_SMALL_NUMBER));
		if (AnchorToRawCm.Size() < SafeLockThresholdCm)
		{
			return false;
		}

		// 첫 움직임에서 더 큰 축을 이번 줄의 방향으로 고정한다.
		LockedStrokeAxis = FMath::Abs(AnchorToRawCm.X)
			>= FMath::Abs(AnchorToRawCm.Y)
			? FVector2D(AnchorToRawCm.X >= 0.0 ? 1.0 : -1.0, 0.0)
			: FVector2D(0.0, AnchorToRawCm.Y >= 0.0 ? 1.0 : -1.0);
		bHasLockedStrokeAxis = true;
	}

	const FVector2D ParallelOffsetCm = LockedStrokeAxis
		* FVector2D::DotProduct(AnchorToRawCm, LockedStrokeAxis);
	FVector2D PerpendicularOffsetCm =
		AnchorToRawCm - ParallelOffsetCm;
	const double PerpendicularDistanceCm =
		PerpendicularOffsetCm.Size();
	const double SafeMaximumDriftCm = FMath::Max(
		0.0,
		static_cast<double>(MaximumPerpendicularDriftCm));
	const double SafeBreakDistanceCm = FMath::Max(
		SafeMaximumDriftCm,
		static_cast<double>(BreakStrokePerpendicularDistanceCm));

	if (PerpendicularDistanceCm > SafeBreakDistanceCm)
	{
		// 큰 옆 이동은 긁지 않고 새 줄의 시작점이 된다.
		FlushPendingContaminationPaintTail();
		PreviousSurfacePositionCm = RawSurfacePositionCm;
		StrokeAnchorPositionCm = RawSurfacePositionCm;
		FurthestSurfacePositionCm = RawSurfacePositionCm;
		LockedStrokeAxis = FVector2D::ZeroVector;
		bHasLockedStrokeAxis = false;
		return false;
	}

	if (PerpendicularDistanceCm > SafeMaximumDriftCm
		&& PerpendicularDistanceCm > UE_SMALL_NUMBER)
	{
		PerpendicularOffsetCm *=
			SafeMaximumDriftCm / PerpendicularDistanceCm;
	}

	OutConstrainedPositionCm = StrokeAnchorPositionCm
		+ ParallelOffsetCm
		+ PerpendicularOffsetCm;
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
	if (!bUseAimCorrection
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

void UMD_ScrapeToolComponent::FlushPendingContaminationPaintTail()
{
	UMD_ScrapeSurfaceComponent* const Surface = PreviousSurface.Get();
	if (!bHasPreviousSurfacePosition
		|| !bHasLockedStrokeAxis
		|| !IsValid(Surface)
		|| GetScrapeLoadStage() != EMD_ScrapeLoadStage::Stage3
		|| FVector2D::Distance(
			StrokeAnchorPositionCm,
			FurthestSurfacePositionCm) <= UE_SMALL_NUMBER)
	{
		return;
	}

	FMD_ScrapeStroke FullPathStroke;
	FullPathStroke.SurfaceFace = PreviousSurfaceFace;
	FullPathStroke.StartPositionCm = StrokeAnchorPositionCm;
	FullPathStroke.EndPositionCm = FurthestSurfacePositionCm;
	Surface->FlushLocalContaminationPaintTail(
		FullPathStroke,
		LockedStrokeAxis,
		ScrapeToolSpec,
		GetSaturatedContaminationStampRatio(),
		GetSaturatedContaminationStampCoverage(),
		GetSaturatedContaminationStampDepthCm(),
		GetSaturatedContaminationStampSpacingCm(),
		SaturatedContaminationStampPatternSeed);
}

void UMD_ScrapeToolComponent::ResetStrokeTracking()
{
	FlushPendingContaminationPaintTail();
	ClearCurrentScrapeContact();
	bHasPreviousSurfacePosition = false;
	bHasLockedStrokeAxis = false;
	PreviousSurfacePositionCm = FVector2D::ZeroVector;
	PreviousSurfaceFace = EMD_ScrapeSurfaceFace::Front;
	StrokeAnchorPositionCm = FVector2D::ZeroVector;
	FurthestSurfacePositionCm = FVector2D::ZeroVector;
	LockedStrokeAxis = FVector2D::ZeroVector;
	PreviousSurface.Reset();
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
