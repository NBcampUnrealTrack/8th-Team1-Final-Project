#include "World/Trash/MD_TrashSpawnManager.h"

#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "TimerManager.h"
#include "Gameplay/Trash/MD_TrashLibrary.h"
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
	if (TransitionGenerator.IsValid() && (!TransitionGenerator->IsStateInitialized() || bMigrateRestoredActors))
	{
		if (!ConvertAuthoredTrash(bMigrateRestoredActors)) return false;
		bMigrateRestoredActors = false;
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

bool UMD_TrashSpawnManager::CaptureStates(TArray<FMD_TrashGeneratorState>& OutStates) const
{
	OutStates.Reset();
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client)
	{
		return false;
	}

	TSet<FName> SeenNames;
	for (const TWeakObjectPtr<AMD_TrashSpawnVolume>& Entry : Generators)
	{
		const AMD_TrashSpawnVolume* Generator = Entry.Get();
		if (!IsValid(Generator))
		{
			continue;
		}

		FMD_TrashGeneratorState State;
		if (!Generator->CaptureState(State) || State.GeneratorName.IsNone()
			|| SeenNames.Contains(State.GeneratorName))
		{
			OutStates.Reset();
			return false;
		}
		SeenNames.Add(State.GeneratorName);
		OutStates.Add(MoveTemp(State));
	}
	OutStates.Sort([](const FMD_TrashGeneratorState& Left, const FMD_TrashGeneratorState& Right)
	{
		return Left.GeneratorName.LexicalLess(Right.GeneratorName);
	});
	return true;
}

bool UMD_TrashSpawnManager::CanRestoreStates(const TArray<FMD_TrashGeneratorState>& Input) const
{
	TArray<FMD_TrashGeneratorState> States;
	if (!NormalizeStates(Input, States)) return false;
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client)
	{
		return false;
	}

	TMap<FName, AMD_TrashSpawnVolume*> ByName;
	for (const TWeakObjectPtr<AMD_TrashSpawnVolume>& Entry : Generators)
	{
		AMD_TrashSpawnVolume* Generator = Entry.Get();
		if (!IsValid(Generator))
		{
			continue;
		}
		const FName Name = Generator->GetFName();
		if (ByName.Contains(Name))
		{
			return false;
		}
		ByName.Add(Name, Generator);
	}

	TSet<FName> SeenNames;
	for (const FMD_TrashGeneratorState& State : States)
	{
		if (!State.IsStructurallyValid() || SeenNames.Contains(State.GeneratorName)
			|| !ByName.Contains(State.GeneratorName))
		{
			return false;
		}
		SeenNames.Add(State.GeneratorName);
		if (!ByName[State.GeneratorName]->CanRestoreState(State)) return false;
		for (const TPair<FName, int32>& KindCount : State.RemainingKindCounts)
		{
			FMD_TrashKindRow KindRow;
			if (KindCount.Key.IsNone() || KindCount.Value < 0
				|| !UMD_TrashLibrary::FindTrashKindRow(KindCount.Key, KindRow))
			{
				return false;
			}
		}
	}

	// A current-format save must describe every registered generator. Missing records
	// must not be silently replaced with a fresh weighted draw.
	if (SeenNames.Num() != ByName.Num())
	{
		return false;
	}

	return true;
}

bool UMD_TrashSpawnManager::RestoreStates(const TArray<FMD_TrashGeneratorState>& Input)
{
	TArray<FMD_TrashGeneratorState> States;
	if (!NormalizeStates(Input, States)) return false;
	if (!CanRestoreStates(States)) return false;
    TMap<FName, AMD_TrashSpawnVolume*> ByName;
    for (const auto& Entry : Generators)
        if (AMD_TrashSpawnVolume* Generator = Entry.Get()) ByName.Add(Generator->GetFName(), Generator);
	// The adapter commits last, after every ordinary generator has restored successfully.
	States.StableSort([this, &ByName](const FMD_TrashGeneratorState& A, const FMD_TrashGeneratorState& B)
	{
        if (ByName[A.GeneratorName]->IsFloorBonusVolume() != ByName[B.GeneratorName]->IsFloorBonusVolume())
            return ByName[A.GeneratorName]->IsFloorBonusVolume();
		const FName Name = TransitionGenerator.IsValid() ? TransitionGenerator->GetFName() : NAME_None;
		return A.GeneratorName != Name && B.GeneratorName == Name;
	});
	TArray<AMD_TrashSpawnVolume*> RestoredGenerators;
	RestoredGenerators.Reserve(States.Num());
	for (const FMD_TrashGeneratorState& State : States)
	{
		AMD_TrashSpawnVolume* Generator = ByName[State.GeneratorName];
		TArray<AMD_TrashItem*> Reusable;
		TArray<AActor*> Ignored;
		if (IsTransitionGenerator(Generator))
		{
			for (const auto& Entry : ReservedTrash)
				if (AMD_TrashItem* Item = Entry.Get()) { Reusable.Add(Item); Ignored.Add(Item); }
			Generator->GetFloorPlacement()->SetTransitionSources(Ignored);
		}
        const bool bRestored = Generator->RestoreState(State, Reusable.IsEmpty() ? nullptr : &Reusable);
		if (IsTransitionGenerator(Generator)) Generator->GetFloorPlacement()->SetTransitionSources({});
		if (!bRestored)
		{
			for (AMD_TrashSpawnVolume* Restored : RestoredGenerators)
			{
				Restored->ClearSpawnedTrash();
			}
			return false;
		}
		RestoredGenerators.Add(Generator);
	}
	for (const auto& Entry : ReservedTrash)
		if (AMD_TrashItem* Item = Entry.Get(); Item && !Item->GetOriginGenerator())
			if (!Item->Destroy()) return false;
	ReservedTrash.Reset();
	bReserveAuthoredTrash = false;
	NotifyGeneratorStateChanged();
	bMigrateRestoredActors = TransitionGenerator.IsValid() && !Input.ContainsByPredicate([this](const FMD_TrashGeneratorState& State)
	{
		return State.GeneratorName == TransitionGenerator->GetFName();
	});
	return true;
}

bool UMD_TrashSpawnManager::PrepareRestoreStates(const TArray<FMD_TrashGeneratorState>& States)
{
	CancelPreparedRestore();
	if (!CanRestoreStates(States)) return false;
	bReserveAuthoredTrash = TransitionGenerator.IsValid() && States.ContainsByPredicate([this](const auto& State)
	{
		return State.GeneratorName == TransitionGenerator->GetFName() && State.bUsesFloorCells;
	});
	return true;
}

bool UMD_TrashSpawnManager::ReserveAuthoredTrashForRestore(AMD_TrashItem* Item)
{
	if (!bReserveAuthoredTrash || !IsValid(Item) || !Item->HasAuthority() || Item->GetOriginGenerator()
		|| !AuthoredPositions.Contains(MakeAuthoredTrashKey(Item))) return false;
	ReservedTrash.AddUnique(Item);
	return true;
}

bool UMD_TrashSpawnManager::IsTransitionGenerator(const AMD_TrashSpawnVolume* Generator) const
{
	return TransitionGenerator.Get() == Generator;
}

bool UMD_TrashSpawnManager::NormalizeStates(const TArray<FMD_TrashGeneratorState>& Input,
	TArray<FMD_TrashGeneratorState>& Out) const
{
	Out = Input;
	if (const auto* Generator = TransitionGenerator.Get())
	{
		if (!Input.ContainsByPredicate([Generator](const FMD_TrashGeneratorState& State) { return State.GeneratorName == Generator->GetFName(); }))
		{
			FMD_TrashGeneratorState Empty;
			if (!Generator->BuildEmptyState(Empty)) return false;
			Out.Add(MoveTemp(Empty));
		}
	}
	return true;
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

bool UMD_TrashSpawnManager::ConvertAuthoredTrash(const bool bPreserveCells)
{
	auto* Generator = TransitionGenerator.Get();
	if (!Generator) return false;
	TArray<AMD_TrashItem*> Items;
	TArray<FVector> Origins;
	for (TActorIterator<AMD_TrashItem> It(GetWorld()); It; ++It)
	{
		if (It->GetOriginGenerator()) continue;
		const FVector* Origin = AuthoredPositions.Find(MakeAuthoredTrashKey(*It));
		// Unknown dynamic/legacy items retain their exact actor snapshot identity.
		if (!Origin) continue;
		Items.Add(*It);
		Origins.Add(*Origin);
	}
	if (Generator->ConvertAuthoredItems(Items, bPreserveCells ? &Origins : nullptr))
	{
		UE_LOG(MarineDemolition, Display, TEXT("Floor transition: converted %d authored trash items (preserve cells=%d)."), Items.Num(), bPreserveCells);
		return true;
	}
	// Never invent a clean-cell history or delete leftovers to make migration fit.
	UE_LOG(MarineDemolition, Warning, TEXT("Floor transition deferred: retaining %d legacy actors and their saved positions."), Items.Num());
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
