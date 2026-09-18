#pragma once
#include "CoreMinimal.h"
#include "EdMode.h"
#include "MDPlacementTools.h"

class FScopedTransaction;

namespace MDHoldSnap
{
    enum class EMode : uint8 { None, Endpoint, Midpoint, Rotate, Floor, Slope, Grid };
    void Register();
    void Unregister();
    FText Shortcuts();
    FText Status();
    bool Solve(AActor* Source, const FTransform& Desired, EMode Mode,
        const MDPlacement::FOptions& Options, FTransform& Out, FText& Result, MDPlacement::FGeometryCache* Cache = nullptr);

    // Native viewport tracking owns the transaction; this session never starts nested transactions.
    class FDrag
    {
    public:
        FDrag();
        ~FDrag();
        void Begin(AActor* Actor);
        void Finish();
        void Rebase();
        bool Move(EMode Mode, const FVector& Delta, const MDPlacement::FOptions& Options, FText& Result);
        void Cancel(bool bIncludeNativeMovement = false);
        void Reset();
        bool HasSource() const { return Source.IsValid(); }
        bool WasUsed() const { return bMoved; }
    private:
        TWeakObjectPtr<AActor> Source;
        FTransform Start, Desired, Last;
        TUniquePtr<FScopedTransaction> OwnedTransaction;
        bool bMoved = false;
        MDPlacement::FGeometryCache Cache;
    };
}

class FMDHoldSnapMode : public FEdMode
{
public:
    static const FEditorModeID ID;
    virtual bool IsCompatibleWith(FEditorModeID Other) const override;
    virtual bool InputKey(FEditorViewportClient* Client, FViewport* Viewport, FKey Key, EInputEvent Event) override;
    virtual bool InputDelta(FEditorViewportClient* Client, FViewport* Viewport, FVector& Drag, FRotator& Rot, FVector& Scale) override;
    virtual bool StartTracking(FEditorViewportClient* Client, FViewport* Viewport) override;
    virtual bool EndTracking(FEditorViewportClient* Client, FViewport* Viewport) override;
    virtual bool LostFocus(FEditorViewportClient* Client, FViewport* Viewport) override;
    virtual void Tick(FEditorViewportClient* Client, float DeltaTime) override;
    virtual void DrawHUD(FEditorViewportClient* Client, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) override;
    virtual void Exit() override;
private:
    void Release();
    MDHoldSnap::EMode Held = MDHoldSnap::EMode::None;
    FKey HeldKey;
    MDHoldSnap::FDrag Session;
    FEditorViewportClient* DragClient = nullptr;
    bool bCanceled = false;
};
