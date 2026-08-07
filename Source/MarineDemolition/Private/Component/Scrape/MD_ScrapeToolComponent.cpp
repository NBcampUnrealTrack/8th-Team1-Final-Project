// Copyright Epic Games, Inc. All Rights Reserved.

#include "Component/Scrape/MD_ScrapeToolComponent.h"

#include "Engine/Engine.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Sound/SoundBase.h"
#include "Net/UnrealNetwork.h"
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
	bIsScraping = false;
	bHasPreviousSurfacePosition = false;
	bHasLockedStrokeAxis = false;
	PreviousSurfacePositionCm = FVector2D::ZeroVector;
	PreviousSurfaceFace = EMD_ScrapeSurfaceFace::Front;
	StrokeAnchorPositionCm = FVector2D::ZeroVector;
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

	ScrapeToolSpec = NewToolSpec;

	// 성능이 바뀐 프레임에 이전 위치부터 새 범위를 소급 적용하지 않도록 경로를 끊는다.
	ResetStrokeTracking();
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
	AMD_CuttableWall* CurrentWall = nullptr;
	if (!TryGetCurrentWallHit(CurrentHit, CurrentWall))
	{
		ResetStrokeTracking();
		return;
	}

	FVector2D RawSurfacePositionCm;
	EMD_ScrapeSurfaceFace CurrentSurfaceFace =
		EMD_ScrapeSurfaceFace::Front;
	if (!CurrentWall->TryGetScrapeSurfacePositionFromHit(
		CurrentHit,
		RawSurfacePositionCm,
		CurrentSurfaceFace))
	{
		ResetStrokeTracking();
		return;
	}

	if (!bHasPreviousSurfacePosition
		|| !PreviousWall.IsValid()
		|| PreviousWall.Get() != CurrentWall
		|| PreviousSurfaceFace != CurrentSurfaceFace)
	{
		BeginStrokeTracking(
			CurrentWall,
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

	// 앞면이 여러 깊이에 걸친 벽에서도 조준 보정이 실제 표면 위에 남도록
	// 지금 맞고 있는 히트 지점을 깊이 기준으로 넘긴다.
	FVector ConstrainedWorldPosition = CurrentHit.ImpactPoint;
	const bool bHasConstrainedWorldPosition =
		CurrentWall->TryGetScrapeSurfaceWorldPositionAtDepth(
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
				ConstrainedWorldPosition))
		{
			KeepAimOnScrapePosition(
				CurrentHit.TraceStart,
				CurrentHit.ImpactPoint,
				ConstrainedWorldPosition,
				DeltaTime);
		}
	}

	FMD_ScrapeStroke Stroke;
	Stroke.SurfaceFace = CurrentSurfaceFace;
	Stroke.StartPositionCm = PreviousSurfacePositionCm;
	Stroke.EndPositionCm = ConstrainedSurfacePositionCm;
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		const FMD_ScrapeResult Result =
			CurrentWall->ApplyLocalScrapeStrokeWithToolSpec(
				Stroke,
				ScrapeToolSpec);

		if (Result.bIsInputValid && Result.bWasStrokeLongEnough)
		{
			const FVector FeedbackLocation = bHasConstrainedWorldPosition
				? ConstrainedWorldPosition
				: CurrentHit.ImpactPoint;

			DispatchAuthoritativeScrapeResult(
				CurrentWall,
				Result,
				FeedbackLocation);
		}
	}

	PreviousSurfacePositionCm = ConstrainedSurfacePositionCm;
}

void UMD_ScrapeToolComponent::DispatchAuthoritativeScrapeResult(
	AMD_CuttableWall* Wall,
	const FMD_ScrapeResult& Result,
	const FVector& ContactLocation)
{
	// 서버 게임 로직은 권한 인스턴스의 이벤트를 계속 구독할 수 있다.
	OnScrapeApplied.Broadcast(Wall, Result);

	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (OwnerPawn
		&& OwnerPawn->IsLocallyControlled()
		&& GetNetMode() != NM_DedicatedServer)
	{
		EmitFeedback(Wall, Result, ContactLocation);
		return;
	}

	// 원격 소유자의 사운드/이펙트와 Blueprint 피드백은 해당 클라이언트에서 실행한다.
	ClientReceiveScrapeResult(Wall, Result, ContactLocation);
}

void UMD_ScrapeToolComponent::ClientReceiveScrapeResult_Implementation(
	AMD_CuttableWall* Wall,
	const FMD_ScrapeResult& Result,
	const FVector_NetQuantize10 ContactLocation)
{
	OnScrapeApplied.Broadcast(Wall, Result);
	EmitFeedback(Wall, Result, ContactLocation);
}

bool UMD_ScrapeToolComponent::TryGetCurrentWallHit(
	FHitResult& OutHit,
	AMD_CuttableWall*& OutWall) const
{
	OutHit = FHitResult();
	OutWall = nullptr;

	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->GetController() || !GetWorld())
	{
		return false;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	OwnerPawn->GetController()->GetPlayerViewPoint(ViewLocation, ViewRotation);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MD_ScrapeTrace), true);
	QueryParams.AddIgnoredActor(OwnerPawn);

	const FVector TraceEnd =
		ViewLocation + (ViewRotation.Vector() * TraceDistanceCm);

	if (!GetWorld()->LineTraceSingleByChannel(
			OutHit,
			ViewLocation,
			TraceEnd,
			TraceChannel,
			QueryParams))
	{
		return false;
	}

	OutWall = Cast<AMD_CuttableWall>(OutHit.GetActor());
	return IsValid(OutWall);
}

void UMD_ScrapeToolComponent::BeginStrokeTracking(
	AMD_CuttableWall* Wall,
	const FVector2D& SurfacePositionCm,
	const EMD_ScrapeSurfaceFace SurfaceFace)
{
	bHasPreviousSurfacePosition = true;
	bHasLockedStrokeAxis = false;
	PreviousSurfacePositionCm = SurfacePositionCm;
	PreviousSurfaceFace = SurfaceFace;
	StrokeAnchorPositionCm = SurfacePositionCm;
	LockedStrokeAxis = FVector2D::ZeroVector;
	PreviousWall = Wall;
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
			? FVector2D(1.0, 0.0)
			: FVector2D(0.0, 1.0);
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
		PreviousSurfacePositionCm = RawSurfacePositionCm;
		StrokeAnchorPositionCm = RawSurfacePositionCm;
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
	const FVector& WorldPosition) const
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

void UMD_ScrapeToolComponent::ResetStrokeTracking()
{
	bHasPreviousSurfacePosition = false;
	bHasLockedStrokeAxis = false;
	PreviousSurfacePositionCm = FVector2D::ZeroVector;
	PreviousSurfaceFace = EMD_ScrapeSurfaceFace::Front;
	StrokeAnchorPositionCm = FVector2D::ZeroVector;
	LockedStrokeAxis = FVector2D::ZeroVector;
	PreviousWall.Reset();
}

void UMD_ScrapeToolComponent::EmitFeedback(
	AMD_CuttableWall* Wall,
	const FMD_ScrapeResult& Result,
	const FVector& ContactLocation)
{
	if (!Wall || !GetWorld() || !Result.bHadSurfaceContact)
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
				TEXT("헤라: %s | 오염 제거 %.1f%%"),
				GetContactTypeLabel(Result.ContactType),
				FMath::Clamp(Result.CleanRatioAfter, 0.0f, 1.0f) * 100.0f));
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
