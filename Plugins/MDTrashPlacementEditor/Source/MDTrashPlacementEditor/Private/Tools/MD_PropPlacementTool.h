#pragma once

#include "CoreMinimal.h"
#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "InteractiveTool.h"
#include "InteractiveToolBuilder.h"
#include "Templates/SubclassOf.h"
#include "Tools/MD_PropPlacementCandidate.h"
#include "Presets/MD_PropPlacementPreset.h"
#include "Tools/MD_PropPlacementStroke.h"
#include "Tools/MD_PropMixSelection.h"
#include "Tools/MD_PropPlacementSurface.h"
#include "Tools/MD_PropPlacementSettings.h"
#include "UObject/SoftObjectPtr.h"
#include "MD_PropPlacementTool.generated.h"

class AActor;
class UMaterialInstanceDynamic;
class UStaticMesh;
class UStaticMeshComponent;
class UWorld;
class ULevel;
struct FHitResult;
struct FMD_PropPaletteEntry;

DECLARE_DELEGATE_OneParam(FMD_OnPropPaletteAssetSelected, bool);

UENUM()
enum class EMD_PropPlacementSource : uint8
{
	StaticMesh UMETA(DisplayName = "Static Mesh"),
	Blueprint UMETA(DisplayName = "Actor Blueprint")
};

UENUM()
enum class EMD_PropBrushAction : uint8
{
	Place,
	Erase
};

UENUM()
enum class EMD_PropBrushSelection : uint8
{
	SelectedItem UMETA(DisplayName = "Selected Item"),
	PaletteMix UMETA(DisplayName = "Palette Mix")
};

UCLASS(Transient, meta = (DisplayName = "Prop Placement Settings"))
class UMD_PropPlacementProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Brush", meta = (DisplayName = "Action", ToolTip = "Place는 소품을 배치하고 Erase는 직접 가리킨 소품을 삭제합니다. Place에서도 Shift를 먼저 누르고 클릭·드래그하면 그 드래그만 Erase로 처리합니다.\n\nErase는 이 도구로 배치 표시를 남긴 현재 레벨의 소품만 삭제합니다. 기존 수동 배치와 이전 버전에서 만든 소품은 제외합니다."))
	EMD_PropBrushAction Action = EMD_PropBrushAction::Place;

	UPROPERTY(EditAnywhere, Category = "Brush", meta = (DisplayName = "Selection Mode", EditCondition = "Action == EMD_PropBrushAction::Place", ToolTip = "Selected Item은 선택한 소품을 사용합니다. Palette Mix는 팔레트에서 Mix를 체크한 항목들을 동일한 기본 확률로 무작위 선택하며, 오래 선택되지 않은 항목의 확률을 조금 높입니다. 반복 선택도 가능합니다.\n\n검색·분류 필터는 혼합 대상에 영향을 주지 않습니다. 항목 이름을 클릭하면 Selected Item으로 돌아갑니다."))
	EMD_PropBrushSelection SelectionMode = EMD_PropBrushSelection::SelectedItem;

	UPROPERTY(EditAnywhere, Category = "Brush", meta = (DisplayName = "Continuous", ToolTip = "켜면 왼쪽 드래그로 연속 배치·삭제합니다. 끄면 클릭할 때 한 개만 처리합니다.\n\n버튼을 누른 순간부터 놓을 때까지 한 번의 Undo로 묶습니다."))
	bool bContinuous = true;

	UPROPERTY(EditAnywhere, Category = "Brush", meta = (DisplayName = "Spacing", EditCondition = "bContinuous && Action == EMD_PropBrushAction::Place", ClampMin = "1.0", ClampMax = "100000.0", ForceUnits = "cm", ToolTip = "드래그 경로 위 배치 기준점 사이의 간격입니다. 단위는 cm입니다.\n\n메시 가장자리 사이의 간격이 아닙니다. 크기가 다른 소품은 겹침 방지로 일부 위치의 배치가 생략될 수 있습니다."))
	double Spacing = 100.0;

	UPROPERTY(EditAnywhere, Category = "Brush", meta = (DisplayName = "Max Per Stroke", ClampMin = "1", ClampMax = "5000", ToolTip = "한 번의 드래그에서 처리할 최대 소품 수입니다. 한도에 도달하면 버튼을 놓고 다시 시작하세요."))
	int32 MaxPerStroke = 500;

	UPROPERTY(EditAnywhere, Category = "Brush", meta = (DisplayName = "Mix Seed", EditCondition = "SelectionMode == EMD_PropBrushSelection::PaletteMix", ToolTip = "혼합 대상의 선택 순서를 정하는 시드입니다. 각 항목의 회전·크기 시드와 별개입니다.\n\n배치 성공 또는 Reroll Preview에서 다음 항목과 자세로 진행합니다."))
	int32 MixSeed = 1337;

	UPROPERTY(VisibleAnywhere, Transient, Category = "Brush", meta = (DisplayName = "Preview Item", TransientToolProperty, ToolTip = "다음에 배치할 미리보기 항목입니다. 혼합 배치에서는 팔레트의 편집 선택과 다를 수 있습니다."))
	FText PreviewItem;

	bool IsSelectedPlacement() const { return Action == EMD_PropBrushAction::Place && SelectionMode == EMD_PropBrushSelection::SelectedItem; }

	UPROPERTY(EditAnywhere, Category = "Prop", meta = (DisplayName = "Source", ToolTip = "배치할 에셋 종류를 선택합니다.\n\n팔레트의 소품별 설정을 쓰려면 목록에서 먼저 항목을 선택하세요. 이 값을 직접 바꾸면 팔레트 선택이 해제됩니다."))
	EMD_PropPlacementSource Source = EMD_PropPlacementSource::StaticMesh;

	UPROPERTY(EditAnywhere, Category = "Prop", meta = (EditCondition = "Source == EMD_PropPlacementSource::StaticMesh", EditConditionHides, DisplayName = "Mesh", ToolTip = "표면을 클릭해 배치할 스태틱 메시입니다.\n\n팔레트 항목을 선택하면 자동으로 지정됩니다."))
	TObjectPtr<UStaticMesh> Mesh = nullptr;

	/** 기존 컴포넌트와 게임 설정을 유지한 채 이 블루프린트를 배치합니다. */
	UPROPERTY(EditAnywhere, Category = "Prop", meta = (EditCondition = "Source == EMD_PropPlacementSource::Blueprint", EditConditionHides, OnlyPlaceable, BlueprintBaseOnly, DisplayName = "Actor Blueprint", ToolTip = "배치할 액터 블루프린트를 지정합니다. 기존 컴포넌트와 게임 설정을 사용합니다.\n\n컴파일이 완료된 배치 가능한 블루프린트를 선택하세요."))
	TSubclassOf<AActor> ActorClass;

	/** 액터 로컬 좌표에 맞춘 전체 외형의 미리보기 메시입니다. 생성되는 BP 메시에는 영향을 주지 않습니다. */
	UPROPERTY(EditAnywhere, Category = "Prop", meta = (EditCondition = "Source == EMD_PropPlacementSource::Blueprint", EditConditionHides, DisplayName = "Blueprint Preview Mesh", ToolTip = "기본 메시만으로 외형을 표현하기 어려운 BP에 지정하는 미리보기용 메시입니다.\n\n액터의 로컬 좌표·크기·피벗에 맞는 전체 외형을 사용하세요. 표면 접촉과 겹침 검사에도 이 메시를 사용합니다.\n\n팔레트 항목별로 저장하며, 실제 생성되는 블루프린트의 메시를 바꾸지는 않습니다."))
	TSoftObjectPtr<UStaticMesh> BlueprintPreviewMesh;

	/** 팔레트 항목을 선택하면 복원하며, Save Palette로 해당 항목의 변경값을 보존합니다. */
	UPROPERTY(EditAnywhere, Category = "Placement", meta = (ShowOnlyInnerProperties, DisplayName = "Placement", ToolTip = "편집 선택된 소품의 회전·크기·표면·겹침 설정입니다. 혼합 미리보기와 다를 수 있으며 값을 바꾸면 Selected Item으로 전환합니다.\n\n팔레트 항목을 선택하면 해당 설정을 불러옵니다. 변경 후 Save Palette를 누르면 다음 실행에도 사용할 수 있습니다."))
	FMD_PropPlacementSettings Placement;

	UPROPERTY(EditAnywhere, Category = "Quick Pose", meta = (DisplayName = "Rotation Axis", ToolTip = "회전 버튼과 Shift+휠로 조절할 축을 선택합니다.\n\n소품을 바꿔도 유지되는 도구 조작용 값입니다."))
	EMD_PropRotationAxis RotationAxis = EMD_PropRotationAxis::Yaw;

	/** 회전 버튼과 표면 위 Shift+휠로 한 번에 조절할 각도입니다. */
	UPROPERTY(EditAnywhere, Category = "Quick Pose", meta = (ClampMin = "1.0", ClampMax = "180.0", ForceUnits = "deg", DisplayName = "Rotation Step", ToolTip = "Rotate - / + 버튼 또는 Shift+휠 한 번으로 바꿀 각도입니다.\n\n1~180°로 지정할 수 있습니다. 랜덤 회전 중에는 해당 축의 최소·최대 범위를 함께 이동합니다."))
	double RotationStep = 15.0;

	UFUNCTION(CallInEditor, Category = "Quick Pose", meta = (DisplayName = "Rotate -", ToolTip = "선택한 축을 회전 간격만큼 감소시킵니다.\n\n랜덤 회전 중에는 범위 전체를 이동하고, 현재 선택된 난수는 유지합니다."))
	void RotateBackward();

	UFUNCTION(CallInEditor, Category = "Quick Pose", meta = (DisplayName = "Rotate +", ToolTip = "선택한 축을 회전 간격만큼 증가시킵니다.\n\n랜덤 회전 중에는 범위 전체를 이동하고, 현재 선택된 난수는 유지합니다."))
	void RotateForward();

	/** 선택한 축을 180도 회전합니다. 랜덤 범위는 함께 이동하고 반전 설정은 유지합니다. */
	UFUNCTION(CallInEditor, Category = "Quick Pose", meta = (DisplayName = "Flip 180", ToolTip = "선택한 축을 반 바퀴 돌립니다.\n\n랜덤 회전 중에는 최소·최대 범위를 함께 이동합니다. 각도 반전 옵션도 유지합니다."))
	void FlipSelectedAxis();

	/** 현재 항목의 배치 설정을 초기화합니다. 에셋, 분류와 미리보기 메시 참조는 유지합니다. */
	UFUNCTION(CallInEditor, Category = "Quick Pose", meta = (DisplayName = "Reset Placement", ToolTip = "현재 소품의 회전·크기·표면·겹침·랜덤 설정을 기본값으로 되돌립니다.\n\n항목 이름, 등록 에셋, 분류와 Blueprint Preview Mesh 참조는 유지합니다. 다음 실행에도 초기값을 쓰려면 팔레트를 저장하세요."))
	void ResetPlacementSettings();

	/** 다음 랜덤 자세를 미리보기에 적용합니다. */
	UFUNCTION(CallInEditor, Category = "Quick Pose", meta = (DisplayName = "Reroll Preview", ToolTip = "랜덤을 켠 회전축과 크기에 대해 다음 값을 뽑아 미리보기에 적용합니다. Palette Mix에서는 다음 항목도 함께 뽑습니다.\n\n설정한 범위 안에서 자세를 바꾸며, 고정값과 이미 배치된 액터는 유지합니다. Selected Item에서 랜덤 옵션이 모두 꺼져 있으면 외형 변화가 없습니다.\n\nSelected Item에서 마음에 드는 모습을 반복해서 쓰려면 Freeze Current Pose를 누르세요. 원래 랜덤 항목도 남기려면 먼저 항목을 복제하세요."))
	void RerollPreview();

	/** 현재 후보의 랜덤 회전·크기를 고정값으로 전환합니다. */
	UFUNCTION(CallInEditor, Category = "Quick Pose", meta = (DisplayName = "Freeze Current Pose", ToolTip = "Selected Item에서 현재 미리보기의 랜덤 회전·크기를 고정값으로 옮기고 해당 랜덤 옵션을 끕니다.\n\n최소·최대 범위와 각도 반전은 보존합니다. 다시 랜덤을 쓰려면 원하는 옵션을 켜세요. 표면 정렬과 위치 이동은 계속 적용됩니다.\n\n원래 랜덤 항목도 남기려면 먼저 팔레트에서 Duplicate Entry를 누르세요. 변경한 항목은 Save Palette로 보존합니다."))
	void FreezeCurrentPose();

	/** 변경된 메시·BP 기본값과 프로젝트 미리보기 재질을 다시 읽습니다. */
	UFUNCTION(CallInEditor, Category = "Prop", meta = (DisplayName = "Refresh Preview", ToolTip = "변경된 메시·블루프린트 기본값과 프로젝트의 미리보기 재질을 다시 읽습니다.\n\n현재 랜덤 자세의 순서는 유지합니다."))
	void RefreshPreview();

	bool SelectPaletteEntry(const FMD_PropPaletteEntry& Entry, FText& OutError, bool bPreservePreviewPose = false);
	void ClearPaletteSelection();
	void StoreActivePaletteSettings();
	void SanitizeBrushSettings();
	void ClearUnavailablePresetSource() { UnavailablePresetSource.Reset(); }
	FMD_PropPlacementPresetData CapturePreset() const;
	void ApplyPreset(const FMD_PropPlacementPresetData& Data, FText& OutWarning);
	bool HasUnsavedPresetChanges() const;
	bool SaveActivePalette(FText& OutError);
	void SetActivePaletteId(const FGuid& Id);

	UPROPERTY(Transient, meta = (TransientToolProperty))
	FGuid SelectedPresetId;

	UPROPERTY(Transient)
	FGuid ActivePaletteEntryId;

	// Keep panel state when Unreal refreshes the Details customization after a source change.
	UPROPERTY(Transient)
	FGuid PaletteSelectedEntryId;

	UPROPERTY(Transient)
	FString PaletteSearchText;

	UPROPERTY(Transient)
	FName PaletteCategoryFilter;

	FMD_OnPropPaletteAssetSelected OnPaletteAssetSelected;
	FSimpleMulticastDelegate OnPaletteSelectionChanged;
	FSimpleDelegate OnRefreshPreview;
	FSimpleDelegate OnPlacementSettingsChanged;
	FSimpleDelegate OnRerollPreview;
	FSimpleDelegate OnFreezePreview;
	FSimpleDelegate OnPoseEditUnavailable;
	FSimpleDelegate OnPresetApplied;

private:
	void RotateSelectedAxis(double DeltaDegrees);
	FSoftObjectPath UnavailablePresetSource;
};

UCLASS()
class UMD_PropPlacementToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};

/** 직접 지정하거나 팔레트에서 고른 메시·블루프린트를 배치하는 도구입니다. */
UCLASS(meta = (DisplayName = "Prop Placement"))
class UMD_PropPlacementTool : public UInteractiveTool, public IClickDragBehaviorTarget, public IHoverBehaviorTarget, public IMouseWheelBehaviorTarget
{
	GENERATED_BODY()

public:
	void SetTargetWorld(UWorld* World);
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;
	virtual void OnTick(float DeltaTime) override;
	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;
	virtual FInputRayHit ShouldRespondToMouseWheel(const FInputDeviceRay& CurrentPos) override;
	virtual void OnMouseWheelScrollUp(const FInputDeviceRay& CurrentPos) override;
	virtual void OnMouseWheelScrollDown(const FInputDeviceRay& CurrentPos) override;
	virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& PressPos) override;
	virtual void OnClickPress(const FInputDeviceRay& PressPos) override;
	virtual void OnClickDrag(const FInputDeviceRay& DragPos) override;
	virtual void OnClickRelease(const FInputDeviceRay& ReleasePos) override;
	virtual void OnTerminateDragSequence() override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual FInputRayHit BeginHoverSequenceHitTest(const FInputDeviceRay& DevicePos) override;
	virtual void OnBeginHover(const FInputDeviceRay& DevicePos) override;
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;
	virtual void OnEndHover() override;
	virtual void OnUpdateModifierState(int ModifierID, bool bIsOn) override;

private:
	void HandlePresetApplied();
	EMD_PropBrushAction GetEffectiveAction() const;
	void HandlePaletteAssetSelected(bool bPreservePreviewPose);
	void HandlePlacementSettingsChanged();
	void AdvancePreviewCandidate();
	void ConsumeMixCandidate();
	void FreezePreviewPose();
	void InvalidatePreview();
	void HandlePaletteChanged();
	void ShowPoseEditUnavailable();
	void RebuildMixCandidates();
	void RefreshCandidatePreview();
	void BeginStrokeTransaction();
	void FinishStroke();
	bool TryPlaceAtHit(const FHitResult& Hit);
	bool TryEraseAtRay(const FInputDeviceRay& Ray);
	bool TraceEraseTarget(const FInputDeviceRay& Ray, FHitResult& Hit) const;
	bool IsEraseTarget(const AActor* Actor) const;
	bool TraceStrokeSample(const FHitResult& Sample, FHitResult& Hit) const;
	bool HasReachedStrokeLimit();
	void RebuildPreview();
	void DestroyPreview();
	void HidePreview();
	bool CanEditLevel() const;
	bool TraceSurface(const FInputDeviceRay& DevicePos, FHitResult& Hit) const;
	FMD_PropSurfacePlacement BuildPlacement(const FHitResult& Hit) const;
	bool IsPlacementSafe(const FMD_PropSurfacePlacement& Placement) const;
	void UpdatePreview(const FHitResult& Hit);
	void ShowWheelPreview();

	UPROPERTY(Transient)
	TObjectPtr<UMD_PropPlacementProperties> Properties;

	UPROPERTY(Transient)
	FMD_PropPlacementCandidate PreviewCandidate;

	UPROPERTY(Transient)
	TArray<FMD_PropPlacementCandidate> MixCandidates;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> GhostComponents;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GhostMaterial;

	TWeakObjectPtr<UWorld> TargetWorld;
	FDelegateHandle BlueprintCompiledHandle;
	FDelegateHandle PaletteChangedHandle;
	FDelegateHandle PalettePlacementChangedHandle;
	TWeakObjectPtr<ULevel> StrokeLevel;
	TWeakObjectPtr<AActor> EraseHoverActor;
	TArray<TWeakObjectPtr<AActor>> StrokePlacedActors;
	FMD_PropPlacementStrokePath StrokePath;
	FMD_PropMixSelection MixSelection;
	FInputDeviceRay LastStrokeRay;
	EMD_PropBrushAction StrokeAction = EMD_PropBrushAction::Place;
	int32 StrokeChangeCount = 0;
	bool bShiftErase = false;
	bool bStrokeActive = false;
	bool bStrokeTransaction = false;
	bool bMixCandidatesDirty = true;
	FText MixWarning;
	FInputDeviceRay LastHoverRay;
	FText PreviewError;
	uint32 PreviewCandidateIndex = 0;
	bool bPreviewDirty = true;
	bool bHasHoverRay = false;
};
