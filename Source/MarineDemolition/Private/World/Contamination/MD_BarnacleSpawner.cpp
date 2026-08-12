#include "World/Contamination/MD_BarnacleSpawner.h"

#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Math/RandomStream.h"
#include "Math/RotationMatrix.h"
#include "Util/MD_LogUtility.h"
#include "World/Contamination/MD_Barnacle.h"
#include "World/Wall/MD_CuttableWall.h"

namespace
{
	/**
	 * 자리 하나당 최대 시도 횟수.
	 * 볼륨이 허공을 크게 덮고 있으면 표면을 못 찾는 시도가 이어질 수 있어 상한을 둔다.
	 */
	constexpr int32 MaximumAttemptsPerBarnacle = 24;

	/** 볼륨 안 임의 지점에서 표면을 찾을 때 쏘아 보는 방향 */
	const FVector SurfaceSearchDirections[] = {
		FVector(1.0, 0.0, 0.0),
		FVector(-1.0, 0.0, 0.0),
		FVector(0.0, 1.0, 0.0),
		FVector(0.0, -1.0, 0.0),
		FVector(0.0, 0.0, 1.0),
		FVector(0.0, 0.0, -1.0) };
}

AMD_BarnacleSpawner::AMD_BarnacleSpawner()
{
	PrimaryActorTick.bCanEverTick = false;
	// 배치기 자신은 복제하지 않는다. 서버가 붙인 따개비 액터가 복제된다.
	bReplicates = false;

	SpawnVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("SpawnVolume"));
	SetRootComponent(SpawnVolume);
	SpawnVolume->SetBoxExtent(FVector(400.0, 400.0, 200.0));
	SpawnVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SpawnVolume->SetHiddenInGame(true);
	SpawnVolume->ShapeColor = FColor(120, 200, 160);
}

void AMD_BarnacleSpawner::BeginPlay()
{
	Super::BeginPlay();

	if (bSpawnOnBeginPlay && HasAuthority())
	{
		SpawnBarnacles();
	}
}

int32 AMD_BarnacleSpawner::SpawnBarnacles()
{
	if (!HasAuthority())
	{
		return 0;
	}

	// 설정이 비어 조용히 빠져나가면 "아무 일도 안 일어나는데 이유를 모르는" 상태가 된다.
	// 따개비가 안 보이는 원인 대부분이 여기라 반드시 남긴다.
	if (!BarnacleClass)
	{
		UE_LOG(MarineDemolition, Warning,
			TEXT("%s: BarnacleClass가 비어 있어 따개비를 붙이지 않았다. "
				"Class Defaults에서 따개비 Blueprint를 지정하라."),
			*GetName());
		return 0;
	}

	if (BarnacleCount <= 0)
	{
		UE_LOG(MarineDemolition, Warning,
			TEXT("%s: BarnacleCount가 %d이라 따개비를 붙이지 않았다."),
			*GetName(), BarnacleCount);
		return 0;
	}

	// 초기 배치는 항상 0번부터 시작한다. Day가 바뀌어도 같은 시드·같은 순번이라
	// 제거 목록으로 걸러 낼 수 있다.
	NextSpawnIndex = 0;
	SpawnedBarnacles.Reset();

	const int32 SpawnedCount = PlaceBarnacles(
		BarnacleCount,
		PlacementSeed,
		0,
		true);

	// 개수가 곧 점수 예산이다 (기획 05 따개비x32). 조용히 모자라면 점검 기준이
	// 설계대로 성립하지 않으므로 반드시 남긴다.
	// 제거 목록 때문에 덜 나온 것은 정상이므로 그 경우는 세지 않는다.
	const int32 ExpectedCount = BarnacleCount - RemovedSpawnIndices.Num();
	if (SpawnedCount < ExpectedCount)
	{
		UE_LOG(MarineDemolition, Warning,
			TEXT("%s: 따개비 %d개를 붙여야 하나 %d개만 붙였다. 볼륨 안에 조건에 맞는 "
				"표면이 부족하다. 확인 순서: (1) 볼륨이 벽에 닿아 있는가 "
				"(2) 그 벽이 철거 대상 격벽은 아닌가 — 기획상 따개비를 붙이지 않는다 "
				"(3) 표면에 충돌이 있는가 (4) MaximumSurfaceUpwardDot(%.2f)이 "
				"바닥·천장만 걸러내고 있는가 (5) SurfaceSearchDistanceCm(%.0f)이 충분한가"),
			*GetName(), ExpectedCount, SpawnedCount,
			MaximumSurfaceUpwardDot, SurfaceSearchDistanceCm);
	}
	else
	{
		// 성공도 남긴다. "안 돌았다"와 "돌았는데 안 보인다"를 구분하지 못하면
		// 메시 미지정 같은 다른 원인을 찾을 수 없다.
		UE_LOG(MarineDemolition, Log,
			TEXT("%s: 따개비 %d개를 붙였다."), *GetName(), SpawnedCount);
	}

	return SpawnedCount;
}

int32 AMD_BarnacleSpawner::GrowBarnacles(
	const int32 GrowthCount,
	const int32 GrowthSeedOffset)
{
	if (!HasAuthority() || !BarnacleClass || GrowthCount <= 0)
	{
		return 0;
	}

	// 상한을 넘지 않는 만큼만 돋운다. 없으면 3일 동안 개수가 계속 늘어
	// 05 배치 예산을 넘긴다.
	const int32 AliveCount = CountAliveBarnacles();
	const int32 AllowedCount = FMath::Min(
		GrowthCount,
		GetEffectiveMaximumAliveCount() - AliveCount);
	if (AllowedCount <= 0)
	{
		return 0;
	}

	// 시드를 어긋내야 초기 배치와 다른 자리에 돋는다. 같은 시드면 제거 목록에
	// 걸려 아무 데도 못 붙는다.
	// 증식분은 제거 목록을 보지 않는다. 새로 돋는 것이지 되살아나는 것이 아니다.
	return PlaceBarnacles(
		AllowedCount,
		PlacementSeed + GrowthSeedOffset,
		NextSpawnIndex,
		false);
}

void AMD_BarnacleSpawner::NotifyBarnacleRemoved(const int32 RemovedSpawnIndex)
{
	if (HasAuthority() && RemovedSpawnIndex != INDEX_NONE)
	{
		RemovedSpawnIndices.Add(RemovedSpawnIndex);
	}
}

void AMD_BarnacleSpawner::RestoreRemovedSpawnIndices(
	const TArray<int32>& InRemovedSpawnIndices)
{
	if (!HasAuthority())
	{
		return;
	}

	RemovedSpawnIndices.Append(InRemovedSpawnIndices);

	// 배치기가 이미 붙여 놓은 뒤에 불리므로, 목록에 든 것을 지운다.
	// BeginPlay 순서에 기대지 않으려고 "안 붙이기"가 아니라 "지우기"로 했다.
	for (const TWeakObjectPtr<AMD_Barnacle>& BarnaclePtr : SpawnedBarnacles)
	{
		AMD_Barnacle* const Barnacle = BarnaclePtr.Get();
		if (Barnacle && RemovedSpawnIndices.Contains(Barnacle->GetSpawnIndex()))
		{
			// 산출물을 떨구지 않고 조용히 지운다. 어제 이미 주웠기 때문이다.
			Barnacle->Destroy();
		}
	}
}

int32 AMD_BarnacleSpawner::CountAliveBarnacles() const
{
	int32 AliveCount = 0;
	for (const TWeakObjectPtr<AMD_Barnacle>& BarnaclePtr : SpawnedBarnacles)
	{
		if (BarnaclePtr.IsValid())
		{
			++AliveCount;
		}
	}
	return AliveCount;
}

int32 AMD_BarnacleSpawner::GetEffectiveMaximumAliveCount() const
{
	return MaximumAliveBarnacleCount > 0
		? MaximumAliveBarnacleCount
		: BarnacleCount;
}

int32 AMD_BarnacleSpawner::PlaceBarnacles(
	const int32 RequestedCount,
	const int32 PlacementSeedToUse,
	const int32 StartSpawnIndex,
	const bool bSkipRemoved)
{
	UWorld* const World = GetWorld();
	if (!World)
	{
		return 0;
	}

	// 같은 시드면 항상 같은 자리에 나온다. 서버만 스폰하므로 클라이언트와 어긋날 일은
	// 없지만, Day가 바뀌어 레벨을 다시 열어도 배치가 같아야 순번이 의미를 가진다.
	FRandomStream RandomStream(PlacementSeedToUse);

	TArray<FVector> PlacedLocations;
	PlacedLocations.Reserve(BarnacleCount);

	// 이미 붙인 따개비는 다음 트레이스에서 표면으로 잡히면 안 된다.
	TArray<AActor*> IgnoredActors;
	IgnoredActors.Add(this);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const int32 MinimumClusterSize = FMath::Max(1, ClusterSizeRange.X);
	const int32 MaximumClusterSize =
		FMath::Max(MinimumClusterSize, ClusterSizeRange.Y);

	// 자리를 소비한 횟수와 실제로 붙인 개수를 따로 센다.
	// 제거 목록에 걸려 건너뛰어도 자리는 소비해야 나머지 순번의 위치가 유지된다.
	// 여기가 어긋나면 어제 3번이 있던 자리에 오늘 4번이 서고, 제거 목록이 무의미해진다.
	int32 ProcessedCount = 0;
	int32 SpawnedCount = 0;
	int32 FailedClusterAttempts = 0;
	while (ProcessedCount < RequestedCount
		&& FailedClusterAttempts < MaximumAttemptsPerBarnacle)
	{
		// 군집 중심부터 잡는다. 볼륨 전체에서 찾는다.
		FHitResult ClusterAnchorHit;
		if (!TryFindSurfaceSpot(
				RandomStream,
				FVector::ZeroVector,
				0.0f,
				PlacedLocations,
				IgnoredActors,
				ClusterAnchorHit))
		{
			++FailedClusterAttempts;
			continue;
		}

		const int32 RemainingCount = RequestedCount - ProcessedCount;
		const int32 ClusterSize = FMath::Min(
			RemainingCount,
			RandomStream.RandRange(MinimumClusterSize, MaximumClusterSize));

		int32 ClusterProcessedCount = 0;
		for (int32 Member = 0; Member < ClusterSize; ++Member)
		{
			// 첫 개체는 중심에 그대로, 나머지는 중심 주변에서 다시 찾는다.
			FHitResult MemberHit = ClusterAnchorHit;
			if (Member > 0
				&& !TryFindSurfaceSpot(
					RandomStream,
					ClusterAnchorHit.ImpactPoint,
					ClusterRadiusCm,
					PlacedLocations,
					IgnoredActors,
					MemberHit))
			{
				// 군집이 다 안 차도 남은 개수는 다음 군집이 가져간다.
				break;
			}

			const int32 CurrentSpawnIndex = StartSpawnIndex + ProcessedCount;
			const FTransform SpawnTransform =
				MakeAttachedTransform(RandomStream, MemberHit);

			// 자리와 순번은 어떤 경우에도 소비한다.
			PlacedLocations.Add(MemberHit.ImpactPoint);
			++ProcessedCount;
			++ClusterProcessedCount;
			NextSpawnIndex = FMath::Max(NextSpawnIndex, CurrentSpawnIndex + 1);

			// 어제 떼어낸 것은 되살리지 않는다 (기획 03:101 강제 초기화 금지).
			if (bSkipRemoved && RemovedSpawnIndices.Contains(CurrentSpawnIndex))
			{
				continue;
			}

			AMD_Barnacle* const Barnacle = World->SpawnActor<AMD_Barnacle>(
				BarnacleClass,
				SpawnTransform,
				SpawnParams);
			if (!Barnacle)
			{
				continue;
			}

			Barnacle->AssignSpawnerIdentity(this, CurrentSpawnIndex);

			// 부착물이라는 관계를 씬 그래프에 남긴다. 벽이 사라지면 같이 사라진다.
			if (bAttachToSurface && MemberHit.GetComponent())
			{
				Barnacle->AttachToComponent(
					MemberHit.GetComponent(),
					FAttachmentTransformRules::KeepWorldTransform);
			}
			// SpawnActor는 플레이 중인 월드에서 BeginPlay까지 실행한 뒤 반환한다.
			// 따라서 BeginPlay 등록은 위 Attach보다 먼저일 수 있어 부착 완료 후 확정한다.
			Barnacle->RegisterWithScrapeSurface();

			IgnoredActors.Add(Barnacle);
			SpawnedBarnacles.Add(Barnacle);
			++SpawnedCount;
		}

		if (ClusterProcessedCount == 0)
		{
			++FailedClusterAttempts;
		}
	}

	return SpawnedCount;
}

bool AMD_BarnacleSpawner::TryFindSurfaceSpot(
	FRandomStream& RandomStream,
	const FVector& SearchCenter,
	const float SearchRadiusCm,
	const TArray<FVector>& PlacedLocations,
	const TArray<AActor*>& IgnoredActors,
	FHitResult& OutHit) const
{
	OutHit = FHitResult();

	const UWorld* const World = GetWorld();
	if (!World || !SpawnVolume)
	{
		return false;
	}

	const FVector VolumeCenter = SpawnVolume->GetComponentLocation();
	const FVector VolumeExtent = SpawnVolume->GetScaledBoxExtent();

	const bool bSearchAroundCenter = SearchRadiusCm > 0.0f;
	// 군집 안에서는 중심 근처만 훑으므로 멀리 쏠 이유가 없다.
	const float SafeSearchDistanceCm = bSearchAroundCenter
		? FMath::Max(10.0f, SearchRadiusCm * 2.0f)
		: FMath::Max(10.0f, SurfaceSearchDistanceCm);
	const float SafeSeparationCm = FMath::Max(0.0f, MinimumSeparationCm);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MD_BarnacleSpawn), true);
	QueryParams.AddIgnoredActors(IgnoredActors);

	for (int32 Attempt = 0; Attempt < MaximumAttemptsPerBarnacle; ++Attempt)
	{
		const FVector SearchOrigin = bSearchAroundCenter
			? SearchCenter
				+ RandomStream.GetUnitVector()
					* RandomStream.FRandRange(0.0f, SearchRadiusCm)
			: VolumeCenter
				+ FVector(
					RandomStream.FRandRange(-VolumeExtent.X, VolumeExtent.X),
					RandomStream.FRandRange(-VolumeExtent.Y, VolumeExtent.Y),
					RandomStream.FRandRange(-VolumeExtent.Z, VolumeExtent.Z));

		// 방향을 매번 새로 섞는다. 한 방향만 쓰면 벽 한 면에만 몰린다.
		const int32 DirectionCount = UE_ARRAY_COUNT(SurfaceSearchDirections);
		const int32 FirstDirection = RandomStream.RandRange(0, DirectionCount - 1);

		for (int32 Offset = 0; Offset < DirectionCount; ++Offset)
		{
			const FVector SearchDirection =
				SurfaceSearchDirections[(FirstDirection + Offset) % DirectionCount];

			FHitResult Hit;
			if (!World->LineTraceSingleByChannel(
					Hit,
					SearchOrigin,
					SearchOrigin + SearchDirection * SafeSearchDistanceCm,
					ECC_Visibility,
					QueryParams))
			{
				continue;
			}

			if (!IsAcceptableSurface(Hit))
			{
				continue;
			}

			bool bTooClose = false;
			for (const FVector& PlacedLocation : PlacedLocations)
			{
				if (FVector::DistSquared(PlacedLocation, Hit.ImpactPoint)
					< FMath::Square(SafeSeparationCm))
				{
					bTooClose = true;
					break;
				}
			}
			if (bTooClose)
			{
				continue;
			}

			OutHit = Hit;
			return true;
		}
	}

	return false;
}

FTransform AMD_BarnacleSpawner::MakeAttachedTransform(
	FRandomStream& RandomStream,
	const FHitResult& Hit) const
{
	const FVector SurfaceNormal = Hit.ImpactNormal.GetSafeNormal();

	// +Z가 면 바깥을 향하게 세운다. AMD_Barnacle::BreakOff가 산출물을 UpVector
	// 방향으로 밀어내므로 이 방향이 곧 "벽에서 떨어져 나가는" 방향이다.
	FRotator SpawnRotation =
		FRotationMatrix::MakeFromZ(SurfaceNormal).Rotator();

	if (bRandomizeYawAroundNormal)
	{
		// 법선 축을 중심으로 돌린다. 세워 둔 축은 그대로 두고 방향만 흩는다.
		const FQuat SpinAroundNormal(
			SurfaceNormal,
			FMath::DegreesToRadians(RandomStream.FRandRange(0.0f, 360.0f)));
		SpawnRotation = (SpinAroundNormal * SpawnRotation.Quaternion()).Rotator();
	}

	// 밑동을 벽에 묻는다. 히트 지점에 그대로 두면 피벗 위치에 따라 떠 보인다.
	const float SafeEmbedCm = FMath::IsFinite(SurfaceEmbedCm)
		? FMath::Max(0.0f, SurfaceEmbedCm)
		: 0.0f;
	const FVector SpawnLocation =
		Hit.ImpactPoint - SurfaceNormal * SafeEmbedCm;

	const float MinimumScale = FMath::Max(0.05f, static_cast<float>(ScaleRange.X));
	const float MaximumScale = FMath::Max(MinimumScale, static_cast<float>(ScaleRange.Y));
	const float SpawnScale = RandomStream.FRandRange(MinimumScale, MaximumScale);

	return FTransform(
		SpawnRotation,
		SpawnLocation,
		FVector(SpawnScale));
}

bool AMD_BarnacleSpawner::IsAcceptableSurface(const FHitResult& Hit) const
{
	// 기획 02 §1: 따개비는 보통벽에만 붙는다. 내력·칸막이 세그먼트에는 두지 않는다.
	if (!bAllowCuttableWallSurfaces && Cast<AMD_CuttableWall>(Hit.GetActor()))
	{
		return false;
	}

	// 기획 02 §1: 따개비는 "보통벽에 붙은 부착물"이다. 바닥·천장은 각종 쓰레기 몫이다.
	const float UpwardDot = FMath::Abs(
		static_cast<float>(Hit.ImpactNormal.Z));
	if (UpwardDot > FMath::Clamp(MaximumSurfaceUpwardDot, 0.0f, 1.0f))
	{
		return false;
	}

	return true;
}
