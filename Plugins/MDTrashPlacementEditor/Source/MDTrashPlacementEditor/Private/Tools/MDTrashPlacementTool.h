#pragma once

#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "GameplayTagContainer.h"
#include "InteractiveTool.h"
#include "InteractiveToolBuilder.h"
#include "Templates/SubclassOf.h"
#include "MDTrashPlacementTool.generated.h"

class AMD_TrashItem;
class IToolsContextRenderAPI;
class UMaterialInstanceDynamic;
class UStaticMesh;
class UStaticMeshComponent;
class UWorld;
struct FHitResult;

UENUM()
enum class EMDTrashPlacementToolMode : uint8
{
	Place,
	Erase
};

UENUM()
enum class EMDTrashPlacementSelectionMode : uint8
{
	SingleKind,
	RandomByCategory
};

UCLASS()
class MDTRASHPLACEMENTEDITOR_API UMDTrashPlacementToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};

UCLASS(Transient)
class MDTRASHPLACEMENTEDITOR_API UMDTrashPlacementToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UMDTrashPlacementToolProperties();

	UPROPERTY(EditAnywhere, Category = "Tool")
	EMDTrashPlacementToolMode Mode = EMDTrashPlacementToolMode::Place;

	UPROPERTY(VisibleAnywhere, Category = "Placement")
	TSubclassOf<AMD_TrashItem> TrashItemClass;

	UPROPERTY(EditAnywhere, Category = "Placement|Selection")
	EMDTrashPlacementSelectionMode SelectionMode = EMDTrashPlacementSelectionMode::SingleKind;

	UPROPERTY(
		EditAnywhere,
		Category = "Placement|Selection",
		meta = (
			EditCondition = "SelectionMode == EMDTrashPlacementSelectionMode::SingleKind",
			GetOptions = "GetTrashKindIdOptions"))
	FName TrashKindId = TEXT("DrinkCan");

	/** 선택한 품목의 이름별 메시 중에서 선택한다. 배치 성공 후에만 다음 변형을 선택한다. */
	UPROPERTY(EditAnywhere, Category = "Placement|Selection", meta = (DisplayName = "Random Variant"))
	bool bRandomVariant = false;

	/** Random By Category에서는 품목의 첫 Variant 또는 Random Variant를 쓴다. */
	UPROPERTY(EditAnywhere, Category = "Placement|Selection",
		meta = (EditCondition = "!bRandomVariant && SelectionMode == EMDTrashPlacementSelectionMode::SingleKind",
			GetOptions = "GetTrashVariantIdOptions", DisplayName = "Variant"))
	FName TrashVariantId;

	/**
	 * Random By Category에서 허용할 Category 태그의 합집합.
	 * Score.Sorting은 전체 종류, Score.Sorting.Metal은 금속 종류만 허용한다.
	 */
	UPROPERTY(
		EditAnywhere,
		Category = "Placement|Selection",
		meta = (
			EditCondition = "SelectionMode == EMDTrashPlacementSelectionMode::RandomByCategory",
			Categories = "Score.Sorting"))
	TArray<FGameplayTag> RandomCategoryTags;

	/** 연속 배치된 두 메시의 보이는 Bounds 표면 사이에 확보할 최소 간격. */
	UPROPERTY(EditAnywhere, Category = "Placement", meta = (ClampMin = "1.0", UIMin = "1.0", ForceUnits = "cm"))
	double PlacementSpacing = 100.0;

	/** 마우스 휠 한 칸으로 변경할 Placement Spacing 값. */
	UPROPERTY(EditAnywhere, Category = "Placement", meta = (ClampMin = "1.0", UIMin = "1.0", ForceUnits = "cm"))
	double WheelSpacingStep = 10.0;

	UPROPERTY(EditAnywhere, Category = "Placement|Orientation")
	bool bAlignToSurface = true;

	UPROPERTY(EditAnywhere, Category = "Placement|Orientation|Pitch", meta = (DisplayName = "Random Pitch"))
	bool bRandomizePitch = false;

	/** Random이 꺼져 있을 때만 사용할 고정 각도. */
	UPROPERTY(EditAnywhere, Category = "Placement|Orientation|Pitch",
		meta = (DisplayName = "Pitch", EditCondition = "!bRandomizePitch", EditConditionHides,
			UIMin = "-180.0", UIMax = "180.0", ForceUnits = "deg"))
	float PitchOffsetDegrees = 0.0f;

	/** 랜덤 회전 각도의 최솟값. Min/Max 입력 순서가 반대여도 내부에서 정렬한다. */
	UPROPERTY(EditAnywhere, Category = "Placement|Orientation|Pitch",
		meta = (DisplayName = "Min Pitch", EditCondition = "bRandomizePitch", EditConditionHides,
			UIMin = "-180.0", UIMax = "180.0", ForceUnits = "deg"))
	float MinimumPitchDegrees = -180.0f;

	/** 랜덤 회전 각도의 최댓값. Min과 같으면 해당 값으로 고정된다. */
	UPROPERTY(EditAnywhere, Category = "Placement|Orientation|Pitch",
		meta = (DisplayName = "Max Pitch", EditCondition = "bRandomizePitch", EditConditionHides,
			UIMin = "-180.0", UIMax = "180.0", ForceUnits = "deg"))
	float MaximumPitchDegrees = 180.0f;

	/** 선택된 각도(고정값 또는 랜덤값)의 부호를 반전한다. 새 난수는 생성하지 않는다. */
	UPROPERTY(EditAnywhere, Category = "Placement|Orientation|Pitch", meta = (DisplayName = "Invert Pitch"))
	bool bInvertPitch = false;

	UPROPERTY(EditAnywhere, Category = "Placement|Orientation|Roll", meta = (DisplayName = "Random Roll"))
	bool bRandomizeRoll = false;

	/** Random이 꺼져 있을 때만 사용할 고정 각도. */
	UPROPERTY(EditAnywhere, Category = "Placement|Orientation|Roll",
		meta = (DisplayName = "Roll", EditCondition = "!bRandomizeRoll", EditConditionHides,
			UIMin = "-180.0", UIMax = "180.0", ForceUnits = "deg"))
	float RollOffsetDegrees = 0.0f;

	/** 랜덤 회전 각도의 최솟값. Min/Max 입력 순서가 반대여도 내부에서 정렬한다. */
	UPROPERTY(EditAnywhere, Category = "Placement|Orientation|Roll",
		meta = (DisplayName = "Min Roll", EditCondition = "bRandomizeRoll", EditConditionHides,
			UIMin = "-180.0", UIMax = "180.0", ForceUnits = "deg"))
	float MinimumRollDegrees = -180.0f;

	/** 랜덤 회전 각도의 최댓값. Min과 같으면 해당 값으로 고정된다. */
	UPROPERTY(EditAnywhere, Category = "Placement|Orientation|Roll",
		meta = (DisplayName = "Max Roll", EditCondition = "bRandomizeRoll", EditConditionHides,
			UIMin = "-180.0", UIMax = "180.0", ForceUnits = "deg"))
	float MaximumRollDegrees = 180.0f;

	/** 선택된 각도(고정값 또는 랜덤값)의 부호를 반전한다. 새 난수는 생성하지 않는다. */
	UPROPERTY(EditAnywhere, Category = "Placement|Orientation|Roll", meta = (DisplayName = "Invert Roll"))
	bool bInvertRoll = false;

	UPROPERTY(EditAnywhere, Category = "Placement|Orientation|Yaw", meta = (DisplayName = "Random Yaw"))
	bool bRandomizeRotation = false;

	/** Random이 꺼져 있을 때만 사용할 고정 각도. */
	UPROPERTY(EditAnywhere, Category = "Placement|Orientation|Yaw",
		meta = (DisplayName = "Yaw", EditCondition = "!bRandomizeRotation", EditConditionHides,
			UIMin = "-180.0", UIMax = "180.0", ForceUnits = "deg"))
	float RotationOffsetDegrees = 0.0f;

	/** 랜덤 회전 각도의 최솟값. Min/Max 입력 순서가 반대여도 내부에서 정렬한다. */
	UPROPERTY(EditAnywhere, Category = "Placement|Orientation|Yaw",
		meta = (DisplayName = "Min Yaw", EditCondition = "bRandomizeRotation", EditConditionHides,
			UIMin = "-180.0", UIMax = "180.0", ForceUnits = "deg"))
	float MinimumYawDegrees = -180.0f;

	/** 랜덤 회전 각도의 최댓값. Min과 같으면 해당 값으로 고정된다. */
	UPROPERTY(EditAnywhere, Category = "Placement|Orientation|Yaw",
		meta = (DisplayName = "Max Yaw", EditCondition = "bRandomizeRotation", EditConditionHides,
			UIMin = "-180.0", UIMax = "180.0", ForceUnits = "deg"))
	float MaximumYawDegrees = 180.0f;

	/** 선택된 각도(고정값 또는 랜덤값)의 부호를 반전한다. 새 난수는 생성하지 않는다. */
	UPROPERTY(EditAnywhere, Category = "Placement|Orientation|Yaw", meta = (DisplayName = "Invert Yaw"))
	bool bInvertYaw = false;

	UPROPERTY(EditAnywhere, Category = "Placement|Scale", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float UniformScale = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Placement|Scale")
	bool bRandomizeScale = false;

	UPROPERTY(
		EditAnywhere,
		Category = "Placement|Scale",
		meta = (EditCondition = "bRandomizeScale", ClampMin = "0.01", UIMin = "0.01"))
	float MinimumScaleMultiplier = 0.9f;

	UPROPERTY(
		EditAnywhere,
		Category = "Placement|Scale",
		meta = (EditCondition = "bRandomizeScale", ClampMin = "0.01", UIMin = "0.01"))
	float MaximumScaleMultiplier = 1.1f;

	UPROPERTY(EditAnywhere, Category = "Placement|Randomization")
	int32 RandomSeed = 1337;

	UPROPERTY(EditAnywhere, Category = "Placement|Safety")
	bool bPreventOverlaps = true;

	/** 오버랩 검사 Bounds를 줄이는 접촉 허용치. 큰 값은 실제 관통도 허용할 수 있다. */
	UPROPERTY(
		EditAnywhere,
		Category = "Placement|Safety",
		meta = (EditCondition = "bPreventOverlaps", ClampMin = "0.0", UIMin = "0.0", ForceUnits = "cm"))
	float OverlapTolerance = 0.1f;

private:
	UFUNCTION()
	TArray<FName> GetTrashKindIdOptions() const;

	UFUNCTION()
	TArray<FName> GetTrashVariantIdOptions() const;
};

UCLASS()
class MDTRASHPLACEMENTEDITOR_API UMDTrashPlacementTool
	: public UInteractiveTool
	, public IClickDragBehaviorTarget
	, public IHoverBehaviorTarget
	, public IMouseWheelBehaviorTarget
{
	GENERATED_BODY()

public:
	void SetTargetWorld(UWorld* InTargetWorld);

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;
	virtual void OnUpdateModifierState(int ModifierID, bool bIsOn) override;
	virtual FInputRayHit ShouldRespondToMouseWheel(const FInputDeviceRay& CurrentPos) override;
	virtual void OnMouseWheelScrollUp(const FInputDeviceRay& CurrentPos) override;
	virtual void OnMouseWheelScrollDown(const FInputDeviceRay& CurrentPos) override;

	virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& PressPos) override;
	virtual void OnClickPress(const FInputDeviceRay& PressPos) override;
	virtual void OnClickDrag(const FInputDeviceRay& DragPos) override;
	virtual void OnClickRelease(const FInputDeviceRay& ReleasePos) override;
	virtual void OnTerminateDragSequence() override;

	virtual FInputRayHit BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos) override;
	virtual void OnBeginHover(const FInputDeviceRay& DevicePos) override;
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;
	virtual void OnEndHover() override;

private:
	static constexpr int32 ShiftEraseModifierId = 1;
	FInputDeviceRay LastHoverRay;

	bool ValidatePlacementSettings() const;
	bool PlaceTrashAtHit(const FHitResult& HitResult, FName TrashKindId, FName VariantId, UStaticMesh* PreviewMesh);
	bool EraseTrashItem(AMD_TrashItem* TrashItem);
	bool TraceWorld(const FInputDeviceRay& DevicePos, FHitResult& OutHit) const;
	bool TraceTrash(const FInputDeviceRay& DevicePos, AMD_TrashItem*& OutTrashItem, FHitResult& OutHit) const;
	double CalculateWheelAdjustedSpacing(int32 StepDirection) const;
	void AdjustPlacementSpacing(int32 StepDirection);
	EMDTrashPlacementToolMode ResolveRequestedMode(bool bShiftEraseActive) const;
	bool IsEraseMode() const;
	bool IsRandomSelectionMode() const;
	TArray<FName> BuildEligibleTrashKindIds() const;
	FName ResolveTrashKindId() const;
	uint32 GetPlacementRandomSeed(uint32 Salt) const;
	bool ResolveTrashAppearance(FName TrashKindId, FName& OutVariantId, UStaticMesh*& OutMesh);
	UStaticMesh* ResolveTrashMesh(FName TrashKindId, FName VariantId);
	FTransform BuildPlacementTransform(const FHitResult& HitResult, const UStaticMesh* StaticMesh) const;
	double CalculatePlacementExtent(
		const UStaticMesh* StaticMesh,
		const FTransform& Transform,
		const FVector& SurfaceLocation,
		const FVector& Direction) const;
	bool IsPlacementSafe(const UStaticMesh* StaticMesh, const FTransform& Transform, const FHitResult& SurfaceHit) const;
	bool IsPlacementAllowedAtHit(const FHitResult& HitResult, const UStaticMesh* StaticMesh) const;
	void UpdateHoverFromHit(const FHitResult& HitResult);
	void SetupGhostPreview();
	void UpdateGhostPreview(const FHitResult& HitResult);
	void HideGhostPreview();
	void DestroyGhostPreview();
	void BeginStrokeTransaction();
	void CancelStroke();
	void FinishStroke();
	void DisplayWarning(const FText& Message) const;

	UPROPERTY()
	TObjectPtr<UMDTrashPlacementToolProperties> Properties;

	UPROPERTY()
	TObjectPtr<UWorld> TargetWorld;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> GhostPreviewComponent;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GhostMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> LastPlacementMesh;

	/** 이 도구에서 실제 선택한 메시만 유지한다. 매 Hover에서 동기 로드를 반복하지 않는다. */
	UPROPERTY(Transient)
	TMap<FSoftObjectPath, TObjectPtr<UStaticMesh>> ResolvedMeshes;

	TArray<TWeakObjectPtr<AMD_TrashItem>> StrokePlacedTrashItems;
	FTransform LastPlacementTransform = FTransform::Identity;
	FVector HoverLocation = FVector::ZeroVector;
	FVector HoverNormal = FVector::UpVector;
	FVector PreviousSurfaceLocation = FVector::ZeroVector;
	FVector PreviousSurfaceNormal = FVector::UpVector;
	FVector LastPlacementSurfaceLocation = FVector::ZeroVector;
	double DistanceSinceLastPlacement = 0.0;
	EMDTrashPlacementToolMode ActiveStrokeMode = EMDTrashPlacementToolMode::Place;
	/** 마우스 이동/거절/스트로크 시작에서는 바꾸지 않는다. 성공한 배치만 진행한다. */
	uint32 NextPlacementIndex = 0;
	int32 StrokePlacementCount = 0;
	int32 StrokeEraseCount = 0;
	bool bHasHoverHit = false;
	bool bIsStrokeActive = false;
	bool bHasActiveUndoTransaction = false;
	bool bHasPreviousSurfaceHit = false;
	bool bIsHoverPlacementValid = false;
	bool bShiftEraseActive = false;
};
