#pragma once

#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "InteractiveTool.h"
#include "Engine/HitResult.h"
#include "InteractiveToolBuilder.h"
#include "MD_StainPlacementTool.generated.h"

class UStaticMeshComponent;
class AActor;
class UStaticMesh;
class UMD_SurfaceStainDefinition;

UCLASS()
class UMD_StainPlacementToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()
public:
	virtual bool CanBuildTool(const FToolBuilderState& State) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& State) const override;
};

UCLASS(Transient)
class UMD_StainPlacementProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Stains")
	TArray<TObjectPtr<UMD_SurfaceStainDefinition>> Palette;

	UPROPERTY(EditAnywhere, Category = "Stains")
	bool bRandomVariant = false;

	UPROPERTY(EditAnywhere, Category = "Stains", meta = (ClampMin = "0", EditCondition = "!bRandomVariant"))
	int32 VariantIndex = 0;

	UPROPERTY(EditAnywhere, Category = "Size", meta = (ClampMin = "100", ClampMax = "10000", EditCondition = "!bRandomSize"))
	int32 InitialSize = 300;

	UPROPERTY(EditAnywhere, Category = "Size")
	bool bRandomSize = false;

	UPROPERTY(EditAnywhere, Category = "Size", meta = (ClampMin = "100", ClampMax = "10000", EditCondition = "bRandomSize"))
	int32 MinSize = 100;

	UPROPERTY(EditAnywhere, Category = "Size", meta = (ClampMin = "100", ClampMax = "10000", EditCondition = "bRandomSize"))
	int32 MaxSize = 10000;

	UPROPERTY(EditAnywhere, Category = "Placement", meta = (ClampMin = "1", Units = "cm"))
	float SpacingCm = 60.0f;

	UPROPERTY(EditAnywhere, Category = "Placement", meta = (Units = "deg"))
	float RotationDegrees = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Placement")
	bool bRandomRotation = true;

	UPROPERTY(EditAnywhere, Category = "Placement")
	int32 RandomSeed = 1337;

	UPROPERTY(EditAnywhere, Category = "Placement")
	bool bErase = false;
};

/** Decal-specific placement, isolated from the mesh overlap/stacking policy of Trash. */
UCLASS()
class UMD_StainPlacementTool : public UInteractiveTool, public IClickDragBehaviorTarget,
	public IHoverBehaviorTarget, public IMouseWheelBehaviorTarget
{
	GENERATED_BODY()
public:
	void SetWorld(UWorld* World) { TargetWorld = World; }
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType Type) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual void OnTick(float DeltaTime) override;
	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;
	virtual void OnUpdateModifierState(int ModifierId, bool bOn) override;
	virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& Ray) override;
	virtual void OnClickPress(const FInputDeviceRay& Ray) override;
	virtual void OnClickDrag(const FInputDeviceRay& Ray) override;
	virtual void OnClickRelease(const FInputDeviceRay& Ray) override;
	virtual void OnTerminateDragSequence() override;
	virtual FInputRayHit BeginHoverSequenceHitTest(const FInputDeviceRay& Ray) override;
	virtual void OnBeginHover(const FInputDeviceRay& Ray) override;
	virtual bool OnUpdateHover(const FInputDeviceRay& Ray) override;
	virtual void OnEndHover() override;
	virtual FInputRayHit ShouldRespondToMouseWheel(const FInputDeviceRay& Ray) override;
	virtual void OnMouseWheelScrollUp(const FInputDeviceRay& Ray) override;
	virtual void OnMouseWheelScrollDown(const FInputDeviceRay& Ray) override;

private:
	bool Trace(const FInputDeviceRay& Ray, FHitResult& Hit, bool bErase) const;
	UMD_SurfaceStainDefinition* ResolveDefinition() const;
	FTransform MakeTransform(const FHitResult& Hit) const;
	int32 ResolveSize() const;
	void ApplyAt(const FInputDeviceRay& Ray);
	void FinishStroke();
	bool WantsErase() const;
	void ChangeSpacing(float Delta);
	void ShowMovingPreview();

	UPROPERTY() TObjectPtr<UWorld> TargetWorld;
	UPROPERTY() TObjectPtr<UMD_StainPlacementProperties> Properties;
	UPROPERTY(Transient) TObjectPtr<AActor> PreviewOwner;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Preview;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> PreviewPlane;
	FInputDeviceRay LastHoverRay;
	FString PreviewError;
	TWeakObjectPtr<UMD_SurfaceStainDefinition> PreviewDefinition;
	FTransform PreviewTransform;
	int32 PreviewSize = 0;
	double PreviewChangedAt = 0;
	bool bPreviewPending = false;
	FHitResult HoverHit;
	FVector LastPlacedLocation = FVector::ZeroVector;
	int32 PlacementIndex = 0;
	bool bHover = false;
	bool bShift = false;
	bool bStroke = false;
	bool bStrokeErase = false;
	bool bPlaced = false;
};
