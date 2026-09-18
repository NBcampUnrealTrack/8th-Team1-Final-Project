#include "MDHoldSnap.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "EditorModeRegistry.h"
#include "EditorModes.h"
#include "EditorViewportClient.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "LevelEditor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UICommandInfo.h"
#include "SceneView.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "UnrealEdGlobals.h"
#include "UnrealWidget.h"

#define LOCTEXT_NAMESPACE "MDHoldSnap"
namespace
{
    FText LiveStatus;
    FDelegateHandle LevelEditorCreatedHandle;
    class FHoldCommands : public TCommands<FHoldCommands>
    {
    public:
        FHoldCommands() : TCommands("MDHoldSnap", LOCTEXT("Context", "Multi-User Placement (hold while dragging)"), NAME_None, FAppStyle::GetAppStyleSetName()) {}
        TSharedPtr<FUICommandInfo> Endpoint, Midpoint, Rotate, Floor, Slope, Grid;
        virtual void RegisterCommands() override
        {
            UI_COMMAND(Endpoint, "Hold: edge endpoint", "Hold while dragging the move gizmo to snap edge endpoints.", EUserInterfaceActionType::Button, FInputChord(EKeys::B));
            UI_COMMAND(Midpoint, "Hold: edge midpoint", "Hold while dragging to match edge midpoints.", EUserInterfaceActionType::Button, FInputChord(EKeys::N));
            UI_COMMAND(Rotate, "Hold: edge rotation", "Hold while dragging to align edge direction and anchor.", EUserInterfaceActionType::Button, FInputChord(EKeys::J));
            UI_COMMAND(Floor, "Hold: floor", "Hold while dragging to place the actor on the surface below.", EUserInterfaceActionType::Button, FInputChord(EKeys::U));
            UI_COMMAND(Slope, "Hold: slope", "Hold while dragging to align the actor up axis with the surface below.", EUserInterfaceActionType::Button, FInputChord(EKeys::I));
            UI_COMMAND(Grid, "Hold: grid", "Hold while dragging to snap the actor pivot to the editor grid.", EUserInterfaceActionType::Button, FInputChord(EKeys::Y));
        }
        TArray<TSharedPtr<FUICommandInfo>> All() const { return {Endpoint,Midpoint,Rotate,Floor,Slope,Grid}; }
    };
}

const FEditorModeID FMDHoldSnapMode::ID = TEXT("EM_MDHoldSnap");
namespace MDHoldSnap
{
    void Register()
    {
        if (IsRunningCommandlet()) { return; }
        FHoldCommands::Register();
        FEditorModeRegistry::Get().RegisterMode<FMDHoldSnapMode>(FMDHoldSnapMode::ID, LOCTEXT("Mode", "Hold snap"), FSlateIcon(), false);
        // PostEngineInit precedes creation of the level editor in UE 5.8.
        // Wait for its mode manager instead of triggering the early-access ensure.
        auto InstallMode = [] {
            auto& Modes = GLevelEditorModeTools();
            const bool bDefault = Modes.IsDefaultModeActive();
            Modes.AddDefaultMode(FMDHoldSnapMode::ID);
            if (bDefault) { Modes.ActivateMode(FMDHoldSnapMode::ID); }
        };
        auto& LevelEditor = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
        LevelEditorCreatedHandle = LevelEditor.OnLevelEditorCreated().AddLambda([InstallMode](TSharedPtr<ILevelEditor>) { InstallMode(); });
        if (LevelEditor.GetLevelEditorInstance().IsValid()) { InstallMode(); }
    }
    void Unregister()
    {
        if (!FHoldCommands::IsRegistered()) { return; }
        auto* LevelEditor = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor"));
        if (LevelEditor)
        { LevelEditor->OnLevelEditorCreated().Remove(LevelEditorCreatedHandle); }
        LevelEditorCreatedHandle.Reset();
        if (GEditor && LevelEditor && LevelEditor->GetLevelEditorInstance().IsValid())
        {
            auto& Modes = GLevelEditorModeTools();
            Modes.RemoveDefaultMode(FMDHoldSnapMode::ID);
            Modes.DeactivateMode(FMDHoldSnapMode::ID);
        }
        FEditorModeRegistry::Get().UnregisterMode(FMDHoldSnapMode::ID);
        FHoldCommands::Unregister();
        LiveStatus = FText::GetEmpty();
    }
    FText Status() { return LiveStatus; }
    FText Shortcuts()
    {
        if (!FHoldCommands::IsRegistered()) { return FText::GetEmpty(); }
        const auto Commands = FHoldCommands::Get().All();
        TArray<FText> Keys;
        for (const auto& Command : Commands) { Keys.Add(Command->GetActiveChord(EMultipleKeyBindingIndex::Primary)->GetInputText()); }
        return FText::Format(MDPlacement::Text(
            TEXT("키를 누른 채 이동 기즈모 드래그\n{0} 끝점 · {1} 중앙 · {2} 모서리 회전\n{3} 바닥 · {4} 경사면 · {5} 그리드\n키 놓기: 일반 이동 · Esc: 드래그 취소\nF: 선택 보기 · End: 바닥 스냅"),
            TEXT("Hold a key and drag the move gizmo\n{0} Endpoint · {1} Midpoint · {2} Edge rotation\n{3} Floor · {4} Slope · {5} Grid\nRelease key: free move · Esc: cancel drag\nF: Frame selection · End: Drop to floor")), Keys[0],Keys[1],Keys[2],Keys[3],Keys[4],Keys[5]);
    }

    bool Solve(AActor* Source, const FTransform& Desired, EMode Mode, const MDPlacement::FOptions& Options, FTransform& Out, FText& Result, MDPlacement::FGeometryCache* Cache)
    {
        Out = Desired;
        if (Mode == EMode::None) { return false; }
        if (Mode == EMode::Grid)
        {
            Out.SetLocation(Desired.GetLocation().GridSnap(FMath::Max(KINDA_SMALL_NUMBER, GEditor->GetGridSize())));
            Result = MDPlacement::Text(TEXT("그리드 맞춤"),TEXT("Grid snap")); return true;
        }
        if (Mode == EMode::Endpoint || Mode == EMode::Midpoint || Mode == EMode::Rotate)
        {
            MDPlacement::FOptions MatchOptions = Options;
            MatchOptions.bRotate = Mode == EMode::Rotate;
            if (Mode != EMode::Rotate) { MatchOptions.Anchor = Mode == EMode::Endpoint ? MDPlacement::EAnchor::Endpoint : MDPlacement::EAnchor::Midpoint; }
            TArray<MDPlacement::FCandidate> Matches;
            MDPlacement::FindCandidates(Source, nullptr, MatchOptions, Matches, Result, &Desired, Cache);
            if (Matches.IsEmpty()) { Result=MDPlacement::Text(TEXT("대상 모서리에 가까이 이동하세요."),TEXT("Move closer to a target edge.")); return false; }
            Out = Matches[0].Transform;
            Result = FText::Format(MDPlacement::Text(TEXT("스냅: {0}"),TEXT("Snap: {0}")),FText::FromString(Matches[0].Target->GetActorLabel()));
            return true;
        }
        const auto* Actor = Cast<AStaticMeshActor>(Source);
        const auto* Component = Actor ? Actor->GetStaticMeshComponent() : nullptr;
        const UStaticMesh* Mesh = Component ? Component->GetStaticMesh() : nullptr;
        if (!Mesh || Component != Source->GetRootComponent()) { return false; }
        const FBox LocalBounds = Mesh->GetBoundingBox();
        const FBox WorldBounds = LocalBounds.TransformBy(Desired);
        const double Radius = Options.Distance;
        // Trace from the mesh's bottom rather than through its top; ignore the moving actor.
        const FVector Start(Desired.GetLocation().X,Desired.GetLocation().Y,WorldBounds.Min.Z+Radius);
        const FVector End(Start.X,Start.Y,WorldBounds.Min.Z-Radius);
        FCollisionQueryParams Params(SCENE_QUERY_STAT(MDHoldFloor),true,Source);
        FHitResult Hit;
        if (!Source->GetWorld()->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility,Params)
            || !Hit.GetActor() || Hit.GetActor()->IsHiddenEd() || Hit.ImpactNormal.Z < .1)
        { Result = MDPlacement::Text(TEXT("아래 표면에 가까이 이동하세요."),TEXT("Move closer to a surface below.")); return false; }
        const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
        if (Mode == EMode::Slope)
        {
            const FQuat Delta = FQuat::FindBetweenNormals(Desired.GetRotation().GetUpVector(),Normal);
            Out.SetRotation((Delta*Desired.GetRotation()).GetNormalized());
        }
        double Support = TNumericLimits<double>::Max();
        for (int32 I=0; I<8; ++I)
        {
            const FVector Corner((I&1)?LocalBounds.Max.X:LocalBounds.Min.X,(I&2)?LocalBounds.Max.Y:LocalBounds.Min.Y,(I&4)?LocalBounds.Max.Z:LocalBounds.Min.Z);
            Support = FMath::Min(Support,FVector::DotProduct(Out.TransformVector(Corner),Normal));
        }
        const double Drop = (FVector::DotProduct(Hit.ImpactPoint-Desired.GetLocation(),Normal)-Support)/Normal.Z;
        if (FMath::Abs(Drop) > Radius) { Result=MDPlacement::Text(TEXT("표면이 이동 범위 밖에 있습니다."),TEXT("Surface is beyond snap distance.")); return false; }
        Out.AddToTranslation(FVector(0,0,Drop));
        Result = FText::Format(MDPlacement::Text(TEXT("표면: {0}"),TEXT("Surface: {0}")),FText::FromString(Hit.GetActor()->GetActorLabel()));
        return true;
    }

    FDrag::FDrag() = default;
    FDrag::~FDrag() = default;
    void FDrag::Finish()
    {
        if (bMoved && Source.IsValid()) { Source->PostEditMove(true); Source->MarkPackageDirty(); }
        Reset();
    }
    void FDrag::Begin(AActor* Actor)
    {
        Source=Actor;
        if (Actor) { Start=Desired=Last=Actor->GetActorTransform(); }
    }
    void FDrag::Rebase() { if (Source.IsValid()) { Desired=Last=Source->GetActorTransform(); } }
    void FDrag::Reset() { OwnedTransaction.Reset(); Source.Reset(); bMoved=false; Cache.Reset(); }
    void FDrag::Cancel(bool bIncludeNativeMovement)
    {
        FText Reason;
        // Key-up returns movement to the native gizmo, which can advance beyond Last.
        if (Source.IsValid() && Source.Get()==MDPlacement::SelectedActor() && MDPlacement::CanMove(Source.Get(),Reason)
            && (bIncludeNativeMovement || Source->GetActorTransform().Equals(Last,.001)))
        {
            Source->SetActorTransform(Start,false,nullptr,ETeleportType::TeleportPhysics);
            Source->PostEditMove(false);
            GLevelEditorModeTools().SetPivotLocation(Start.GetLocation(),false);
        }
        if (OwnedTransaction) { OwnedTransaction->Cancel(); }
        Reset();
    }
    bool FDrag::Move(EMode Mode,const FVector& Delta,const MDPlacement::FOptions& Options,FText& Result)
    {
        if (!Source.IsValid() || Source.Get()!=MDPlacement::SelectedActor() || !MDPlacement::CanMove(Source.Get(),Result)) { Reset(); return false; }
        if (!Source->GetActorTransform().Equals(Last,.001))
        { Result=MDPlacement::Text(TEXT("외부 위치 변경으로 스냅을 중지했습니다."),TEXT("Snap stopped: actor transform changed externally.")); Reset(); return false; }
        Desired.AddToTranslation(Delta);
        FTransform Applied;
        Solve(Source.Get(),Desired,Mode,Options,Applied,Result,&Cache);
        if (!GEditor->IsTransactionActive() && !OwnedTransaction)
        { OwnedTransaction = MakeUnique<FScopedTransaction>(MDPlacement::Text(TEXT("드래그 스냅"),TEXT("Drag snap"))); }
        Source->Modify(); Source->GetRootComponent()->Modify();
        if (!Source->SetActorTransform(Applied,false,nullptr,ETeleportType::TeleportPhysics)) { Reset(); return false; }
        Source->PostEditMove(false);
        bMoved=true;
        Last=Source->GetActorTransform();
        GLevelEditorModeTools().SetPivotLocation(Last.GetLocation(),false);
        GEditor->RedrawLevelEditingViewports();
        return true;
    }
}

bool FMDHoldSnapMode::IsCompatibleWith(FEditorModeID Other) const { return Other==FBuiltinEditorModes::EM_Default; }
void FMDHoldSnapMode::Release() { Held=MDHoldSnap::EMode::None; HeldKey=FKey(); Session.Rebase(); LiveStatus=FText::GetEmpty(); }
bool FMDHoldSnapMode::InputKey(FEditorViewportClient* Client,FViewport* Viewport,FKey Key,EInputEvent Event)
{
    if (Event==IE_Released && Key==HeldKey) { Release(); return true; }
    if (Key==EKeys::Escape && Event==IE_Pressed && DragClient && Session.HasSource() && Session.WasUsed())
    { Session.Cancel(Held==MDHoldSnap::EMode::None); Release(); bCanceled=true; return true; }
    if (Event==IE_Repeat) { return Key==HeldKey; }
    if (Event!=IE_Pressed) { return false; }
    if (!GEditor || !Client || Client->GetWorld()!=GEditor->GetEditorWorldContext().World()
        || Client->GetWidgetMode()!=UE::Widget::WM_Translate || !Cast<AStaticMeshActor>(MDPlacement::SelectedActor())) { return false; }
    if (Viewport && (Viewport->KeyState(EKeys::RightMouseButton) || Viewport->KeyState(EKeys::MiddleMouseButton))) { return false; }
    const FInputChord Chord(Key,Client->IsShiftPressed(),Client->IsCtrlPressed(),Client->IsAltPressed(),Client->IsCmdPressed());
    const auto Commands=FHoldCommands::Get().All();
    for (int32 I=0; I<Commands.Num(); ++I)
    {
        if (Commands[I]->HasActiveChord(Chord))
        {
            FText Reason;
            if (!MDPlacement::CanMove(MDPlacement::SelectedActor(),Reason)) { LiveStatus=Reason; return true; }
            const auto Mode=static_cast<MDHoldSnap::EMode>(I+1);
            if (Held!=Mode) { Session.Rebase(); }
            Held=Mode; HeldKey=Key;
            LiveStatus=MDPlacement::Text(TEXT("스냅 준비 · 이동 기즈모를 드래그하세요."),TEXT("Snap ready. Drag the move gizmo."));
            return true;
        }
    }
    return false;
}
bool FMDHoldSnapMode::StartTracking(FEditorViewportClient* Client,FViewport*)
{
    if (Client && Client->GetWidgetMode()==UE::Widget::WM_Translate && Client->GetCurrentWidgetAxis()!=EAxisList::None)
    { DragClient=Client; bCanceled=false; Session.Begin(MDPlacement::SelectedActor()); }
    return false;
}
bool FMDHoldSnapMode::EndTracking(FEditorViewportClient*,FViewport*)
{ Session.Finish(); DragClient=nullptr; bCanceled=false; LiveStatus=FText::GetEmpty(); return false; }
bool FMDHoldSnapMode::InputDelta(FEditorViewportClient* Client,FViewport*,FVector& Drag,FRotator& Rot,FVector& Scale)
{
    if (Client!=DragClient) { return false; }
    if (bCanceled) { return true; }
    if (Held==MDHoldSnap::EMode::None || !Rot.IsNearlyZero() || !Scale.IsNearlyZero()) { return false; }
    const FInputChord CurrentChord(HeldKey,Client->IsShiftPressed(),Client->IsCtrlPressed(),Client->IsAltPressed(),Client->IsCmdPressed());
    if (!FHoldCommands::Get().All()[static_cast<int32>(Held)-1]->HasActiveChord(CurrentChord)) { Release(); return false; }
    if (Client->IsAltPressed()) { Release(); return false; } // Never interfere with Alt-duplicate.
    if (Drag.IsNearlyZero()) { return true; }
    if (!Session.Move(Held,Drag,MDPlacement::SessionOptions(),LiveStatus)) { const FText Reason=LiveStatus; Release(); LiveStatus=Reason; bCanceled=true; }
    return true;
}
bool FMDHoldSnapMode::LostFocus(FEditorViewportClient*,FViewport*) { Release(); return false; }
void FMDHoldSnapMode::Tick(FEditorViewportClient* Client,float DeltaTime)
{
    FEdMode::Tick(Client,DeltaTime);
    if (Held!=MDHoldSnap::EMode::None && (!FSlateApplication::Get().IsActive() || GEditor->PlayWorld)) { Release(); }
}
void FMDHoldSnapMode::DrawHUD(FEditorViewportClient*,FViewport*,const FSceneView*,FCanvas* Canvas)
{
    if (!LiveStatus.IsEmpty() && Canvas && GEngine)
    {
        FCanvasTextItem Item(FVector2D(24,70),LiveStatus,GEngine->GetSmallFont(),FLinearColor(.2f,1.f,.8f));
        Item.EnableShadow(FLinearColor::Black); Canvas->DrawItem(Item);
    }
}
void FMDHoldSnapMode::Exit() { Release(); Session.Finish(); DragClient=nullptr; FEdMode::Exit(); }
#undef LOCTEXT_NAMESPACE
