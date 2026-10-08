#include "World/Trash/MD_TrashSpawnManager.h"

#include "Engine/World.h"
#include "Data/Merchant/MD_SiteDefinition.h"
#include "Data/Trash/MD_TrashSpawnProfile.h"
#include "Game/MD_GameModeBase.h"
#include "Game/MD_GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "Gameplay/Trash/MD_TrashLibrary.h"
#include "Gameplay/Trash/MD_TrashSettings.h"
#include "Misc/ConfigCacheIni.h"
#include "Engine/StaticMesh.h"
#include "HAL/PlatformTime.h"
#include "TimerManager.h"
#include "Util/MD_LogUtility.h"
#include "World/Trash/MD_TrashSpawnVolume.h"
#include "World/Trash/MD_TrashFloorGenerator.h"
#include "World/Trash/MD_TrashFloorPlacementComponent.h"
#include "World/Trash/MD_TrashItem.h"
#include "EngineUtils.h"

namespace
{
	// 구역 인스턴스와 액터 이름을 하나의 키로 구성 (옛 월드 퍼시스턴스 키와 동일 형식)
	FName MakeAuthoredTrashKey(AActor* Actor)
	{
		return FName(UWorld::RemovePIEPrefix(Actor->GetLevel()->GetOutermost()->GetName()) + TEXT(":") + Actor->GetName());
	}
}

void UMD_TrashSpawnManager::AppendOccupiedTrash(const AMD_TrashSpawnVolume* Requester,
    TArray<FVector>& Positions, TArray<float>& Radii, float& MinimumSpacing) const
{
    for (const auto& Entry : Generators)
    {
        const auto* Generator = Entry.Get();
        if (!Generator || Generator == Requester) continue;
        for (const TObjectPtr<AMD_TrashItem>& Item : Generator->SpawnedTrashItems)
        {
            if (!IsValid(Item)) continue;
            const FBox Bounds = Item->GetComponentsBoundingBox(true);
            const FVector Position = Item->GetActorLocation();
            Positions.Add(Position);
            Radii.Add(static_cast<float>(Bounds.GetExtent().Size() + FVector::Distance(Bounds.GetCenter(), Position)));
            MinimumSpacing = FMath::Max(MinimumSpacing, Generator->MinimumSpacing);
        }
    }
}

void UMD_TrashSpawnManager::NotifyGeneratorStateChanged()
{
    if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client || bCountBroadcastPending)
    {
        return;
    }

    bCountBroadcastPending = true;
    GetWorld()->GetTimerManager().SetTimerForNextTick(this, &ThisClass::FlushCountChanged);
}

void UMD_TrashSpawnManager::FlushCountChanged()
{
    bCountBroadcastPending = false;
    const int32 CurrentCount = GetRemainingTrashCount();
    if (CurrentCount != CachedRemainingCount)
    {
        CachedRemainingCount = CurrentCount;
        OnTotalTrashCountChanged.Broadcast(CurrentCount);
    }
}

void UMD_TrashSpawnManager::RegisterGenerator(AMD_TrashSpawnVolume* Generator)
{
	if (!IsValid(Generator) || !GetWorld() || GetWorld()->GetNetMode() == NM_Client)
	{
		return;
	}

	Generators.RemoveAll([](const TWeakObjectPtr<AMD_TrashSpawnVolume>& Entry)
	{
		return !Entry.IsValid();
	});
	Generators.AddUnique(Generator);
	NotifyGeneratorStateChanged();

	// A dynamically added generator starts once the map's restore phase has ended.
	if (bWorldInitialized)
	{
		RequestInitialization();
	}
}

bool UMD_TrashSpawnManager::PrepareGenerators(bool& bReady)
{
	bReady = false;
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client) return false;
	const FName Package(*UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName()));
	if (GetWorld()->IsGameWorld() && AutoFloorMaps.Contains(Package) && !EnableAuthoredFloorTransition()) return false;
	const double Deadline = FPlatformTime::Seconds() + 0.002;
    // Prepare the shared map cache before subsets, regardless of BeginPlay order.
    for (int32 Pass = 0; Pass < 2; ++Pass)
	for (const auto& Entry : Generators)
	{
		if (AMD_TrashSpawnVolume* Generator = Entry.Get())
		{
			bool bGeneratorReady = false;
            if (Generator->IsFloorBonusVolume() != (Pass == 1)) continue;
			if (!Generator->PrepareSpawnLocations(FMath::Max(0.00001, Deadline - FPlatformTime::Seconds()), bGeneratorReady)) return false;
			if (!bGeneratorReady) return true;
		}
	}
	bReady = true;
	return true;
}

void UMD_TrashSpawnManager::RequestInitialization()
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client || bInitializationScheduled) return;
	bInitializationScheduled = true;
	GetWorld()->GetTimerManager().SetTimerForNextTick(this, &ThisClass::InitializeWhenReady);
}

void UMD_TrashSpawnManager::InitializeWhenReady()
{
	bInitializationScheduled = false;
	bool bReady = false;
	if (!PrepareGenerators(bReady))
	{
		UE_LOG(MarineDemolition, Error, TEXT("Automatic trash floor preparation failed."));
		return;
	}
	if (!bReady) { RequestInitialization(); return; }
	if (!InitializeUnrestoredGenerators())
		UE_LOG(MarineDemolition, Error, TEXT("Automatic trash initialization failed."));
}

void UMD_TrashSpawnManager::UnregisterGenerator(AMD_TrashSpawnVolume* Generator)
{
	Generators.RemoveAll([Generator](const TWeakObjectPtr<AMD_TrashSpawnVolume>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Generator;
	});
	NotifyGeneratorStateChanged();
}

bool UMD_TrashSpawnManager::InitializeUnrestoredGenerators()
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client)
	{
		return false;
	}

	bool bSuccessful = true;
	if (TransitionGenerator.IsValid() && !TransitionGenerator->IsStateInitialized())
	{
		if (!ConvertAuthoredTrash()) return false;
	}
	// 첫 배치 직전에 인원·난이도 총량을 비율 볼륨들에 나눈다. 바닥 셀이 준비된 뒤라 상한을 알 수 있다
	DistributeSpawnShares();
    TArray<AMD_TrashSpawnVolume*> Order = GetGenerators();
    Order.StableSort([](const AMD_TrashSpawnVolume& A, const AMD_TrashSpawnVolume& B)
    { return A.IsFloorBonusVolume() && !B.IsFloorBonusVolume(); });
    TArray<AMD_TrashSpawnVolume*> Initialized;
    // 앞선 비율 볼륨이 공간 부족으로 못 놓은 개수. 뒤의 비율 볼륨이 상한 안에서 이어받는다
    int32 Carry = 0;
    for (AMD_TrashSpawnVolume* Generator : Order)
    {
        if (Generator->IsStateInitialized()) continue;
        const bool bShareVolume = FMath::IsFinite(Generator->SpawnShare) && Generator->SpawnShare > 0.0f && Generator->bSpawnOnBeginPlay;
        if (bShareVolume && Carry > 0)
        {
            const UMD_TrashFloorPlacementComponent* Floor = Generator->GetFloorPlacement();
            const int32 Capacity = Generator->UsesAutomaticFloorCells() && Floor && Floor->IsReady() && !Generator->UsesTowerPlacement()
                ? Floor->GetCandidateCount() : MAX_int32;
            const int32 Extra = FMath::Clamp(Capacity - Generator->SpawnCount, 0, Carry);
            Generator->SpawnCount += Extra;
            Carry -= Extra;
        }
        if (!Generator->InitializeFresh())
        {
            for (auto* Created : Initialized) Created->ClearSpawnedTrash();
            bSuccessful = false;
            break;
        }
        if (bShareVolume) Carry += Generator->GetLastSpawnShortfall();
        Initialized.Add(Generator);
    }
    if (bSuccessful && DistributedTrashTotal > 0)
    {
        UE_LOG(MarineDemolition, Display, TEXT("Trash share result: placed %d of %d (unplaced %d)."),
            DistributedTrashTotal - Carry, DistributedTrashTotal, Carry);
    }
	bWorldInitialized = bSuccessful;
	NotifyGeneratorStateChanged();
	return bSuccessful;
}

namespace
{
	// 미리보기용 점유 지름². 배치 코드와 같은 반경 산식(가로·세로 extent, 높이 2배)에 최소 간격을 적용하고
	// 종류 안의 모든 Variant를 균등 평균한다. 실제 추첨이 Variant를 균등 선택하기 때문이다
	bool ComputeKindMeanDiameterSquared(const FName KindId, const float MinimumSpacing, double& OutMeanDiameterSquared)
	{
		FMD_TrashKindRow Row;
		if (!UMD_TrashLibrary::FindTrashKindRow(KindId, Row))
		{
			return false;
		}
		double Sum = 0.0;
		int32 Count = 0;
		for (const FName VariantId : Row.GetVariantIds())
		{
			TSoftObjectPtr<UStaticMesh> Mesh;
			if (!UMD_TrashLibrary::ResolveTrashKindMesh(KindId, VariantId, Mesh) || !Mesh.LoadSynchronous()) continue;
			FVector Scale = FVector::OneVector;
			FString ConfiguredScale;
			const FString Key = KindId.ToString() + TEXT(".") + VariantId.ToString();
			if (GConfig && GConfig->GetString(TEXT("MarineDemolition.TrashFloorScales"), *Key, ConfiguredScale, GGameIni))
			{
				if (!Scale.InitFromString(ConfiguredScale)) Scale = FVector::OneVector;
			}
			const FVector Extent = Mesh->GetBoundingBox().GetExtent() * Scale;
			const double Radius = FVector(Extent.X, Extent.Y, Extent.Z * 2.0).Size() + 2.0;
			const double Diameter = FMath::Max(static_cast<double>(MinimumSpacing), Radius * 2.0);
			Sum += Diameter * Diameter;
			++Count;
		}
		if (Count == 0)
		{
			return false;
		}
		OutMeanDiameterSquared = Sum / Count;
		return true;
	}

	// 면적 대비 실제 배치 비율. L__GilTest 실측(293m²→38개, 145m²→15개)으로 보정한 값이며 가장자리·접근성 손실을 포함한다
	constexpr double TrashCapacityFillFactor = 0.33;
}

UMD_TrashSpawnProfile* UMD_TrashSpawnManager::ResolveProfile(UWorld* World, const AMD_TrashSpawnVolume* Preferred)
{
	if (!World)
	{
		return nullptr;
	}
	// 1) 레벨에 놓인 생성기의 재정의. 요청한 볼륨을 먼저 보고, 없으면 월드의 다른 생성기를 본다
	if (Preferred && Preferred->GetTrashSpawnProfileOverride())
	{
		return Preferred->GetTrashSpawnProfileOverride();
	}
	for (TActorIterator<AMD_TrashSpawnVolume> It(World); It; ++It)
	{
		if (It->GetTrashSpawnProfileOverride())
		{
			return It->GetTrashSpawnProfileOverride();
		}
	}
	// 2) 진행 중인 계약의 현장
	if (const UGameInstance* GameInstance = World->GetGameInstance())
	{
		if (const UMD_MerchantSubsystem* Merchant = GameInstance->GetSubsystem<UMD_MerchantSubsystem>())
		{
			if (const UMD_SiteDefinition* Site = Merchant->GetCurrentSite(); Site && Site->TrashSpawnProfile)
			{
				return Site->TrashSpawnProfile;
			}
		}
	}
	// 3) 계약 전·에디터: 월드 세팅의 게임모드 클래스가 이 맵에 등록한 현장
	const AWorldSettings* Settings = World->GetWorldSettings();
	const UClass* GameModeClass = Settings ? Settings->DefaultGameMode.Get() : nullptr;
	if (const AMD_GameModeBase* GameMode = GameModeClass ? Cast<AMD_GameModeBase>(GameModeClass->GetDefaultObject()) : nullptr)
	{
		if (const UMD_SiteDefinition* Site = GameMode->FindExpeditionSiteForWorld(World); Site && Site->TrashSpawnProfile)
		{
			return Site->TrashSpawnProfile;
		}
	}
	return nullptr;
}

int32 UMD_TrashSpawnManager::ComputeTotalTrashCount(UWorld* World, const int32 PlayerCount, const EMD_Difficulty Difficulty, const AMD_TrashSpawnVolume* Preferred)
{
	if (const UMD_TrashSpawnProfile* Profile = ResolveProfile(World, Preferred))
	{
		return Profile->ComputeTotalTrashCount(PlayerCount, Difficulty);
	}
	const UMD_TrashSettings* Settings = UMD_TrashSettings::Get();
	if (!Settings)
	{
		return 0;
	}
	const float PlayerMultiplier = Settings->GetPlayerCountTrashMultiplier(FMath::Max(1, PlayerCount));
	const float DifficultyMultiplier = Settings->bScaleTrashCountByDifficulty
		? MDTravel::GetDifficultyMultiplier(Difficulty) : 1.0f;
	return FMath::Max(0, FMath::RoundToInt32(Settings->ProceduralTrashBaseCount * PlayerMultiplier * DifficultyMultiplier));
}

int32 UMD_TrashSpawnManager::ApportionShares(const int32 Total, const TArray<float>& Shares, const TArray<int32>& Capacity, TArray<int32>& OutAssigned)
{
	OutAssigned.Init(0, Shares.Num());
	double TotalShare = 0.0;
	for (const float Share : Shares) TotalShare += FMath::Max(0.0f, Share);
	if (Shares.IsEmpty() || Capacity.Num() != Shares.Num() || TotalShare <= 0.0 || Total <= 0)
	{
		return FMath::Max(0, Total);
	}
	// 최대 잉여법: 정수부를 먼저 주고 남은 개수를 소수부가 큰 순서로 하나씩 더한다
	TArray<double> Fractions;
	Fractions.Init(0.0, Shares.Num());
	int32 Remaining = Total;
	for (int32 I = 0; I < Shares.Num(); ++I)
	{
		const double Ideal = Total * FMath::Max(0.0f, Shares[I]) / TotalShare;
		OutAssigned[I] = FMath::Min(static_cast<int32>(Ideal), Capacity[I]);
		Fractions[I] = Ideal - static_cast<int32>(Ideal);
		Remaining -= OutAssigned[I];
	}
	TArray<int32> Order;
	for (int32 I = 0; I < Shares.Num(); ++I) Order.Add(I);
	Order.StableSort([&](int32 A, int32 B) { return Fractions[A] > Fractions[B]; });
	// 소수부 순서를 먼저 채우고, 그래도 남으면 상한이 남은 볼륨에 반복해 넘긴다
	bool bProgress = true;
	while (Remaining > 0 && bProgress)
	{
		bProgress = false;
		for (const int32 I : Order)
		{
			if (Remaining <= 0) break;
			if (OutAssigned[I] < Capacity[I])
			{
				++OutAssigned[I];
				--Remaining;
				bProgress = true;
			}
		}
	}
	return Remaining;
}

int32 UMD_TrashSpawnManager::ResolveTargetTrashTotal(int32& OutPlayerCount, EMD_Difficulty& OutDifficulty) const
{
	OutPlayerCount = 0;
	OutDifficulty = EMD_Difficulty::Standard;
	UWorld* World = GetWorld();
	if (!World)
	{
		return 0;
	}
	if (const AGameStateBase* GameState = World->GetGameState())
	{
		for (const APlayerState* PlayerState : GameState->PlayerArray)
		{
			if (IsValid(PlayerState) && !PlayerState->IsOnlyASpectator() && !PlayerState->IsInactive())
			{
				++OutPlayerCount;
			}
		}
		if (const AMD_GameStateBase* MDGameState = Cast<AMD_GameStateBase>(GameState))
		{
			OutDifficulty = MDGameState->GetContractDifficulty();
		}
	}
	// 접속 전 초기화나 전용 서버 테스트처럼 PlayerState가 없으면 1인 기준으로 계산한다
	OutPlayerCount = FMath::Max(OutPlayerCount, 1);
	return ComputeTotalTrashCount(World, OutPlayerCount, OutDifficulty);
}

void UMD_TrashSpawnManager::DistributeSpawnShares()
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client)
	{
		return;
	}
	TArray<AMD_TrashSpawnVolume*> Participants;
	TArray<float> Shares;
	TArray<int32> Capacity;
	for (AMD_TrashSpawnVolume* Generator : GetGenerators())
	{
		// 이미 배치가 끝난 생성기와 비율을 쓰지 않는 생성기는 자기 SpawnCount를 유지한다
		if (Generator->IsStateInitialized() || !Generator->bSpawnOnBeginPlay
			|| !FMath::IsFinite(Generator->SpawnShare) || Generator->SpawnShare <= 0.0f) continue;
		Participants.Add(Generator);
		Shares.Add(Generator->SpawnShare);
		// 바닥 셀 볼륨은 후보 지점 수를 넘기면 InitializeFresh가 실패하므로 그 수를 상한으로 둔다. 스플라인 볼륨은 상한 없음
		const UMD_TrashFloorPlacementComponent* Floor = Generator->GetFloorPlacement();
		const bool bCapped = Generator->UsesAutomaticFloorCells() && Floor && Floor->IsReady() && !Generator->UsesTowerPlacement();
		Capacity.Add(bCapped ? Floor->GetCandidateCount() : MAX_int32);
	}
	if (Participants.IsEmpty())
	{
		return;
	}

	int32 PlayerCount = 0;
	EMD_Difficulty Difficulty = EMD_Difficulty::Standard;
	const int32 Total = ResolveTargetTrashTotal(PlayerCount, Difficulty);
	DistributedTrashTotal = Total;
	DistributedPlayerCount = PlayerCount;
	DistributedDifficulty = Difficulty;

	TArray<int32> Assigned;
	const int32 Remaining = ApportionShares(Total, Shares, Capacity, Assigned);
	if (Remaining > 0)
	{
		UE_LOG(MarineDemolition, Warning,
			TEXT("Trash share distribution: %d of %d items have no floor capacity left and were dropped."), Remaining, Total);
	}
	for (int32 I = 0; I < Participants.Num(); ++I)
	{
		Participants[I]->SpawnCount = Assigned[I];
		UE_LOG(MarineDemolition, Display, TEXT("Trash share: '%s' share=%.1f -> %d items (capacity %s)"),
			*Participants[I]->GetName(), Shares[I], Assigned[I],
			Capacity[I] == MAX_int32 ? TEXT("unbounded") : *FString::FromInt(Capacity[I]));
	}
	const UMD_TrashSpawnProfile* Profile = ResolveProfile(GetWorld());
	UE_LOG(MarineDemolition, Display,
		TEXT("Trash share distribution: total=%d players=%d difficulty=%d volumes=%d profile=%s"),
		Total, PlayerCount, static_cast<int32>(Difficulty), Participants.Num(), Profile ? *Profile->GetName() : TEXT("ProjectSettings"));
}

#if WITH_EDITOR
void UMD_TrashSpawnManager::PreviewShareDistribution(UWorld* World, const AMD_TrashSpawnVolume* Requester, const int32 PlayerCount, const EMD_Difficulty Difficulty)
{
	if (!World)
	{
		return;
	}
	TArray<AMD_TrashSpawnVolume*> Participants;
	TArray<float> Shares;
	TArray<int32> Capacity;
	TArray<AMD_TrashSpawnVolume*> All;
	for (TActorIterator<AMD_TrashSpawnVolume> It(World); It; ++It)
	{
		All.Add(*It);
		It->PreviewAssignedCount = 0;
		It->PreviewEstimatedCapacity = 0;
		It->PreviewTotalCount = 0;
		if (!It->bSpawnOnBeginPlay || !FMath::IsFinite(It->SpawnShare) || It->SpawnShare <= 0.0f) continue;
		Participants.Add(*It);
		Shares.Add(It->SpawnShare);
		// 에디터에는 바닥 샘플이 없어 상한을 두지 않고, 면적 기반 추정치는 참고값으로만 표시한다
		Capacity.Add(MAX_int32);
	}
	const int32 Total = ComputeTotalTrashCount(World, PlayerCount, Difficulty, Requester);
	TArray<int32> Assigned;
	ApportionShares(Total, Shares, Capacity, Assigned);
	const UMD_TrashSpawnProfile* Profile = ResolveProfile(World, Requester);
	UE_LOG(MarineDemolition, Display, TEXT("[Trash preview] players=%d difficulty=%d total=%d profile=%s volumes=%d"),
		PlayerCount, static_cast<int32>(Difficulty), Total, Profile ? *Profile->GetName() : TEXT("ProjectSettings"), Participants.Num());
	for (AMD_TrashSpawnVolume* Volume : All) Volume->PreviewTotalCount = Total;
	int32 OverCapacity = 0;
	for (int32 I = 0; I < Participants.Num(); ++I)
	{
		AMD_TrashSpawnVolume* Volume = Participants[I];
		Volume->PreviewAssignedCount = Assigned[I];
		// 종류별 평균 점유 지름²을 종류 확률로 가중 평균해 수용량을 추정한다
		TArray<TPair<FName, double>> Kinds;
		for (const FMD_TrashSpawnKindChance& Chance : Volume->GetEffectiveKindChances())
		{
			if (Chance.ProbabilityPercent > 0.0f) Kinds.Emplace(Chance.TrashKindId, Chance.ProbabilityPercent);
		}
		if (Kinds.IsEmpty())
		{
			for (const FName KindId : Volume->BuildEligibleTrashKindIds()) Kinds.Emplace(KindId, 1.0);
		}
		double WeightedDiameterSquared = 0.0;
		double WeightSum = 0.0;
		for (const TPair<FName, double>& Kind : Kinds)
		{
			double MeanDiameterSquared = 0.0;
			if (ComputeKindMeanDiameterSquared(Kind.Key, Volume->MinimumSpacing, MeanDiameterSquared))
			{
				WeightedDiameterSquared += MeanDiameterSquared * Kind.Value;
				WeightSum += Kind.Value;
			}
		}
		const double Area = Volume->ComputeBoundaryArea();
		Volume->PreviewEstimatedCapacity = WeightSum > 0.0 && WeightedDiameterSquared > 0.0
			? FMath::FloorToInt32(Area * TrashCapacityFillFactor / (WeightedDiameterSquared / WeightSum)) : 0;
		const bool bOver = Volume->PreviewEstimatedCapacity > 0 && Assigned[I] > Volume->PreviewEstimatedCapacity;
		if (bOver) OverCapacity += Assigned[I] - Volume->PreviewEstimatedCapacity;
		UE_LOG(MarineDemolition, Display, TEXT("[Trash preview] %s share=%.1f -> %d items, estimated capacity %d (area %.0f m2)%s"),
			*Volume->GetActorLabel(), Shares[I], Assigned[I], Volume->PreviewEstimatedCapacity, Area / 10000.0,
			bOver ? TEXT("  <-- OVER CAPACITY") : TEXT(""));
	}
	if (OverCapacity > 0)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("[Trash preview] about %d items exceed the estimated capacity; lower the base count or enlarge/add volumes."), OverCapacity);
	}
}
#endif

bool UMD_TrashSpawnManager::EnableAuthoredFloorTransition()
{
	if (TransitionGenerator.IsValid()) return true;
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client) return false;
	// An explicitly placed automatic generator owns this map instead of the runtime adapter.
	for (const auto& Entry : Generators)
		if (Entry.IsValid() && Entry->UsesAutomaticFloorCells()) return true;
	FActorSpawnParameters Params;
	Params.Name = TEXT("MD_AuthoredFloorTransition");
	Params.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_ErrorAndReturnNull;
	Params.ObjectFlags |= RF_Transient;
	auto* Generator = GetWorld()->SpawnActor<AMD_TrashFloorGenerator>(Params);
	if (!Generator) return false;
	TransitionGenerator = Generator;
	RegisterGenerator(Generator);
	Generator->SpawnCount = 0;
	for (TActorIterator<AMD_TrashItem> It(GetWorld()); It; ++It)
	{
		if (It->GetOriginGenerator() || !It->HasAuthority()) continue;
		AuthoredPositions.Add(MakeAuthoredTrashKey(*It), It->GetActorLocation());
		Generator->TrashItemClass = It->GetClass();
		auto& Profiles = Generator->FloorAppearances.FindOrAdd(It->GetTrashKindId());
		const FName Variant = It->GetTrashVariantId();
		const FVector Scale = It->GetActorScale3D();
		if (auto* Existing = Profiles.FindByPredicate([&](const FMD_TrashFloorAppearance& Profile)
			{ return Profile.Variant == Variant && Profile.Scale.Equals(Scale, 0.0001); })) ++Existing->Weight;
		else Profiles.Add({Variant, Scale, 1});
	}
	return true;
}

bool UMD_TrashSpawnManager::ConvertAuthoredTrash()
{
	auto* Generator = TransitionGenerator.Get();
	if (!Generator) return false;
	TArray<AMD_TrashItem*> Items;
	for (TActorIterator<AMD_TrashItem> It(GetWorld()); It; ++It)
	{
		// 전환 수집 시점에 없던 동적·외부 쓰레기는 그대로 둔다.
		if (It->GetOriginGenerator() || !AuthoredPositions.Contains(MakeAuthoredTrashKey(*It))) continue;
		Items.Add(*It);
	}
	if (Generator->ConvertAuthoredItems(Items, nullptr))
	{
		UE_LOG(MarineDemolition, Display, TEXT("Floor transition: converted %d authored trash items."), Items.Num());
		return true;
	}
	// 전환에 실패해도 배치된 액터는 지우지 않고 빈 생성기 상태로 둔다.
	UE_LOG(MarineDemolition, Warning, TEXT("Floor transition deferred: retaining %d legacy actors."), Items.Num());
	if (Generator->IsStateInitialized()) return true;
	FMD_TrashGeneratorState Empty;
	return Generator->BuildEmptyState(Empty) && Generator->RestoreState(Empty);
}

TArray<AMD_TrashSpawnVolume*> UMD_TrashSpawnManager::GetGenerators() const
{
	TArray<AMD_TrashSpawnVolume*> Result;
	Result.Reserve(Generators.Num());
	for (const TWeakObjectPtr<AMD_TrashSpawnVolume>& Entry : Generators)
	{
		if (AMD_TrashSpawnVolume* Generator = Entry.Get())
		{
			Result.Add(Generator);
		}
	}
	return Result;
}
int32 UMD_TrashSpawnManager::GetGeneratorCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<AMD_TrashSpawnVolume>& Entry : Generators)
	{
		Count += Entry.IsValid() ? 1 : 0;
	}
	return Count;
}

int32 UMD_TrashSpawnManager::GetRemainingTrashCount() const
{
	int64 Count = 0;
	for (const TWeakObjectPtr<AMD_TrashSpawnVolume>& Entry : Generators)
	{
		if (const AMD_TrashSpawnVolume* Generator = Entry.Get())
		{
			Count += Generator->GetRemainingTrashCount();
		}
	}
	return static_cast<int32>(FMath::Min<int64>(Count, MAX_int32));
}
