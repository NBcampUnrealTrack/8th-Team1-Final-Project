#include "World/Trash/MD_TrashSpawnManager.h"

#include "Engine/World.h"
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
    TArray<AMD_TrashSpawnVolume*> Order = GetGenerators();
    Order.StableSort([](const AMD_TrashSpawnVolume& A, const AMD_TrashSpawnVolume& B)
    { return A.IsFloorBonusVolume() && !B.IsFloorBonusVolume(); });
    TArray<AMD_TrashSpawnVolume*> Initialized;
    for (AMD_TrashSpawnVolume* Generator : Order)
    {
        if (Generator->IsStateInitialized()) continue;
        if (!Generator->InitializeFresh())
        {
            for (auto* Created : Initialized) Created->ClearSpawnedTrash();
            bSuccessful = false;
            break;
        }
        Initialized.Add(Generator);
    }
	bWorldInitialized = bSuccessful;
	NotifyGeneratorStateChanged();
	return bSuccessful;
}

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
