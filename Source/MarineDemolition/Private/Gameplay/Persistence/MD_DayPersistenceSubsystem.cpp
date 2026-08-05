#include "Gameplay/Persistence/MD_DayPersistenceSubsystem.h"

#include "EngineUtils.h"
#include "Game/MD_GameStateBase.h"
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

    for (TActorIterator<AMD_CuttableWall> It(World); It; ++It)
    {
        Snapshot.SurvivingWallNames.Add(It->GetFName());
        if (It->IsSupported())
        {
            Snapshot.SupportedWallNames.Add(It->GetFName());
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

    // 점수 캐리오버 + Day 진행
    AMD_GameStateBase* GameState = World->GetGameState<AMD_GameStateBase>();
    if (IsValid(GameState))
    {
        GameState->AddScoreDelta(Snapshot.Score);
        GameState->SetInspectionWarned(Snapshot.bWarningIssued);
        GameState->SetCurrentDay(Snapshot.CompletedDay + 1);
    }

    ApplyOvernightWorsening(World);

    return true;
}

void UMD_DayPersistenceSubsystem::ApplyOvernightWorsening(UWorld* World)
{
    // 약해진 지지대 1개 (기획 03 §3)
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

    // 잔해 위치 소폭 이동 (동선이 조금 더 지저분해짐)
    for (TActorIterator<AMD_WallPiece> It(World); It; ++It)
    {
        const FVector Jitter(
            FMath::FRandRange(-30.f, 30.f), FMath::FRandRange(-30.f, 30.f), 0.f);
        It->AddActorWorldOffset(Jitter, true);
    }
}
