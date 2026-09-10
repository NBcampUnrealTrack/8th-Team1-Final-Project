#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "Types/Customization/MD_CharacterCustomizationTypes.h"
#include "MD_CharacterCustomizationWidget.generated.h"

class UBorder;
class UButton;
class UEditableTextBox;
class UImage;
class UMD_CharacterCustomizationComponent;
class UMD_CharacterCustomizationSubsystem;
class AMD_CharacterCustomizationPreviewActor;
class USlider;
class UTextBlock;
class UTextureRenderTarget2D;
class UTexture2D;
class UWrapBox;
class USkeletalMesh;
class UMaterialInterface;
class UMDCharacterCustomizationSwatchHandler;

UCLASS(Abstract, Blueprintable)
class MARINEDEMOLITION_API UMD_CharacterCustomizationWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	UMD_CharacterCustomizationWidget(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization")
	void InitializeCustomization(UMD_CharacterCustomizationComponent* InPreviewComponent);

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization")
	UTextureRenderTarget2D* GetExpressionRenderTarget();

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization")
	UTextureRenderTarget2D* GetExpressionRenderTargetConst() const { return ExpressionRenderTarget; }

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization")
	void SelectPart(EMDCharacterCustomizationPart Part);

	UFUNCTION(BlueprintPure, Category = "MarineDemolition|Customization")
	EMDCharacterCustomizationPart GetSelectedPart() const { return SelectedPart; }

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization")
	void SetPickerColor(FLinearColor NewColor);

	UFUNCTION(BlueprintPure, Category = "MarineDemolition|Customization")
	FLinearColor GetPickerColor() const { return PickerColor; }

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization")
	void ApplyPickerColorToSelectedPart();

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization")
	void SetPartColor(EMDCharacterCustomizationPart Part, FLinearColor NewColor);

	UFUNCTION(BlueprintPure, Category = "MarineDemolition|Customization")
	FLinearColor GetPartColor(EMDCharacterCustomizationPart Part) const;

	UFUNCTION(BlueprintPure, Category = "MarineDemolition|Customization")
	int32 GetSelectedSlotIndex() const { return SelectedSlotIndex; }

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization")
	void SelectSlot(int32 SlotIndex);

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization|Expression")
	void ClearExpressionCanvas();

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization|Expression")
	void PaintExpressionStroke(FVector2D Start, FVector2D End, FLinearColor StrokeColor, float Thickness = 4.f);

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization|Expression")
	void PaintExpressionDot(FVector2D Position, FLinearColor DotColor, float Radius = 6.f);

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization")
	bool SaveSelectedSlot();

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization")
	bool LoadSelectedSlot();

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization")
	bool DeleteSelectedSlot();

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization")
	bool SaveToSlot(int32 SlotIndex, const FString& SlotLabel = FString());

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization")
	bool LoadFromSlot(int32 SlotIndex);

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization")
	bool DeleteSlot(int32 SlotIndex);

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization")
	TArray<FMDCharacterCustomizationSlotSummary> GetSlotSummaries() const;

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization")
	void RefreshPreview();

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization")
	void RefreshSlotStatus();

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization")
	void AddPickerColorToCustomSwatches();

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization|Preview")
	void SetAutoSpawnPreviewActor(bool bEnabled) { bAutoSpawnPreviewActor = bEnabled; }

	UFUNCTION(BlueprintPure, Category = "MarineDemolition|Customization|Preview")
	bool GetAutoSpawnPreviewActor() const { return bAutoSpawnPreviewActor; }

protected:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

	/** Copies Project Settings (DefaultGame.ini) onto this widget. Engine settings always win. */
	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization")
	void ApplyEngineSettings();

	UFUNCTION(BlueprintImplementableEvent, Category = "MarineDemolition|Customization")
	void OnCustomizationInitialized();

	UFUNCTION(BlueprintImplementableEvent, Category = "MarineDemolition|Customization")
	void OnSlotLoaded(int32 SlotIndex);

	UFUNCTION(BlueprintImplementableEvent, Category = "MarineDemolition|Customization")
	void OnSlotSaved(int32 SlotIndex);

	UFUNCTION(BlueprintImplementableEvent, Category = "MarineDemolition|Customization")
	void OnPartSelected(EMDCharacterCustomizationPart Part);

	UPROPERTY(BlueprintReadOnly, Category = "Customization")
	TObjectPtr<UMD_CharacterCustomizationComponent> PreviewComponent;

	UPROPERTY(BlueprintReadOnly, Category = "Customization")
	TObjectPtr<UTextureRenderTarget2D> ExpressionRenderTarget;

	UPROPERTY(BlueprintReadOnly, Category = "Customization")
	FMDCharacterCustomizationAppearance CurrentAppearance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization|Expression")
	FLinearColor ExpressionBrushColor = FLinearColor(0.02f, 0.02f, 0.04f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization|Expression")
	float ExpressionBrushThickness = 6.f;

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization|Expression")
	void SetExpressionBrushColor(FLinearColor NewColor);

	UFUNCTION(BlueprintPure, Category = "MarineDemolition|Customization|Expression")
	FLinearColor GetExpressionBrushColor() const { return ExpressionBrushColor; }

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization|Expression")
	void SetExpressionBrushThickness(float NewThickness);

	UFUNCTION(BlueprintPure, Category = "MarineDemolition|Customization|Expression")
	float GetExpressionBrushThickness() const { return ExpressionBrushThickness; }

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> IMG_ExpressionCanvas;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> IMG_FaceCanvas;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> IMG_SVPicker;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> IMG_ColorPreview;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBorder> BDR_Preview;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> IMG_Preview;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_PreviewHint;

	/**
	 * Preview values are overwritten at PreConstruct/Construct from
	 * Project Settings → Game → MD Character Customization (engine config wins).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization|Preview", meta = (DisplayName = "Preview Mesh (overridden by Project Settings)"))
	TSoftObjectPtr<USkeletalMesh> PreviewSkeletalMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization|Preview", meta = (DisplayName = "Preview Material (overridden by Project Settings)"))
	TSoftObjectPtr<UMaterialInterface> PreviewMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization|Preview")
	FIntPoint PreviewRenderSize = FIntPoint(512, 512);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization|Preview")
	bool bAutoSpawnPreviewActor = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization|Preview")
	bool bOrbitPreview = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization|Preview")
	FVector PreviewSpawnOffset = FVector(-2500.f, 0.f, 100.f);

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_SelectedPart;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_SlotStatus;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> TXT_Hex;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> TXT_R;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> TXT_G;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> TXT_B;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USlider> SL_Hue;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USlider> SL_Value;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_ApplyFill;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_ClearExpression;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USlider> SL_BrushThickness;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> IMG_BrushColorPreview;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_BrushThickness;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_SetBrushColor;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_AddCustomColor;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_PartVisor;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_PartFace;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_PartTorso;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_PartHands;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_PartRim;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_SaveSlot;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_LoadSlot;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_DeleteSlot;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Slot0;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Slot1;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Slot2;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Slot3;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Slot4;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Slot5;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Slot6;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Slot7;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWrapBox> WRAP_PresetColors;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWrapBox> WRAP_CustomColors;

private:
	UMD_CharacterCustomizationSubsystem* GetCustomizationSubsystem() const;
	UImage* GetExpressionCanvasImage() const;
	bool ImportExpressionPNGToCanvas(const TArray<uint8>& PNGBytes);
	void BindUiCallbacks();
	void BuildPresetSwatchButtons();
	void RebuildCustomSwatchButtons();
	void UpdateExpressionCanvasBrush();
	void UpdateColorPreview();
	void UpdateSVPickerTexture();
	void UpdatePickerTextFields();
	void SyncPickerFromHSV();
	void SyncHSVFromPicker();
	bool TryGetExpressionCanvasPixel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, FVector2D& OutCanvasPixel) const;
	bool TryGetExpressionPaintSample(
		const FPointerEvent& InMouseEvent,
		FVector2D& OutViewUV,
		FVector2D& OutCanvasPixel) const;
	bool IsPointerOverExpressionCanvas(const FPointerEvent& InMouseEvent) const;
	bool TryPickSVFromPointer(const FPointerEvent& InMouseEvent, FVector2D& OutSaturationValue) const;
	void UpdatePreviewHint();
	void UpdateSelectedPartLabel();
	void EnsurePreviewActor();
	void DestroyPreviewActor();
	void UpdatePreviewBrush();
	void UpdateFacePreviewBrush();
	void SyncExpressionToMeshes();
	void SyncAppearanceToExternalComponent();
	FMDCharacterCustomizationAppearance BuildCurrentAppearance() const;
	void ApplyAppearanceToPreview(const FMDCharacterCustomizationAppearance& Appearance);
	void SetPickerColorInternal(FLinearColor NewColor, bool bUpdateHSV, bool bUpdateTextFields, bool bUpdateSVTexture);
	void UpdateBrushUI();
	FLinearColor GetActiveDrawColor() const;

	UFUNCTION()
	void HandleClearExpressionClicked();

	UFUNCTION()
	void HandleApplyFillClicked();

	UFUNCTION()
	void HandleSetBrushColorClicked();

	UFUNCTION()
	void HandleBrushThicknessChanged(float NewValue);

	UFUNCTION()
	void HandleAddCustomColorClicked();

	UFUNCTION()
	void HandleSaveSlotClicked();

	UFUNCTION()
	void HandleLoadSlotClicked();

	UFUNCTION()
	void HandleDeleteSlotClicked();

	UFUNCTION()
	void HandleSlot0Clicked();

	UFUNCTION()
	void HandleSlot1Clicked();

	UFUNCTION()
	void HandleSlot2Clicked();

	UFUNCTION()
	void HandleSlot3Clicked();

	UFUNCTION()
	void HandleSlot4Clicked();

	UFUNCTION()
	void HandleSlot5Clicked();

	UFUNCTION()
	void HandleSlot6Clicked();

	UFUNCTION()
	void HandleSlot7Clicked();

	UFUNCTION()
	void HandlePartVisorClicked();

	UFUNCTION()
	void HandlePartFaceClicked();

	UFUNCTION()
	void HandlePartTorsoClicked();

	UFUNCTION()
	void HandlePartHandsClicked();

	UFUNCTION()
	void HandlePartRimClicked();

	UFUNCTION()
	void HandleHueChanged(float NewValue);

	UFUNCTION()
	void HandleValueChanged(float NewValue);

	UFUNCTION()
	void HandleHexCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void HandleRCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void HandleGCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void HandleBCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	EMDCharacterCustomizationPart SelectedPart = EMDCharacterCustomizationPart::Torso;
	FLinearColor PickerColor = FLinearColor(0.92f, 0.94f, 0.96f, 1.f);
	float PickerHue = 0.f;
	float PickerSaturation = 0.f;
	float PickerValue = 1.f;
	int32 SelectedSlotIndex = 0;
	bool bIsPaintingExpression = false;
	bool bIsPickingSV = false;
	bool bSuppressPickerCallbacks = false;
	FVector2D LastExpressionCanvasPixel = FVector2D::ZeroVector;
	FVector2D LastExpressionViewUV = FVector2D::ZeroVector;
	bool bHasLastExpressionCanvasPixel = false;
	bool bHasLastExpressionViewUV = false;

	/** Keeps PNG→UTexture2D alive across the Load canvas blit (avoids GC mid-draw). */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> ImportedExpressionScratch;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> SVPickerRenderTarget;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> PreviewRenderTarget;

	/** Live head close-up for the expression draw panel. */
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> FacePreviewRenderTarget;

	/** UI-only composite of face capture + expression strokes (single-canvas WBP). */
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> FaceDisplayRenderTarget;

	UPROPERTY(Transient)
	TObjectPtr<AMD_CharacterCustomizationPreviewActor> PreviewActor;

	/** Optional world/host mesh component (e.g. TestHost) kept in sync with the UI preview. */
	UPROPERTY(Transient)
	TObjectPtr<UMD_CharacterCustomizationComponent> ExternalCustomizationComponent;

	UPROPERTY(Transient)
	TArray<FLinearColor> CustomSwatchColors;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> PresetSwatchButtons;

	/** Keeps swatch OnClicked targets alive (NewObject alone is GC'd). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMDCharacterCustomizationSwatchHandler>> SwatchHandlers;
};

UCLASS()
class MARINEDEMOLITION_API UMDCharacterCustomizationSwatchHandler : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UMD_CharacterCustomizationWidget* InWidget, FLinearColor InColor);

	UFUNCTION()
	void HandleClicked();

private:
	UPROPERTY(Transient)
	TObjectPtr<UMD_CharacterCustomizationWidget> OwnerWidget;

	FLinearColor SwatchColor = FLinearColor::White;
};
