#include "MDPlacementTools.h"
#include "MDHoldSnap.h"
#include "EditorModeManager.h"
#include "EditorModes.h"
#include "LevelEditorViewport.h"
#include "EngineUtils.h"
#include "SceneView.h"
#include "Framework/Commands/InputBindingManager.h"
#include "Framework/Commands/UICommandInfo.h"
#include "UnrealEdGlobals.h"
#include "ScopedTransaction.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Editor.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/Selection.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDEdgeMathTest, "MDMultiUser.Placement.EdgeMath", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMDEdgeMathTest::RunTest(const FString& Parameters)
{
    using namespace MDPlacement;
    const MDPlacement::FEdge Source{FVector(0,0,0), FVector(100,0,0)};
    const MDPlacement::FEdge Target{FVector(100,10,0), FVector(0,10,0)};
    FOptions Options; FMatch Match;
    TestTrue(TEXT("Reversed parallel endpoints match"), MatchEdges(Source,Target,FTransform::Identity,Options,Match));
    TestTrue(TEXT("Exact translation, not bounding-box snap"), Match.Transform.GetLocation().Equals(FVector(0,10,0),1.e-6));
    TestTrue(TEXT("Endpoint lands exactly"), Match.Transform.TransformPosition(Match.SourceAnchor).Equals(Match.TargetAnchor,1.e-6));
    Options.Distance=5;
    TestFalse(TEXT("Distance limit respected"), MatchEdges(Source,Target,FTransform::Identity,Options,Match));
    Options.Distance=100;
    Options.Anchor=EAnchor::Midpoint;
    const MDPlacement::FEdge ShortTarget{FVector(30,10,0),FVector(70,10,0)};
    TestTrue(TEXT("Different length midpoint match"),MatchEdges(Source,ShortTarget,FTransform::Identity,Options,Match));
    TestTrue(TEXT("Midpoints agree"),Match.Transform.TransformPosition(FVector(50,0,0)).Equals(FVector(50,10,0),1.e-6));
    const FQuat Rotation(FVector::UpVector,FMath::DegreesToRadians(20.));
    const MDPlacement::FEdge Angled{FVector(0,10,0),Rotation.RotateVector(FVector(100,0,0))+FVector(0,10,0)};
    TestFalse(TEXT("Rotation disabled rejects nonparallel edges"),MatchEdges(Source,Angled,FTransform::Identity,Options,Match));
    Options.bRotate=true; Options.AngleDegrees=15;
    TestFalse(TEXT("Angle tolerance respected"),MatchEdges(Source,Angled,FTransform::Identity,Options,Match));
    Options.AngleDegrees=30;
    TestTrue(TEXT("Optional rotation matches"),MatchEdges(Source,Angled,FTransform::Identity,Options,Match));
    TestTrue(TEXT("Rotation and anchor exact"),Match.Transform.TransformPosition(Match.SourceAnchor).Equals(Match.TargetAnchor,1.e-6));
    TestTrue(TEXT("Result edge parallel"),FMath::Abs(FVector::DotProduct(Match.Transform.TransformVectorNoScale(FVector::ForwardVector),(Angled.B-Angled.A).GetSafeNormal()))>0.999999);
    const FTransform Mirrored(FQuat::Identity,FVector(20,30,40),FVector(-2,3,1));
    const MDPlacement::FEdge WorldSource{Mirrored.TransformPosition(Source.A),Mirrored.TransformPosition(Source.B)};
    const MDPlacement::FEdge WorldTarget{WorldSource.A+FVector(0,10,0),WorldSource.B+FVector(0,10,0)};
    TestTrue(TEXT("Mirrored nonuniform source supported"),MatchEdges(WorldSource,WorldTarget,Mirrored,Options,Match));
    TestTrue(TEXT("Scale preserved"),Match.Transform.GetScale3D().Equals(Mirrored.GetScale3D()));
    TestTrue(TEXT("Mirrored world anchor correct"),Match.Transform.TransformPosition(Mirrored.InverseTransformPosition(Match.SourceAnchor)).Equals(Match.TargetAnchor,1.e-6));
    TestFalse(TEXT("Degenerate edge rejected"),MatchEdges({FVector::ZeroVector,FVector::ZeroVector},Target,FTransform::Identity,Options,Match));
    Options.bRotate=false; Options.Anchor=EAnchor::Endpoint;
    TestFalse(TEXT("No-op does not generate transactions"),MatchEdges(Source,Source,FTransform::Identity,Options,Match));
    Options.bRotate=true; Options.AngleDegrees=90; Options.Distance=20;
    const FTransform FarPivot(FQuat::Identity,FVector(1000,0,0));
    TestFalse(TEXT("Rotation cannot exceed pivot movement budget"),MatchEdges(Source,Angled,FarPivot,Options,Match));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDEdgeGeometryTest, "MDMultiUser.Placement.MeshFeatures", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMDEdgeGeometryTest::RunTest(const FString& Parameters)
{
    using namespace MDPlacement;
    UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview,false);
    if (!TestNotNull(TEXT("Preview world"),World)) { return false; }
    AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>();
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (Actor && Cube)
    {
        Actor->GetStaticMeshComponent()->SetStaticMesh(Cube);
        TArray<MDPlacement::FEdge> Edges; FText Reason;
        TestTrue(TEXT("Source mesh extraction"),ExtractEdges(Actor,Edges,Reason));
        TestEqual(TEXT("Cube has 12 physical edges; UV seams and face diagonals excluded"),Edges.Num(),12);
        Actor->SetActorTransform(FTransform(FRotator(15,30,5),FVector(100,20,90),FVector(-2,3,1)));
        TestTrue(TEXT("Rotated mirrored scale extraction"),ExtractEdges(Actor,Edges,Reason));
        TestEqual(TEXT("Feature topology retained under transforms"),Edges.Num(),12);
        FCandidate C; C.Source=Actor; C.Target=Actor; C.OriginalSource=Actor->GetActorTransform(); C.OriginalTarget=C.OriginalSource;
        TestTrue(TEXT("Snapshot initially current"),IsSnapshotCurrent(C));
        Actor->AddActorWorldOffset(FVector(1,0,0));
        TestFalse(TEXT("Remote/local transform changes invalidate preview"),IsSnapshotCurrent(C));
    }
    else { AddError(TEXT("Could not create cube fixture")); }
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDEdgeApplyTest, "MDMultiUser.Placement.ApplyUndo", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMDEdgeApplyTest::RunTest(const FString& Parameters)
{
    using namespace MDPlacement;
    // This integration fixture is deliberately opt-in and run in a separate unattended editor.
    if (!FParse::Param(FCommandLine::Get(),TEXT("MDPlacementTest")))
    { AddWarning(TEXT("Run in isolated editor with -MDPlacementTest to execute ApplyUndo fixture.")); return true; }
    UWorld* World=GEditor->GetEditorWorldContext().World();
    if (!TestNotNull(TEXT("Editor world"),World)) { return false; }
    const bool bWasDirty=World->GetOutermost()->IsDirty();
    FActorSpawnParameters Params; Params.ObjectFlags=RF_Transient|RF_Transactional;
    AStaticMeshActor* Source=World->SpawnActor<AStaticMeshActor>(Params);
    AStaticMeshActor* Target=World->SpawnActor<AStaticMeshActor>(Params);
    if (!Source || !Target) { AddError(TEXT("Spawn failed")); return false; }
    UStaticMesh* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    Source->GetStaticMeshComponent()->SetStaticMesh(Cube); Target->GetStaticMeshComponent()->SetStaticMesh(Cube);
    Source->SetActorLocation(FVector(0,0,10000)); Target->SetActorLocation(FVector(120,0,10000));
    GEditor->SelectNone(false,true); GEditor->SelectActor(Source,true,true);
    const FTransform Before=Source->GetActorTransform();
    TArray<FCandidate> Candidates; FText Result; FOptions Options;
    FindCandidates(Source,Target,Options,Candidates,Result);
    TestTrue(TEXT("Pinned cube candidate search"),Candidates.Num()>0);
    if (!Candidates.IsEmpty())
    {
        const FCandidate Candidate=Candidates[0];
        TestTrue(TEXT("Preview leaves actor unchanged"),Source->GetActorTransform().Equals(Before));
        TestTrue(TEXT("Apply candidate"),Apply(Candidate,Result));
        TestTrue(TEXT("Result transform exact"),Source->GetActorTransform().Equals(Candidate.Transform,.001));
        GEditor->UndoTransaction();
        TestTrue(TEXT("One undo restores original transform"),Source->GetActorTransform().Equals(Before,.001));
        Target->AddActorWorldOffset(FVector(1,0,0));
        TestFalse(TEXT("Stale target refuses apply"),Apply(Candidate,Result));
        TestTrue(TEXT("Rejected apply leaves actor unchanged"),Source->GetActorTransform().Equals(Before,.001));
    }
    GEditor->SelectNone(false,true);
    World->DestroyActor(Source); World->DestroyActor(Target);
    World->GetOutermost()->SetDirtyFlag(bWasDirty);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDClickSnapTest,"MDMultiUser.Placement.ClickSnap",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMDClickSnapTest::RunTest(const FString& Parameters)
{
    using namespace MDHoldSnap;
    if (!FParse::Param(FCommandLine::Get(),TEXT("MDPlacementTest")))
    { AddWarning(TEXT("ClickSnap requires isolated editor with -MDPlacementTest.")); return true; }
    UWorld* World=GEditor->GetEditorWorldContext().World();
    const bool bWasDirty=World->GetOutermost()->IsDirty();
    UStaticMesh* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    FActorSpawnParameters Params; Params.ObjectFlags=RF_Transactional;
    auto MakeActor=[&]() {
        AActor* Actor=World->SpawnActor<AActor>(Params);
        auto* Mesh=NewObject<UStaticMeshComponent>(Actor,NAME_None,RF_Transactional);
        Actor->AddInstanceComponent(Mesh); Actor->SetRootComponent(Mesh);
        Mesh->SetStaticMesh(Cube); Mesh->RegisterComponent(); return Actor;
    };
    AActor* Source=MakeActor(); AActor* Target=MakeActor(); AActor* Ceiling=MakeActor();
    Source->SetActorLocation(FVector(20000,0,10250));
    Target->SetActorLocation(FVector(20000,0,9950)); Target->SetActorScale3D(FVector(10,10,1));
    Ceiling->SetActorLocation(FVector(20000,0,10400)); Ceiling->SetActorScale3D(FVector(10,10,1));
    GEditor->SelectNone(false,true); GEditor->SelectActor(Source,true,true);
    const FTransform Before=Source->GetActorTransform();
    FText Result;
    TestFalse(TEXT("Blueprint-layout source is not StaticMeshActor"),Source->IsA<AStaticMeshActor>());
    TArray<MDPlacement::FEdge> Edges;
    TestTrue(TEXT("Blueprint root mesh exposes edges"),MDPlacement::ExtractEdges(Source,Edges,Result));
    TestEqual(TEXT("Blueprint cube has twelve edges"),Edges.Num(),12);
    auto& Modes=GLevelEditorModeTools();
    TestTrue(TEXT("Click mode active"),Modes.IsModeActive(FMDHoldSnapMode::ID));
    FLevelEditorViewportClient* Client=nullptr;
    for (auto* C:GEditor->GetLevelViewportClients()) { if (C && C->Viewport && C->GetWorld()==World && C->IsPerspective() && C->Viewport->GetSizeXY().X>0 && C->Viewport->GetSizeXY().Y>0) { Client=C; break; } }
    if (TestNotNull(TEXT("Native viewport"),Client))
    {
        const auto OldWidget=Client->GetWidgetMode();
        const FVector OldLocation=Client->GetViewLocation(); const FRotator OldRotation=Client->GetViewRotation();
        Client->SetWidgetMode(UE::Widget::WM_Rotate);
        TestTrue(TEXT("U press works without move gizmo"),Modes.InputKey(Client,Client->Viewport,EKeys::U,IE_Pressed,false));
        TestTrue(TEXT("U press alone drops to floor, ignores ceiling"),FMath::IsNearlyEqual(Source->GetActorLocation().Z,10050.,.01));
        TestTrue(TEXT("U preserves XY"),FVector2D(Source->GetActorLocation()).Equals(FVector2D(Before.GetLocation()),.001));
        TestTrue(TEXT("U preserves rotation and scale"),Source->GetActorQuat().Equals(Before.GetRotation()) && Source->GetActorScale3D().Equals(Before.GetScale3D()));
        TestTrue(TEXT("U autorepeat consumed"),Modes.InputKey(Client,Client->Viewport,EKeys::U,IE_Repeat,false));
        Modes.InputKey(Client,Client->Viewport,EKeys::U,IE_Released,false);
        GEditor->UndoTransaction(); TestTrue(TEXT("One Undo restores U"),Source->GetActorTransform().Equals(Before,.001));
        Source->SetActorLocation(FVector(20003,7,10253));
        const FTransform GridBefore=Source->GetActorTransform();
        Modes.InputKey(Client,Client->Viewport,EKeys::Y,IE_Pressed,false);
        TestTrue(TEXT("Y press snaps grid"),Source->GetActorLocation().Equals(GridBefore.GetLocation().GridSnap(GEditor->GetGridSize()),.001));
        Modes.InputKey(Client,Client->Viewport,EKeys::Y,IE_Released,false);
        if (!Source->GetActorTransform().Equals(GridBefore,.001)) { GEditor->UndoTransaction(); }
        Source->SetActorTransform(Before);
        World->DestroyActor(Ceiling); Ceiling=nullptr;
        Target->SetActorRotation(FRotator(0,0,15));
        Client->SetViewLocation(FVector(20000,0,12000)); Client->SetViewRotation(FRotator(-90,0,0));
        FSceneViewFamilyContext Family(FSceneViewFamily::ConstructionValues(Client->Viewport,World->Scene,Client->EngineShowFlags).SetRealtimeUpdate(true));
        FSceneView* View=Client->CalcSceneView(&Family);
        const FIntPoint Size=Client->Viewport->GetSizeXY();
        const FViewportClick Click(View,Client,EKeys::LeftMouseButton,IE_Pressed,Size.X/2,Size.Y/2);
        HActor Proxy(Target,Target->GetRootComponent() ? Cast<UPrimitiveComponent>(Target->GetRootComponent()) : nullptr);
        auto* ClickMode=Modes.GetActiveMode(FMDHoldSnapMode::ID);
        for (FKey Key:{EKeys::N,EKeys::J,EKeys::I})
        {
            TestFalse(TEXT("Disabled key is not registered"),FInputBindingManager::Get().GetCommandInfoFromInputChord(FName("MDHoldSnap"),FInputChord(Key),false).IsValid());
            TestFalse(TEXT("Disabled key is not consumed"),ClickMode->InputKey(Client,Client->Viewport,Key,IE_Pressed));
            TestFalse(TEXT("Disabled key cannot arm target click"),ClickMode->HandleClick(Client,&Proxy,Click));
            TestTrue(TEXT("Disabled key leaves source unchanged"),Source->GetActorTransform().Equals(Before,.001));
        }
        for (EMode Disabled:{EMode::Midpoint,EMode::Rotate,EMode::Slope})
        {
            TestFalse(TEXT("Disabled action cannot bypass input guard"),ApplyTarget(Source,Target,Disabled,Click.GetOrigin(),Click.GetDirection(),Result));
            TestTrue(TEXT("Disabled action preserves transform"),Source->GetActorTransform().Equals(Before,.001));
        }
        TestFalse(TEXT("Slope quick action disabled"),MDPlacement::QuickAction(MDPlacement::EQuickAction::Slope,Result));
        TestFalse(TEXT("Extra focus quick action disabled"),MDPlacement::QuickAction(MDPlacement::EQuickAction::Focus,Result));
        Target->SetActorTransform(FTransform(FQuat::Identity,FVector(20000,0,10000),FVector(1)));
        for (FKey Key:{EKeys::B})
        {
            Source->SetActorTransform(FTransform(FRotator::ZeroRotator,FVector(21200,0,10000),FVector(1)));
            const FTransform EdgeBefore=Source->GetActorTransform();
            Modes.InputKey(Client,Client->Viewport,Key,IE_Pressed,false);
            TestTrue(TEXT("Edge key alone does not move"),Source->GetActorTransform().Equals(EdgeBefore,.001));
            TestTrue(TEXT("Native edge target click consumed"),Modes.HandleClick(Client,&Proxy,Click));
            TestFalse(*FString::Printf(TEXT("%s click moves distant Blueprint source"),*Key.ToString()),Source->GetActorTransform().Equals(EdgeBefore,.001));
            TestTrue(TEXT("Edge target unchanged"),Target->GetActorLocation().Equals(FVector(20000,0,10000)));
            TestTrue(TEXT("Edge click preserves selection"),MDPlacement::SelectedActor()==Source);
            Modes.InputKey(Client,Client->Viewport,Key,IE_Released,false);
            GEditor->UndoTransaction(); TestTrue(TEXT("One Undo restores edge click"),Source->GetActorTransform().Equals(EdgeBefore,.001));
        }
        Source->SetActorTransform(Before);
        Modes.InputKey(Client,Client->Viewport,EKeys::B,IE_Pressed,false);
        Modes.InputKey(Client,Client->Viewport,EKeys::Escape,IE_Pressed,false);
        auto* Mode=Modes.GetActiveMode(FMDHoldSnapMode::ID);
        TestFalse(TEXT("Esc cancels target interception"),Mode->HandleClick(Client,&Proxy,Click));
        TestTrue(TEXT("Canceled action unchanged"),Source->GetActorTransform().Equals(Before,.001));
        Modes.InputKey(Client,Client->Viewport,EKeys::B,IE_Pressed,false); Mode->LostFocus(Client,Client->Viewport);
        TestTrue(TEXT("Focus loss clears pending click"),Status().IsEmpty());
        TestFalse(TEXT("No floor leaves actor unchanged"),ApplyImmediate(Target,EMode::Floor,Result));
        TestTrue(TEXT("Invalid target refuses action"),!ApplyTarget(Source,Source,EMode::Endpoint,FVector::ZeroVector,FVector::DownVector,Result));
        Client->SetWidgetMode(OldWidget); Client->SetViewLocation(OldLocation); Client->SetViewRotation(OldRotation);
    }
    GEditor->SelectNone(false,true); World->DestroyActor(Source); World->DestroyActor(Target); if (Ceiling) { World->DestroyActor(Ceiling); }
    World->GetOutermost()->SetDirtyFlag(bWasDirty);
    return true;
}
#endif