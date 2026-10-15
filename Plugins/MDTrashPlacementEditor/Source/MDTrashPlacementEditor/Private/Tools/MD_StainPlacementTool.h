#pragma once

#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "InteractiveTool.h"
#include "Engine/HitResult.h"
#include "UObject/SoftObjectPtr.h"
#include "Gameplay/Contamination/MD_StainStickerPatch.h"
#include "InteractiveToolBuilder.h"
#include "MD_StainPlacementTool.generated.h"

class AMD_SurfaceStain;
class UStaticMeshComponent;
class UPrimitiveComponent;
class ULevel;
class AActor;
class UStaticMesh;
class UMD_SurfaceStainDefinition;
class FMD_StainStickerAsyncBake;
struct FMD_StainStickerSourceCache;

struct FMD_StainPlacementRequest
{
	FTransform Transform, SurfaceTransform;
	TWeakObjectPtr<UPrimitiveComponent> Surface;
	TWeakObjectPtr<UStaticMesh> SourceMesh;
	FGuid SourceGuid;
	TWeakObjectPtr<ULevel> Level;
	TWeakObjectPtr<UMD_SurfaceStainDefinition> Definition;
	int32 Size = 0;
	uint32 DefinitionRevision = 0;
	bool bFitSurface = false;
};

UCLASS()
class UMD_StainPlacementToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()
public:
	virtual bool CanBuildTool(const FToolBuilderState& State) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& State) const override;
};

UENUM()
enum class EMD_StainPaletteGroup : uint8
{
	Existing UMETA(DisplayName = "기존 원형"),
	Wall UMETA(DisplayName = "벽용 원형")
};

UENUM()
enum class EMD_StainSizeInput : uint8
{
	ContaminationAmount UMETA(DisplayName = "오염량"),
	WidthCm UMETA(DisplayName = "가로 폭 (cm)")
};

UCLASS(Transient)
class UMD_StainPlacementProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** Every approved wall source has this full-stage width at reference size 300. */
	static constexpr double WallReferenceWidthCm = 60.0;
	static constexpr int32 WallProfileCount = 4;

	UPROPERTY(EditAnywhere, Category = "Stains", meta = (DisplayName = "원형 목록"))
	EMD_StainPaletteGroup PaletteGroup = EMD_StainPaletteGroup::Existing;

	UPROPERTY(EditAnywhere, Category = "Stains", meta = (EditCondition = "PaletteGroup == EMD_StainPaletteGroup::Existing", EditConditionHides))
	TArray<TObjectPtr<UMD_SurfaceStainDefinition>> Palette;

	/** Soft references do not load the four sources or their twenty meshes together. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Stains", meta = (TransientToolProperty, DisplayName = "벽 원형 목록", EditCondition = "PaletteGroup == EMD_StainPaletteGroup::Wall", EditConditionHides))
	TArray<TSoftObjectPtr<UMD_SurfaceStainDefinition>> WallPalette;

	UPROPERTY(EditAnywhere, Category = "Stains", meta = (DisplayName = "벽 원형 선택", ClampMin = "0", ClampMax = "3", EditCondition = "PaletteGroup == EMD_StainPaletteGroup::Wall && !bRandomVariant", EditConditionHides, ToolTip = "0: 넓은 비대칭, 1: 짧은 아래 흐름, 2: 짧은 사방 흐름, 3: 뭉친 덩어리"))
	int32 WallVariantIndex = 0;

	UPROPERTY(EditAnywhere, Category = "Stains")
	bool bRandomVariant = false;

	UPROPERTY(EditAnywhere, Category = "Stains", meta = (ClampMin = "0", EditCondition = "PaletteGroup == EMD_StainPaletteGroup::Existing && !bRandomVariant", EditConditionHides))
	int32 VariantIndex = 0;

	UPROPERTY(EditAnywhere, Category = "Size", meta = (DisplayName = "크기 입력 방식"))
	EMD_StainSizeInput SizeInput = EMD_StainSizeInput::ContaminationAmount;

	UPROPERTY(EditAnywhere, Category = "Size", meta = (DisplayName = "원형 기준 가로 폭", Units = "cm", ClampMin = "0.1", EditCondition = "SizeInput == EMD_StainSizeInput::WidthCm", EditConditionHides))
	double WidthCm = 0.0;

	UPROPERTY(EditAnywhere, Category = "Size", meta = (ClampMin = "100", ClampMax = "10000", EditCondition = "SizeInput == EMD_StainSizeInput::ContaminationAmount && !bRandomSize", EditConditionHides))
	int32 InitialSize = 300;

	UPROPERTY(EditAnywhere, Category = "Size", meta = (EditCondition = "SizeInput == EMD_StainSizeInput::ContaminationAmount", EditConditionHides))
	bool bRandomSize = false;

	UPROPERTY(EditAnywhere, Category = "Size", meta = (ClampMin = "100", ClampMax = "10000", EditCondition = "SizeInput == EMD_StainSizeInput::ContaminationAmount && bRandomSize", EditConditionHides))
	int32 MinSize = 100;

	UPROPERTY(EditAnywhere, Category = "Size", meta = (ClampMin = "100", ClampMax = "10000", EditCondition = "SizeInput == EMD_StainSizeInput::ContaminationAmount && bRandomSize", EditConditionHides))
	int32 MaxSize = 10000;

	UPROPERTY(VisibleAnywhere, Transient, Category = "Size", meta = (DisplayName = "원형 치수와 적용 오염량", MultiLine = "true"))
	FString SizeInfo;

	UPROPERTY(VisibleAnywhere, Transient, Category = "Size", meta = (DisplayName = "허용 폭 안내", MultiLine = "true"))
	FString WidthRangeInfo;

	UPROPERTY(VisibleAnywhere, Transient, Category = "Size", meta = (DisplayName = "맞춤 후 치수", MultiLine = "true"))
	FString MeasuredSizeInfo;

	UPROPERTY(EditAnywhere, Category = "Size", meta = (DisplayName = "기준 치수 자 표시"))
	bool bShowSizeRuler = true;

	UPROPERTY(EditAnywhere, Category = "Placement", meta = (ClampMin = "1", Units = "cm"))
	float SpacingCm = 60.0f;

	/** Current cursor result only; old queued placements must not overwrite this status. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Placement", meta = (TransientToolProperty, DisplayName = "배치 상태와 불가 이유", MultiLine = "true"))
	FString PlacementStatusInfo;

	UPROPERTY(EditAnywhere, Category = "Placement", meta = (Units = "deg"))
	float RotationDegrees = 0.0f;

	UPROPERTY(VisibleAnywhere, Transient, Category = "Placement", meta = (TransientToolProperty, DisplayName = "벽 방향 안내", MultiLine = "true", EditCondition = "PaletteGroup == EMD_StainPaletteGroup::Wall", EditConditionHides))
	FString WallDirectionInfo = TEXT("벽의 아래 방향에 맞춥니다. 무작위 회전은 ±8°이며, 지정 회전은 추가로 적용합니다.");

	UPROPERTY(EditAnywhere, Category = "Placement")
	bool bRandomRotation = true;

	UPROPERTY(EditAnywhere, Category = "Placement")
	int32 RandomSeed = 1337;

	/** Bake the selected volume around connected receiving faces at authoring time. */
	UPROPERTY(EditAnywhere, Category = "Placement", meta = (DisplayName = "Follow Surface"))
	bool bFollowSurface = true;

	UPROPERTY(EditAnywhere, Category = "Placement")
	bool bErase = false;
};

/** Surface stain placement, isolated from the mesh overlap/stacking policy of Trash. */
UCLASS()
class UMD_StainPlacementTool : public UInteractiveTool, public IClickDragBehaviorTarget,
	public IHoverBehaviorTarget, public IMouseWheelBehaviorTarget
{
	GENERATED_BODY()
public:
	/** Shared editor validation used by the live preview, committing, and authoring automation. */
	UFUNCTION(BlueprintCallable, Category = "MD|Stain|Placement")
	static bool ValidateVolumePlacement(UWorld* World, UMD_SurfaceStainDefinition* Definition,
		UPrimitiveComponent* Surface, const FTransform& ContactTransform, int32 InitialSize, FString& Error);

	/** Resolve a nearby supported edge from a main-face hit; planar profiles retain their input frame. */
	UFUNCTION(BlueprintCallable, Category = "MD|Stain|Placement")
	static bool ResolveVolumePlacement(UWorld* World, UMD_SurfaceStainDefinition* Definition,
		UPrimitiveComponent* Surface, const FTransform& Hint, int32 InitialSize, FTransform& Contact, FString& Error);

	/** Places an already-authored shared shape with the same contact checks and undo contract as this tool. */
	UFUNCTION(BlueprintCallable, Category = "MD|Stain|Placement")
	static AMD_SurfaceStain* PlaceVolume(UWorld* World, UMD_SurfaceStainDefinition* Definition,
		UPrimitiveComponent* Surface, const FTransform& ContactTransform, int32 InitialSize, FString& Error);

	UFUNCTION(BlueprintCallable, Category = "MD|Stain|Placement")
	static AMD_SurfaceStain* PlaceFittedVolume(UWorld* World, UMD_SurfaceStainDefinition* Definition,
		UPrimitiveComponent* Surface, const FTransform& ContactTransform, int32 InitialSize, FString& Error);

	void SetWorld(UWorld* World) { TargetWorld = World; }
	bool IsPlacementPending() const { return !PendingPlacements.IsEmpty() || (ActiveBake.IsValid() && bBakeForPlacement); }
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
	UMD_SurfaceStainDefinition* ResolveDefinition();
	bool IsWallDefinitionCompatible(const UMD_SurfaceStainDefinition& Definition, FString& Error) const;
	FTransform MakeTransform(const FHitResult& Hit) const;
	int32 ResolveSize(UMD_SurfaceStainDefinition* Definition, FString& Error) const;
	bool ResolveWidthRange(FVector2D& Range, FString& Error) const;
	void InitializeWidth();
	void UpdateSizeInfo(UMD_SurfaceStainDefinition* Definition, int32 Size, const FString& Error);
	void UpdateMeasuredSize();
	void ApplyAt(const FInputDeviceRay& Ray);
	void FinishStroke();
	bool WantsErase() const;
	void ChangeSpacing(float Delta);
	void ShowMovingPreview();
	bool PreparePreview();
	FMD_StainPlacementRequest PreviewRequest() const;
	bool ValidateRequest(const FMD_StainPlacementRequest& Request, FString& Error) const;
	bool MatchesPreview(const FMD_StainPlacementRequest& Request) const;
	void TickBake();
	void StartBake(const FMD_StainPlacementRequest& Request, bool bForPlacement);
	void CommitFit(const FMD_StainPlacementRequest& Request, UMD_SurfaceStainDefinition* Fit);
	void CommitPrepared(const FMD_StainPlacementRequest& Request, FMD_StainStickerPatch Patch, UStaticMesh* Mesh);
	void BeginStrokeTransaction();
	void SetPreviewError(const FString& Error);
	void ClearPreviewError();
	void ReportPlacementError(const FMD_StainPlacementRequest& Request, const FString& Error);

	UPROPERTY() TObjectPtr<UWorld> TargetWorld;
	UPROPERTY() TObjectPtr<UMD_StainPlacementProperties> Properties;
	UPROPERTY(Transient) TObjectPtr<AActor> PreviewOwner;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Preview;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> StickerPreviewMesh;
	/** Keep only wall sources actually chosen, including those in queued weak-reference requests. */
	UPROPERTY(Transient) TArray<TObjectPtr<UMD_SurfaceStainDefinition>> LoadedWallDefinitions;
	TArray<FSoftObjectPath> FailedWallLoads;
	FString DefinitionError;
	UPROPERTY(Transient) TObjectPtr<UMD_SurfaceStainDefinition> PreparedFit;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> DraftPreview;
	bool bDraftAttempted = false;
	TSharedPtr<FMD_StainStickerAsyncBake> PreparedSurface;
	bool bCompletingBake = false;
	FInputDeviceRay LastHoverRay;
	FInputDeviceRay PendingDragRay;
	TSharedPtr<FMD_StainStickerAsyncBake> ActiveBake;
	TSharedPtr<FMD_StainStickerSourceCache> SourceCache;
	FMD_StainPlacementRequest ActiveRequest;
	TArray<FMD_StainPlacementRequest> PendingPlacements;
	bool bBakeForPlacement = false;
	bool bPreviewRejected = false;
	int32 LastPendingCount = 0;
	int32 StrokePlaced = 0, StrokeFailed = 0;
	FMD_StainStickerPatch PreviewPatch;
	TWeakObjectPtr<UPrimitiveComponent> PreviewTarget;
	FTransform PreviewTargetTransform;
	TWeakObjectPtr<UStaticMesh> PreviewSourceMesh;
	FGuid PreviewSourceGuid;
	FString PreviewError;
	FString PreviewErrorInfo;
	TWeakObjectPtr<UMD_SurfaceStainDefinition> PreviewDefinition;
	FTransform PreviewTransform;
	FTransform PreviewInputTransform;
	int32 PreviewSize = 0;
	uint32 PreviewDefinitionRevision = 0;
	double PreviewChangedAt = 0;
	bool bPreviewPending = false;
	bool bPreviewValid = false;
	bool bDragPending = false;
	bool bTransaction = false;
	FHitResult HoverHit;
	FVector LastAttemptLocation = FVector::ZeroVector;
	int32 PlacementIndex = 0;
	bool bHover = false;
	bool bHoverEndPending = false;
	bool bPreviewWasVisible = false;
	bool bPreviewFollowSurface = true;
	FVector NominalDimensions = FVector::ZeroVector;
	mutable TArray<TPair<TWeakObjectPtr<UMD_SurfaceStainDefinition>, uint32>> WidthRangeDefinitions;
	mutable FVector2D CachedWidthRange = FVector2D::ZeroVector;
	mutable FString CachedWidthRangeError;
	mutable bool bWidthRangeCached = false;
	mutable EMD_StainPaletteGroup CachedPaletteGroup = EMD_StainPaletteGroup::Existing;
	mutable bool bCachedRandomVariant = false;
	mutable bool bCachedFollowSurface = true;
	mutable int32 CachedVariantIndex = INDEX_NONE;
	mutable uint32 WidthRangeVersion = 0;
	uint32 DisplayedWidthRangeVersion = 0;
	TWeakObjectPtr<UMD_SurfaceStainDefinition> SizeInfoDefinition;
	uint32 SizeInfoRevision = 0;
	int32 SizeInfoBudget = INDEX_NONE;
	bool bSizeInfoFollowSurface = true;
	FString SizeInfoError;
	bool bShift = false;
	bool bStroke = false;
	bool bStrokeErase = false;
	bool bAttempted = false;
};
