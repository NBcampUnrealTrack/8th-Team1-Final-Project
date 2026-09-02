#include "World/Trash/MD_TrashSpawnVolume.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/World.h"
#include "Gameplay/Trash/MD_TrashLibrary.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Kismet/GameplayStatics.h"
#include "Math/RandomStream.h"
#include "Misc/Guid.h"
#include "Util/MD_LogUtility.h"
#include "World/Trash/MD_TrashItem.h"

static constexpr int32 CirclePreviewSegmentCount = 32;

static bool BuildPolygonVertices(
	const USplineComponent* PolygonSpline,
	const int32 SamplesPerSegment,
	TArray<FVector2D>& OutVertices,
	FBox2D& OutBounds)
{
	OutVertices.Reset();
	OutBounds = FBox2D(ForceInit);

	if (!IsValid(PolygonSpline)
		|| !PolygonSpline->IsClosedLoop()
		|| PolygonSpline->GetNumberOfSplinePoints() < 3)
	{
		return false;
	}

	const int32 SegmentCount = PolygonSpline->GetNumberOfSplineSegments();
	const int32 SafeSamplesPerSegment = FMath::Clamp(SamplesPerSegment, 1, 32);
	OutVertices.Reserve(SegmentCount * SafeSamplesPerSegment);

	for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
	{
		for (int32 SampleIndex = 0; SampleIndex < SafeSamplesPerSegment; ++SampleIndex)
		{
			const float Alpha = static_cast<float>(SampleIndex) / SafeSamplesPerSegment;
			const FVector LocalLocation = PolygonSpline->GetLocationAtSplineInputKey(
				static_cast<float>(SegmentIndex) + Alpha,
				ESplineCoordinateSpace::Local);
			const FVector2D Point(LocalLocation.X, LocalLocation.Y);

			if (!OutVertices.IsEmpty() && OutVertices.Last().Equals(Point, UE_KINDA_SMALL_NUMBER))
			{
				continue;
			}

			OutVertices.Add(Point);
			OutBounds += Point;
		}
	}

	if (OutVertices.Num() < 3)
	{
		return false;
	}

	double TwiceSignedArea = 0.0;
	for (int32 PointIndex = 0; PointIndex < OutVertices.Num(); ++PointIndex)
	{
		const FVector2D& Current = OutVertices[PointIndex];
		const FVector2D& Next = OutVertices[(PointIndex + 1) % OutVertices.Num()];
		TwiceSignedArea += Current.X * Next.Y - Next.X * Current.Y;
	}

	return !FMath::IsNearlyZero(TwiceSignedArea);
}

static bool IsPointInsidePolygon(const FVector2D& Point, const TArray<FVector2D>& Vertices)
{
	bool bInside = false;
	for (int32 CurrentIndex = 0, PreviousIndex = Vertices.Num() - 1;
		CurrentIndex < Vertices.Num();
		PreviousIndex = CurrentIndex++)
	{
		const FVector2D& Current = Vertices[CurrentIndex];
		const FVector2D& Previous = Vertices[PreviousIndex];
		const bool bCrossesHorizontalRay = (Current.Y > Point.Y) != (Previous.Y > Point.Y);
		if (!bCrossesHorizontalRay)
		{
			continue;
		}

		const double IntersectionX = Current.X
			+ (Point.Y - Current.Y) * (Previous.X - Current.X) / (Previous.Y - Current.Y);
		if (Point.X < IntersectionX)
		{
			bInside = !bInside;
		}
	}
	return bInside;
}

AMD_TrashSpawnVolume::AMD_TrashSpawnVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	SpawnVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("SpawnVolume"));
	SpawnVolume->SetupAttachment(SceneRoot);
	SpawnVolume->SetBoxExtent(FVector(500.0f, 500.0f, 100.0f));
	SpawnVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SpawnVolume->SetGenerateOverlapEvents(false);
	SpawnVolume->SetHiddenInGame(true);

	CircleBoundary = CreateDefaultSubobject<USplineComponent>(TEXT("CircleBoundary"));
	CircleBoundary->SetupAttachment(SceneRoot);
	CircleBoundary->SetClosedLoop(true);
	CircleBoundary->SetHiddenInGame(true);
	CircleBoundary->bDrawDebug = true;

	PolygonBoundary = CreateDefaultSubobject<USplineComponent>(TEXT("PolygonBoundary"));
	PolygonBoundary->SetupAttachment(SceneRoot);
	PolygonBoundary->SetHiddenInGame(true);
	PolygonBoundary->bDrawDebug = true;
	PolygonBoundary->bEditableWhenInherited = true;
	PolygonBoundary->bInputSplinePointsToConstructionScript = false;
	PolygonBoundary->ClearSplinePoints(false);
	PolygonBoundary->AddSplinePoint(FVector(-500.0f, -500.0f, 0.0f), ESplineCoordinateSpace::Local, false);
	PolygonBoundary->AddSplinePoint(FVector(500.0f, -500.0f, 0.0f), ESplineCoordinateSpace::Local, false);
	PolygonBoundary->AddSplinePoint(FVector(500.0f, 500.0f, 0.0f), ESplineCoordinateSpace::Local, false);
	PolygonBoundary->AddSplinePoint(FVector(-500.0f, 500.0f, 0.0f), ESplineCoordinateSpace::Local, false);
	for (int32 PointIndex = 0; PointIndex < PolygonBoundary->GetNumberOfSplinePoints(); ++PointIndex)
	{
		PolygonBoundary->SetSplinePointType(PointIndex, ESplinePointType::Linear, false);
	}
	PolygonBoundary->SetClosedLoop(true, true);

	TrashItemClass = AMD_TrashItem::StaticClass();
	AllowedTrashCategoryTags.Add(MDGameplayTags::Score_Sorting);

	UpdateCircleBoundary();
	UpdateShapeVisualization();
}

void AMD_TrashSpawnVolume::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	UpdateCircleBoundary();
	UpdateShapeVisualization();
}

void AMD_TrashSpawnVolume::BeginPlay()
{
	Super::BeginPlay();

	if (bSpawnOnBeginPlay && GetNetMode() != NM_Client && HasAuthority())
	{
		SpawnTrash();
	}
}

int32 AMD_TrashSpawnVolume::SpawnTrash()
{
	if (GetNetMode() == NM_Client || !HasAuthority() || SpawnCount <= 0 || !IsValid(GetWorld()))
	{
		return 0;
	}

	if (!TrashItemClass)
	{
		UE_LOG(MarineDemolition, Warning,
			TEXT("TrashSpawnVolume '%s'에 TrashItemClass가 없어 생성하지 않습니다."), *GetName());
		return 0;
	}

	if (!IsSpawnShapeConfigured())
	{
		UE_LOG(MarineDemolition, Warning,
			TEXT("TrashSpawnVolume '%s'의 Spawn Shape 설정이 유효하지 않아 생성하지 않습니다."),
			*GetName());
		return 0;
	}

	const TArray<FName> EligibleTrashKindIds = BuildEligibleTrashKindIds();
	if (EligibleTrashKindIds.IsEmpty())
	{
		UE_LOG(MarineDemolition, Warning,
			TEXT("TrashSpawnVolume '%s'의 태그 필터와 일치하는 쓰레기 종류가 없습니다."), *GetName());
		return 0;
	}

	CompactSpawnedTrashItems();

	LastUsedSeed = bRandomizeSeedEachSpawn
		? static_cast<int32>(GetTypeHash(FGuid::NewGuid()))
		: SpawnSeed;
	FRandomStream RandomStream(LastUsedSeed);

	TArray<FVector> OccupiedLocations;
	OccupiedLocations.Reserve(SpawnedTrashItems.Num() + SpawnCount);
	for (const AMD_TrashItem* ExistingItem : SpawnedTrashItems)
	{
		if (IsValid(ExistingItem))
		{
			OccupiedLocations.Add(ExistingItem->GetActorLocation());
		}
	}

	int32 SpawnedCount = 0;
	for (int32 SpawnIndex = 0; SpawnIndex < SpawnCount; ++SpawnIndex)
	{
		FVector SpawnLocation;
		if (!FindSpawnLocation(RandomStream, OccupiedLocations, SpawnLocation))
		{
			break;
		}

		const FName TrashKindId = EligibleTrashKindIds[
			RandomStream.RandRange(0, EligibleTrashKindIds.Num() - 1)];
		const FRotator SpawnRotation(0.0f, RandomStream.FRandRange(0.0f, 360.0f), 0.0f);
		const FTransform SpawnTransform(SpawnRotation, SpawnLocation);

		AMD_TrashItem* SpawnedItem = GetWorld()->SpawnActorDeferred<AMD_TrashItem>(
			TrashItemClass,
			SpawnTransform,
			this,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (!IsValid(SpawnedItem))
		{
			continue;
		}

		SpawnedItem->SetTrashKindId(TrashKindId);
		UGameplayStatics::FinishSpawningActor(SpawnedItem, SpawnTransform);

		SpawnedTrashItems.Add(SpawnedItem);
		OccupiedLocations.Add(SpawnedItem->GetActorLocation());
		++SpawnedCount;
	}

	if (SpawnedCount < SpawnCount)
	{
		UE_LOG(MarineDemolition, Warning,
			TEXT("TrashSpawnVolume '%s'가 요청한 %d개 중 %d개만 배치했습니다. 볼륨 크기, 최소 간격, 재시도 횟수를 확인하세요."),
			*GetName(), SpawnCount, SpawnedCount);
	}

	return SpawnedCount;
}

void AMD_TrashSpawnVolume::ClearSpawnedTrash()
{
	if (GetNetMode() == NM_Client || !HasAuthority())
	{
		return;
	}

	for (AMD_TrashItem* SpawnedItem : SpawnedTrashItems)
	{
		if (IsValid(SpawnedItem))
		{
			SpawnedItem->Destroy();
		}
	}

	SpawnedTrashItems.Reset();
}

int32 AMD_TrashSpawnVolume::RegenerateTrash()
{
	if (GetNetMode() == NM_Client || !HasAuthority())
	{
		return 0;
	}

	ClearSpawnedTrash();
	return SpawnTrash();
}

int32 AMD_TrashSpawnVolume::GetSpawnedTrashCount() const
{
	int32 LiveCount = 0;
	for (const AMD_TrashItem* SpawnedItem : SpawnedTrashItems)
	{
		LiveCount += IsValid(SpawnedItem) ? 1 : 0;
	}
	return LiveCount;
}

TArray<FName> AMD_TrashSpawnVolume::BuildEligibleTrashKindIds() const
{
	TArray<FName> EligibleTrashKindIds;
	if (AllowedTrashCategoryTags.IsEmpty())
	{
		return EligibleTrashKindIds;
	}

	for (const FName TrashKindId : UMD_TrashLibrary::GetTrashKindIds())
	{
		FMD_TrashKindRow TrashKindRow;
		if (!UMD_TrashLibrary::FindTrashKindRow(TrashKindId, TrashKindRow)
			|| !TrashKindRow.Category.IsValid())
		{
			continue;
		}

		for (const FGameplayTag& AllowedCategoryTag : AllowedTrashCategoryTags)
		{
			if (AllowedCategoryTag.IsValid() && TrashKindRow.Category.MatchesTag(AllowedCategoryTag))
			{
				EligibleTrashKindIds.Add(TrashKindId);
				break;
			}
		}
	}

	return EligibleTrashKindIds;
}

bool AMD_TrashSpawnVolume::FindSpawnLocation(
	FRandomStream& RandomStream,
	const TArray<FVector>& OccupiedLocations,
	FVector& OutLocation) const
{
	TArray<FVector2D> PolygonVertices;
	FBox2D PolygonBounds(ForceInit);
	if (SpawnShape == EMD_TrashSpawnShape::CustomPolygon
		&& !BuildPolygonVertices(
			PolygonBoundary,
			PolygonSamplesPerSegment,
			PolygonVertices,
			PolygonBounds))
	{
		return false;
	}

	const int32 SafeMaxAttempts = FMath::Max(1, MaxPlacementAttemptsPerItem);
	const float SafeMinimumSpacing = FMath::Max(0.0f, MinimumSpacing);
	const float MinimumSpacingSquared = FMath::Square(SafeMinimumSpacing);
	const float SafeShapeHalfHeight = FMath::Max(0.0f, ShapeHalfHeight);

	for (int32 Attempt = 0; Attempt < SafeMaxAttempts; ++Attempt)
	{
		FVector CandidateLocation = FVector::ZeroVector;
		switch (SpawnShape)
		{
		case EMD_TrashSpawnShape::Box:
		{
			const FVector BoxExtent = SpawnVolume->GetUnscaledBoxExtent();
			const FVector LocalLocation(
				RandomStream.FRandRange(-BoxExtent.X, BoxExtent.X),
				RandomStream.FRandRange(-BoxExtent.Y, BoxExtent.Y),
				RandomStream.FRandRange(-BoxExtent.Z, BoxExtent.Z));
			CandidateLocation = SpawnVolume->GetComponentTransform().TransformPosition(LocalLocation);
			break;
		}

		case EMD_TrashSpawnShape::Circle:
		{
			const float Radius = FMath::Max(CircleRadius, 1.0f)
				* FMath::Sqrt(RandomStream.FRand());
			const float AngleRadians = RandomStream.FRandRange(0.0f, 2.0f * UE_PI);
			float SinAngle = 0.0f;
			float CosAngle = 0.0f;
			FMath::SinCos(&SinAngle, &CosAngle, AngleRadians);

			const FVector LocalLocation(
				CosAngle * Radius,
				SinAngle * Radius,
				RandomStream.FRandRange(-SafeShapeHalfHeight, SafeShapeHalfHeight));
			CandidateLocation = CircleBoundary->GetComponentTransform().TransformPosition(LocalLocation);
			break;
		}

		case EMD_TrashSpawnShape::CustomPolygon:
		{
			const FVector2D CandidatePoint(
				RandomStream.FRandRange(PolygonBounds.Min.X, PolygonBounds.Max.X),
				RandomStream.FRandRange(PolygonBounds.Min.Y, PolygonBounds.Max.Y));
			if (!IsPointInsidePolygon(CandidatePoint, PolygonVertices))
			{
				continue;
			}

			const FVector LocalLocation(
				CandidatePoint.X,
				CandidatePoint.Y,
				RandomStream.FRandRange(-SafeShapeHalfHeight, SafeShapeHalfHeight));
			CandidateLocation = PolygonBoundary->GetComponentTransform().TransformPosition(LocalLocation);
			break;
		}

		default:
			return false;
		}

		bool bHasEnoughSpacing = true;
		if (SafeMinimumSpacing > 0.0f)
		{
			for (const FVector& OccupiedLocation : OccupiedLocations)
			{
				if (FVector::DistSquared(CandidateLocation, OccupiedLocation) < MinimumSpacingSquared)
				{
					bHasEnoughSpacing = false;
					break;
				}
			}
		}

		if (bHasEnoughSpacing)
		{
			OutLocation = CandidateLocation;
			return true;
		}
	}

	return false;
}

bool AMD_TrashSpawnVolume::IsSpawnShapeConfigured() const
{
	switch (SpawnShape)
	{
	case EMD_TrashSpawnShape::Box:
		if (IsValid(SpawnVolume))
		{
			const FVector BoxExtent = SpawnVolume->GetUnscaledBoxExtent();
			return BoxExtent.X > UE_KINDA_SMALL_NUMBER
				&& BoxExtent.Y > UE_KINDA_SMALL_NUMBER
				&& BoxExtent.Z >= 0.0f;
		}
		return false;

	case EMD_TrashSpawnShape::Circle:
		return IsValid(CircleBoundary)
			&& FMath::IsFinite(CircleRadius)
			&& CircleRadius > UE_KINDA_SMALL_NUMBER
			&& FMath::IsFinite(ShapeHalfHeight)
			&& ShapeHalfHeight >= 0.0f;

	case EMD_TrashSpawnShape::CustomPolygon:
	{
		TArray<FVector2D> PolygonVertices;
		FBox2D PolygonBounds(ForceInit);
		return FMath::IsFinite(ShapeHalfHeight)
			&& ShapeHalfHeight >= 0.0f
			&& BuildPolygonVertices(
				PolygonBoundary,
				PolygonSamplesPerSegment,
				PolygonVertices,
				PolygonBounds);
	}

	default:
		return false;
	}
}

void AMD_TrashSpawnVolume::UpdateShapeVisualization()
{
	const bool bShowBox = SpawnShape == EMD_TrashSpawnShape::Box;
	const bool bShowCircle = SpawnShape == EMD_TrashSpawnShape::Circle;
	const bool bShowPolygon = SpawnShape == EMD_TrashSpawnShape::CustomPolygon;

	if (IsValid(SpawnVolume))
	{
		SpawnVolume->SetVisibility(bShowBox, true);
	}
	if (IsValid(CircleBoundary))
	{
		CircleBoundary->SetVisibility(bShowCircle, true);
	}
	if (IsValid(PolygonBoundary))
	{
		if (!PolygonBoundary->IsClosedLoop())
		{
			PolygonBoundary->SetClosedLoop(true, true);
		}
		PolygonBoundary->SetVisibility(bShowPolygon, true);
	}
}

void AMD_TrashSpawnVolume::UpdateCircleBoundary()
{
	if (!IsValid(CircleBoundary))
	{
		return;
	}

	const float SafeRadius = FMath::Max(CircleRadius, 1.0f);
	CircleBoundary->ClearSplinePoints(false);
	for (int32 PointIndex = 0; PointIndex < CirclePreviewSegmentCount; ++PointIndex)
	{
		const float AngleRadians = 2.0f * UE_PI * PointIndex
			/ CirclePreviewSegmentCount;
		float SinAngle = 0.0f;
		float CosAngle = 0.0f;
		FMath::SinCos(&SinAngle, &CosAngle, AngleRadians);
		CircleBoundary->AddSplinePoint(
			FVector(CosAngle * SafeRadius, SinAngle * SafeRadius, 0.0f),
			ESplineCoordinateSpace::Local,
			false);
		CircleBoundary->SetSplinePointType(PointIndex, ESplinePointType::Linear, false);
	}
	CircleBoundary->SetClosedLoop(true, true);
}

void AMD_TrashSpawnVolume::CompactSpawnedTrashItems()
{
	SpawnedTrashItems.RemoveAll([](const TObjectPtr<AMD_TrashItem>& SpawnedItem)
	{
		return !IsValid(SpawnedItem);
	});
}
