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
#include "Engine/World.h"
#include "EngineUtils.h"
#include "LevelEditor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UICommandInfo.h"
#include "SceneView.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "UnrealEdGlobals.h"

#define LOCTEXT_NAMESPACE "MDHoldSnap"
namespace
{
    FText LiveStatus;
    FDelegateHandle LevelEditorCreatedHandle;
    class FHoldCommands : public TCommands<FHoldCommands>
    {
    public:
        FHoldCommands() : TCommands("MDHoldSnap", LOCTEXT("ClickContext", "Multi-User Placement"), NAME_None, FAppStyle::GetAppStyleSetName()) {}
        TSharedPtr<FUICommandInfo> Endpoint, Floor, Grid;
        virtual void RegisterCommands() override
        {
            UI_COMMAND(Endpoint, "Click: edge endpoint", "Hold the key and click a target actor to match edge endpoints.", EUserInterfaceActionType::Button, FInputChord(EKeys::B));
            UI_COMMAND(Floor, "Drop vertically", "Press once to drop straight down. XY, rotation and scale are unchanged.", EUserInterfaceActionType::Button, FInputChord(EKeys::U));
            UI_COMMAND(Grid, "Snap to grid", "Press once to snap the selected actor pivot to the editor grid.", EUserInterfaceActionType::Button, FInputChord(EKeys::Y));
        }
        TArray<TPair<MDHoldSnap::EMode,TSharedPtr<FUICommandInfo>>> All() const
        { return {{MDHoldSnap::EMode::Endpoint,Endpoint},{MDHoldSnap::EMode::Floor,Floor},{MDHoldSnap::EMode::Grid,Grid}}; }
    };
    bool CommitPlacement(AActor* Source, const FTransform& Transform, FText& Result)
    {
        if (!MDPlacement::CanMove(Source,Result) || Source!=MDPlacement::SelectedActor()) { return false; }
        if (Transform.ContainsNaN()) { return false; }
        if (Source->GetActorTransform().Equals(Transform,.001))
        { Result=MDPlacement::Text(TEXT("이미 정렬되어 있습니다."),TEXT("Already aligned.")); return true; }
        const FScopedTransaction Transaction(MDPlacement::Text(TEXT("클릭 스냅"),TEXT("Click snap")));
        Source->Modify(); Source->GetRootComponent()->Modify();
        Source->SetActorTransform(Transform,false,nullptr,ETeleportType::TeleportPhysics);
        Source->PostEditMove(true);
        Source->MarkPackageDirty();
        GLevelEditorModeTools().SetPivotLocation(Source->GetActorLocation(),false);
        GEditor->RedrawLevelEditingViewports();
        Result=MDPlacement::Text(TEXT("스냅 완료 · Ctrl+Z로 되돌리기"),TEXT("Snapped. Ctrl+Z to undo."));
        return true;
    }
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
        TArray<FText> Keys;
        for (const auto& Command:FHoldCommands::Get().All()) { Keys.Add(Command.Value->GetActiveChord(EMultipleKeyBindingIndex::Primary)->GetInputText()); }
        return FText::Format(MDPlacement::Text(
            TEXT("액터 선택 후\n{1} 한 번: 수직 바닥 스냅\n{0} 누른 채 대상 클릭: 모서리 끝점 맞춤\n{2} 한 번: 그리드 맞춤\n키 놓기 / Esc: 대기 취소 · Ctrl+Z: 실행 취소"),
            TEXT("Select an actor\nPress {1}: vertical drop\nHold {0} + click target: edge endpoint\nPress {2}: grid snap\nRelease / Esc: cancel targeting · Ctrl+Z: undo")),Keys[0],Keys[1],Keys[2]);
    }
    bool ApplyImmediate(AActor* Source, EMode Mode, FText& Result)
    {
        if (!MDPlacement::CanMove(Source,Result)) { return false; }
        FTransform Transform=Source->GetActorTransform();
        if (Mode==EMode::Grid)
        { Transform.SetLocation(Transform.GetLocation().GridSnap(FMath::Max(KINDA_SMALL_NUMBER,GEditor->GetGridSize()))); }
        else if (Mode==EMode::Floor)
        {
            auto* Mesh=MDPlacement::RootMesh(Source);
            if (!Mesh || !Mesh->GetStaticMesh())
            { Result=MDPlacement::Text(TEXT("메시가 루트인 액터를 선택하세요."),TEXT("Select an actor with a static mesh root.")); return false; }
            const FBox Bounds=Mesh->GetStaticMesh()->GetBoundingBox().TransformBy(Transform);
            const FVector Start(Bounds.GetCenter().X,Bounds.GetCenter().Y,Bounds.Min.Z+.1);
            FCollisionQueryParams Params(SCENE_QUERY_STAT(MDClickFloor),true,Source);
            FHitResult Hit;
            if (!Source->GetWorld()->LineTraceSingleByChannel(Hit,Start,Start-FVector(0,0,100000),ECC_Visibility,Params)
                || Hit.bStartPenetrating || !Hit.GetActor() || Hit.GetActor()->IsHiddenEd() || Hit.ImpactNormal.Z<.1)
            { Result=MDPlacement::Text(TEXT("액터 바로 아래에 충돌 바닥이 없습니다."),TEXT("No collision floor directly below the actor.")); return false; }
            Transform.AddToTranslation(FVector(0,0,FMath::Min(0.,Hit.ImpactPoint.Z-Bounds.Min.Z)));
        }
        else { return false; }
        return CommitPlacement(Source,Transform,Result);
    }
    bool ApplyTarget(AActor* Source,AActor* Target,EMode Mode,const FVector& RayOrigin,const FVector& RayDirection,FText& Result)
    {
        if (Mode!=EMode::Endpoint) { Result=MDPlacement::Text(TEXT("비활성화된 배치 기능입니다."),TEXT("This placement feature is disabled.")); return false; }
        if (!MDPlacement::CanMove(Source,Result)) { return false; }
        if (!IsValid(Target) || Target==Source || Target->GetWorld()!=Source->GetWorld() || Target->IsHiddenEd())
        { Result=MDPlacement::Text(TEXT("다른 대상 액터를 클릭하세요."),TEXT("Click a different target actor.")); return false; }
        auto* SourceMesh=MDPlacement::RootMesh(Source);
        auto* TargetMesh=MDPlacement::RootMesh(Target);
        if (!SourceMesh || !SourceMesh->GetStaticMesh() || !TargetMesh || !TargetMesh->GetStaticMesh())
        { Result=MDPlacement::Text(TEXT("두 액터 모두 메시가 루트여야 합니다."),TEXT("Both actors need a static mesh root.")); return false; }
        const FTransform Before=Source->GetActorTransform();
        FHitResult Hit;
        const bool bHit=TargetMesh->LineTraceComponent(Hit,RayOrigin,RayOrigin+RayDirection.GetSafeNormal()*1000000,
            FCollisionQueryParams(SCENE_QUERY_STAT(MDClickSurface),true,Source));
        MDPlacement::FOptions Options=MDPlacement::SessionOptions();
        // Explicitly clicking a target authorizes the distance to that target, not a nearby search radius.
        Options.Distance=FVector::Distance(Source->GetActorLocation(),Target->GetActorLocation())
            +Source->GetComponentsBoundingBox(true).GetSize().Length()+Target->GetComponentsBoundingBox(true).GetSize().Length()+1;
        Options.bRotate=false; Options.Anchor=MDPlacement::EAnchor::Endpoint;
        if (bHit) { Options.TargetPoint=Hit.ImpactPoint; }
        TArray<MDPlacement::FCandidate> Matches;
        MDPlacement::FindCandidates(Source,Target,Options,Matches,Result,&Before);
        if (Matches.IsEmpty()) { return false; }
        return CommitPlacement(Source,Matches[0].Transform,Result);
    }
}

bool FMDHoldSnapMode::IsCompatibleWith(FEditorModeID Other) const { return Other==FBuiltinEditorModes::EM_Default; }
void FMDHoldSnapMode::Release() { Held=MDHoldSnap::EMode::None; HeldKey=FKey(); Source.Reset(); LiveStatus=FText::GetEmpty(); }
bool FMDHoldSnapMode::InputKey(FEditorViewportClient* Client,FViewport* Viewport,FKey Key,EInputEvent Event)
{
    if (Event==IE_Released && Key==HeldKey) { Release(); return true; }
    if (Key==EKeys::Escape && Event==IE_Pressed && HeldKey.IsValid()) { Release(); return true; }
    if (Event==IE_Repeat) { return Key==HeldKey; }
    if (Event!=IE_Pressed || !GEditor || !Client || Client->GetWorld()!=GEditor->GetEditorWorldContext().World()) { return false; }
    if (Viewport && (Viewport->KeyState(EKeys::RightMouseButton) || Viewport->KeyState(EKeys::MiddleMouseButton) || Viewport->KeyState(EKeys::LeftMouseButton))) { return false; }
    const FInputChord Chord(Key,Client->IsShiftPressed(),Client->IsCtrlPressed(),Client->IsAltPressed(),Client->IsCmdPressed());
    const auto Commands=FHoldCommands::Get().All();
    for (int32 I=0;I<Commands.Num();++I)
    {
        if (!Commands[I].Value->HasActiveChord(Chord)) { continue; }
        Release();
        AActor* Actor=MDPlacement::SelectedActor();
        if (!MDPlacement::CanMove(Actor,LiveStatus)) { return true; }
        HeldKey=Key;
        const auto Mode=Commands[I].Key;
        if (Mode==MDHoldSnap::EMode::Floor || Mode==MDHoldSnap::EMode::Grid)
        { MDHoldSnap::ApplyImmediate(Actor,Mode,LiveStatus); return true; }
        if (!MDPlacement::RootMesh(Actor))
        { LiveStatus=MDPlacement::Text(TEXT("메시가 루트인 액터를 선택하세요."),TEXT("Select an actor with a static mesh root.")); return true; }
        Source=Actor; Held=Mode;
        LiveStatus=MDPlacement::Text(TEXT("키를 누른 채 다른 대상 액터를 클릭하세요. Esc: 취소"),TEXT("Keep the key held and click another actor. Esc: cancel."));
        return true;
    }
    return false;
}
bool FMDHoldSnapMode::HandleClick(FEditorViewportClient* Client,HHitProxy* HitProxy,const FViewportClick& Click)
{
    if (Held==MDHoldSnap::EMode::None || Click.GetKey()!=EKeys::LeftMouseButton) { return false; }
    if (!Source.IsValid() || Source.Get()!=MDPlacement::SelectedActor()) { Release(); return false; }
    const FInputChord Chord(HeldKey,Client->IsShiftPressed(),Client->IsCtrlPressed(),Client->IsAltPressed(),Client->IsCmdPressed());
    if (!FHoldCommands::Get().Endpoint->HasActiveChord(Chord)) { Release(); return false; }
    AActor* Target=HitProxy && HitProxy->IsA(HActor::StaticGetType()) ? static_cast<HActor*>(HitProxy)->Actor.Get() : nullptr;
    MDHoldSnap::ApplyTarget(Source.Get(),Target,Held,Click.GetOrigin(),Click.GetDirection(),LiveStatus);
    return true; // Consume target selection; keep the original moving actor selected.
}
bool FMDHoldSnapMode::LostFocus(FEditorViewportClient*,FViewport*) { Release(); return false; }
void FMDHoldSnapMode::Tick(FEditorViewportClient* Client,float DeltaTime)
{
    FEdMode::Tick(Client,DeltaTime);
    if (HeldKey.IsValid() && (!FSlateApplication::Get().IsActive() || GEditor->PlayWorld || (Source.IsValid() && Source.Get()!=MDPlacement::SelectedActor()))) { Release(); }
}
void FMDHoldSnapMode::DrawHUD(FEditorViewportClient*,FViewport*,const FSceneView*,FCanvas* Canvas)
{
    if (!LiveStatus.IsEmpty() && Canvas && GEngine)
    {
        FCanvasTextItem Item(FVector2D(24,70),LiveStatus,GEngine->GetSmallFont(),FLinearColor(.2f,1.f,.8f));
        Item.EnableShadow(FLinearColor::Black); Canvas->DrawItem(Item);
    }
}
void FMDHoldSnapMode::Exit() { Release(); FEdMode::Exit(); }
#undef LOCTEXT_NAMESPACE