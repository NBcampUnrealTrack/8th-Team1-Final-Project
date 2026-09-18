#include "MDPlacementTools.h"
#include "MDHoldSnap.h"
#include "EditorModeManager.h"
#include "EditorModes.h"
#include "LevelEditorViewport.h"
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDHoldSnapTest, "MDMultiUser.Placement.HoldDrag", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMDHoldSnapTest::RunTest(const FString& Parameters)
{
    using namespace MDHoldSnap;
    if (!FParse::Param(FCommandLine::Get(),TEXT("MDPlacementTest")))
    { AddWarning(TEXT("HoldDrag requires isolated editor with -MDPlacementTest.")); return true; }
    UWorld* World=GEditor->GetEditorWorldContext().World();
    const bool bWasDirty=World->GetOutermost()->IsDirty();
    FActorSpawnParameters Params; Params.ObjectFlags=RF_Transactional;
    AStaticMeshActor* Source=World->SpawnActor<AStaticMeshActor>(Params);
    AStaticMeshActor* Target=World->SpawnActor<AStaticMeshActor>(Params);
    UStaticMesh* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    Source->GetStaticMeshComponent()->SetStaticMesh(Cube); Target->GetStaticMeshComponent()->SetStaticMesh(Cube);
    Source->SetActorLocation(FVector(0,0,10000)); Target->SetActorLocation(FVector(120,0,10000));
    GEditor->SelectNone(false,true); GEditor->SelectActor(Source,true,true);
    const FTransform Before=Source->GetActorTransform();
    MDPlacement::FOptions Options; Options.Distance=15;
    FText Result;
    {
        FDrag Drag; Drag.Begin(Source);
        TestTrue(TEXT("Hold moves to nearby edge"),Drag.Move(EMode::Endpoint,FVector(10,0,0),Options,Result));
        TestTrue(TEXT("Snap at 20 cm"),FMath::IsNearlyEqual(Source->GetActorLocation().X,20.));
        Drag.Move(EMode::Endpoint,FVector(1,0,0),Options,Result);
        TestTrue(TEXT("Subthreshold movement stays snapped"),FMath::IsNearlyEqual(Source->GetActorLocation().X,20.));
        Drag.Move(EMode::Endpoint,FVector(-30,0,0),Options,Result);
        TestTrue(TEXT("Raw drag accumulator can pull free without sticking"),FMath::IsNearlyEqual(Source->GetActorLocation().X,-19.));
        Drag.Finish();
        GEditor->UndoTransaction();
        TestTrue(TEXT("Whole held drag has one undo"),Source->GetActorTransform().Equals(Before,.001));
    }
    {
        FDrag Drag; Drag.Begin(Source); Drag.Move(EMode::Endpoint,FVector(10,0,0),Options,Result);
        Drag.Cancel();
        TestTrue(TEXT("Escape restores drag beginning"),Source->GetActorTransform().Equals(Before,.001));
    }
    // Exactly aligned anchors must stay eligible during a held drag, even with a wide radius.
    {
        MDPlacement::FOptions WideOptions; WideOptions.Distance=100;
        FTransform Aligned=Before; Aligned.SetLocation(FVector(20,0,10000));
        FTransform Stable;
        TestTrue(TEXT("Already aligned endpoint remains a snap candidate"),Solve(Source,Aligned,EMode::Endpoint,WideOptions,Stable,Result));
        TestTrue(TEXT("Exact endpoint does not jump to another edge"),Stable.Equals(Aligned,.001));
        TestTrue(TEXT("Already aligned midpoint remains a snap candidate"),Solve(Source,Aligned,EMode::Midpoint,WideOptions,Stable,Result));
        TestTrue(TEXT("Exact midpoint does not jump to another edge"),Stable.Equals(Aligned,.001));
    }
    FTransform Desired=Before; Desired.SetLocation(FVector(3,7,10000)); FTransform Solved;
    Solve(Source,Desired,EMode::Grid,Options,Solved,Result);
    TestTrue(TEXT("Grid uses current editor interval"),Solved.GetLocation().Equals(Desired.GetLocation().GridSnap(GEditor->GetGridSize())));
    {
        AStaticMeshActor* Floor=World->SpawnActor<AStaticMeshActor>(Params);
        Floor->GetStaticMeshComponent()->SetStaticMesh(Cube);
        Floor->SetActorLocation(FVector(0,0,9950)); Floor->SetActorScale3D(FVector(10,10,1));
        MDPlacement::FOptions SurfaceOptions; SurfaceOptions.Distance=300;
        FTransform Above=Before; Above.SetLocation(FVector(0,0,10250));
        TestTrue(TEXT("Held floor finds collision surface"),Solve(Source,Above,EMode::Floor,SurfaceOptions,Solved,Result));
        TestTrue(TEXT("Held floor places bottom exactly on surface"),FMath::IsNearlyEqual(Solved.GetLocation().Z,10050.,.01));
        SurfaceOptions.Distance=20;
        TestFalse(TEXT("Held floor respects distance"),Solve(Source,Above,EMode::Floor,SurfaceOptions,Solved,Result));
        SurfaceOptions.Distance=300;
        Floor->SetActorRotation(FRotator(0,0,10));
        TestTrue(TEXT("Held slope finds angled surface"),Solve(Source,Above,EMode::Slope,SurfaceOptions,Solved,Result));
        TestTrue(TEXT("Held slope up matches normal"),Solved.GetRotation().GetUpVector().Equals(Floor->GetActorQuat().GetUpVector(),1.e-4));
        World->DestroyActor(Floor);
    }
    {
        MDPlacement::FGeometryCache Cache;
        TArray<MDPlacement::FEdge> Cached,Fresh;
        Cache.Read(Source,Cached,Result);
        Source->SetActorTransform(FTransform(FRotator(10,25,0),FVector(20,30,10000),FVector(1)));
        Cache.Read(Source,Cached,Result); MDPlacement::ExtractEdges(Source,Fresh,Result);
        TestEqual(TEXT("Cached topology count matches fresh extraction"),Cached.Num(),Fresh.Num());
        for (const auto& E:Fresh)
        { TestTrue(TEXT("Cached edges rebase after actor motion"),Cached.ContainsByPredicate([&](const auto& C){return (C.A.Equals(E.A,.001)&&C.B.Equals(E.B,.001))||(C.B.Equals(E.A,.001)&&C.A.Equals(E.B,.001));})); }
        Source->SetActorTransform(Before);
    }
    // Native editor routing, including key-up during the same drag and its transaction.
    auto& Modes=GLevelEditorModeTools();
    TestTrue(TEXT("Hold mode active alongside selection mode"),Modes.IsModeActive(FMDHoldSnapMode::ID));
    FLevelEditorViewportClient* Client=nullptr;
    for (auto* C:GEditor->GetLevelViewportClients()) { if (C && C->Viewport && C->GetWorld()==World) { Client=C; break; } }
    if (TestNotNull(TEXT("Native level viewport available"),Client))
    {
        const auto OldWidget=Client->GetWidgetMode();
        const auto OldAxis=Client->GetCurrentWidgetAxis();
        const auto OldOptions=MDPlacement::SessionOptions(); MDPlacement::SessionOptions()=Options;
        Client->SetWidgetMode(UE::Widget::WM_Translate); Client->SetCurrentWidgetAxis(EAxisList::X);
        TestTrue(TEXT("Key down enters hold mode"),Modes.InputKey(Client,Client->Viewport,EKeys::B,IE_Pressed,false));
        Client->TrackingStarted(FInputEventState(Client->Viewport,EKeys::LeftMouseButton,IE_Pressed),true,false);
        FVector Delta(10,0,0), Scale=FVector::ZeroVector; FRotator Rot=FRotator::ZeroRotator;
        Client->InputWidgetDelta(Client->Viewport,EAxisList::X,Delta,Rot,Scale);
        TestTrue(TEXT("Native gizmo path snaps"),FMath::IsNearlyEqual(Source->GetActorLocation().X,20.));
        TestTrue(TEXT("Key up exits hold mode"),Modes.InputKey(Client,Client->Viewport,EKeys::B,IE_Released,false));
        Delta=FVector(5,0,0); Client->InputWidgetDelta(Client->Viewport,EAxisList::X,Delta,Rot,Scale);
        TestTrue(TEXT("Same drag resumes ordinary movement on key release"),FMath::IsNearlyEqual(Source->GetActorLocation().X,25.));
        Client->TrackingStopped();
        GEditor->UndoTransaction();
        TestTrue(TEXT("Native mixed drag has one undo"),Source->GetActorTransform().Equals(Before,.001));
        Modes.InputKey(Client,Client->Viewport,EKeys::B,IE_Pressed,false);
        Client->TrackingStarted(FInputEventState(Client->Viewport,EKeys::LeftMouseButton,IE_Pressed),true,false);
        Delta=FVector(10,0,0); Client->InputWidgetDelta(Client->Viewport,EAxisList::X,Delta,Rot,Scale);
        TestTrue(TEXT("Esc consumed only for snap drag"),Modes.InputKey(Client,Client->Viewport,EKeys::Escape,IE_Pressed,false));
        TestTrue(TEXT("Native Esc restores drag start"),Source->GetActorTransform().Equals(Before,.001));
        Client->TrackingStopped();
        Modes.InputKey(Client,Client->Viewport,EKeys::B,IE_Released,false);
        // Escape must also undo the native portion after releasing the snap key.
        Modes.InputKey(Client,Client->Viewport,EKeys::B,IE_Pressed,false);
        Client->TrackingStarted(FInputEventState(Client->Viewport,EKeys::LeftMouseButton,IE_Pressed),true,false);
        Delta=FVector(10,0,0); Client->InputWidgetDelta(Client->Viewport,EAxisList::X,Delta,Rot,Scale);
        Modes.InputKey(Client,Client->Viewport,EKeys::B,IE_Released,false);
        Delta=FVector(5,0,0); Client->InputWidgetDelta(Client->Viewport,EAxisList::X,Delta,Rot,Scale);
        TestTrue(TEXT("Mixed cancellation fixture has native movement"),FMath::IsNearlyEqual(Source->GetActorLocation().X,25.));
        TestTrue(TEXT("Escape handles released-key mixed drag"),Modes.InputKey(Client,Client->Viewport,EKeys::Escape,IE_Pressed,false));
        TestTrue(TEXT("Mixed Escape restores original drag start"),Source->GetActorTransform().Equals(Before,.001));
        Client->TrackingStopped();
        // Blueprint walls typically derive from Actor and own a StaticMeshComponent root.
        // Exercise that layout through the actual U-key/native gizmo path.
        {
            AActor* RootMeshActor=World->SpawnActor<AActor>(Params);
            UStaticMeshComponent* RootMesh=NewObject<UStaticMeshComponent>(RootMeshActor,NAME_None,RF_Transactional);
            RootMeshActor->AddInstanceComponent(RootMesh);
            RootMeshActor->SetRootComponent(RootMesh);
            RootMesh->SetStaticMesh(Cube); RootMesh->RegisterComponent();
            RootMeshActor->SetActorLocation(FVector(20000,0,10250));
            AStaticMeshActor* Support=World->SpawnActor<AStaticMeshActor>(Params);
            Support->GetStaticMeshComponent()->SetStaticMesh(Cube);
            Support->SetActorLocation(FVector(20000,0,9950)); Support->SetActorScale3D(FVector(10,10,1));
            GEditor->SelectNone(false,true); GEditor->SelectActor(RootMeshActor,true,true);
            const FTransform RootBefore=RootMeshActor->GetActorTransform();
            TestFalse(TEXT("Blueprint-layout fixture is not a StaticMeshActor"),RootMeshActor->IsA<AStaticMeshActor>());
            MDPlacement::SessionOptions().Distance=300;
            TestTrue(TEXT("U arms for an Actor with static mesh root"),Modes.InputKey(Client,Client->Viewport,EKeys::U,IE_Pressed,false));
            TestTrue(TEXT("U alone leaves actor unchanged"),RootMeshActor->GetActorTransform().Equals(RootBefore));
            Client->TrackingStarted(FInputEventState(Client->Viewport,EKeys::LeftMouseButton,IE_Pressed),true,false);
            Delta=FVector(5,0,0); Client->InputWidgetDelta(Client->Viewport,EAxisList::X,Delta,Rot,Scale);
            TestTrue(TEXT("U native drag drops root-mesh actor to floor"),FMath::IsNearlyEqual(RootMeshActor->GetActorLocation().Z,10050.,.01));
            Modes.InputKey(Client,Client->Viewport,EKeys::U,IE_Released,false);
            Client->TrackingStopped();
            GEditor->UndoTransaction();
            TestTrue(TEXT("One undo restores root-mesh actor"),RootMeshActor->GetActorTransform().Equals(RootBefore,.001));
            Support->SetActorRotation(FRotator(0,0,10));
            TestTrue(TEXT("I supports root-mesh actor slope solve"),Solve(RootMeshActor,RootBefore,EMode::Slope,MDPlacement::SessionOptions(),Solved,Result));
            TestTrue(TEXT("Root-mesh actor slope aligns up"),Solved.GetRotation().GetUpVector().Equals(Support->GetActorQuat().GetUpVector(),1.e-4));
            GEditor->SelectNone(false,true);
            World->DestroyActor(RootMeshActor); World->DestroyActor(Support);
            GEditor->SelectActor(Source,true,true);
            MDPlacement::SessionOptions()=Options;
        }
        Client->SetCurrentWidgetAxis(EAxisList::None);
        Client->SetWidgetModeOverride(UE::Widget::WM_Rotate);
        TestEqual(TEXT("Rotation widget override selected"),Client->GetWidgetMode(),UE::Widget::WM_Rotate);
        auto* HoldMode=Modes.GetActiveMode(FMDHoldSnapMode::ID);
        if (HoldMode) { TestFalse(TEXT("Rotation gizmo does not arm hold snap"),HoldMode->InputKey(Client,Client->Viewport,EKeys::B,IE_Pressed)); }
        Client->SetWidgetModeOverride(UE::Widget::WM_None);
        Client->SetWidgetMode(UE::Widget::WM_Translate);
        Modes.InputKey(Client,Client->Viewport,EKeys::B,IE_Pressed,false);
        auto* Mode=Modes.GetActiveMode(FMDHoldSnapMode::ID);
        if (Mode) { Mode->LostFocus(Client,Client->Viewport); }
        TestTrue(TEXT("Focus loss clears held mode"),MDHoldSnap::Status().IsEmpty());
        Client->SetCurrentWidgetAxis(OldAxis); Client->SetWidgetMode(OldWidget); MDPlacement::SessionOptions()=OldOptions;
    }
    // Audit only contexts used by the ordinary level viewport, rather than unrelated asset editors.
    for (const FName Context : {FName("LevelEditor"),FName("EditorViewport"),FName("GenericCommands")})
    {
        for (const FKey Key : {EKeys::B,EKeys::N,EKeys::J,EKeys::U,EKeys::I,EKeys::Y})
        {
            const auto Conflict=FInputBindingManager::Get().GetCommandInfoFromInputChord(Context,FInputChord(Key),false);
            if (Conflict) { AddWarning(FString::Printf(TEXT("Shortcut conflict: %s %s"),*Key.ToString(),*Conflict->GetCommandName().ToString())); }
        }
    }
    GEditor->SelectNone(false,true); World->DestroyActor(Source); World->DestroyActor(Target);
    World->GetOutermost()->SetDirtyFlag(bWasDirty);
    return true;
}
#endif
