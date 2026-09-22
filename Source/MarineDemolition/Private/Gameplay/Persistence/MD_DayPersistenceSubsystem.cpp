#include "Gameplay/Persistence/MD_DayPersistenceSubsystem.h"
#include "Gameplay/Persistence/MD_WorldActorPersistence.h"
#include "Gameplay/Persistence/MD_TrashBinPersistence.h"
#include "Gameplay/Scrape/MD_ScrapeInitializationSubsystem.h"
#include "Data/SaveGame/MD_SaveSlotManager.h"
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
#include "World/Wall/MD_CuttableWall.h"
#include "World/Wall/MD_WallPiece.h"

namespace
{
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
    // 이전 맵의 구역 식별 형식 초기화
    bUseQualifiedActorNames = false;
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
    // 해당 기록을 작성한 형식에 따라 표면 식별 방법 결정
    bUseQualifiedActorNames = SavedWorld != nullptr ? SavedWorld->ActorSnapshot.bCaptured : Snapshot.ActorSnapshot.bCaptured;
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
    const auto* Components = SavedContaminationLayouts.Find(bUseQualifiedActorNames == true ? FMD_WorldActorPersistence::GetActorKey(Surface->GetOwner()) : Surface->GetOwner()->GetFName());
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
    const TMap<FName, FMD_ContaminationGeneration>* Components = SavedContaminationGenerations.Find(bUseQualifiedActorNames == true ? FMD_WorldActorPersistence::GetActorKey(Surface->GetOwner()) : Surface->GetOwner()->GetFName());
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
    const auto* Components = DeferredContaminationRecords.Find(bUseQualifiedActorNames == true ? FMD_WorldActorPersistence::GetActorKey(Surface->GetOwner()) : Surface->GetOwner()->GetFName());
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
            // 붕괴 대기 벽의 배치 구역까지 구분하여 보관
            Snapshot.PendingCollapseWallNames.Add(FMD_WorldActorPersistence::GetActorKey(*It));
        else if (It->CanCut())
            // 같은 이름의 다른 구역 벽과 분리한 생존 기록
            Snapshot.SurvivingWallNames.Add(FMD_WorldActorPersistence::GetActorKey(*It));
        else
            continue; // Already collapsed/cut; waiting only for the short multicast grace period.
        if (It->IsSupported())
        {
            // 실제로 지지된 벽의 배치 구역 보관
            Snapshot.SupportedWallNames.Add(FMD_WorldActorPersistence::GetActorKey(*It));
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
        // 다른 구역의 같은 이름 표면이 서로의 오염 상태를 덮지 않도록 식별
        ContaminationRecord.WallName = FMD_WorldActorPersistence::GetActorKey(SurfaceOwner);
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
            // 지지한 벽의 구역을 포함한 식별자 보관
            Record.SupportedWallName = FMD_WorldActorPersistence::GetActorKey(Wall);
        }
        for (const TWeakObjectPtr<AMD_CuttableWall>& Wall : It->GetSupportedWalls())
        {
            // 여러 구역의 같은 이름 벽을 별도 지지 대상으로 기록
            if (Wall.IsValid() == true)
            {
                // 해당 구역의 정확한 벽 연결 보관
                Record.SupportedWallNames.AddUnique(FMD_WorldActorPersistence::GetActorKey(Wall.Get()));
            }
        }
        Record.bWeakened = It->IsWeakened();
        Snapshot.Braces.Add(Record);
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
	// 구역별 보급기 귀속을 포함한 동적 통의 일괄 수집
	if (FMD_TrashBinPersistence::Capture(World, Snapshot.TrashBins) == false)
	{
		Snapshot = FMD_DaySnapshot();
		return;
	}

    for (TActorIterator<AMD_SectorCloseoutMarker> It(World); It; ++It)
    {
        if (It->IsClosed())
        {
            // 마감한 구역 표식의 배치 레벨까지 구분
            Snapshot.ClosedMarkerNames.Add(FMD_WorldActorPersistence::GetActorKey(*It));
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
        // 따개비 배치기의 구역까지 포함한 제거 이력 식별자
        RemovalRecord.SpawnerName = FMD_WorldActorPersistence::GetActorKey(*It);
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
    // 퀘스트 물품과 쓰레기 및 작업 중 상태를 같은 시점의 스냅샷에 기록
    if (FMD_WorldActorPersistence::Capture(World, Snapshot.ActorSnapshot) == false)
    {
        // 일부 상태만 수집된 저장의 사용 방지
        Snapshot = FMD_DaySnapshot();
        // 불완전한 저장 수집 중단
        return;
    }
    // 전체 월드 수집 완료 표시
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
    // 동적 물품과 진행 중 작업의 파일 저장 데이터 이관
    OutWorldData.ActorSnapshot = MoveTemp(Snapshot.ActorSnapshot);

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

    // 월드 변경 전에 저장된 클래스와 배치 레벨 및 필수 액터 검사
    if (FMD_WorldActorPersistence::CanRestore(World, WorldData.ActorSnapshot) == false)
    {
        // 복원 불가능한 저장의 부분 적용 차단
        return false;
    }

	// 벽 등 다른 월드 상태를 바꾸기 전에 모든 통 class·보급기 연결을 먼저 확인한다.
	FMD_TrashBinRestoreContext TrashBinRestoreContext;
	// 보급기와 통을 한 번씩 조회한 문맥으로 후속 복원 준비
	if (FMD_TrashBinPersistence::BuildRestoreContext(World, WorldData.TrashBins, TrashBinRestoreContext) == false)
	{
		return false;
	}

    TMap<FName, AMD_CuttableWall*> WallsByName;
    for (TActorIterator<AMD_CuttableWall> It(World); It; ++It)
    {
        // 새 기록의 구역 식별자와 기존 기록의 단순 이름을 구분하여 조회표 구성
        WallsByName.Add(WorldData.ActorSnapshot.bCaptured == true ? FMD_WorldActorPersistence::GetActorKey(*It) : It->GetFName(), *It);
    }
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

    // 전체 지지대 목록을 교체할 때 이전 연결이 벽 철거나 감점을 발생시키지 않도록 분리
    for (TActorIterator<AMD_SupportBrace> It(World); It; ++It)
    {
        // 실제 해체 이벤트 없이 기존 지지 연결만 제거
        It->DetachForSaveRestore();
        // 반복 로드에서 지지대가 누적되지 않도록 이전 인스턴스 제거
        if (It->Destroy() == false)
        {
            // 교체 실패를 정상 복원으로 처리하지 않도록 전파
            return false;
        }
    }
    // 저장의 잔해 목록이 현재 월드의 전체 잔해를 대체하도록 처리
    for (TActorIterator<AMD_WallPiece> It(World); It; ++It)
    {
        // 잔해 회수나 보상 없이 이전 인스턴스 제거
        if (It->Destroy() == false)
        {
            // 제거되지 않은 잔해와 저장 기록의 중복 생성 차단
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
				BarnacleStatesBySpawnerName.Find(WorldData.ActorSnapshot.bCaptured == true ? FMD_WorldActorPersistence::GetActorKey(*It) : It->GetFName());
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
            // 구역이 다른 표면의 오염 복원 기록 혼합 방지
            SurfacesByOwnerName.FindOrAdd(WorldData.ActorSnapshot.bCaptured == true ? FMD_WorldActorPersistence::GetActorKey(Owner) : Owner->GetFName()).Add(Surface);
        }
    }

    // 지지된 벽과 지지되지 않은 벽을 모두 저장 상태로 교체
    TSet<FName> SupportedWalls(WorldData.SupportedWallNames);
    // 이전 플레이의 지지 플래그가 남지 않도록 전체 생존 벽 순회
    for (TPair<FName, AMD_CuttableWall*>& Pair : WallsByName)
    {
        // 이미 철거된 액터에 상태를 다시 적용하지 않도록 제한
        if (IsValid(Pair.Value) == true && Pair.Value->IsActorBeingDestroyed() == false)
        {
            // 붕괴나 감점 없이 지지 여부의 양방향 복원
            Pair.Value->RestoreSupportState(SupportedWalls.Contains(Pair.Key));
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

    // 저장된 위치를 충돌 보정으로 이동시키지 않는 생성 정책
    FActorSpawnParameters SpawnParams;
    // 기존 지오메트리와 겹친 저장도 원래 위치로 복원
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    // BP 생성과 BeginPlay 전에 저장된 속성을 적용할 지연 생성
    SpawnParams.bDeferConstruction = true;
    // 루트 기본 배율 대신 저장된 절대 크기 사용
    SpawnParams.TransformScaleMethod = ESpawnActorScaleMethod::OverrideRootScale;
    // 지지대의 약화 상태와 연결을 생명주기 시작 전에 복원
    for (const FMD_BraceRecord& Record : WorldData.Braces)
    {
        // 검증을 통과한 클래스와 저장 트랜스폼으로 인스턴스 생성
        AMD_SupportBrace* Brace = World->SpawnActor<AMD_SupportBrace>(Record.BraceClass, Record.Transform, SpawnParams);
        // 스폰 실패가 월드 복원 성공으로 숨겨지는 상황 차단
        if (IsValid(Brace) == false)
        {
            // 지지대 생성 실패 전파
            return false;
        }
        // 새 인스턴스에 저장된 약화 상태 적용
        Brace->RestoreWeakenedState(Record.bWeakened);
        // 현재 월드의 생존 벽과 지지 연결 구성
        TArray<TWeakObjectPtr<AMD_CuttableWall>> LinkedWalls;
        // 다중 연결과 최초 단일 연결 형식의 공통 처리
        TArray<FName> WallNames = Record.SupportedWallNames;
        // 최초 저장의 단일 연결 보완
        if (WallNames.IsEmpty() == true && Record.SupportedWallName.IsNone() == false)
        {
            // 구형 단일 연결을 같은 순회에 포함
            WallNames.Add(Record.SupportedWallName);
        }
        // 실제 존재하는 벽의 연결 복원
        for (FName WallName : WallNames)
        {
            // 저장된 구역 식별자로 벽 조회
            AMD_CuttableWall** Wall = WallsByName.Find(WallName);
            // 저장 이후 삭제된 벽의 참조 제외
            if (Wall != nullptr && IsValid(*Wall) == true && (*Wall)->IsActorBeingDestroyed() == false)
            {
                // 약화되지 않은 지지대의 연결 후보 보관
                LinkedWalls.Add(*Wall);
            }
        }
        // BP 시작 시 사용할 지지 대상 설정
        Brace->SetSupportedWalls(LinkedWalls);
        // 저장된 크기로 Construction Script와 BeginPlay 실행
        Brace->FinishSpawning(Record.Transform, false, nullptr, ESpawnActorScaleMethod::OverrideRootScale);
        // 생성 스크립트가 액터를 제거한 실패도 복원 결과에 반영
        if (IsValid(Brace) == false || Brace->IsActorBeingDestroyed() == true)
        {
            // 불완전한 지지대 복원 거부
            return false;
        }
        // 생성 스크립트의 기본값 변경 이후에도 저장된 최종 상태 유지
        Brace->RestoreWeakenedState(Record.bWeakened);
        // 생성 스크립트가 수정한 지지 연결 재확정
        Brace->SetSupportedWalls(LinkedWalls);
        // 시각적 기본값으로 변경된 위치와 크기 재확정
        Brace->SetActorTransform(Record.Transform, false, nullptr, ETeleportType::TeleportPhysics);
        // 접속 클라이언트에 최종 상태 복제 요청
        Brace->ForceNetUpdate();
    }

    // 추가 기록이 없던 기존 파일에만 이름 기반 샘플 복원 적용
    if (WorldData.ActorSnapshot.bCaptured == false)
    {
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

    }

    // 미수거 잔해도 저장된 상태로 생명주기 시작
    for (const FMD_PieceRecord& Record : WorldData.Pieces)
    {
        // 루트 기본 배율과 충돌 이동 없이 저장된 잔해 생성
        AMD_WallPiece* Piece = World->SpawnActor<AMD_WallPiece>(Record.PieceClass, Record.Transform, SpawnParams);
        // 생성 실패의 조용한 누락 차단
        if (IsValid(Piece) == false)
        {
            // 잔해 복원 실패 전파
            return false;
        }
        // BeginPlay가 조회할 오염 상태 선행 복원
        Piece->SetContaminated(Record.bContaminated);
        // 저장된 상태와 크기로 생성 과정 완료
        Piece->FinishSpawning(Record.Transform, false, nullptr, ESpawnActorScaleMethod::OverrideRootScale);
        // 생성 중 제거된 잔해를 정상 복원으로 인정하지 않도록 검사
        if (IsValid(Piece) == false || Piece->IsActorBeingDestroyed() == true)
        {
            // 생성 생명주기 실패 전파
            return false;
        }
        // 생성 스크립트 실행 후에도 최종 저장 상태 유지
        Piece->SetContaminated(Record.bContaminated);
        // 생성 스크립트가 변경한 위치와 크기 재확정
        Piece->SetActorTransform(Record.Transform, false, nullptr, ETeleportType::TeleportPhysics);
        // 최종 복원 상태의 네트워크 전송 요청
        Piece->ForceNetUpdate();
    }

    // 저장에 포함되지 않은 마커도 미마감 상태로 되돌릴 조회 집합
    TSet<FName> ClosedMarkers(WorldData.ClosedMarkerNames);
    // 보상 재지급 없이 마감과 미마감을 모두 복원
    for (TActorIterator<AMD_SectorCloseoutMarker> It(World); It; ++It)
    {
        // 구역 식별자를 사용하는 새 기록과 기존 단일 이름의 호환 조회
        FName MarkerKey = WorldData.ActorSnapshot.bCaptured == true ? FMD_WorldActorPersistence::GetActorKey(*It) : It->GetFName();
        // 파일에 없는 완료 상태가 이전 플레이에서 남지 않도록 교체
        It->RestoreClosed(ClosedMarkers.Contains(MarkerKey));
    }

	// 사전 검사에서 확인한 원래 보급기 연결을 재사용하는 통 복원
	if (FMD_TrashBinPersistence::Restore(World, WorldData.TrashBins, TrashBinRestoreContext) == false)
	{
		return false;
	}

    if (UMD_ContaminationDebrisSubsystem* Debris = World->GetSubsystem<UMD_ContaminationDebrisSubsystem>())
    {
        if (!Debris->RestorePendingDrops(WorldData.PendingContaminationDrops)) return false;
    }
    else if (!WorldData.PendingContaminationDrops.IsEmpty()) return false;
    if (!FMD_SurfaceStainPersistence::Restore(World, WorldData.SurfaceStains)) return false;

    // 생존 물품 재생성과 삭제 이력 및 작업 상태 복원의 실패 전파
    return FMD_WorldActorPersistence::Restore(World, WorldData.ActorSnapshot);
}

bool UMD_DayPersistenceSubsystem::PrepareSnapshotStreaming(UWorld* World, bool& bReady)
{
    // 현재 맵과 일치하는 일차 기록만 구역 복원에 사용
    if (Snapshot.bValid == true && IsValid(World) == true && Snapshot.WorldName == World->GetFName())
    {
        // 다음 일차의 물품 복원에 필요한 구역부터 준비
        return FMD_WorldActorPersistence::PrepareStreaming(World, Snapshot.ActorSnapshot, bReady);
    }
    // 일차 기록이 없는 신규 월드의 준비 완료
    bReady = true;
    // 추가 구역 복원이 없는 정상 진입 반환
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
    // 다음 일차에도 같은 물품 및 작업 상태를 유지할 추가 기록 복사
    WorldData.ActorSnapshot = Snapshot.ActorSnapshot;

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
    const FMD_WorldData* SaveData = GameData.FindDataField<FMD_WorldData>(FMD_WorldData::DataFieldKey);
    // 전달된 데이터에서 자신의 태그에 해당하는 필드 조회
    if (SaveData == nullptr)
    {
        // 다른 구독자의 성공으로 덮어쓰지 않는 현재 요청의 실패 반영
        bWasSuccessful = false;
        // 실패한 기능의 필드 키 기록
        UE_LOG(LogTemp, Warning, TEXT("저장 데이터 처리 실패: %s"), *FMD_WorldData::DataFieldKey.ToString());
        // 필드 조회 실패 시 복원 API 호출 차단
        return;
    }
    // 조회를 마친 필드를 기존 복원 API에 적용
    if (ApplyWorldSaveData(GetWorld(), *SaveData) == false)
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
