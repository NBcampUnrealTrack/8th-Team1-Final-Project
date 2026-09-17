#include "Gameplay/Persistence/MD_DayPersistenceSubsystem.h"
#include "Gameplay/Scrape/MD_ScrapeInitializationSubsystem.h"
#include "Data/SaveGame/MD_SaveSlotManager.h"
#include "Engine/GameInstance.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceLayout.h"
#include "Gameplay/Scrape/MD_ContaminationDebrisSubsystem.h"
#include "Gameplay/Contamination/MD_SurfaceStainPersistence.h"

#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"
#include "EngineUtils.h"
#include "Game/MD_GameStateBase.h"
#include "World/Contamination/MD_BarnacleSpawner.h"
#include "World/Interaction/MD_SectorCloseoutMarker.h"
#include "World/Sample/MD_SampleItem.h"
#include "World/Support/MD_SupportBrace.h"
#include "World/Trash/MD_TrashBin.h"
#include "World/Trash/MD_TrashDispenser.h"
#include "World/Wall/MD_CuttableWall.h"
#include "World/Wall/MD_WallPiece.h"
#include "Types/SaveGame/MD_SaveDataTypes.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
	struct FMD_TrashBinRestoreContext
	{
		TMap<FName, AMD_TrashDispenser*> DispensersByName;
		TMap<FGuid, AMD_TrashBin*> ReusableBinsById;
		TSet<FGuid> AmbiguousExistingIds;
		TArray<AMD_TrashBin*> ExistingDispensedBins;
	};

	bool GatherPersistentTrashBinRecords(
		UWorld* World,
		TArray<FMD_TrashBinRecord>& OutRecords)
	{
		OutRecords.Reset();
		if (!IsValid(World) || World->GetNetMode() == NM_Client)
		{
			return false;
		}

		TSet<FGuid> CapturedIds;
		TSet<FName> AvailableDispenserNames;
		for (TActorIterator<AMD_TrashDispenser> It(World); It; ++It)
		{
			AvailableDispenserNames.Add(It->GetFName());
		}
		for (TActorIterator<AMD_TrashBin> It(World); It; ++It)
		{
			AMD_TrashBin* const Bin = *It;
			const bool bWasDispensed =
				!Bin->GetSourceTrashDispenserName().IsNone()
				|| IsValid(Cast<AMD_TrashDispenser>(Bin->GetOwner()));
			if (!bWasDispensed)
			{
				continue;
			}

			FMD_TrashBinRecord Record;
			if (!Bin->BuildPersistenceRecord(Record)
				|| !AvailableDispenserNames.Contains(Record.SourceDispenserName)
				|| CapturedIds.Contains(Record.StableBinId))
			{
				OutRecords.Reset();
				return false;
			}

			CapturedIds.Add(Record.StableBinId);
			OutRecords.Add(MoveTemp(Record));
		}

		return true;
	}

	bool BuildTrashBinRestoreContext(
		UWorld* World,
		TConstArrayView<FMD_TrashBinRecord> Records,
		FMD_TrashBinRestoreContext& OutContext)
	{
		OutContext = FMD_TrashBinRestoreContext();
		if (!IsValid(World))
		{
			return false;
		}

		for (TActorIterator<AMD_TrashDispenser> It(World); It; ++It)
		{
			if (OutContext.DispensersByName.Contains(It->GetFName()))
			{
				return false;
			}
			OutContext.DispensersByName.Add(It->GetFName(), *It);
		}

		for (const FMD_TrashBinRecord& Record : Records)
		{
			AMD_TrashDispenser* const* SavedDispenser =
				OutContext.DispensersByName.Find(Record.SourceDispenserName);
			if (!Record.IsStructurallyValid()
				|| !SavedDispenser
				|| !IsValid(*SavedDispenser))
			{
				return false;
			}
		}

		for (TActorIterator<AMD_TrashBin> It(World); It; ++It)
		{
			AMD_TrashBin* const Bin = *It;
			if (Bin->GetSourceTrashDispenserName().IsNone())
			{
				for (const FMD_TrashBinRecord& Record : Records)
				{
					if (Bin->GetStableBinId().IsValid()
						&& Bin->GetStableBinId() == Record.StableBinId)
					{
						// 레벨 배치 통과 저장 동적 통이 같은 ID를 공유하면 고유성을 보장할 수 없다.
						return false;
					}
				}
				continue;
			}

			OutContext.ExistingDispensedBins.Add(Bin);
			const FGuid& ExistingId = Bin->GetStableBinId();
			if (!ExistingId.IsValid()
				|| OutContext.AmbiguousExistingIds.Contains(ExistingId))
			{
				continue;
			}
			if (OutContext.ReusableBinsById.Contains(ExistingId))
			{
				OutContext.ReusableBinsById.Remove(ExistingId);
				OutContext.AmbiguousExistingIds.Add(ExistingId);
				continue;
			}
			OutContext.ReusableBinsById.Add(ExistingId, Bin);
		}

		return true;
	}

	bool RestorePersistentTrashBins(
		UWorld* World,
		TConstArrayView<FMD_TrashBinRecord> Records,
		FMD_TrashBinRestoreContext& Context)
	{
		TSet<TWeakObjectPtr<AMD_TrashBin>> RestoredBins;
		for (const FMD_TrashBinRecord& Record : Records)
		{
			AMD_TrashDispenser* const* DispenserPtr =
				Context.DispensersByName.Find(Record.SourceDispenserName);
			AMD_TrashDispenser* const Dispenser = DispenserPtr
				? *DispenserPtr
				: nullptr;
			if (!IsValid(Dispenser))
			{
				return false;
			}

			AMD_TrashBin* Bin = nullptr;
			if (AMD_TrashBin* const* ExistingBin =
				Context.ReusableBinsById.Find(Record.StableBinId))
			{
				if (IsValid(*ExistingBin)
					&& (*ExistingBin)->GetClass() == Record.TrashBinClass.Get()
					&& (*ExistingBin)->GetSourceTrashDispenserName()
						== Record.SourceDispenserName)
				{
					Bin = *ExistingBin;
				}
			}

			if (!IsValid(Bin))
			{
				const FTransform SpawnTransform = Record.Transform;
				Bin = World->SpawnActorDeferred<AMD_TrashBin>(
					Record.TrashBinClass,
					SpawnTransform,
					Dispenser,
					nullptr,
					ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (!IsValid(Bin)
					|| !Bin->RestorePersistenceRecord(Record))
				{
					if (IsValid(Bin))
					{
						Bin->Destroy();
					}
					return false;
				}
				Bin->FinishSpawning(SpawnTransform);
			}
			else
			{
				// 먼저 물리 통을 저장 transform으로 옮긴 뒤 원장을 복원해야
				// 정적 채움 시각화와 spill 기준점이 같은 frame을 사용한다.
				if (!Bin->SetActorTransform(
						Record.Transform,
						false,
						nullptr,
						ETeleportType::TeleportPhysics)
					|| !Bin->RestorePersistenceRecord(Record))
				{
					return false;
				}
			}

			const TWeakObjectPtr<AMD_TrashBin> BinPtr(Bin);
			if (!Bin->SetActorTransform(
					Record.Transform,
					false,
					nullptr,
					ETeleportType::TeleportPhysics)
				|| !Dispenser->RegisterDispensedBin(Bin))
			{
				if (!RestoredBins.Contains(BinPtr))
				{
					Bin->Destroy();
				}
				return false;
			}

			RestoredBins.Add(BinPtr);
		}

		// 저장 목록에 없는 보급 통은 로드 시점의 과거 월드에는 존재하지 않았던 개체다.
		for (AMD_TrashBin* const ExistingBin : Context.ExistingDispensedBins)
		{
			const TWeakObjectPtr<AMD_TrashBin> ExistingBinPtr(ExistingBin);
			if (IsValid(ExistingBin) && !RestoredBins.Contains(ExistingBinPtr))
			{
				ExistingBin->Destroy();
			}
		}

		return true;
	}

    void GatherScrapeSurfaces(
        UWorld* World,
        TArray<UMD_ScrapeSurfaceComponent*>& OutSurfaces)
    {
        OutSurfaces.Reset();
        if (!World)
        {
            return;
        }

        for (TActorIterator<AActor> It(World); It; ++It)
        {
            TInlineComponentArray<UMD_ScrapeSurfaceComponent*> ActorSurfaces(*It);
            for (UMD_ScrapeSurfaceComponent* const Surface : ActorSurfaces)
            {
                if (IsValid(Surface) && Surface->IsContaminationEnabled())
                {
                    OutSurfaces.Add(Surface);
                }
            }
        }
    }

}


void UMD_DayPersistenceSubsystem::ResetContaminationContext()
{
    ContaminationContextWorld.Reset();
    bHasContaminationSessionSeed=false;
    ContaminationSessionSeed=0;
    bDeferSavedContaminationGeneration=false;
    SavedContaminationGenerations.Empty();
    SavedContaminationLayouts.Empty();
    DeferredContaminationRecords.Empty();
}

bool UMD_DayPersistenceSubsystem::IsContaminationContextReady(const UWorld* World) const
{
    return World && ContaminationContextWorld.Get()==World;
}

bool UMD_DayPersistenceSubsystem::TryGetContaminationSessionSeed(const UWorld* World, int32& OutSeed) const
{
    if (!IsContaminationContextReady(World) || !bHasContaminationSessionSeed) return false;
    OutSeed=ContaminationSessionSeed;
    return true;
}

void UMD_DayPersistenceSubsystem::PrepareContaminationContext(
    UWorld* World, const FMD_WorldData* SavedWorld, const bool bNewContract, const bool bDeferSavedGeneration)
{
    if (!World || World->GetNetMode()==NM_Client) return;
    if (!SavedWorld && !bNewContract && IsContaminationContextReady(World)) return;
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Contamination_PrepareSession);
    ResetContaminationContext();
    ContaminationContextWorld=World;
    const TArray<FMD_WallContaminationRecord>* Records=nullptr;
    if (SavedWorld)
    {
        bHasContaminationSessionSeed=SavedWorld->bHasContaminationSessionSeed;
        ContaminationSessionSeed=SavedWorld->ContaminationSessionSeed;
        Records=&SavedWorld->WallContaminations;
    }
    else if (!bNewContract && Snapshot.bValid && Snapshot.WorldName==World->GetFName())
    {
        bHasContaminationSessionSeed=Snapshot.bHasContaminationSessionSeed;
        ContaminationSessionSeed=Snapshot.ContaminationSessionSeed;
        Records=&Snapshot.WallContaminations;
    }
    else
    {
        bHasContaminationSessionSeed=true;
        ContaminationSessionSeed=static_cast<int32>(GetTypeHash(FGuid::NewGuid()));
    }
    bDeferSavedContaminationGeneration=bDeferSavedGeneration && Records;
    if (Records)
        for (const auto& Record:*Records)
        {
            if (bDeferSavedContaminationGeneration && Record.CellIndices.IsEmpty())
                DeferredContaminationRecords.FindOrAdd(Record.WallName).Add(Record.SurfaceComponentName, Record);
            if (Record.LayoutVersion == 1 && Record.LayoutId.IsValid())
                SavedContaminationLayouts.FindOrAdd(Record.WallName)
                    .Add(Record.SurfaceComponentName, Record.LayoutId);
            if (Record.Generation.IsValid())
                SavedContaminationGenerations.FindOrAdd(Record.WallName)
                    .Add(Record.SurfaceComponentName,Record.Generation);
        }
}

bool UMD_DayPersistenceSubsystem::TryGetSavedContaminationLayout(
    const UMD_ScrapeSurfaceComponent* Surface, FGuid& OutLayout) const
{
    if (!Surface || !Surface->GetOwner() || !IsContaminationContextReady(Surface->GetWorld())) return false;
    const auto* Components = SavedContaminationLayouts.Find(Surface->GetOwner()->GetFName());
    // 저장 기록과 동일한 실제 컴포넌트 이름으로 표면 배치 식별자 조회
    const FGuid* Layout = Components != nullptr ? Components->Find(Surface->GetFName()) : nullptr;
    if (!Layout) return false;
    OutLayout = *Layout;
    return true;
}

bool UMD_DayPersistenceSubsystem::TryGetSavedContaminationGeneration(const UMD_ScrapeSurfaceComponent* Surface, FMD_ContaminationGeneration& OutGeneration) const
{
    // 유효한 현재 월드의 표면과 소유 액터만 조회 허용
    if (IsValid(Surface) == false || IsValid(Surface->GetOwner()) == false || IsContaminationContextReady(Surface->GetWorld()) == false)
    {
        // 저장된 생성 정보를 연결할 표면 부재 반환
        return false;
    }
    // 소유 액터별로 보관한 컴포넌트 생성 기록 조회
    const TMap<FName, FMD_ContaminationGeneration>* Components = SavedContaminationGenerations.Find(Surface->GetOwner()->GetFName());
    // 현재 컴포넌트의 실제 이름으로 생성 정보 조회
    const FMD_ContaminationGeneration* Generation = Components != nullptr ? Components->Find(Surface->GetFName()) : nullptr;
    // 기록이 없는 표면에는 현재 신규 생성 정책을 사용하도록 조회 실패 반환
    if (Generation == nullptr)
    {
        // 대응하는 생성 정보 부재 반환
        return false;
    }
    // 확인한 생성 정보를 호출자에게 전달
    OutGeneration = *Generation;
    // 현재 형식의 생성 정보 조회 성공 반환
    return true;
}

const FMD_WallContaminationRecord* UMD_DayPersistenceSubsystem::FindDeferredContaminationRecord(const UMD_ScrapeSurfaceComponent* Surface) const
{
    // 현재 월드에 속한 유효한 표면과 소유 액터만 조회 허용
    if (IsValid(Surface) == false || IsValid(Surface->GetOwner()) == false || IsContaminationContextReady(Surface->GetWorld()) == false)
    {
        // 분할 복원 기록을 연결할 표면 부재 반환
        return nullptr;
    }
    // 소유 액터별 분할 복원 기록 조회
    const auto* Components = DeferredContaminationRecords.Find(Surface->GetOwner()->GetFName());
    // 현재 저장 형식의 실제 컴포넌트 이름으로 분할 복원 기록 조회
    return Components != nullptr ? Components->Find(Surface->GetFName()) : nullptr;
}

bool UMD_DayPersistenceSubsystem::ShouldDeferContaminationGeneration(const UMD_ScrapeSurfaceComponent* Surface) const
{
    // 저장 복원에 의한 생성 지연이 필요한 경우에만 기록 조회
    if (bDeferSavedContaminationGeneration == false)
    {
        // 일반 월드 시작에서 표면 생성 허용
        return false;
    }
    // 현재 표면에 대응하는 생성 기록 존재 여부 확인용 데이터
    FMD_ContaminationGeneration Saved;
    // 실제 저장 기록을 가진 표면만 생성 시점 지연
    return TryGetSavedContaminationGeneration(Surface, Saved);
}

void UMD_DayPersistenceSubsystem::CaptureSnapshot(UWorld* World)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_DayPersistence_CaptureSnapshot);
    if (!World || World->GetNetMode() == NM_Client)
    {
        return;
    }

    AMD_GameStateBase* GameState = World->GetGameState<AMD_GameStateBase>();
    if (!IsValid(GameState))
    {
        return;
    }

    Snapshot = FMD_DaySnapshot();
    Snapshot.WorldName = World->GetFName();
    Snapshot.bHasContaminationSessionSeed=TryGetContaminationSessionSeed(World,Snapshot.ContaminationSessionSeed);
    Snapshot.CompletedDay = GameState->GetCurrentDay();
    Snapshot.Score = GameState->GetScore();
    Snapshot.bWarningIssued = GameState->IsInspectionWarned();
    Snapshot.CrewCount = GameState->GetContractCrewCount();
    Snapshot.ContractDifficulty = GameState->GetContractDifficulty();
    Snapshot.bEmergencyDay = GameState->IsEmergencyDay();

    for (TActorIterator<AMD_CuttableWall> It(World); It; ++It)
    {
        if (It->IsAwaitingSupportCollapse())
            Snapshot.PendingCollapseWallNames.Add(It->GetFName());
        else if (It->CanCut())
            Snapshot.SurvivingWallNames.Add(It->GetFName());
        else
            continue; // Already collapsed/cut; waiting only for the short multicast grace period.
        if (It->IsSupported())
        {
            Snapshot.SupportedWallNames.Add(It->GetFName());
        }
    }

    TArray<UMD_ScrapeSurfaceComponent*> ScrapeSurfaces;
    GatherScrapeSurfaces(World, ScrapeSurfaces);
    for (UMD_ScrapeSurfaceComponent* const Surface : ScrapeSurfaces)
    {
        AActor* const SurfaceOwner = Surface->GetOwner();
        if (!IsValid(SurfaceOwner))
        {
            continue;
        }

        // 생성 기준값과 달라진 오염 셀만 기록. 밤샘 분출로 총량이 오른 셀도 포함된다
        FMD_WallContaminationRecord ContaminationRecord;
        ContaminationRecord.WallName = SurfaceOwner->GetFName();
        // 기본 철거벽을 포함한 실제 표면 컴포넌트 이름 기록
        ContaminationRecord.SurfaceComponentName = Surface->GetFName();
        ContaminationRecord.LayoutVersion = Surface->GetContaminationLayoutVersion();
        ContaminationRecord.LayoutId = Surface->GetContaminationLayoutId();
        ContaminationRecord.CarrierScale = Surface->GetContaminationCarrierWeightScale();
        ContaminationRecord.GridDimensions = Surface->GetContaminationGridDimensions();
        ContaminationRecord.Generation = Surface->GetContaminationGeneration();
        Surface->ExportContaminationDeltas(
            ContaminationRecord.CellIndices,
            ContaminationRecord.InitialAmounts,
            ContaminationRecord.RemainingAmounts,
            ContaminationRecord.TransferredAmounts,
            ContaminationRecord.TransferredPatternDensities,
            ContaminationRecord.CoveredFractions);
        TArray<uint8> BoundaryBytes;
        if (!Surface->ExportContaminationBoundary(BoundaryBytes)
            || !ContaminationRecord.BoundaryPayload.Set(MoveTemp(BoundaryBytes)))
        { Snapshot=FMD_DaySnapshot(); return; }
        if (ContaminationRecord.Generation.IsValid() || ContaminationRecord.CellIndices.Num() > 0 || ContaminationRecord.BoundaryPayload.Num()>0)
        {
            if (!ContaminationRecord.CompactCells())
            {
                Snapshot = FMD_DaySnapshot();
                return;
            }
            Snapshot.WallContaminations.Add(MoveTemp(ContaminationRecord));
        }
    }

    for (TActorIterator<AMD_SupportBrace> It(World); It; ++It)
    {
        FMD_BraceRecord Record;
        Record.BraceClass = It->GetClass();
        Record.Transform = It->GetActorTransform();
        if (AMD_CuttableWall* Wall = It->GetSupportedWall())
        {
            Record.SupportedWallName = Wall->GetFName();
        }
        for (const TWeakObjectPtr<AMD_CuttableWall>& Wall : It->GetSupportedWalls())
        {
            if (Wall.IsValid()) Record.SupportedWallNames.AddUnique(Wall->GetFName());
        }
        Record.bWeakened = It->IsWeakened();
        Snapshot.Braces.Add(Record);
    }

    for (TActorIterator<AMD_SampleItem> It(World); It; ++It)
    {
        FMD_SampleRecord Record;
        Record.ActorName = It->GetFName();
        Record.Transform = It->GetActorTransform();
        Record.bDamaged = It->bDamaged;
        Snapshot.Samples.Add(Record);
    }

    for (TActorIterator<AMD_WallPiece> It(World); It; ++It)
    {
        FMD_PieceRecord Record;
        Record.PieceClass = It->GetClass();
        Record.Transform = It->GetActorTransform();
        Record.bContaminated = It->bContaminated;
        Snapshot.Pieces.Add(Record);
    }

	// 보급 통은 액터 이름이 아니라 stable ID와 원래 보급기 이름으로 전체 액체 원장을 기록한다.
	if (!GatherPersistentTrashBinRecords(World, Snapshot.TrashBins))
	{
		Snapshot = FMD_DaySnapshot();
		return;
	}

    for (TActorIterator<AMD_SectorCloseoutMarker> It(World); It; ++It)
    {
        if (It->IsClosed())
        {
            Snapshot.ClosedMarkerNames.Add(It->GetFName());
        }
    }

    // 새 게임별 런타임 시드와 어제 떼어낸 순번. 배치기는 레벨 배치 액터라 이름이
    // 안정적이다. 랜덤 시드는 제거된 따개비가 없어도 반드시 기록해야 재방문 때
    // 같은 배치를 재구성할 수 있다.
    for (TActorIterator<AMD_BarnacleSpawner> It(World); It; ++It)
    {
        const TSet<int32>& RemovedIndices = It->GetRemovedSpawnIndices();
        int32 RuntimePlacementSeed = 0;
        const bool bHasRuntimePlacementSeed =
            It->TryGetRuntimePlacementSeed(RuntimePlacementSeed);
        // 고정 시드와 미제거 상태도 누락 없이 복원할 배치기 기록
        FMD_BarnacleRemovalRecord RemovalRecord;
        RemovalRecord.SpawnerName = It->GetFName();
        RemovalRecord.bHasRuntimePlacementSeed = bHasRuntimePlacementSeed;
        RemovalRecord.RuntimePlacementSeed = RuntimePlacementSeed;
        RemovalRecord.RemovedSpawnIndices = RemovedIndices.Array();
        RemovalRecord.RemovedSpawnIndices.Sort();
        Snapshot.BarnacleRemovals.Add(RemovalRecord);
    }

    if (const UMD_ContaminationDebrisSubsystem* Debris = World->GetSubsystem<UMD_ContaminationDebrisSubsystem>())
    {
        Debris->CapturePendingDrops(Snapshot.PendingContaminationDrops);
    }
    FMD_SurfaceStainPersistence::Capture(World, Snapshot.SurfaceStains);
    Snapshot.bValid = true;
}

bool UMD_DayPersistenceSubsystem::BuildWorldSaveData(UWorld* World, FMD_WorldData& OutWorldData)
{
    // 클라이언트 월드나 준비되지 않은 월드에서 로컬 상태를 저장하지 않기 위한 검사
    if (IsValid(World) == false || World->GetNetMode() == NM_Client)
    {
        return false;
    }

    // 파일 저장 수집이 일차 경계용 메모리 스냅샷을 변경하지 않도록 하는 임시 보존
    FMD_DaySnapshot PreservedSnapshot = MoveTemp(Snapshot);
    Snapshot = FMD_DaySnapshot();
    CaptureSnapshot(World);

    if (Snapshot.bValid == false)
    {
        Snapshot = MoveTemp(PreservedSnapshot);
        return false;
    }

    // 파일 직렬화 구조에 현재 월드 변경 상태를 복사하는 처리
    OutWorldData = FMD_WorldData();
    OutWorldData.bHasContaminationSessionSeed=Snapshot.bHasContaminationSessionSeed;
    OutWorldData.ContaminationSessionSeed=Snapshot.ContaminationSessionSeed;
    OutWorldData.SurvivingWallNames = MoveTemp(Snapshot.SurvivingWallNames);
    OutWorldData.SupportedWallNames = MoveTemp(Snapshot.SupportedWallNames);
    OutWorldData.PendingCollapseWallNames = MoveTemp(Snapshot.PendingCollapseWallNames);
    OutWorldData.Braces = MoveTemp(Snapshot.Braces);
    OutWorldData.Samples = MoveTemp(Snapshot.Samples);
    OutWorldData.Pieces = MoveTemp(Snapshot.Pieces);
	OutWorldData.TrashBins = MoveTemp(Snapshot.TrashBins);
    OutWorldData.WallContaminations = MoveTemp(Snapshot.WallContaminations);
    OutWorldData.PendingContaminationDrops = MoveTemp(Snapshot.PendingContaminationDrops);
    OutWorldData.SurfaceStains = MoveTemp(Snapshot.SurfaceStains);
    OutWorldData.BarnacleRemovals = MoveTemp(Snapshot.BarnacleRemovals);
    OutWorldData.ClosedMarkerNames = MoveTemp(Snapshot.ClosedMarkerNames);

    // 일차 경계 복원 흐름이 기존 상태를 계속 사용하도록 하는 메모리 스냅샷 복구
    Snapshot = MoveTemp(PreservedSnapshot);

    return true;
}

bool UMD_DayPersistenceSubsystem::ApplyWorldSaveData(UWorld* World, const FMD_WorldData& WorldData)
{
	// 파일 데이터 적용을 서버 권위 월드로 제한하고 손상 배열의 부분 적용을 막기 위한 검사
	if (IsValid(World) == false || World->GetNetMode() == NM_Client || WorldData.IsStructurallyValid() == false)
    {
        return false;
    }

	// 벽 등 다른 월드 상태를 바꾸기 전에 모든 통 class·보급기 연결을 먼저 확인한다.
	FMD_TrashBinRestoreContext TrashBinRestoreContext;
	if (!BuildTrashBinRestoreContext(
			World,
			WorldData.TrashBins,
			TrashBinRestoreContext))
	{
		return false;
	}

    TMap<FName, AMD_CuttableWall*> WallsByName;
    for (TActorIterator<AMD_CuttableWall> It(World); It; ++It)
        WallsByName.Add(It->GetFName(), *It);
    const TSet<FName> PendingLoads(WorldData.PendingCollapseWallNames);
    for (const FName WallName : PendingLoads)
    {
        AMD_CuttableWall* const* Wall = WallsByName.Find(WallName);
        if (!Wall || !IsValid(*Wall) || (*Wall)->GetWallType() != EMD_WallType::Strut)
        {
            UE_LOG(LogTemp, Error, TEXT("Cannot restore pending strut load: %s"), *WallName.ToString());
            return false;
        }
    }

    PrepareContaminationContext(World,&WorldData);
	// 저장 시드는 초기 레벨 지오메트리가 그대로 있을 때 먼저 적용한다. 철거된 벽이나
	// 동적 액터를 복원한 뒤 재배치하면 같은 시드라도 Trace 후보가 달라질 수 있다.
	TMap<FName, const FMD_BarnacleRemovalRecord*> BarnacleStatesBySpawnerName;
	for (const FMD_BarnacleRemovalRecord& Record : WorldData.BarnacleRemovals)
	{
		BarnacleStatesBySpawnerName.Add(Record.SpawnerName, &Record);
	}

	for (TActorIterator<AMD_BarnacleSpawner> It(World); It; ++It)
	{
		if (const FMD_BarnacleRemovalRecord* const* Record =
				BarnacleStatesBySpawnerName.Find(It->GetFName());
			Record != nullptr)
		{
            // 현재 저장 기록의 시드와 제거 이력을 함께 적용하고 실패 전파
            if (It->RestorePersistentState((*Record)->RemovedSpawnIndices, (*Record)->bHasRuntimePlacementSeed, (*Record)->RuntimePlacementSeed) == false)
            {
                // 필수 런타임 시드가 없는 배치기의 복원 중단
                return false;
            }
		}
	}
    // 어제 철거된 벽 제거 (조각 스폰·점수 없이 통로만 유지)
    const TSet<FName> Surviving(WorldData.SurvivingWallNames);
    for (const TPair<FName, AMD_CuttableWall*>& Pair : WallsByName)
    {
        if (PendingLoads.Contains(Pair.Key))
            Pair.Value->RestorePendingSupportCollapse();
        else if (!Surviving.Contains(Pair.Key))
            Pair.Value->Destroy();
    }

    // 철거된 벽을 제거한 뒤 살아 있는 모든 표면을 복원용 키로 묶는다.
    TArray<UMD_ScrapeSurfaceComponent*> ScrapeSurfaces;
    GatherScrapeSurfaces(World, ScrapeSurfaces);
    TMap<FName, TArray<UMD_ScrapeSurfaceComponent*>> SurfacesByOwnerName;
    for (UMD_ScrapeSurfaceComponent* const Surface : ScrapeSurfaces)
    {
        if (AActor* const Owner = Surface->GetOwner(); IsValid(Owner))
        {
            SurfacesByOwnerName.FindOrAdd(Owner->GetFName()).Add(Surface);
        }
    }

    // 지지대 설치 상태 복원
    for (const FName& WallName : WorldData.SupportedWallNames)
    {
        if (AMD_CuttableWall* const* Wall = WallsByName.Find(WallName); Wall && IsValid(*Wall))
        {
            (*Wall)->SetSupported(true);
        }
    }

    // 어제 긁은 오염 셀 상태 복원. 이후 ApplyOvernightWorsening이 이 위에 소량 재부착
    for (const FMD_WallContaminationRecord& Record : WorldData.WallContaminations)
    {
        if (UMD_ScrapeSurfaceComponent* const Surface = FindSavedScrapeSurface(Record, SurfacesByOwnerName); Surface != nullptr)
        {
            if (Record.LayoutVersion == 1 && !Surface->SelectCompatibleContaminationLayout(Record.LayoutId))
            {
                UE_LOG(LogTemp, Error, TEXT("Cannot restore exact saved contamination layout for %s/%s."),
                    *Record.WallName.ToString(), *Record.SurfaceComponentName.ToString());
                return false;
            }
            if (!Surface->CanRestoreContaminationLayout(
                    Record.LayoutVersion, Record.LayoutId, Record.GridDimensions)
                || (Record.LayoutVersion == 1 && !FMD_ScrapeSurfaceLayout::AreCarrierScalesCompatible(
                    Record.CarrierScale, Surface->GetContaminationCarrierWeightScale())))
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("Skipped contamination restore for %s/%s: saved surface layout no longer matches."),
                    *Record.WallName.ToString(), *Record.SurfaceComponentName.ToString());
                continue;
            }
            // 로딩 중 분할 복원을 완료한 표면의 중복 복원 방지
            if (auto* Initializer = World->GetSubsystem<UMD_ScrapeInitializationSubsystem>(); IsValid(Initializer) == true)
            {
                // 현재 저장 기록과 일치하는 사전 복원 결과 소비
                if (Initializer->ConsumePreparedSavedState(*Surface, Record) == true)
                {
                    // 이미 적용된 셀과 회수 원장을 유지하고 다음 표면 처리
                    continue;
                }
            }
            // 모든 현재 표면 기록에 포함된 생성 기준을 셀 복원 전에 적용
            if (Surface->RestoreContaminationGeneration(Record.Generation, true) == false) return false;
            // Restore scratch belongs to this wall only, never all walls or all save slots.
            FMD_WallContaminationRecord Expanded;
            if (!Record.ExpandCells(Expanded)) return false;
            Surface->RestoreContaminationCells(
                Expanded.CellIndices,
                Expanded.InitialAmounts,
                Expanded.RemainingAmounts,
                Expanded.TransferredAmounts,
                Expanded.TransferredPatternDensities,
                Expanded.CoveredFractions);
            if (!Surface->RestoreContaminationBoundary(Record.BoundaryPayload.Get())) return false;
        }
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

    // 지지대 액터 재스폰 + 벽 재연결
    for (const FMD_BraceRecord& Record : WorldData.Braces)
    {
        if (Record.BraceClass.Get() == nullptr)
        {
            continue;
        }

        // 스폰 스케일은 루트 기본 스케일과 곱해지므로 위치·회전만 주고 스케일은 절대값으로 대입
        const FTransform SpawnTransform(Record.Transform.GetRotation(), Record.Transform.GetLocation());
        AMD_SupportBrace* Brace = World->SpawnActor<AMD_SupportBrace>(Record.BraceClass, SpawnTransform, SpawnParams);
        if (Brace != nullptr)
        {
            Brace->SetActorScale3D(Record.Transform.GetScale3D());

            Brace->RestoreWeakenedState(Record.bWeakened);
            TArray<TWeakObjectPtr<AMD_CuttableWall>> LinkedWalls;
            TArray<FName> WallNames = Record.SupportedWallNames;
            if (WallNames.IsEmpty() && !Record.SupportedWallName.IsNone())
                WallNames.Add(Record.SupportedWallName);
            for (const FName WallName : WallNames)
            {
                AMD_CuttableWall* const* Wall = WallsByName.Find(WallName);
                if (Wall && IsValid(*Wall)) LinkedWalls.Add(*Wall);
            }
            Brace->SetSupportedWalls(LinkedWalls);
        }
    }

    // 회수된 샘플 제거, 남은 샘플은 위치·손상 복원
    TMap<FName, const FMD_SampleRecord*> SampleRecords;
    for (const FMD_SampleRecord& Record : WorldData.Samples)
    {
        SampleRecords.Add(Record.ActorName, &Record);
    }

    for (TActorIterator<AMD_SampleItem> It(World); It; ++It)
    {
        if (const FMD_SampleRecord* const* Found = SampleRecords.Find(It->GetFName()); Found != nullptr)
        {
            It->SetActorTransform((*Found)->Transform);
            if ((*Found)->bDamaged == true)
            {
                It->SetDamaged();
            }
        }
        else
        {
            It->Destroy();
        }
    }

    // 미수거 잔해 재스폰 (스케일은 Transform에 포함)
    for (const FMD_PieceRecord& Record : WorldData.Pieces)
    {
        if (Record.PieceClass.Get() == nullptr)
        {
            continue;
        }

        const FTransform SpawnTransform(Record.Transform.GetRotation(), Record.Transform.GetLocation());
        AMD_WallPiece* Piece = World->SpawnActor<AMD_WallPiece>(Record.PieceClass, SpawnTransform, SpawnParams);
        if (Piece != nullptr)
        {
            Piece->SetActorScale3D(Record.Transform.GetScale3D());
            Piece->SetContaminated(Record.bContaminated);
        }
    }

    // 어제 마감한 섹터는 마감 상태 유지 (점수 재지급 없음 — Score 캐리오버에 이미 포함)
    const TSet<FName> ClosedMarkers(WorldData.ClosedMarkerNames);
    for (TActorIterator<AMD_SectorCloseoutMarker> It(World); It; ++It)
    {
        if (ClosedMarkers.Contains(It->GetFName()) == true)
        {
            It->RestoreClosed();
        }
    }

	if (!RestorePersistentTrashBins(
			World,
			WorldData.TrashBins,
			TrashBinRestoreContext))
	{
		return false;
	}

    if (UMD_ContaminationDebrisSubsystem* Debris = World->GetSubsystem<UMD_ContaminationDebrisSubsystem>())
    {
        if (!Debris->RestorePendingDrops(WorldData.PendingContaminationDrops)) return false;
    }
    else if (!WorldData.PendingContaminationDrops.IsEmpty()) return false;
    if (!FMD_SurfaceStainPersistence::Restore(World, WorldData.SurfaceStains)) return false;

    // 점수 캐리오버 + Day 진행
    return true;
}

bool UMD_DayPersistenceSubsystem::RestoreSnapshot(UWorld* World)
{
    // 일차 경계 스냅샷과 대상 서버 월드의 유효성 검사
    if (Snapshot.bValid == false || IsValid(World) == false || World->GetNetMode() == NM_Client)
    {
        return false;
    }

    // 다른 맵의 일차 스냅샷이 현재 월드에 적용되지 않도록 하는 맵 검사
    if (World->GetFName() != Snapshot.WorldName)
    {
        return false;
    }

    // 파일 저장과 일차 경계 복원이 같은 월드 상태 적용 경로를 사용하기 위한 데이터 변환
    FMD_WorldData WorldData;
    WorldData.bHasContaminationSessionSeed=Snapshot.bHasContaminationSessionSeed;
    WorldData.ContaminationSessionSeed=Snapshot.ContaminationSessionSeed;
    WorldData.SurvivingWallNames = Snapshot.SurvivingWallNames;
    WorldData.SupportedWallNames = Snapshot.SupportedWallNames;
    WorldData.PendingCollapseWallNames = Snapshot.PendingCollapseWallNames;
    WorldData.Braces = Snapshot.Braces;
    WorldData.Samples = Snapshot.Samples;
    WorldData.Pieces = Snapshot.Pieces;
	WorldData.TrashBins = Snapshot.TrashBins;
    WorldData.WallContaminations = Snapshot.WallContaminations;
    WorldData.PendingContaminationDrops = Snapshot.PendingContaminationDrops;
    WorldData.SurfaceStains = Snapshot.SurfaceStains;
    WorldData.BarnacleRemovals = Snapshot.BarnacleRemovals;
    WorldData.ClosedMarkerNames = Snapshot.ClosedMarkerNames;

    if (ApplyWorldSaveData(World, WorldData) == false)
    {
        return false;
    }

    // 다음 일차에 필요한 점수와 계약 진행 상태를 서버 GameState에 정확히 반영하는 처리
    AMD_GameStateBase* GameState = World->GetGameState<AMD_GameStateBase>();
    if (IsValid(GameState) == true)
    {
        const int32 ScoreDelta = Snapshot.Score - GameState->GetScore();
        if (ScoreDelta != 0)
        {
            GameState->AddScoreDelta(ScoreDelta);
        }

        GameState->SetInspectionWarned(Snapshot.bWarningIssued);
        GameState->SetContractCrewCount(Snapshot.CrewCount);
        GameState->SetContractDifficulty(Snapshot.ContractDifficulty);
        GameState->SetEmergencyDay(Snapshot.bEmergencyDay);
        GameState->SetCurrentDay(Snapshot.CompletedDay + 1);
    }

    ApplyOvernightWorsening(World);

    return true;
}

void UMD_DayPersistenceSubsystem::ApplyOvernightWorsening(UWorld* World)
{
    // 약해진 지지대 1개
    TArray<AMD_SupportBrace*> Braces;
    for (TActorIterator<AMD_SupportBrace> It(World); It; ++It)
    {
        if (!It->IsWeakened()) Braces.Add(*It);
    }
    if (Braces.Num() > 0)
    {
        Braces[FMath::RandRange(0, Braces.Num() - 1)]->Weaken();
    }

    // 살아남은 일반 표면과 철거벽 표면 일부에 소형 오염 재부착
    TArray<UMD_ScrapeSurfaceComponent*> ScrapeSurfaces;
    GatherScrapeSurfaces(World, ScrapeSurfaces);
    for (UMD_ScrapeSurfaceComponent* const Surface : ScrapeSurfaces)
    {
        if (FMath::FRand() < 0.3f)
        {
            Surface->ReapplyContaminationPatch(0.6f, 3);
        }
    }

    // 따개비 소량 재부착 (기획 03 §3).
    // 떼어낸 것을 되살리지 않고 새 자리에 돋운다. 어제 한 청소를 무의미하게 만들지
    // 않으면서 "밤새 조금 나빠진" 연출만 남긴다.
    // 살아 있는 개수는 배치기의 상한을 넘지 않으므로 3일 동안 05 배치 예산을 넘지 않는다.
    // 개수·확률은 05 수치 기획에 대응 항목이 없어 임시값이다.
    const int32 CurrentDay = Snapshot.CompletedDay + 1;
    for (TActorIterator<AMD_BarnacleSpawner> It(World); It; ++It)
    {
        if (FMath::FRand() < 0.5f)
        {
            // Day마다 시드를 어긋내야 매번 다른 자리에 돋는다.
            It->GrowBarnacles(1, CurrentDay * 977);
        }
    }

    // 잔해 위치 소폭 이동 (동선이 조금 더 지저분해짐)
    for (TActorIterator<AMD_WallPiece> It(World); It; ++It)
    {
        const FVector Jitter(
            FMath::FRandRange(-30.f, 30.f), FMath::FRandRange(-30.f, 30.f), 0.f);
        It->AddActorWorldOffset(Jitter, true);
    }
}

void UMD_DayPersistenceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    // 엔진 서브시스템 초기화 수행
    Super::Initialize(Collection);
    // 저장 관리자보다 먼저 구독을 시도하지 않도록 초기화 의존성 지정
    Collection.InitializeDependency<UMD_SaveSlotManager>();
    // 기능별 데이터 소유자를 등록할 저장 관리자 조회
    UMD_SaveSlotManager* SaveSlotManager = GetGameInstance()->GetSubsystem<UMD_SaveSlotManager>();
    // 저장 관리자가 유효한 경우에만 현재 서브시스템 연결
    if (IsValid(SaveSlotManager) == true)
    {
        // 소유 데이터 수집 이벤트 구독
        SaveSlotManager->OnCollectGameData.AddUniqueDynamic(this, &ThisClass::CollectGameData);
        // 소유 데이터 적용 이벤트 구독
        SaveSlotManager->OnOverwriteGameData.AddUniqueDynamic(this, &ThisClass::OverwriteGameData);
    }
}

void UMD_DayPersistenceSubsystem::Deinitialize()
{
    // 종료 중인 게임 인스턴스의 저장 관리자 조회 준비
    UGameInstance* GameInstance = GetGameInstance();
    // 게임 인스턴스가 남아 있는 경우에만 관리자 조회
    UMD_SaveSlotManager* SaveSlotManager = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_SaveSlotManager>() : nullptr;
    // 종료한 서브시스템이 다음 저장 요청에 참여하지 않도록 구독 해제
    if (IsValid(SaveSlotManager) == true)
    {
        // 소유 데이터 수집 연결 해제
        SaveSlotManager->OnCollectGameData.RemoveDynamic(this, &ThisClass::CollectGameData);
        // 소유 데이터 적용 연결 해제
        SaveSlotManager->OnOverwriteGameData.RemoveDynamic(this, &ThisClass::OverwriteGameData);
    }
    // 엔진 서브시스템 종료 수행
    Super::Deinitialize();
}

void UMD_DayPersistenceSubsystem::CollectGameData(FMD_GameData& GameData, bool& bWasSuccessful)
{
    // 현재 서버의 파괴 및 오염 상태를 수집할 월드 필드
    FMD_WorldData WorldData;
    // 현재 월드의 상태 수집 결과 확인
    if (BuildWorldSaveData(GetWorld(), WorldData) == false)
    {
        // 다른 구독자의 성공으로 덮어쓰지 않는 현재 요청의 실패 반영
        bWasSuccessful = false;
        // 실패한 기능의 필드 키 기록
        UE_LOG(LogTemp, Warning, TEXT("저장 데이터 처리 실패: %s"), *FMD_WorldData::DataFieldKey.ToString());
        // 월드 수집 실패 시 저장 필드 등록 차단
        return;
    }
    // 수집을 마친 월드 데이터를 전달된 저장 구조체에 등록
    if (GameData.AddDataField(FMD_WorldData::DataFieldKey, WorldData) == false)
    {
        // 다른 구독자의 성공으로 덮어쓰지 않는 현재 요청의 실패 반영
        bWasSuccessful = false;
        // 실패한 기능의 필드 키 기록
        UE_LOG(LogTemp, Warning, TEXT("저장 데이터 처리 실패: %s"), *FMD_WorldData::DataFieldKey.ToString());
    }
}

void UMD_DayPersistenceSubsystem::OverwriteGameData(const FMD_GameData& GameData, bool& bWasSuccessful)
{
    // 현재 회차 이동에서 의도적으로 제외한 월드 필드는 적용 생략
    if (GameData.DataFields.Contains(FMD_WorldData::DataFieldKey) == false)
    {
        // 새 출항의 월드 상태 유지
        return;
    }
    // 전달된 읽기 전용 데이터에서 조회할 담당 필드
    FMD_WorldData SaveData;
    // 전달된 데이터에서 자신의 태그에 해당하는 필드 조회
    if (GameData.GetDataField(FMD_WorldData::DataFieldKey, SaveData) == false)
    {
        // 다른 구독자의 성공으로 덮어쓰지 않는 현재 요청의 실패 반영
        bWasSuccessful = false;
        // 실패한 기능의 필드 키 기록
        UE_LOG(LogTemp, Warning, TEXT("저장 데이터 처리 실패: %s"), *FMD_WorldData::DataFieldKey.ToString());
        // 필드 조회 실패 시 복원 API 호출 차단
        return;
    }
    // 조회를 마친 필드를 기존 복원 API에 적용
    if (ApplyWorldSaveData(GetWorld(), SaveData) == false)
    {
        // 다른 구독자의 성공으로 덮어쓰지 않는 현재 요청의 실패 반영
        bWasSuccessful = false;
        // 실패한 기능의 필드 키 기록
        UE_LOG(LogTemp, Warning, TEXT("저장 데이터 처리 실패: %s"), *FMD_WorldData::DataFieldKey.ToString());
    }
}

UMD_ScrapeSurfaceComponent* UMD_DayPersistenceSubsystem::FindSavedScrapeSurface(const FMD_WallContaminationRecord& Record, TMap<FName, TArray<UMD_ScrapeSurfaceComponent*>>& SurfacesByOwnerName)
{
    // 저장된 액터 식별자에 속한 현재 표면 목록 조회
    TArray<UMD_ScrapeSurfaceComponent*>* OwnerSurfaces = SurfacesByOwnerName.Find(Record.WallName);
    // 제거되거나 저장 당시 존재하지 않은 소유 액터의 표면 조회 종료
    if (OwnerSurfaces == nullptr || Record.SurfaceComponentName.IsNone() == true)
    {
        // 정확한 표면 식별자가 없는 기록 거부
        return nullptr;
    }
    // 현재 형식에 기록된 컴포넌트 이름으로 복원 대상을 특정
    for (UMD_ScrapeSurfaceComponent* Surface : *OwnerSurfaces)
    {
        // 유효하고 활성화된 동일 이름의 표면만 복원 대상으로 반환
        if (IsValid(Surface) == true && Surface->IsContaminationEnabled() == true && Surface->GetFName() == Record.SurfaceComponentName)
        {
            // 현재 저장 키와 일치하는 복원 대상 반환
            return Surface;
        }
    }
    // 현재 월드에서 대응하는 표면을 찾지 못한 결과 반환
    return nullptr;
}
