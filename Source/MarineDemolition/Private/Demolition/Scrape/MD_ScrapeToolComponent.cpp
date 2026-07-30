// Copyright Epic Games, Inc. All Rights Reserved.

#include "Demolition/Scrape/MD_ScrapeToolComponent.h"

#include "Engine/Engine.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Sound/SoundBase.h"
#include "Wall/MD_CuttableWall.h"

UMD_ScrapeToolComponent::UMD_ScrapeToolComponent()
{
	TraceDistanceCm = 220.0f;
	TraceChannel = ECC_Visibility;
	ContaminationScrapeSound = nullptr;
	BareSurfaceScrapeSound = nullptr;
	ContaminationScrapeEffect = nullptr;
	BareSurfaceScrapeEffect = nullptr;
	MinimumFeedbackIntervalSeconds = 0.08f;
	bShowDebugFeedback = true;
	bIsScraping = false;
	bHasPreviousHit = false;
	LastFeedbackWorldSeconds = -DBL_MAX;

	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
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

void UMD_ScrapeToolComponent::StartScraping()
{
	if (bIsScraping)
	{
		return;
	}

	bIsScraping = true;
	ResetPreviousHit();
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
	ResetPreviousHit();
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
		ResetPreviousHit();
		return;
	}

	if (bHasPreviousHit
		&& PreviousWall.IsValid()
		&& PreviousWall.Get() == CurrentWall)
	{
		const FMD_ScrapeResult Result =
			CurrentWall->ApplyScrapeStrokeFromHits(PreviousHit, CurrentHit);

		if (Result.bIsInputValid && Result.bWasStrokeLongEnough)
		{
			OnScrapeApplied.Broadcast(CurrentWall, Result);
			EmitFeedback(CurrentWall, Result, CurrentHit.ImpactPoint);
		}
	}

	PreviousHit = CurrentHit;
	PreviousWall = CurrentWall;
	bHasPreviousHit = true;
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

void UMD_ScrapeToolComponent::ResetPreviousHit()
{
	bHasPreviousHit = false;
	PreviousHit = FHitResult();
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
				Wall->GetContaminationRemovedPercent()));
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
