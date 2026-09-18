#pragma once
#include "CoreMinimal.h"
#include "EdMode.h"
#include "MDPlacementTools.h"

namespace MDHoldSnap
{
    enum class EMode : uint8 { None, Endpoint, Midpoint, Rotate, Floor, Slope, Grid };
    void Register();
    void Unregister();
    FText Shortcuts();
    FText Status();
    bool ApplyImmediate(AActor* Source, EMode Mode, FText& Result);
    bool ApplyTarget(AActor* Source, AActor* Target, EMode Mode,
        const FVector& RayOrigin, const FVector& RayDirection, FText& Result);
}

class FMDHoldSnapMode : public FEdMode
{
public:
    static const FEditorModeID ID;
    virtual bool IsCompatibleWith(FEditorModeID Other) const override;
    virtual bool InputKey(FEditorViewportClient* Client, FViewport* Viewport, FKey Key, EInputEvent Event) override;
    virtual bool HandleClick(FEditorViewportClient* Client, HHitProxy* HitProxy, const FViewportClick& Click) override;
    virtual bool LostFocus(FEditorViewportClient* Client, FViewport* Viewport) override;
    virtual void Tick(FEditorViewportClient* Client, float DeltaTime) override;
    virtual void DrawHUD(FEditorViewportClient* Client, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) override;
    virtual void Exit() override;
private:
    void Release();
    MDHoldSnap::EMode Held = MDHoldSnap::EMode::None;
    FKey HeldKey;
    TWeakObjectPtr<AActor> Source;
};