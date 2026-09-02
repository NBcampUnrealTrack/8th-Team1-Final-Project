#include "Gameplay/Persistence/MD_DayPersistenceSubsystem.h"

#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"
#include "EngineUtils.h"
#include "Game/MD_GameStateBase.h"
#include "World/Contamination/MD_BarnacleSpawner.h"
#include "World/Interaction/MD_SectorCloseoutMarker.h"
#include "World/Sample/MD_SampleItem.h"
#include "World/Support/MD_SupportBrace.h"
#include "World/Wall/MD_CuttableWall.h"
#include "World/Wall/MD_WallPiece.h"
#include "Types/SaveGame/MD_SaveDataTypes.h"

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

    FName GetPersistentSurfaceComponentName(
        const UMD_ScrapeSurfaceComponent* Surface)
    {
        const AMD_CuttableWall* const LegacyWall = Surface
            ? Cast<AMD_CuttableWall>(Surface->GetOwner())
            : nullptr;
        if (LegacyWall
            && LegacyWall->GetScrapeSurfaceComponent() == Surface)
        {
            // 기존 저장은 벽 이름만 가졌다. 기본 철거벽 표면은 그 키를 계속 쓴다.
            return NAME_None;
        }

        return Surface ? Surface->GetFName() : NAME_None;
    }

    UMD_ScrapeSurfaceComponent* FindSavedScrapeSurface(
        const FMD_WallContaminationRecord& Record,
        const TMap<FName, AMD_CuttableWall*>& WallsByName,
        const TMap<FName, TArray<UMD_ScrapeSurfaceComponent*>>& SurfacesByOwnerName)
    {
        if (Record.SurfaceComponentName.IsNone())
        {
            if (AMD_CuttableWall* const* Wall = WallsByName.Find(Record.WallName))
            {
                UMD_ScrapeSurfaceComponent* const Surface = IsValid(*Wall)
                    ? (*Wall)->GetScrapeSurfaceComponent()
                    : nullptr;
                return IsValid(Surface) && Surface->IsContaminationEnabled()
                    ? Surface
                    : nullptr;
            }

            return nullptr;
        }

        const TArray<UMD_ScrapeSurfaceComponent*>* const OwnerSurfaces =
            SurfacesByOwnerName.Find(Record.WallName);
        if (!OwnerSurfaces)
        {
            return nullptr;
        }

        for (UMD_ScrapeSurfaceComponent* const Surface : *OwnerSurfaces)
        {
            if (IsValid(Surface)
				&& Surface->IsContaminationEnabled()
                && Surface->GetFName() == Record.SurfaceComponentName)
            {
                return Surface;
            }
        }

        return nullptr;
    }
}

void UMD_DayPersistenceSubsystem::CaptureSnapshot(UWorld* World)
{
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
    Snapshot.CompletedDay = GameState->GetCurrentDay();
    Snapshot.Score = GameState->GetScore();
    Snapshot.bWarningIssued = GameState->IsInspectionWarned();
    Snapshot.CrewCount = GameState->GetContractCrewCount();
    Snapshot.ContractDifficulty = GameState->GetContractDifficulty();
    Snapshot.bEmergencyDay = GameState->IsEmergencyDay();

    for (TActorIterator<AMD_CuttableWall> It(World); It; ++It)
    {
        Snapshot.SurvivingWallNames.Add(It->GetFName());
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
        ContaminationRecord.SurfaceComponentName =
            GetPersistentSurfaceComponentName(Surface);
        Surface->ExportContaminationDeltas(
            ContaminationRecord.CellIndices,
            ContaminationRecord.InitialAmounts,
            ContaminationRecord.RemainingAmounts,
            ContaminationRecord.TransferredAmounts,
            ContaminationRecord.TransferredPatternDensities,
            ContaminationRecord.CoveredFractions);
        if (ContaminationRecord.CellIndices.Num() > 0)
        {
            Snapshot.WallContaminations.Add(ContaminationRecord);
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
        if (RemovedIndices.IsEmpty() && !bHasRuntimePlacementSeed)
        {
            continue;
        }

        FMD_BarnacleRemovalRecord RemovalRecord;
        RemovalRecord.SpawnerName = It->GetFName();
        RemovalRecord.bHasRuntimePlacementSeed = bHasRuntimePlacementSeed;
        RemovalRecord.RuntimePlacementSeed = RuntimePlacementSeed;
        RemovalRecord.RemovedSpawnIndices = RemovedIndices.Array();
        RemovalRecord.RemovedSpawnIndices.Sort();
        Snapshot.BarnacleRemovals.Add(RemovalRecord);
    }

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
    FMD_DaySnapshot PreservedSnapshot = Snapshot;
    Snapshot = FMD_DaySnapshot();
    CaptureSnapshot(World);

    if (Snapshot.bValid == false)
    {
        Snapshot = MoveTemp(PreservedSnapshot);
        return false;
    }

    // 파일 직렬화 구조에 현재 월드 변경 상태를 복사하는 처리
    OutWorldData = FMD_WorldData();
    OutWorldData.SurvivingWallNames = Snapshot.SurvivingWallNames;
    OutWorldData.SupportedWallNames = Snapshot.SupportedWallNames;
    OutWorldData.Braces = Snapshot.Braces;
    OutWorldData.Samples = Snapshot.Samples;
    OutWorldData.Pieces = Snapshot.Pieces;
    OutWorldData.WallContaminations = Snapshot.WallContaminations;
    OutWorldData.BarnacleRemovals = Snapshot.BarnacleRemovals;
    OutWorldData.ClosedMarkerNames = Snapshot.ClosedMarkerNames;

    // 일차 경계 복원 흐름이 기존 상태를 계속 사용하도록 하는 메모리 스냅샷 복구
    Snapshot = MoveTemp(PreservedSnapshot);

    return true;
}

bool UMD_DayPersistenceSubsystem::ApplyWorldSaveData(UWorld* World, const FMD_WorldData& WorldData) const
{
	// 파일 데이터 적용을 서버 권위 월드로 제한하고 손상 배열의 부분 적용을 막기 위한 검사
	if (IsValid(World) == false || World->GetNetMode() == NM_Client || WorldData.IsStructurallyValid() == false)
    {
        return false;
    }

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
            It->RestorePersistentState(
                (*Record)->RemovedSpawnIndices,
                (*Record)->bHasRuntimePlacementSeed,
                (*Record)->RuntimePlacementSeed);
        }
        else
        {
            // 구버전 세이브는 제거가 없던 스포너 레코드를 만들지 않았다. 새 게임과
            // 달리 WorldData를 적용하는 상황이므로 기존 에디터 시드로 고정해 같은
            // 구형 세이브를 반복 로드할 때마다 위치가 바뀌지 않게 한다.
            It->RestorePersistentState({}, false, 0);
        }
    }

    // 다른 레벨(모선 등)에서는 적용하지 않음
    // 이름 → 벽 조회용 맵
    TMap<FName, AMD_CuttableWall*> WallsByName;
    for (TActorIterator<AMD_CuttableWall> It(World); It; ++It)
    {
        WallsByName.Add(It->GetFName(), *It);
    }

    // 어제 철거된 벽 제거 (조각 스폰·점수 없이 통로만 유지)
    const TSet<FName> Surviving(WorldData.SurvivingWallNames);
    for (const TPair<FName, AMD_CuttableWall*>& Pair : WallsByName)
    {
        if (!Surviving.Contains(Pair.Key))
        {
            Pair.Value->Destroy();
        }
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
        if (AMD_CuttableWall* const* Wall = WallsByName.Find(WallName); Wall != nullptr)
        {
            (*Wall)->SetSupported(true);
        }
    }

    // 어제 긁은 오염 셀 상태 복원. 이후 ApplyOvernightWorsening이 이 위에 소량 재부착
    for (const FMD_WallContaminationRecord& Record : WorldData.WallContaminations)
    {
        if (UMD_ScrapeSurfaceComponent* const Surface = FindSavedScrapeSurface(Record, WallsByName, SurfacesByOwnerName); Surface != nullptr)
        {
            Surface->RestoreContaminationCells(
                Record.CellIndices,
                Record.InitialAmounts,
                Record.RemainingAmounts,
                Record.TransferredAmounts,
                Record.TransferredPatternDensities,
                Record.CoveredFractions);
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

            if (AMD_CuttableWall* const* Wall = WallsByName.Find(Record.SupportedWallName); Wall != nullptr)
            {
                Brace->SetSupportedWall(*Wall);
            }
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
    WorldData.SurvivingWallNames = Snapshot.SurvivingWallNames;
    WorldData.SupportedWallNames = Snapshot.SupportedWallNames;
    WorldData.Braces = Snapshot.Braces;
    WorldData.Samples = Snapshot.Samples;
    WorldData.Pieces = Snapshot.Pieces;
    WorldData.WallContaminations = Snapshot.WallContaminations;
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
        Braces.Add(*It);
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
