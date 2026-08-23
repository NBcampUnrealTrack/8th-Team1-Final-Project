// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/Trash/MD_TrashBinSpillStreamVisualComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UObjectGlobals.h"
#include "World/Trash/MD_TrashBin.h"

namespace MD_TrashBinSpillStreamPrivate
{
	constexpr float StreamDiameterCm = 3.2f;
	constexpr float CylinderDiameterCm = 100.0f;
	constexpr float CylinderHeightCm = 100.0f;
	constexpr float SegmentOverlapCm = 0.6f;
	constexpr double MinimumStreamLengthCm = 0.5;
	constexpr double MaximumCurveSagCm = 12.0;
	constexpr double CurveSagDistanceFraction = 0.08;

	bool IsFiniteVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y)
			&& FMath::IsFinite(Value.Z);
	}
}

UMD_TrashBinSpillStreamVisualComponent::
	UMD_TrashBinSpillStreamVisualComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.bAllowTickOnDedicatedServer = false;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	SetIsReplicatedByDefault(false);
}

void UMD_TrashBinSpillStreamVisualComponent::InitializeVisual()
{
	HideStream();
	if (SegmentVisuals.Num() == VisualSegmentCount
		|| !CanRunCosmeticVisual())
	{
		return;
	}
	// 이 함수는 렌더링 peer의 BeginPlay에서만 호출된다. 따라서 전용 서버는
	// stream class의 CDO를 만들더라도 mesh/material을 동기 로드하지 않는다.
	if (!IsValid(SegmentMeshAsset))
	{
		SegmentMeshAsset = LoadObject<UStaticMesh>(
			nullptr,
			TEXT("/Game/Assets/Contamination/Spill/SM_MD_SpillStreamSegment.SM_MD_SpillStreamSegment"));
	}
	if (!IsValid(SegmentMaterial))
	{
		SegmentMaterial = LoadObject<UMaterialInterface>(
			nullptr,
			TEXT("/Game/Materials/MI_MD_TrashBinSludge.MI_MD_TrashBinSludge"));
	}
	if (!IsValid(SegmentMeshAsset) || !IsValid(SegmentMaterial))
	{
		return;
	}

	DestroyVisualSegments();
	AActor* const Owner = GetOwner();
	if (!IsValid(Owner))
	{
		return;
	}

	SegmentVisuals.Reserve(VisualSegmentCount);
	for (int32 SegmentIndex = 0;
		SegmentIndex < VisualSegmentCount;
		++SegmentIndex)
	{
		const FName SegmentName = MakeUniqueObjectName(
			Owner,
			UStaticMeshComponent::StaticClass(),
			FName(*FString::Printf(
				TEXT("SpillStreamSegment_%d"),
				SegmentIndex)));
		UStaticMeshComponent* const Segment =
			NewObject<UStaticMeshComponent>(
				Owner,
				SegmentName,
				RF_Transient);
		if (!IsValid(Segment))
		{
			DestroyVisualSegments();
			return;
		}

		Owner->AddInstanceComponent(Segment);
		Segment->SetIsReplicated(false);
		Segment->SetMobility(EComponentMobility::Movable);
		Segment->SetStaticMesh(SegmentMeshAsset);
		Segment->SetMaterial(0, SegmentMaterial);
		Segment->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Segment->SetCollisionResponseToAllChannels(ECR_Ignore);
		Segment->SetGenerateOverlapEvents(false);
		Segment->SetCanEverAffectNavigation(false);
		Segment->SetCastShadow(false);
		Segment->SetReceivesDecals(false);
		Segment->SetHiddenInGame(true);
		Segment->SetVisibility(false, /*bPropagateToChildren=*/true);
		Segment->RegisterComponent();
		SegmentVisuals.Add(Segment);
	}
}

bool UMD_TrashBinSpillStreamVisualComponent::PlayCommittedPulse(
	const FMD_TrashBinSpillCosmeticResult& Result)
{
	const UWorld* const World = GetWorld();
	return IsValid(World)
		&& PlayCommittedPulseAtTime(Result, World->GetTimeSeconds());
}

bool UMD_TrashBinSpillStreamVisualComponent::ShouldCreateForNetMode(
	const ENetMode NetMode)
{
	return NetMode != NM_DedicatedServer;
}

void UMD_TrashBinSpillStreamVisualComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* const ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	(void)DeltaTime;

	const UWorld* const World = GetWorld();
	if (!IsValid(World))
	{
		HideStream();
		return;
	}

	RefreshVisualAtTime(World->GetTimeSeconds());
}

void UMD_TrashBinSpillStreamVisualComponent::OnUnregister()
{
	HideStream();
	DestroyVisualSegments();
	Super::OnUnregister();
}

bool UMD_TrashBinSpillStreamVisualComponent::PlayCommittedPulseAtTime(
	const FMD_TrashBinSpillCosmeticResult& Result,
	const double CurrentWorldSeconds)
{
	AActor* const Owner = GetOwner();
	if (!FMath::IsFinite(CurrentWorldSeconds)
		|| CurrentWorldSeconds < 0.0
		|| !Result.IsValid()
		|| Result.Trigger != EMD_TrashBinSpillTrigger::SustainedTilt
		|| !CanRunCosmeticVisual()
		|| SegmentVisuals.Num() != VisualSegmentCount
		|| !IsValid(Owner))
	{
		return false;
	}

	const FVector CandidateSourceActorLocal =
		Result.SourceActorLocalLocation;
	const FVector SourceWorldLocation = Owner->GetActorTransform()
		.TransformPosition(CandidateSourceActorLocal);
	const FVector FixedContactWorldLocation = Result.ContactWorldLocation;
	const double StreamLengthCm = FVector::Distance(
		SourceWorldLocation,
		FixedContactWorldLocation);
	if (!MD_TrashBinSpillStreamPrivate::IsFiniteVector(
			CandidateSourceActorLocal)
		|| !FMath::IsFinite(StreamLengthCm)
		|| StreamLengthCm
			< MD_TrashBinSpillStreamPrivate::MinimumStreamLengthCm)
	{
		return false;
	}

	const double CandidateLeaseExpiry =
		CurrentWorldSeconds + Result.StreamLeaseSeconds;
	if (!FMath::IsFinite(CandidateLeaseExpiry)
		|| CandidateLeaseExpiry <= CurrentWorldSeconds)
	{
		return false;
	}

	SourceActorLocal = CandidateSourceActorLocal;
	ContactWorldLocation = FixedContactWorldLocation;
	CurrentSourceWorldLocation = SourceWorldLocation;
	LeaseExpiresWorldSeconds = CandidateLeaseExpiry;
	bStreamActive = true;
	SetComponentTickEnabled(true);
	RefreshVisualAtTime(CurrentWorldSeconds);
	return bStreamActive;
}

void UMD_TrashBinSpillStreamVisualComponent::RefreshVisualAtTime(
	const double CurrentWorldSeconds)
{
	if (!bStreamActive)
	{
		SetComponentTickEnabled(false);
		return;
	}
	if (!FMath::IsFinite(CurrentWorldSeconds)
		|| CurrentWorldSeconds < 0.0
		|| !FMath::IsFinite(LeaseExpiresWorldSeconds)
		|| CurrentWorldSeconds >= LeaseExpiresWorldSeconds
		|| !CanRunCosmeticVisual())
	{
		HideStream();
		return;
	}

	const AActor* const Owner = GetOwner();
	if (!IsValid(Owner))
	{
		HideStream();
		return;
	}
	CurrentSourceWorldLocation = Owner->GetActorTransform()
		.TransformPosition(SourceActorLocal);
	if (!MD_TrashBinSpillStreamPrivate::IsFiniteVector(
			CurrentSourceWorldLocation)
		|| !MD_TrashBinSpillStreamPrivate::IsFiniteVector(
			ContactWorldLocation))
	{
		HideStream();
		return;
	}

	const double RemainingSeconds =
		LeaseExpiresWorldSeconds - CurrentWorldSeconds;
	const float DiameterScale = RemainingSeconds < DiameterShrinkSeconds
		? static_cast<float>(FMath::Clamp(
			RemainingSeconds / DiameterShrinkSeconds,
			0.0,
			1.0))
		: 1.0f;
	UpdateSegmentTransforms(DiameterScale);
}

void UMD_TrashBinSpillStreamVisualComponent::UpdateSegmentTransforms(
	const float DiameterScale)
{
	const double StreamLengthCm = FVector::Distance(
		CurrentSourceWorldLocation,
		ContactWorldLocation);
	if (!FMath::IsFinite(StreamLengthCm)
		|| StreamLengthCm
			< MD_TrashBinSpillStreamPrivate::MinimumStreamLengthCm)
	{
		HideStream();
		return;
	}

	const double SagCm = FMath::Min(
		MD_TrashBinSpillStreamPrivate::MaximumCurveSagCm,
		StreamLengthCm
			* MD_TrashBinSpillStreamPrivate::CurveSagDistanceFraction);
	const FVector ControlPoint = FMath::Lerp(
		CurrentSourceWorldLocation,
		ContactWorldLocation,
		0.5)
		+ FVector::DownVector * SagCm;
	const float SafeDiameterScale = FMath::Clamp(
		DiameterScale,
		0.0f,
		1.0f);
	const float RadiusScale =
		MD_TrashBinSpillStreamPrivate::StreamDiameterCm
		* SafeDiameterScale
		/ MD_TrashBinSpillStreamPrivate::CylinderDiameterCm;

	for (int32 SegmentIndex = 0;
		SegmentIndex < SegmentVisuals.Num();
		++SegmentIndex)
	{
		UStaticMeshComponent* const Segment = SegmentVisuals[SegmentIndex];
		if (!IsValid(Segment))
		{
			HideStream();
			return;
		}

		const double StartAlpha =
			static_cast<double>(SegmentIndex) / VisualSegmentCount;
		const double EndAlpha =
			static_cast<double>(SegmentIndex + 1) / VisualSegmentCount;
		const FVector SegmentStart = EvaluateQuadraticBezier(
			CurrentSourceWorldLocation,
			ControlPoint,
			ContactWorldLocation,
			StartAlpha);
		const FVector SegmentEnd = EvaluateQuadraticBezier(
			CurrentSourceWorldLocation,
			ControlPoint,
			ContactWorldLocation,
			EndAlpha);
		const FVector SegmentDirection = SegmentEnd - SegmentStart;
		const double SegmentLengthCm = SegmentDirection.Size();
		if (!FMath::IsFinite(SegmentLengthCm)
			|| SegmentLengthCm <= UE_SMALL_NUMBER)
		{
			HideStream();
			return;
		}

		const FQuat SegmentRotation = FRotationMatrix::MakeFromZ(
			SegmentDirection / SegmentLengthCm).ToQuat();
		const FVector SegmentScale(
			RadiusScale,
			RadiusScale,
			(SegmentLengthCm
				+ MD_TrashBinSpillStreamPrivate::SegmentOverlapCm)
				/ MD_TrashBinSpillStreamPrivate::CylinderHeightCm);
		Segment->SetWorldTransform(FTransform(
			SegmentRotation,
			(SegmentStart + SegmentEnd) * 0.5,
			SegmentScale));
		Segment->SetHiddenInGame(false);
		Segment->SetVisibility(true, /*bPropagateToChildren=*/true);
	}
}

void UMD_TrashBinSpillStreamVisualComponent::HideStream()
{
	bStreamActive = false;
	LeaseExpiresWorldSeconds = -1.0;
	SetComponentTickEnabled(false);
	for (UStaticMeshComponent* const Segment : SegmentVisuals)
	{
		if (IsValid(Segment))
		{
			Segment->SetHiddenInGame(true);
			Segment->SetVisibility(false, /*bPropagateToChildren=*/true);
		}
	}
}

void UMD_TrashBinSpillStreamVisualComponent::DestroyVisualSegments()
{
	for (UStaticMeshComponent* const Segment : SegmentVisuals)
	{
		if (IsValid(Segment))
		{
			Segment->DestroyComponent();
		}
	}
	SegmentVisuals.Reset();
}

bool UMD_TrashBinSpillStreamVisualComponent::CanRunCosmeticVisual() const
{
	const UWorld* const World = GetWorld();
	return IsValid(GetOwner())
		&& IsValid(World)
		&& ShouldCreateForNetMode(World->GetNetMode());
}

FVector UMD_TrashBinSpillStreamVisualComponent::EvaluateQuadraticBezier(
	const FVector& Start,
	const FVector& Control,
	const FVector& End,
	const double Alpha)
{
	const double SafeAlpha = FMath::Clamp(Alpha, 0.0, 1.0);
	const double InverseAlpha = 1.0 - SafeAlpha;
	return Start * (InverseAlpha * InverseAlpha)
		+ Control * (2.0 * InverseAlpha * SafeAlpha)
		+ End * (SafeAlpha * SafeAlpha);
}
