#include "World/Contamination/MD_BarnacleSpawner.h"

#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Math/RandomStream.h"
#include "Math/RotationMatrix.h"
#include "Misc/Guid.h"
#include "Util/MD_LogUtility.h"
#include "World/Contamination/MD_Barnacle.h"
#include "World/Contamination/MD_BarnaclePlacementPolicy.h"
#include "World/Wall/MD_CuttableWall.h"

namespace
{
	/** 후보가 잘 나오지 않는 빈 볼륨에서도 탐색이 끝나도록 원점 샘플 수를 제한한다. */
	constexpr int32 CandidateOriginAttemptMultiplier = 8;
	constexpr int32 MinimumCandidatePoolSize = 64;
	constexpr int32 MaximumCandidatePoolSize = 2048;
	constexpr int32 MaximumCandidateOriginAttempts = 4096;
	constexpr int32 MaximumStalledCandidateOriginAttempts = 256;
	constexpr float CandidateDuplicateToleranceCm = 2.0f;
	constexpr float SpawnVolumeSurfaceToleranceCm = 1.0f;
	constexpr int32 MaximumSupportedBarnacleDurability = 1000000;
	constexpr uint32 BarnacleDurabilitySeedSalt = 0x9E3779B9u;

	/** 시드와 SpawnIndex의 비트가 고르게 섞이도록 고정된 32비트 믹서를 적용한다. */
	uint32 MixDurabilitySeed(uint32 Value)
	{
		Value ^= Value >> 16;
		Value *= 0x7FEB352Du;
		Value ^= Value >> 15;
		Value *= 0x846CA68Bu;
		Value ^= Value >> 16;
		return Value;
	}

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

	if (!HasAuthority())
	{
		return;
	}

	// Blueprint가 초기 스폰을 나중에 호출하더라도 첫 자동 저장에는 새 게임 시드가
	// 반드시 들어가야 하므로 서버 BeginPlay에서 먼저 확정한다.
	InitializeRuntimePlacementSeed();

	if (bSpawnOnBeginPlay)
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

	// 초기 배치는 한 번만 구성한다. Blueprint에서 실수로 다시 호출해 기존 액터를
	// 추적 목록 밖에 남기거나 Trace 후보를 오염시키지 않게 한다.
	if (bHasSpawnedInitialPlacement)
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

	if (!bAllowFrontBackSurfaces
		&& !bAllowLeftRightSurfaces
		&& !bAllowTopBottomSurfaces)
	{
		UE_LOG(MarineDemolition, Log,
			TEXT("%s: 앞/뒤, 좌/우 및 위/아래 표면이 모두 비활성화되어 "
				"따개비를 붙이지 않았다."),
			*GetName());
		return 0;
	}

	bHasSpawnedInitialPlacement = true;

	// 초기 배치는 항상 0번부터 시작한다. Day가 바뀌어도 같은 시드·같은 순번이라
	// 제거 목록으로 걸러 낼 수 있다.
	NextSpawnIndex = 0;
	SpawnedBarnacles.Reset();

	const int32 SpawnedCount = PlaceBarnacles(
		BarnacleCount,
		GetEffectivePlacementSeed(),
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
				"(3) 대상 컴포넌트가 BarnacleTrace를 Block하는가 "
				"(4) SpawnVolume 로컬 X/Y/Z 축과 허용 플래그가 "
				"대상 면에 맞는가 (5) SurfaceSearchDistanceCm(%.0f)이 충분한가"),
			*GetName(), ExpectedCount, SpawnedCount,
			SurfaceSearchDistanceCm);
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
	if (!HasAuthority()
		|| !BarnacleClass
		|| !bHasSpawnedInitialPlacement
		|| GrowthCount <= 0
		|| (!bAllowFrontBackSurfaces
			&& !bAllowLeftRightSurfaces
			&& !bAllowTopBottomSurfaces))
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
	const uint32 GrowthPlacementSeed =
		static_cast<uint32>(GetEffectivePlacementSeed())
		+ static_cast<uint32>(GrowthSeedOffset);
	return PlaceBarnacles(
		AllowedCount,
		static_cast<int32>(GrowthPlacementSeed),
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

bool AMD_BarnacleSpawner::TryGetRuntimePlacementSeed(
	int32& OutRuntimePlacementSeed) const
{
	if (PlacementSeedMode != EMD_BarnaclePlacementSeedMode::RandomPerNewGame
		|| !bHasRuntimePlacementSeed)
	{
		return false;
	}

	OutRuntimePlacementSeed = RuntimePlacementSeed;
	return true;
}

void AMD_BarnacleSpawner::RestorePersistentState(
	const TArray<int32>& InRemovedSpawnIndices,
	const bool bHasSavedRuntimePlacementSeed,
	const int32 SavedRuntimePlacementSeed)
{
	if (!HasAuthority())
	{
		return;
	}

	if (PlacementSeedMode == EMD_BarnaclePlacementSeedMode::RandomPerNewGame)
	{
		// 구버전 기록에는 런타임 시드가 없다. 당시 배치는 PlacementSeed 고정값을
		// 사용했으므로 그 값을 최초 런타임 시드로 승격해야 제거 순번이 어긋나지 않는다.
		RuntimePlacementSeed = bHasSavedRuntimePlacementSeed
			? SavedRuntimePlacementSeed
			: PlacementSeed;
		bHasRuntimePlacementSeed = true;
	}
	else
	{
		// Fixed는 세이브 값보다 현재 레벨의 에디터 설정을 정본으로 사용한다.
		RuntimePlacementSeed = 0;
		bHasRuntimePlacementSeed = false;
	}

	RemovedSpawnIndices.Reset();
	for (const int32 RemovedSpawnIndex : InRemovedSpawnIndices)
	{
		if (RemovedSpawnIndex != INDEX_NONE)
		{
			RemovedSpawnIndices.Add(RemovedSpawnIndex);
		}
	}

	// bSpawnOnBeginPlay=false인 Blueprint가 아직 초기 배치를 요청하지 않았다면
	// 저장 상태만 주입하고, 나중의 첫 SpawnBarnacles 호출이 그 시드를 사용하게 한다.
	if (!bHasSpawnedInitialPlacement)
	{
		return;
	}

	DestroySpawnedBarnaclesForRebuild();
	bHasSpawnedInitialPlacement = false;
	SpawnBarnacles();
}

void AMD_BarnacleSpawner::InitializeRuntimePlacementSeed()
{
	if (!HasAuthority()
		|| PlacementSeedMode != EMD_BarnaclePlacementSeedMode::RandomPerNewGame
		|| bHasRuntimePlacementSeed)
	{
		return;
	}

	// 게임 전역 RNG를 소비하면 다른 절차 생성 결과가 호출 순서에 따라 달라진다.
	// GUID 해시는 이 스포너의 새 게임 시드만 독립적으로 만든다.
	RuntimePlacementSeed = static_cast<int32>(GetTypeHash(FGuid::NewGuid()));
	bHasRuntimePlacementSeed = true;
}

int32 AMD_BarnacleSpawner::GetEffectivePlacementSeed()
{
	if (PlacementSeedMode == EMD_BarnaclePlacementSeedMode::Fixed)
	{
		return PlacementSeed;
	}

	InitializeRuntimePlacementSeed();
	return bHasRuntimePlacementSeed ? RuntimePlacementSeed : PlacementSeed;
}

void AMD_BarnacleSpawner::DestroySpawnedBarnaclesForRebuild()
{
	for (const TWeakObjectPtr<AMD_Barnacle>& BarnaclePtr : SpawnedBarnacles)
	{
		if (AMD_Barnacle* const Barnacle = BarnaclePtr.Get(); IsValid(Barnacle))
		{
			// Destroy가 프레임 끝에 마무리되더라도 이어지는 재배치 Trace를 막지 않게 한다.
			Barnacle->SetActorEnableCollision(false);
			Barnacle->Destroy();
		}
	}

	SpawnedBarnacles.Reset();
	NextSpawnIndex = 0;
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
	if (!World || RequestedCount <= 0)
	{
		return 0;
	}

	// 같은 시드면 항상 같은 자리에 나온다. 서버만 스폰하므로 클라이언트와 어긋날 일은
	// 없지만, Day가 바뀌어 레벨을 다시 열어도 배치가 같아야 순번이 의미를 가진다.
	FRandomStream RandomStream(PlacementSeedToUse);

	TArray<AActor*> IgnoredActors;
	IgnoredActors.Add(this);

	// 밤샘 증식도 기존 개체에서 멀리 떨어져야 한다. 위치와 면 법선을 정책 입력으로
	// 넘기고, 트레이스에서는 따개비 자체를 표면으로 오인하지 않도록 무시한다.
	TArray<FMD_BarnaclePlacementCandidate> ExistingPlacements;
	for (const TWeakObjectPtr<AMD_Barnacle>& BarnaclePtr : SpawnedBarnacles)
	{
		AMD_Barnacle* const Barnacle = BarnaclePtr.Get();
		if (!IsValid(Barnacle))
		{
			continue;
		}

		IgnoredActors.Add(Barnacle);
		const EMD_BarnaclePlacementFace ExistingFace =
			MDBarnaclePlacementPolicy::ClassifySurfaceFace(
				Barnacle->GetActorUpVector(),
				SpawnVolume->GetForwardVector(),
				SpawnVolume->GetRightVector(),
				SpawnVolume->GetUpVector(),
				bAllowFrontBackSurfaces,
				bAllowLeftRightSurfaces,
				bAllowTopBottomSurfaces);
		if (ExistingFace == EMD_BarnaclePlacementFace::Other)
		{
			continue;
		}

		FMD_BarnaclePlacementCandidate& ExistingPlacement =
			ExistingPlacements.Emplace_GetRef();
		ExistingPlacement.Location = Barnacle->GetActorLocation();
		ExistingPlacement.SurfaceNormal = Barnacle->GetActorUpVector();
		ExistingPlacement.Face = ExistingFace;
	}

	const int32 SafeCandidatesPerBarnacle = FMath::Clamp(
		PlacementCandidatesPerBarnacle,
		4,
		64);
	const int64 DesiredCandidateCount64 = FMath::Clamp<int64>(
		static_cast<int64>(RequestedCount) * SafeCandidatesPerBarnacle,
		MinimumCandidatePoolSize,
		MaximumCandidatePoolSize);
	const int32 DesiredCandidateCount = static_cast<int32>(DesiredCandidateCount64);

	TArray<FHitResult> SurfaceCandidates;
	GatherSurfaceCandidates(
		RandomStream,
		DesiredCandidateCount,
		IgnoredActors,
		SurfaceCandidates);
	if (SurfaceCandidates.IsEmpty())
	{
		return 0;
	}

	TArray<FMD_BarnaclePlacementCandidate> PlacementCandidates;
	PlacementCandidates.Reserve(SurfaceCandidates.Num());
	for (int32 HitIndex = 0; HitIndex < SurfaceCandidates.Num(); ++HitIndex)
	{
		const EMD_BarnaclePlacementFace CandidateFace =
			MDBarnaclePlacementPolicy::ClassifySurfaceFace(
				SurfaceCandidates[HitIndex].ImpactNormal,
				SpawnVolume->GetForwardVector(),
				SpawnVolume->GetRightVector(),
				SpawnVolume->GetUpVector(),
				bAllowFrontBackSurfaces,
				bAllowLeftRightSurfaces,
				bAllowTopBottomSurfaces);
		if (CandidateFace == EMD_BarnaclePlacementFace::Other)
		{
			continue;
		}

		FMD_BarnaclePlacementCandidate& Candidate =
			PlacementCandidates.Emplace_GetRef();
		Candidate.Location = SurfaceCandidates[HitIndex].ImpactPoint;
		Candidate.SurfaceNormal = SurfaceCandidates[HitIndex].ImpactNormal;
		Candidate.SourceIndex = HitIndex;
		Candidate.Face = CandidateFace;
	}

	const FMD_BarnaclePlacementPlan PlacementPlan =
		MDBarnaclePlacementPolicy::BuildPlan(
			PlacementCandidates,
			ExistingPlacements,
			RequestedCount,
			MinimumSeparationCm,
			RandomStream);

	if (PlacementPlan.bUsedFrontBackBalance)
	{
		UE_LOG(MarineDemolition, Log,
			TEXT("%s: 앞/뒤 따개비 배치를 %d:%d로 계획했다."),
			*GetName(),
			PlacementPlan.FrontCount,
			PlacementPlan.BackCount);
	}
	if (PlacementPlan.bUsedTopBottomBalance)
	{
		UE_LOG(MarineDemolition, Log,
			TEXT("%s: 위/아래 따개비 배치를 %d:%d로 계획했다."),
			*GetName(),
			PlacementPlan.TopCount,
			PlacementPlan.BottomCount);
	}
	if (PlacementPlan.bUsedLeftRightBalance)
	{
		UE_LOG(MarineDemolition, Log,
			TEXT("%s: 좌/우 따개비 배치를 %d:%d로 계획했다."),
			*GetName(),
			PlacementPlan.LeftCount,
			PlacementPlan.RightCount);
	}

	// 자리를 소비한 횟수와 실제로 붙인 개수를 따로 센다.
	// 제거 목록에 걸려 건너뛰어도 자리는 소비해야 나머지 순번의 위치가 유지된다.
	// 여기가 어긋나면 어제 3번이 있던 자리에 오늘 4번이 서고, 제거 목록이 무의미해진다.
	int32 ProcessedCount = 0;
	int32 SpawnedCount = 0;
	for (const int32 HitIndex : PlacementPlan.OrderedCandidateIndices)
	{
		if (!SurfaceCandidates.IsValidIndex(HitIndex))
		{
			continue;
		}

		const FHitResult& SurfaceHit = SurfaceCandidates[HitIndex];
		const int32 CurrentSpawnIndex = StartSpawnIndex + ProcessedCount;
		const FTransform SpawnTransform =
			MakeAttachedTransform(RandomStream, SurfaceHit);

		// 자리와 순번은 제거 여부와 관계없이 소비한다. 그래야 Day 저장의 SpawnIndex가
		// 같은 시드에서 항상 같은 위치를 가리킨다.
		++ProcessedCount;
		NextSpawnIndex = FMath::Max(NextSpawnIndex, CurrentSpawnIndex + 1);

		if (bSkipRemoved && RemovedSpawnIndices.Contains(CurrentSpawnIndex))
		{
			continue;
		}

		const int32 MaximumDurability = GenerateDurabilityForSpawn(
			PlacementSeedToUse,
			CurrentSpawnIndex);

		// 생성 완료 전에 신원과 내구도를 넣어 BeginPlay와 최초 복제가 기본값을
		// 잠깐이라도 관찰하지 않게 한다.
		AMD_Barnacle* const Barnacle = World->SpawnActorDeferred<AMD_Barnacle>(
			BarnacleClass,
			SpawnTransform,
			this,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Barnacle)
		{
			continue;
		}

		if (!Barnacle->InitializeFromSpawner(
				this,
				CurrentSpawnIndex,
				MaximumDurability))
		{
			Barnacle->Destroy();
			continue;
		}
		Barnacle->FinishSpawning(SpawnTransform);
		if (!IsValid(Barnacle))
		{
			continue;
		}

		if (bAttachToSurface && SurfaceHit.GetComponent())
		{
			Barnacle->AttachToComponent(
				SurfaceHit.GetComponent(),
				FAttachmentTransformRules::KeepWorldTransform);
		}

		SpawnedBarnacles.Add(Barnacle);
		++SpawnedCount;
	}

	return SpawnedCount;
}

FIntPoint AMD_BarnacleSpawner::GetSanitizedDurabilityRange() const
{
	const int32 ClampedMinimum = FMath::Clamp(
		MinimumBarnacleDurability,
		1,
		MaximumSupportedBarnacleDurability);
	const int32 ClampedMaximum = FMath::Clamp(
		MaximumBarnacleDurability,
		1,
		MaximumSupportedBarnacleDurability);

	return FIntPoint(
		FMath::Min(ClampedMinimum, ClampedMaximum),
		FMath::Max(ClampedMinimum, ClampedMaximum));
}

int32 AMD_BarnacleSpawner::GenerateDurabilityForSpawn(
	const int32 PlacementSeedToUse,
	const int32 SpawnIndex) const
{
	const FIntPoint DurabilityRange = GetSanitizedDurabilityRange();
	const uint32 CombinedSeed =
		static_cast<uint32>(PlacementSeedToUse)
		^ (static_cast<uint32>(SpawnIndex) + BarnacleDurabilitySeedSalt);
	FRandomStream DurabilityRandomStream(
		static_cast<int32>(MixDurabilitySeed(CombinedSeed)));

	return DurabilityRandomStream.RandRange(
		DurabilityRange.X,
		DurabilityRange.Y);
}

void AMD_BarnacleSpawner::GatherSurfaceCandidates(
	FRandomStream& RandomStream,
	const int32 DesiredCandidateCount,
	const TArray<AActor*>& IgnoredActors,
	TArray<FHitResult>& OutHits) const
{
	OutHits.Reset();

	const UWorld* const World = GetWorld();
	if (!World || !SpawnVolume || DesiredCandidateCount <= 0)
	{
		return;
	}

	const FTransform VolumeTransform = SpawnVolume->GetComponentTransform();
	const FVector VolumeExtent = SpawnVolume->GetUnscaledBoxExtent();
	const float SafeSearchDistanceCm = FMath::IsFinite(SurfaceSearchDistanceCm)
		? FMath::Max(10.0f, SurfaceSearchDistanceCm)
		: 10.0f;
	const int32 MaximumOriginAttempts = FMath::Min(
		MaximumCandidateOriginAttempts,
		FMath::Max(
			DesiredCandidateCount,
			DesiredCandidateCount * CandidateOriginAttemptMultiplier));
	const double DuplicateToleranceSquared =
		FMath::Square(static_cast<double>(CandidateDuplicateToleranceCm));

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MD_BarnacleSpawn), true);
	QueryParams.AddIgnoredActors(IgnoredActors);

	int32 StalledOriginAttempts = 0;
	for (int32 OriginAttempt = 0;
		OriginAttempt < MaximumOriginAttempts && OutHits.Num() < DesiredCandidateCount;
		++OriginAttempt)
	{
		const int32 CandidateCountBeforeAttempt = OutHits.Num();
		const FVector LocalSearchOrigin(
			RandomStream.FRandRange(-VolumeExtent.X, VolumeExtent.X),
			RandomStream.FRandRange(-VolumeExtent.Y, VolumeExtent.Y),
			RandomStream.FRandRange(-VolumeExtent.Z, VolumeExtent.Z));
		const FVector SearchOrigin = VolumeTransform.TransformPosition(LocalSearchOrigin);

		// 시작 인덱스만 돌리면 +축이 -축보다 먼저 검사되는 5:1 편향이 생긴다.
		// 여섯 방향 전체를 Fisher-Yates로 섞고, 각 방향의 유효 히트를 모두 후보로 모은다.
		const int32 DirectionCount = UE_ARRAY_COUNT(SurfaceSearchDirections);
		int32 DirectionOrder[UE_ARRAY_COUNT(SurfaceSearchDirections)];
		for (int32 DirectionIndex = 0; DirectionIndex < DirectionCount; ++DirectionIndex)
		{
			DirectionOrder[DirectionIndex] = DirectionIndex;
		}
		for (int32 DirectionIndex = DirectionCount - 1; DirectionIndex > 0; --DirectionIndex)
		{
			Swap(
				DirectionOrder[DirectionIndex],
				DirectionOrder[RandomStream.RandRange(0, DirectionIndex)]);
		}

		for (const int32 DirectionIndex : DirectionOrder)
		{
			const FVector& LocalSearchDirection =
				SurfaceSearchDirections[DirectionIndex];
			const bool bIsFrontBackDirection =
				FMath::Abs(LocalSearchDirection.X) > 0.5;
			const bool bIsLeftRightDirection =
				FMath::Abs(LocalSearchDirection.Y) > 0.5;
			const bool bIsTopBottomDirection =
				FMath::Abs(LocalSearchDirection.Z) > 0.5;
			if ((!bAllowFrontBackSurfaces && bIsFrontBackDirection)
				|| (!bAllowLeftRightSurfaces && bIsLeftRightDirection)
				|| (!bAllowTopBottomSurfaces && bIsTopBottomDirection)
				|| (!bIsFrontBackDirection
					&& !bIsLeftRightDirection
					&& !bIsTopBottomDirection))
			{
				continue;
			}

			const FVector SearchDirection =
				VolumeTransform.TransformVectorNoScale(
					LocalSearchDirection).GetSafeNormal();

			FHitResult Hit;
			// BarnacleTrace는 기본 Block이다. 벽·대형 장식물은 별도 설정 없이 찾고,
			// 전구 같은 제외 소품은 Ignore로 설정해 후보와 가림에서 함께 뺀다.
			if (!World->LineTraceSingleByChannel(
					Hit,
					SearchOrigin,
					SearchOrigin + SearchDirection * SafeSearchDistanceCm,
					MDCollisionChannels::BarnacleTrace,
					QueryParams))
			{
				continue;
			}

			if (!IsPointInsideSpawnVolume(Hit.ImpactPoint)
				|| !IsAcceptableSurface(Hit))
			{
				continue;
			}

			bool bDuplicateCandidate = false;
			for (const FHitResult& ExistingHit : OutHits)
			{
				if (ExistingHit.GetComponent() == Hit.GetComponent()
					&& FVector::DistSquared(ExistingHit.ImpactPoint, Hit.ImpactPoint)
						< DuplicateToleranceSquared)
				{
					bDuplicateCandidate = true;
					break;
				}
			}
			if (bDuplicateCandidate)
			{
				continue;
			}

			OutHits.Add(Hit);
			if (OutHits.Num() >= DesiredCandidateCount)
			{
				break;
			}
		}

		if (OutHits.Num() == CandidateCountBeforeAttempt)
		{
			++StalledOriginAttempts;
			if (StalledOriginAttempts >= MaximumStalledCandidateOriginAttempts)
			{
				break;
			}
		}
		else
		{
			StalledOriginAttempts = 0;
		}
	}
}

bool AMD_BarnacleSpawner::IsPointInsideSpawnVolume(
	const FVector& WorldLocation) const
{
	if (!SpawnVolume)
	{
		return false;
	}

	const FTransform VolumeTransform = SpawnVolume->GetComponentTransform();
	const FVector LocalLocation = VolumeTransform.InverseTransformPosition(WorldLocation);
	const FVector VolumeExtent = SpawnVolume->GetUnscaledBoxExtent();
	const FVector AbsoluteScale = VolumeTransform.GetScale3D().GetAbs();
	const FVector LocalTolerance(
		SpawnVolumeSurfaceToleranceCm / FMath::Max(AbsoluteScale.X, UE_SMALL_NUMBER),
		SpawnVolumeSurfaceToleranceCm / FMath::Max(AbsoluteScale.Y, UE_SMALL_NUMBER),
		SpawnVolumeSurfaceToleranceCm / FMath::Max(AbsoluteScale.Z, UE_SMALL_NUMBER));
	const FVector AllowedExtent = VolumeExtent + LocalTolerance;

	return FMath::Abs(LocalLocation.X) <= AllowedExtent.X
		&& FMath::Abs(LocalLocation.Y) <= AllowedExtent.Y
		&& FMath::Abs(LocalLocation.Z) <= AllowedExtent.Z;
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
	// 벽과 가구는 모두 허용하지만 기존 기획 제약상 철거 대상 격벽은 기본 제외한다.
	if (!bAllowCuttableWallSurfaces && Cast<AMD_CuttableWall>(Hit.GetActor()))
	{
		return false;
	}

	if (!SpawnVolume)
	{
		return false;
	}

	return MDBarnaclePlacementPolicy::ClassifySurfaceFace(
		Hit.ImpactNormal,
		SpawnVolume->GetForwardVector(),
		SpawnVolume->GetRightVector(),
		SpawnVolume->GetUpVector(),
		bAllowFrontBackSurfaces,
		bAllowLeftRightSurfaces,
		bAllowTopBottomSurfaces)
		!= EMD_BarnaclePlacementFace::Other;
}
