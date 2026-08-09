#include "Gameplay/Persistence/MD_DayPersistenceSubsystem.h"

#include "EngineUtils.h"
#include "Game/MD_GameStateBase.h"
#include "World/Contamination/MD_BarnacleSpawner.h"
#include "World/Interaction/MD_SectorCloseoutMarker.h"
#include "World/Sample/MD_SampleItem.h"
#include "World/Support/MD_SupportBrace.h"
#include "World/Wall/MD_CuttableWall.h"
#include "World/Wall/MD_WallPiece.h"

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
    Snapshot.bEmergencyDay = GameState->IsEmergencyDay();

    for (TActorIterator<AMD_CuttableWall> It(World); It; ++It)
    {
        Snapshot.SurvivingWallNames.Add(It->GetFName());
        if (It->IsSupported())
        {
            Snapshot.SupportedWallNames.Add(It->GetFName());
        }

        // 생성 기준값과 달라진 오염 셀만 기록. 밤샘 분출로 총량이 오른 셀도 포함된다
        FMD_WallContaminationRecord ContaminationRecord;
        ContaminationRecord.WallName = It->GetFName();
        It->ExportContaminationDeltas(
            ContaminationRecord.CellIndices,
            ContaminationRecord.InitialAmounts,
            ContaminationRecord.RemainingAmounts);
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

    // 어제 떼어낸 따개비 순번. 배치기는 레벨 배치 액터라 이름이 안정적이다
    for (TActorIterator<AMD_BarnacleSpawner> It(World); It; ++It)
    {
        const TSet<int32>& RemovedIndices = It->GetRemovedSpawnIndices();
        if (RemovedIndices.IsEmpty())
        {
            continue;
        }

        FMD_BarnacleRemovalRecord RemovalRecord;
        RemovalRecord.SpawnerName = It->GetFName();
        RemovalRecord.RemovedSpawnIndices = RemovedIndices.Array();
        Snapshot.BarnacleRemovals.Add(RemovalRecord);
    }

    Snapshot.bValid = true;
}

bool UMD_DayPersistenceSubsystem::RestoreSnapshot(UWorld* World)
{
    if (!Snapshot.bValid || !World || World->GetNetMode() == NM_Client)
    {
        return false;
    }

    // 다른 레벨(모선 등)에서는 적용하지 않음
    if (World->GetFName() != Snapshot.WorldName)
    {
        return false;
    }

    // 이름 → 벽 조회용 맵
    TMap<FName, AMD_CuttableWall*> WallsByName;
    for (TActorIterator<AMD_CuttableWall> It(World); It; ++It)
    {
        WallsByName.Add(It->GetFName(), *It);
    }

    // 어제 철거된 벽 제거 (조각 스폰·점수 없이 통로만 유지)
    const TSet<FName> Surviving(Snapshot.SurvivingWallNames);
    for (const TPair<FName, AMD_CuttableWall*>& Pair : WallsByName)
    {
        if (!Surviving.Contains(Pair.Key))
        {
            Pair.Value->Destroy();
        }
    }

    // 지지대 설치 상태 복원
    for (const FName& WallName : Snapshot.SupportedWallNames)
    {
        if (AMD_CuttableWall* const* Wall = WallsByName.Find(WallName))
        {
            (*Wall)->SetSupported(true);
        }
    }

    // 어제 긁은 오염 셀 상태 복원. 이후 ApplyOvernightWorsening이 이 위에 소량 재부착
    for (const FMD_WallContaminationRecord& Record : Snapshot.WallContaminations)
    {
        if (AMD_CuttableWall* const* Wall = WallsByName.Find(Record.WallName))
        {
            (*Wall)->RestoreContaminationCells(
                Record.CellIndices,
                Record.InitialAmounts,
                Record.RemainingAmounts);
        }
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

    // 지지대 액터 재스폰 + 벽 재연결
    for (const FMD_BraceRecord& Record : Snapshot.Braces)
    {
        if (!Record.BraceClass)
        {
            continue;
        }

        // 스폰 스케일은 루트 기본 스케일과 곱해지므로 위치·회전만 주고 스케일은 절대값으로 대입
        const FTransform SpawnTransform(Record.Transform.GetRotation(), Record.Transform.GetLocation());
        AMD_SupportBrace* Brace = World->SpawnActor<AMD_SupportBrace>(
            Record.BraceClass, SpawnTransform, SpawnParams);
        if (Brace)
        {
            Brace->SetActorScale3D(Record.Transform.GetScale3D());

            if (AMD_CuttableWall* const* Wall = WallsByName.Find(Record.SupportedWallName))
            {
                Brace->SetSupportedWall(*Wall);
            }
        }
    }

    // 회수된 샘플 제거, 남은 샘플은 위치·손상 복원
    TMap<FName, const FMD_SampleRecord*> SampleRecords;
    for (const FMD_SampleRecord& Record : Snapshot.Samples)
    {
        SampleRecords.Add(Record.ActorName, &Record);
    }

    for (TActorIterator<AMD_SampleItem> It(World); It; ++It)
    {
        if (const FMD_SampleRecord* const* Found = SampleRecords.Find(It->GetFName()))
        {
            It->SetActorTransform((*Found)->Transform);
            if ((*Found)->bDamaged)
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
    for (const FMD_PieceRecord& Record : Snapshot.Pieces)
    {
        if (!Record.PieceClass)
        {
            continue;
        }

        const FTransform SpawnTransform(Record.Transform.GetRotation(), Record.Transform.GetLocation());
        if (AMD_WallPiece* Piece = World->SpawnActor<AMD_WallPiece>(
            Record.PieceClass, SpawnTransform, SpawnParams))
        {
            Piece->SetActorScale3D(Record.Transform.GetScale3D());
            Piece->SetContaminated(Record.bContaminated);
        }
    }

    // 어제 떼어낸 따개비는 되살리지 않는다 (기획 03:101 강제 초기화 금지).
    // 배치기가 BeginPlay에 이미 다 붙여 놓은 뒤이므로 목록에 든 것을 지운다.
    if (!Snapshot.BarnacleRemovals.IsEmpty())
    {
        TMap<FName, const FMD_BarnacleRemovalRecord*> RemovalsBySpawnerName;
        for (const FMD_BarnacleRemovalRecord& Record : Snapshot.BarnacleRemovals)
        {
            RemovalsBySpawnerName.Add(Record.SpawnerName, &Record);
        }

        for (TActorIterator<AMD_BarnacleSpawner> It(World); It; ++It)
        {
            if (const FMD_BarnacleRemovalRecord* const* Record =
                    RemovalsBySpawnerName.Find(It->GetFName()))
            {
                It->RestoreRemovedSpawnIndices((*Record)->RemovedSpawnIndices);
            }
        }
    }

    // 어제 마감한 섹터는 마감 상태 유지 (점수 재지급 없음 — Score 캐리오버에 이미 포함)
    const TSet<FName> ClosedMarkers(Snapshot.ClosedMarkerNames);
    for (TActorIterator<AMD_SectorCloseoutMarker> It(World); It; ++It)
    {
        if (ClosedMarkers.Contains(It->GetFName()))
        {
            It->RestoreClosed();
        }
    }

    // 점수 캐리오버 + Day 진행
    AMD_GameStateBase* GameState = World->GetGameState<AMD_GameStateBase>();
    if (IsValid(GameState))
    {
        GameState->AddScoreDelta(Snapshot.Score);
        GameState->SetInspectionWarned(Snapshot.bWarningIssued);
        GameState->SetContractCrewCount(Snapshot.CrewCount);
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

    // 살아남은 벽 일부에 소형 오염 재부착
    for (TActorIterator<AMD_CuttableWall> It(World); It; ++It)
    {
        if (FMath::FRand() < 0.3f)
        {
            It->ReapplyContaminationPatch(0.6f, 3);
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
