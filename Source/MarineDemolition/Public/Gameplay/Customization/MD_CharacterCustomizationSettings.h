#pragma once

#include "CoreMinimal.h"
#include "Types/Customization/MD_CharacterCustomizationTypes.h"
#include "Engine/DeveloperSettings.h"
#include "MD_CharacterCustomizationSettings.generated.h"

class UMD_CharacterCustomizationWidget;
class USkeletalMesh;
class UMaterialInterface;

/**
 * Project Settings → Game → Marine Demolition Character Customization.
 * These values take priority over WBP / C++ instance defaults on the customization widget.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "MD Character Customization"))
class MARINEDEMOLITION_API UMD_CharacterCustomizationSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMD_CharacterCustomizationSettings();

	static const UMD_CharacterCustomizationSettings* Get();

	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }
	virtual FName GetSectionName() const override { return FName(TEXT("MD Character Customization")); }

#if WITH_EDITOR
	virtual FText GetSectionText() const override;
	virtual FText GetSectionDescription() const override;
#endif

	/** Widget Blueprint class opened by test host / systems. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Widget")
	TSoftClassPtr<UMD_CharacterCustomizationWidget> CustomizationWidgetClass;

	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Preview")
	TSoftObjectPtr<USkeletalMesh> PreviewSkeletalMesh;

	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Preview")
	TSoftObjectPtr<UMaterialInterface> PreviewMaterial;

	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Preview", meta = (ClampMin = "64", ClampMax = "2048"))
	FIntPoint PreviewRenderSize = FIntPoint(512, 512);

	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Preview")
	bool bAutoSpawnPreviewActor = true;

	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Preview")
	bool bOrbitPreview = true;

	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Preview")
	FVector PreviewSpawnOffset = FVector(-2500.f, 0.f, 100.f);

	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Expression")
	FLinearColor ExpressionBrushColor = FLinearColor(0.02f, 0.02f, 0.04f, 1.f);

	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Expression", meta = (ClampMin = "1.0"))
	float ExpressionBrushThickness = 6.f;

	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Expression", meta = (ClampMin = "32", ClampMax = "1024"))
	int32 ExpressionCanvasSize = MDCharacterCustomization::FaceTextureSize;

	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Color Picker", meta = (ClampMin = "32", ClampMax = "512"))
	int32 SVPickerSize = 32;

	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Color Picker")
	EMDCharacterCustomizationPart DefaultSelectedPart = EMDCharacterCustomizationPart::Torso;

	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FMDCharacterCustomizationAppearance DefaultAppearance;

	/** Empty = built-in palette at runtime. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	TArray<FLinearColor> PresetSwatchColors;

	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxCustomSwatches = MDCharacterCustomization::MaxCustomSwatches;

	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Save")
	FString SaveSlotName = MDCharacterCustomization::SaveSlotName;

	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Save", meta = (ClampMin = "1", ClampMax = "32"))
	int32 MaxSaveSlots = MDCharacterCustomization::MaxSaveSlots;

	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Test Host")
	bool bOpenUIOnBeginPlay = true;

	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Test Host")
	bool bUseUIOnlyInput = true;

	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Test Host")
	int32 WidgetZOrder = 10;

	const TArray<FLinearColor>& GetPresetSwatchColorsOrDefault() const;

private:
	mutable TArray<FLinearColor> CachedBuiltinPresets;
};
