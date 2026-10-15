#include "MDMultiUserGit.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "UObject/SavePackage.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDGitReferenceTest,"MDMultiUser.Git.PlacedReferences",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMDGitReferenceTest::RunTest(const FString&)
{
    if (!FParse::Param(FCommandLine::Get(),TEXT("MDMultiUserGitTest")))
    { AddError(TEXT("Isolated host with -MDMultiUserGitTest required")); return false; }
    UWorld* World=GEditor->GetEditorWorldContext().World();
    if (!TestNotNull(TEXT("Isolated world"),World)) return false;
    const FString Id=FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString PackageRoot=FString(TEXT("/Game/MDGitReferenceFixture/"))+Id;
    const FString FileRoot=FString(TEXT("Content/MDGitReferenceFixture/"))+Id+TEXT("/");
    auto SaveFixture=[&Id](UObject* Asset) {
        FString File=FPaths::ProjectContentDir()/TEXT("MDGitReferenceFixture")/Id/(Asset->GetName()+TEXT(".uasset"));
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);
        FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone;
        return UPackage::SavePackage(Asset->GetOutermost(),Asset,*File,Args);
    };
    UMaterial* Parent=NewObject<UMaterial>(CreatePackage(*(PackageRoot/TEXT("Parent"))),TEXT("Parent"),RF_Public|RF_Standalone);
    UMaterialInstanceConstant* Placed=NewObject<UMaterialInstanceConstant>(CreatePackage(*(PackageRoot/TEXT("Placed"))),TEXT("Placed"),RF_Public|RF_Standalone);
    Placed->SetParentEditorOnly(Parent);
    UMaterial* Unplaced=NewObject<UMaterial>(CreatePackage(*(PackageRoot/TEXT("Unplaced"))),TEXT("Unplaced"),RF_Public|RF_Standalone);
    TestTrue(TEXT("Parent fixture saved"),SaveFixture(Parent));
    TestTrue(TEXT("Placed fixture saved"),SaveFixture(Placed));
    TestTrue(TEXT("Unplaced fixture saved"),SaveFixture(Unplaced));
    AStaticMeshActor* Actor=World->SpawnActor<AStaticMeshActor>();
    Actor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    Actor->GetStaticMeshComponent()->SetMaterial(0,Placed);
    const FTransform Before=Actor->GetActorTransform();
    const bool Dirty=World->GetOutermost()->IsDirty();
    auto& Git=FMDMultiUserGit::Get();
    Git.Files.Reset(); Git.ManualSelection.Reset(); Git.Excluded.Reset();
    for (const TCHAR* Name:{TEXT("Placed"),TEXT("Parent"),TEXT("Unplaced")})
    {
        auto Row=MakeShared<FMDGitFile>(); Row->Path=FileRoot+Name+TEXT(".uasset"); Row->State=TEXT("??"); Git.Files.Add(Row);
    }
    Git.AutoSelectPlaced();
    const FString PlacedPath=FileRoot+TEXT("Placed.uasset");
    TestTrue(TEXT("Unsaved placed actor reference auto-selected"),Git.Selected(PlacedPath));
    TestTrue(TEXT("Transitive material parent auto-selected"),Git.Selected(FileRoot+TEXT("Parent.uasset")));
    TestFalse(TEXT("Unplaced unrelated asset not selected"),Git.Selected(FileRoot+TEXT("Unplaced.uasset")));
    Git.Select(PlacedPath,false); Git.AutoSelectPlaced();
    TestFalse(TEXT("Manual exclusion survives auto refresh"),Git.Selected(PlacedPath));
    TestTrue(TEXT("Selection scan preserves actor pose"),Actor->GetActorTransform().Equals(Before));
    TestEqual(TEXT("Selection scan preserves map dirty state"),World->GetOutermost()->IsDirty(),Dirty);
    Git.Branch=TEXT("fixture"); Git.Upstream=TEXT("origin/fixture");
    FString Error;
    World->GetOutermost()->SetDirtyFlag(true);
    TestFalse(TEXT("Commit blocks unsaved maps"),Git.CanWrite(TEXT("commit"),Error));
    TestTrue(TEXT("Map protection reason"),Error.Contains(TEXT("미저장")));
    World->GetOutermost()->SetDirtyFlag(Dirty);
    Placed->GetOutermost()->SetDirtyFlag(true);
    TestFalse(TEXT("Commit blocks unsaved content"),Git.CanWrite(TEXT("commit"),Error));
    TestTrue(TEXT("Content protection reason"),Error.Contains(TEXT("미저장")));
    Placed->GetOutermost()->SetDirtyFlag(false);
    World->DestroyActor(Actor);
    Git.ManualSelection.Reset(); Git.Excluded.Reset(); Git.AutoSelectPlaced();
    TestFalse(TEXT("Removed actor no longer selects its reference"),Git.Selected(PlacedPath));
    return !HasAnyErrors();
}
#endif
