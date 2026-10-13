#pragma once

#include "CoreMinimal.h"
#include "UI/MD_CommonActivatableWidget.h"
#include "MD_BreakerOperateWidget.generated.h"

class AMD_RepairInstructionController;
class UMD_InteractionComponent;
class ACameraActor;

/** Local camera and captured mouse drag; only a validated server slot request changes the puzzle. */
UCLASS()
class MARINEDEMOLITION_API UMD_BreakerOperateWidget : public UMD_CommonActivatableWidget
{
	GENERATED_BODY()
public:
	UMD_BreakerOperateWidget();
	void InitializeOperation(AMD_RepairInstructionController* InBreaker, UMD_InteractionComponent* InInteraction, int32 Revision, int32 Id);
	void UseFallbackInput() { bFallbackInput=true; }
	bool PointerPressedRay(const FVector& Origin, const FVector& Direction);
	void PointerMovedRay(const FVector& Origin, const FVector& Direction);
	bool PointerReleased();
	void CancelDrag();
	void HandleSlotResult(AMD_RepairInstructionController* Source, int32 Id, int32 Column, int32 Row, bool bAccepted);
	int32 GetDraggingColumn() const { return DragColumn; }
	ACameraActor* GetOperationCamera() const { return OperationCamera; }
	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply NativeOnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& Event) override;
	virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	virtual bool NativeOnHandleBackAction() override;
	virtual UWidget* NativeGetDesiredFocusTarget() const override { return const_cast<UMD_BreakerOperateWidget*>(this); }
private:
	UPROPERTY(Transient) TObjectPtr<AMD_RepairInstructionController> Breaker;
	UPROPERTY(Transient) TObjectPtr<UMD_InteractionComponent> Interaction;
	UPROPERTY(Transient) TObjectPtr<ACameraActor> OperationCamera;
	TWeakObjectPtr<AActor> PreviousViewTarget;
TArray<TWeakObjectPtr<AActor>> AddedHiddenActors;
	int32 ExpectedRevision=0, ExpectedOperationId=0, DragColumn=INDEX_NONE, PendingColumn=INDEX_NONE, PendingRow=INDEX_NONE;
	double OpenedAt=0, PendingAt=0;
	double DragOffset=0, DragAlpha=0;
	bool bFallbackInput=false, bRestored=true, bPreviousCursor=false, bConfirmedOperation=false;
	FTimerHandle OperationTimer;
	void TickOperation();
	bool RayToPanel(const FVector& Origin, const FVector& Direction, FVector& RootLocalPoint) const;
	bool EventRay(const FPointerEvent& Event, FVector& Origin, FVector& Direction) const;
	void RestoreOperation();
};
