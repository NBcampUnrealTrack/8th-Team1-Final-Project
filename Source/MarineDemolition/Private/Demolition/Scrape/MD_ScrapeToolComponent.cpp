// Copyright Epic Games, Inc. All Rights Reserved.

#include "Demolition/Scrape/MD_ScrapeToolComponent.h"

#include "Engine/Engine.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Sound/SoundBase.h"
#include "Net/UnrealNetwork.h"
#include "Wall/MD_CuttableWall.h"

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
	bLockLookInputToStrokeAxis = true;
	bIsScraping = false;
	bHasPreviousSurfacePosition = false;
	bHasLockedStrokeAxis = false;
	PreviousSurfacePositionCm = FVector2D::ZeroVector;
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
	if (!bLockLookInputToStrokeAxis
		|| !bIsScraping
		|| !bHasLockedStrokeAxis)
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
	ProcessScrapeTrace();
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

	ProcessScrapeTrace();
}

void UMD_ScrapeToolComponent::ProcessScrapeTrace()
{
	FHitResult CurrentHit;
	AMD_CuttableWall* CurrentWall = nullptr;
	if (!TryGetCurrentWallHit(CurrentHit, CurrentWall))
	{
		ResetStrokeTracking();
		return;
	}

	FVector2D RawSurfacePositionCm;
	if (!CurrentWall->TryGetScrapeSurfacePositionFromHit(
		CurrentHit,
		RawSurfacePositionCm))
	{
		ResetStrokeTracking();
		return;
	}

	if (!bHasPreviousSurfacePosition
		|| !PreviousWall.IsValid()
		|| PreviousWall.Get() != CurrentWall)
	{
		BeginStrokeTracking(CurrentWall, RawSurfacePositionCm);
		return;
	}

	FVector2D ConstrainedSurfacePositionCm;
	if (!TryConstrainStrokePosition(
		RawSurfacePositionCm,
		ConstrainedSurfacePositionCm))
	{
		return;
	}

	FVector ConstrainedWorldPosition = CurrentHit.ImpactPoint;
	const bool bHasConstrainedWorldPosition =
		CurrentWall->TryGetScrapeSurfaceWorldPosition(
			ConstrainedSurfacePositionCm,
			ConstrainedWorldPosition);
	if (bLockLookInputToStrokeAxis
		&& bHasConstrainedWorldPosition
		&& !RawSurfacePositionCm.Equals(
			ConstrainedSurfacePositionCm,
			0.01))
	{
		// 조준점과 실제 자국을 한곳에 둔다.
		KeepAimOnScrapePosition(ConstrainedWorldPosition);
	}

	FMD_ScrapeStroke Stroke;
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
	const FVector2D& SurfacePositionCm)
{
	bHasPreviousSurfacePosition = true;
	bHasLockedStrokeAxis = false;
	PreviousSurfacePositionCm = SurfacePositionCm;
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

void UMD_ScrapeToolComponent::KeepAimOnScrapePosition(
	const FVector& WorldPosition) const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn
		|| !OwnerPawn->IsLocallyControlled()
		|| WorldPosition.ContainsNaN())
	{
		return;
	}

	AController* Controller = OwnerPawn->GetController();
	if (!Controller)
	{
		return;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector ViewToScrapePosition = WorldPosition - ViewLocation;
	if (ViewToScrapePosition.IsNearlyZero())
	{
		return;
	}

	FRotator CorrectedViewRotation = ViewToScrapePosition.Rotation();
	CorrectedViewRotation.Roll = ViewRotation.Roll;
	Controller->SetControlRotation(CorrectedViewRotation);
}

void UMD_ScrapeToolComponent::ResetStrokeTracking()
{
	bHasPreviousSurfacePosition = false;
	bHasLockedStrokeAxis = false;
	PreviousSurfacePositionCm = FVector2D::ZeroVector;
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
