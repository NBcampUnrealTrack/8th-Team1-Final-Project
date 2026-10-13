#include "Gameplay/Scrape/MD_ContaminationDebrisSubsystem.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "World/Contamination/MD_ContaminationItem.h"

namespace
{
    TAutoConsoleVariable<int32> CVarDebrisMaxLive(TEXT("MD.Scrape.Debris.MaxLive"), 256,
        TEXT("Maximum live contamination actors from the durable drop queue; excess units remain pending."));
    TAutoConsoleVariable<int32> CVarDebrisMaxSpawns(TEXT("MD.Scrape.Debris.MaxSpawnsPerFrame"), 8,
        TEXT("Maximum contamination spawn attempts per frame across the world."));
    bool EqualCarrier(const double A, const double B)
    {
        return FMath::Abs(A - B) <= 1.0e-9 * FMath::Max(1.0, FMath::Max(FMath::Abs(A), FMath::Abs(B)));
    }
    FMD_ContaminationPendingDrop CopyMultiItemRecordForDrain(FMD_ContaminationPendingDrop& Working)
    {
        TArray<FMD_ScrapeRecoveryLot> RecoveryLots = MoveTemp(Working.RecoveryLots);
        FMD_ContaminationPendingDrop ItemRecord = Working;
        Working.RecoveryLots = MoveTemp(RecoveryLots);
        return ItemRecord;
    }
    bool HasAuthorityWorld(const UWorld* World)
    {
        return World && World->GetNetMode() != NM_Client;
    }
}
TRACE_DECLARE_INT_COUNTER(MD_Debris_LiveItems, TEXT("MD/ScrapeDebris/LiveItems"));
TRACE_DECLARE_INT_COUNTER(MD_Debris_PendingBatches, TEXT("MD/ScrapeDebris/PendingBatches"));
TRACE_DECLARE_INT_COUNTER(MD_Debris_LedgerBytes, TEXT("MD/ScrapeDebris/LedgerBytes"));

bool FMD_ContaminationPendingDrop::IsStructurallyValid() const
{
    if (!BatchId.IsValid() || (SourceActorName.IsNone() && SourceComponentName.IsNone())
        || (SurfaceFace != EMD_ScrapeSurfaceFace::Front && SurfaceFace != EMD_ScrapeSurfaceFace::Back)
        || SpawnTransform.ContainsNaN() || LinearVelocity.ContainsNaN() || AngularVelocityRadians.ContainsNaN()
        || !FMath::IsFinite(PendingCarrierAmount) || PendingCarrierAmount < 0.0
        || (!bSinglePhysicalItem && PendingCarrierAmount <= 0.0)
        || !FMath::IsFinite(CarrierAmountPerItem) || CarrierAmountPerItem < 0.0) return false;
    if (bSpawnConfigurationReady && (ItemClass.IsNull()
        || (!bSinglePhysicalItem && CarrierAmountPerItem <= UE_KINDA_SMALL_NUMBER))) return false;
    const double Amount = FMD_ScrapeRecoveryLedger::GetTotalCarrierAmount(RecoveryLots);
    const double Reward = FMD_ScrapeRecoveryLedger::GetTotalRewardUnits(RecoveryLots);
    return Amount >= 0.0 && Reward >= 0.0 && EqualCarrier(Amount, PendingCarrierAmount);
}

bool UMD_ContaminationDebrisSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UMD_ContaminationDebrisSubsystem::IsTickable() const
{
    return !IsTemplate() && HasAuthorityWorld(GetWorld()) && !PendingDrops.IsEmpty();
}

TStatId UMD_ContaminationDebrisSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UMD_ContaminationDebrisSubsystem, STATGROUP_Tickables);
}

void UMD_ContaminationDebrisSubsystem::Tick(float DeltaTime)
{
    DrainPendingDrops(GFrameCounter);
}

void UMD_ContaminationDebrisSubsystem::RegisterItem(AMD_ContaminationItem* Item)
{
    if (HasAuthorityWorld(GetWorld()) && IsValid(Item) && Item->GetWorld() == GetWorld()) ActiveItems.Add(Item);
    TraceMemory();
}

void UMD_ContaminationDebrisSubsystem::UnregisterItem(AMD_ContaminationItem* Item)
{
    ActiveItems.Remove(Item);
    // The next world tick retries pending units; never recursively spawn from EndPlay.
    TraceMemory();
}

bool UMD_ContaminationDebrisSubsystem::EnqueuePendingDrop(
    const FMD_ContaminationPendingDrop& Incoming, int32& OutSpawnedCount)
{
    OutSpawnedCount = 0;
    if (!HasAuthorityWorld(GetWorld()) || bRestoring || !Incoming.IsStructurallyValid()) return false;
    int32 MergeIndex = INDEX_NONE;
    if (!Incoming.bSinglePhysicalItem)
    {
        for (int32 Index = 0; Index < PendingDrops.Num(); ++Index)
        {
            const auto& Existing = PendingDrops[Index];
            if (Existing.BatchId != SpawningBatchId && !Existing.bSinglePhysicalItem && Existing.SourceActorName == Incoming.SourceActorName
                && Existing.SourceComponentName == Incoming.SourceComponentName && Existing.SurfaceFace == Incoming.SurfaceFace
                && (!Existing.bSpawnConfigurationReady || !Incoming.bSpawnConfigurationReady
                    || (Existing.ItemClass == Incoming.ItemClass
                        && FMemory::Memcmp(&Existing.CarrierAmountPerItem, &Incoming.CarrierAmountPerItem, sizeof(double)) == 0
                        && (Existing.PendingCarrierAmount < Existing.CarrierAmountPerItem - UE_KINDA_SMALL_NUMBER
                            || (Existing.SpawnTransform.Equals(Incoming.SpawnTransform, 0.0)
                                && Existing.LinearVelocity == Incoming.LinearVelocity
                                && Existing.AngularVelocityRadians == Incoming.AngularVelocityRadians)))))
            { MergeIndex = Index; break; }
        }
    }
    FGuid AcceptedId = Incoming.BatchId;
    if (MergeIndex != INDEX_NONE)
    {
        auto Working = PendingDrops[MergeIndex];
        const double Combined = Working.PendingCarrierAmount + Incoming.PendingCarrierAmount;
        if (!FMath::IsFinite(Combined)) return false;
        for (const auto& Lot : Incoming.RecoveryLots)
            if (!FMD_ScrapeRecoveryLedger::AppendLot(Working.RecoveryLots, Lot)) return false;
        if (!FMD_ScrapeRecoveryLedger::RebaseCarrierAmount(Working.RecoveryLots, Combined)) return false;
        Working.PendingCarrierAmount = Combined;
        if (Incoming.bSpawnConfigurationReady)
        {
            Working.ItemClass = Incoming.ItemClass;
            Working.CarrierAmountPerItem = Incoming.CarrierAmountPerItem;
            Working.SpawnTransform = Incoming.SpawnTransform;
            Working.bSpawnConfigurationReady = true;
        }
        Working.EnqueueOrder = NextEnqueueOrder++;
        AcceptedId = Working.BatchId;
        PendingDrops[MergeIndex] = MoveTemp(Working);
    }
    else
    {
        for (const auto& Existing : PendingDrops) if (Existing.BatchId == Incoming.BatchId) return false;
        FMD_ContaminationPendingDrop Working = Incoming;
        Working.EnqueueOrder = NextEnqueueOrder++;
        PendingDrops.Add(MoveTemp(Working));
    }
    if (!bDraining) OutSpawnedCount = DrainPendingDrops(GFrameCounter, AcceptedId);
    TraceMemory();
    return true;
}

void UMD_ContaminationDebrisSubsystem::UpdateSourceConfiguration(const FMD_ContaminationPendingDrop& Configuration)
{
    if (!HasAuthorityWorld(GetWorld()) || bRestoring || !Configuration.bSpawnConfigurationReady
        || Configuration.ItemClass.IsNull() || Configuration.SpawnTransform.ContainsNaN()
        || !FMath::IsFinite(Configuration.CarrierAmountPerItem) || Configuration.CarrierAmountPerItem <= UE_KINDA_SMALL_NUMBER) return;
    for (auto& Existing : PendingDrops)
    {
        if (Existing.BatchId != SpawningBatchId && !Existing.bSinglePhysicalItem && !Existing.bSpawnConfigurationReady
            && Existing.SourceActorName == Configuration.SourceActorName
            && Existing.SourceComponentName == Configuration.SourceComponentName && Existing.SurfaceFace == Configuration.SurfaceFace)
        {
            Existing.ItemClass = Configuration.ItemClass;
            Existing.CarrierAmountPerItem = Configuration.CarrierAmountPerItem;
            Existing.SpawnTransform = Configuration.SpawnTransform;
            Existing.bSpawnConfigurationReady = true;
            Existing.EnqueueOrder = NextEnqueueOrder++;
            RetryAfterSeconds.Remove(Existing.BatchId);
        }
    }
}

void UMD_ContaminationDebrisSubsystem::MergeResidualBatch(const FGuid& BatchId)
{
    // Full physical units retain their due pose. Only incomplete face residues combine,
    // matching the previous pending-per-face behavior, at the most recent contributing pose.
    int32 Target = PendingDrops.IndexOfByPredicate([&](const auto& Drop) { return Drop.BatchId == BatchId; });
    if (Target == INDEX_NONE) return;
    auto Working = PendingDrops[Target];
    if (Working.bSinglePhysicalItem || !Working.bSpawnConfigurationReady
        || Working.PendingCarrierAmount >= Working.CarrierAmountPerItem - UE_KINDA_SMALL_NUMBER) return;
    for (int32 Index = PendingDrops.Num() - 1; Index >= 0; --Index)
    {
        const auto& Other = PendingDrops[Index];
        if (Other.BatchId == BatchId || Other.bSinglePhysicalItem || !Other.bSpawnConfigurationReady
            || Other.SourceActorName != Working.SourceActorName || Other.SourceComponentName != Working.SourceComponentName
            || Other.SurfaceFace != Working.SurfaceFace || Other.ItemClass != Working.ItemClass
            || Other.CarrierAmountPerItem != Working.CarrierAmountPerItem
            || Other.PendingCarrierAmount >= Other.CarrierAmountPerItem - UE_KINDA_SMALL_NUMBER) continue;
        auto Combined = Working;
        const double Amount = Working.PendingCarrierAmount + Other.PendingCarrierAmount;
        bool bValid = FMath::IsFinite(Amount);
        for (const auto& Lot : Other.RecoveryLots) bValid &= FMD_ScrapeRecoveryLedger::AppendLot(Combined.RecoveryLots, Lot);
        if (!bValid || !FMD_ScrapeRecoveryLedger::RebaseCarrierAmount(Combined.RecoveryLots, Amount)) continue;
        Combined.PendingCarrierAmount = Amount;
        if (Other.EnqueueOrder > Combined.EnqueueOrder)
        {
            Combined.SpawnTransform = Other.SpawnTransform;
            Combined.EnqueueOrder = Other.EnqueueOrder;
        }
        Working = MoveTemp(Combined);
        RetryAfterSeconds.Remove(Other.BatchId);
        PendingDrops.RemoveAt(Index);
        Target = PendingDrops.IndexOfByPredicate([&](const auto& Drop) { return Drop.BatchId == BatchId; });
        check(Target != INDEX_NONE);
        PendingDrops[Target] = Working;
        if (Working.PendingCarrierAmount >= Working.CarrierAmountPerItem - UE_KINDA_SMALL_NUMBER) break;
    }
}

int32 UMD_ContaminationDebrisSubsystem::DrainPendingDrops(const uint64 FrameId, const FGuid& CountBatch)
{
    UWorld* World = GetWorld();
    if (!HasAuthorityWorld(World) || bDraining || bRestoring || World->bIsTearingDown) return 0;
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_ContaminationDebris_Drain);
    TGuardValue<bool> DrainGuard(bDraining, true);
    if (BudgetFrame != FrameId) { BudgetFrame = FrameId; SpawnAttemptsThisFrame = 0; }
    for (auto It = ActiveItems.CreateIterator(); It; ++It) if (!It->IsValid()) It.RemoveCurrent();
    const int32 MaxLive = FMath::Max(0, CVarDebrisMaxLive.GetValueOnGameThread());
    const int32 MaxAttempts = FMath::Max(0, CVarDebrisMaxSpawns.GetValueOnGameThread());
    int32 CountedSpawns = 0;
    while (SpawnAttemptsThisFrame < MaxAttempts && ActiveItems.Num() + ReservedSlots < MaxLive)
    {
        const double Now = World->GetTimeSeconds();
        int32 Index = PendingDrops.IndexOfByPredicate([this, Now](const FMD_ContaminationPendingDrop& Drop)
        {
            const double* Retry = RetryAfterSeconds.Find(Drop.BatchId);
            return Drop.bSpawnConfigurationReady && (!Retry || Now >= *Retry)
                && (Drop.bSinglePhysicalItem || Drop.PendingCarrierAmount >= Drop.CarrierAmountPerItem - UE_KINDA_SMALL_NUMBER);
        });
        if (Index == INDEX_NONE) break;
        FMD_ContaminationPendingDrop Working = PendingDrops[Index];
        const FGuid SourceBatchId = Working.BatchId;
        FMD_ContaminationPendingDrop ItemRecord = Working.bSinglePhysicalItem ? Working : CopyMultiItemRecordForDrain(Working);
        ItemRecord.BatchId = Working.bSinglePhysicalItem ? Working.BatchId : FGuid::NewGuid();
        ItemRecord.bSinglePhysicalItem = true;
        const double Amount = Working.bSinglePhysicalItem ? Working.PendingCarrierAmount
            : FMath::Min(Working.PendingCarrierAmount, Working.CarrierAmountPerItem);
        ++SpawnAttemptsThisFrame;
        if (!Working.bSinglePhysicalItem)
        {
            const double Extracted = FMD_ScrapeRecoveryLedger::ExtractProportional(Working.RecoveryLots, Amount, ItemRecord.RecoveryLots);
            if (!EqualCarrier(Extracted, Amount)) { RetryAfterSeconds.Add(Working.BatchId, Now + 1.0); continue; }
        }
        ItemRecord.PendingCarrierAmount = Amount;
        UClass* ItemClass = Working.ItemClass.LoadSynchronous();
        if (!ItemClass || !ItemClass->IsChildOf(AMD_ContaminationItem::StaticClass()) || ItemClass->HasAnyClassFlags(CLASS_Abstract))
        { RetryAfterSeconds.Add(Working.BatchId, Now + 1.0); continue; }
        ++ReservedSlots;
        AMD_ContaminationItem* Item = nullptr;
        {
            // OnActorSpawned runs even for deferred actors. Keep this row immutable while
            // those callbacks enqueue other rows; no queue references cross that boundary.
            TGuardValue<FGuid> SpawnGuard(SpawningBatchId, SourceBatchId);
            Item = World->SpawnActorDeferred<AMD_ContaminationItem>(
                ItemClass, Working.SpawnTransform, nullptr, nullptr,
                Working.bSinglePhysicalItem ? ESpawnActorCollisionHandlingMethod::AlwaysSpawn
                    : ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
        }
        --ReservedSlots;
        if (!IsValid(Item) || Item->IsActorBeingDestroyed()) { RetryAfterSeconds.Add(Working.BatchId, Now + 1.0); continue; }
        Index = PendingDrops.IndexOfByPredicate([&](const auto& Drop) { return Drop.BatchId == SourceBatchId; });
        check(Index != INDEX_NONE);
        RegisterItem(Item); // Count before BeginPlay; its registration is idempotent.
        Item->InitializeRecoveryRecord(MoveTemp(ItemRecord));
        // Commit the ledger before user BeginPlay callbacks can enqueue/save again.
        if (Working.bSinglePhysicalItem || Amount >= Working.PendingCarrierAmount)
        {
            RetryAfterSeconds.Remove(Working.BatchId);
            PendingDrops.RemoveAt(Index);
        }
        else
        {
            Working.PendingCarrierAmount -= Amount;
            PendingDrops[Index] = MoveTemp(Working);
            MergeResidualBatch(SourceBatchId);
        }
        // No references into the queue span user BeginPlay callbacks.
        Item->FinishSpawning(Item->GetActorTransform());
        if (IsValid(Item)) Item->RestoreSavedVelocity();
        if (!CountBatch.IsValid() || SourceBatchId == CountBatch) ++CountedSpawns;
    }
    if (PendingDrops.IsEmpty()) { PendingDrops.Empty(); RetryAfterSeconds.Empty(); }
    TraceMemory();
    return CountedSpawns;
}

void UMD_ContaminationDebrisSubsystem::CapturePendingDrops(TArray<FMD_ContaminationPendingDrop>& Out) const
{
    Out = PendingDrops;
    for (const auto& Weak : ActiveItems)
    {
        if (const AMD_ContaminationItem* Item = Weak.Get(); IsValid(Item) && !Item->IsActorBeingDestroyed())
        {
            FMD_ContaminationPendingDrop Record;
            if (Item->CaptureRecoveryRecord(Record)) Out.Add(MoveTemp(Record));
        }
    }
}

bool UMD_ContaminationDebrisSubsystem::RestorePendingDrops(const TConstArrayView<FMD_ContaminationPendingDrop> Saved)
{
    if (!HasAuthorityWorld(GetWorld()) || bDraining || bRestoring) return false;
    TSet<FGuid> Ids;
    TArray<FMD_ContaminationPendingDrop> Candidate;
    Candidate.Reserve(Saved.Num());
    for (const auto& Record : Saved)
    {
        if (!Record.IsStructurallyValid() || Ids.Contains(Record.BatchId)) return false;
        Ids.Add(Record.BatchId);
        Candidate.Add(Record);
    }
    TGuardValue<bool> RestoreGuard(bRestoring, true);
    const auto PreviousItems = ActiveItems.Array();
    for (const auto& Weak : PreviousItems)
    {
        AMD_ContaminationItem* Item = Weak.Get();
        if (!IsValid(Item) || Item->IsActorBeingDestroyed()) continue;
        FMD_ContaminationPendingDrop Backup;
        if (!Item->CaptureRecoveryRecord(Backup) || !Item->Destroy()) return false;
        ActiveItems.Remove(Weak);
        // If a later destruction fails, previous units remain durable and retriable.
        PendingDrops.Add(MoveTemp(Backup));
    }
    PendingDrops = MoveTemp(Candidate);
    for (const auto& Record : PendingDrops) NextEnqueueOrder = FMath::Max(NextEnqueueOrder, Record.EnqueueOrder + 1);
    RetryAfterSeconds.Empty();
    TraceMemory();
    return true;
}

uint64 UMD_ContaminationDebrisSubsystem::GetAllocatedSize() const
{
    uint64 Bytes = PendingDrops.GetAllocatedSize() + ActiveItems.GetAllocatedSize() + RetryAfterSeconds.GetAllocatedSize();
    for (const auto& Record : PendingDrops) Bytes += Record.RecoveryLots.GetAllocatedSize();
    for (const auto& Weak : ActiveItems)
        if (const AMD_ContaminationItem* Item = Weak.Get(); IsValid(Item)) Bytes += Item->GetRecoveryAllocatedSize();
    return Bytes;
}

void UMD_ContaminationDebrisSubsystem::TraceMemory() const
{
    TRACE_COUNTER_SET(MD_Debris_LiveItems, ActiveItems.Num());
    TRACE_COUNTER_SET(MD_Debris_PendingBatches, PendingDrops.Num());
#if COUNTERSTRACE_ENABLED
    if (UE_TRACE_CHANNELEXPR_IS_ENABLED(CountersChannel))
    {
        TRACE_COUNTER_SET(MD_Debris_LedgerBytes, GetAllocatedSize());
    }
#endif
}

void UMD_ContaminationDebrisSubsystem::Deinitialize()
{
    PendingDrops.Empty();
    ActiveItems.Empty();
    RetryAfterSeconds.Empty();
    Super::Deinitialize();
}
