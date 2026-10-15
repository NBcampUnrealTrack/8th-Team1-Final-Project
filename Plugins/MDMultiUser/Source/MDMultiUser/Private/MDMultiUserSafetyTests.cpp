#include "MDMultiUserSafety.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Engine/World.h"
#include "Editor.h"
#include "HAL/PlatformTime.h"
#include "Engine/StaticMeshActor.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDSafetyRecoveryTest, "MDMultiUser.Safety.CheckpointRecovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMDSafetyRecoveryTest::RunTest(const FString&)
{
    // Explicit isolated-host opt-in. Never mutate a team's live map for a test.
    if (!FParse::Param(FCommandLine::Get(), TEXT("MDMultiUserSafetyTest")))
    { AddError(TEXT("Run in isolated host with -MDMultiUserSafetyTest")); return false; }
    TestEqual(TEXT("Five-minute production interval"), FMDMultiUserSafety::IntervalSeconds, 300.0);
    UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, FName(TEXT("MDSafetyFixture")));
    if (!TestNotNull(TEXT("Fixture world"), World)) return false;
    AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>();
    if (!TestNotNull(TEXT("Fixture actor"), Actor)) { World->DestroyWorld(false); return false; }
    Actor->SetActorLocation(FVector(23, 45, 67));
    FActorSpawnParameters TransientParams; TransientParams.ObjectFlags=RF_Transient;
    AStaticMeshActor* Generated=World->SpawnActor<AStaticMeshActor>(TransientParams);
    const FTransform Before = Actor->GetActorTransform();
    const bool Dirty = World->GetOutermost()->IsDirty();
    FString Error, Folder;
    auto& Safety = FMDMultiUserSafety::Get();
    const bool Saved = Safety.Checkpoint(World, Error);
    TestTrue(*(FString(TEXT("Checkpoint: ")) + Error), Saved);
    TestTrue(TEXT("Actor transform preserved"), Actor->GetActorTransform().Equals(Before));
    TestEqual(TEXT("Dirty state preserved"), World->GetOutermost()->IsDirty(), Dirty);
    if (Saved)
    {
        World->DestroyActor(Generated);
        Safety.CheckForMissingActors(World);
        TestFalse(TEXT("Generated transient actor deletion does not trigger recovery"),Safety.bPendingRecovery);
        TestTrue(TEXT("Recovery candidate validates"), Safety.PrepareRecovery(World, Folder, Error));
        TestTrue(TEXT("Separate recovery folder"), Folder.Contains(TEXT("/Recovered/")) || Folder.Contains(TEXT("\\Recovered\\")));
        TestTrue(TEXT("Candidate has actors"), IFileManager::Get().FileExists(*(Folder / TEXT("actors.t3d"))));
        TArray<FString> Heads;
        const FString HeadDir = Safety.RecoveryRoot() / TEXT("Heads");
        IFileManager::Get().FindFiles(Heads, *(HeadDir / TEXT("*.json")), true, false);
        // Isolated host contains only this fixture's checkpoint head.
        TestEqual(TEXT("One isolated head"), Heads.Num(), 1);
        if (Heads.Num() == 1)
        {
            FString Original;
            FFileHelper::LoadFileToString(Original, *(HeadDir / Heads[0]));
            FFileHelper::SaveStringToFile(TEXT("{}"), *(HeadDir / Heads[0]));
            TestFalse(TEXT("Malformed index rejected without crash"), Safety.PrepareRecovery(World, Folder, Error));
            FFileHelper::SaveStringToFile(Original, *(HeadDir / Heads[0]));
            TestTrue(TEXT("Original head remains usable"), Safety.PrepareRecovery(World, Folder, Error));
        }
        TestTrue(TEXT("Recovery never moves source actor"), Actor->GetActorTransform().Equals(Before));
        TArray<FString> Checkpoints;
        const FString CheckpointDir = Safety.RecoveryRoot() / TEXT("Checkpoints");
        IFileManager::Get().FindFiles(Checkpoints, *(CheckpointDir / TEXT("*")), false, true);
        TestEqual(TEXT("One immutable checkpoint"), Checkpoints.Num(), 1);
        if (Checkpoints.Num() == 1)
        {
            const FString Manifest = CheckpointDir / Checkpoints[0] / TEXT("manifest.json");
            FString Original; FFileHelper::LoadFileToString(Original, *Manifest);
            FFileHelper::SaveStringToFile(TEXT("{}"), *Manifest);
            TestFalse(TEXT("Malformed manifest rejected without assertion"), Safety.PrepareRecovery(World, Folder, Error));
            FFileHelper::SaveStringToFile(Original, *Manifest);
            const FString Export = CheckpointDir / Checkpoints[0] / TEXT("actors.t3d");
            FString OriginalExport; FFileHelper::LoadFileToString(OriginalExport, *Export);
            FFileHelper::SaveStringToFile(TEXT("corrupt"), *Export);
            TestFalse(TEXT("Corrupt export rejected"), Safety.PrepareRecovery(World, Folder, Error));
            FFileHelper::SaveStringToFile(OriginalExport, *Export);
            TestTrue(TEXT("Restored fixture remains valid"), Safety.PrepareRecovery(World, Folder, Error));
            Safety.LastRecoverySource.Reset();
            World->DestroyActor(Actor);
            Safety.CheckForMissingActors(World);
            TestTrue(TEXT("Missing actor pauses automatic promotion"), Safety.bPendingRecovery);
            TestFalse(TEXT("Invalid world cannot replace good head"), Safety.Checkpoint(nullptr, Error));
            TestTrue(TEXT("Prior checkpoint remains recoverable"), Safety.PrepareRecovery(World, Folder, Error));
        }
    }
    World->DestroyWorld(false);
    UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
    if (TestNotNull(TEXT("Isolated editor world"), EditorWorld))
    {
        const bool WasDirty = EditorWorld->GetOutermost()->IsDirty();
        AStaticMeshActor* TimerActor = EditorWorld->SpawnActor<AStaticMeshActor>();
        Safety.ObservedWorld = EditorWorld;
        Safety.bWasConnected = false;
        Safety.bCheckedRecovery = true;
        Safety.bPendingRecovery = false;
        Safety.StableAfter = 0.;
        Safety.NextCheckpoint = FPlatformTime::Seconds() + 300.;
        const double Due = Safety.NextCheckpoint;
        Safety.Tick(0.f);
        TestEqual(TEXT("No early automatic checkpoint"), Safety.NextCheckpoint, Due);
        Safety.NextCheckpoint = FPlatformTime::Seconds() - 1.;
        Safety.Tick(0.f);
        TestTrue(TEXT("Expired timer schedules next five-minute checkpoint"), Safety.NextCheckpoint - FPlatformTime::Seconds() > 295.);
        TestTrue(TEXT("Timer wrote a valid recoverable checkpoint"), Safety.PrepareRecovery(EditorWorld, Folder, Error));
        EditorWorld->DestroyActor(TimerActor);
        EditorWorld->GetOutermost()->SetDirtyFlag(WasDirty);
    }
    return !HasAnyErrors();
}
#endif
