#pragma once

#include "CoreMinimal.h"
#include "MD_CharacterCustomizationTypes.generated.h"

namespace MDCharacterCustomization
{
	inline constexpr int32 MaxSaveSlots = 8;
	inline constexpr int32 FaceTextureSize = 256;
	inline constexpr int32 MaxCustomSwatches = 20;
	inline constexpr int32 DefaultPresetSwatchCount = 48;
	inline constexpr TCHAR SaveSlotName[] = TEXT("MD_CharacterCustomization");
}

/** Matches Blender vertex groups / mask packing (5 parts). */
UENUM(BlueprintType)
enum class EMDCharacterCustomizationPart : uint8
{
	Visor UMETA(DisplayName = "Helmet"),
	Face UMETA(DisplayName = "Face Glass"),
	Torso UMETA(DisplayName = "Torso"),
	Hands UMETA(DisplayName = "Hands"),
	Rim UMETA(DisplayName = "Rim")
};

USTRUCT(BlueprintType)
struct FMDCharacterCustomizationAppearance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization")
	FLinearColor VisorColor = FLinearColor(0.08f, 0.18f, 0.32f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization")
	FLinearColor FaceColor = FLinearColor(0.92f, 0.78f, 0.68f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization")
	FLinearColor TorsoColor = FLinearColor(0.92f, 0.94f, 0.96f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization")
	FLinearColor HandsColor = FLinearColor(0.92f, 0.94f, 0.96f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization", meta = (FormerlySerializedAs = "BadgeColor"))
	FLinearColor RimColor = FLinearColor(0.92f, 0.45f, 0.12f, 1.f);

	/** PNG bytes for painted face expression overlay (alpha = ink). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization")
	TArray<uint8> ExpressionTexturePNG;

	/** Legacy v1 fields — migrated on load when part colors are unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization")
	FLinearColor SuitPrimaryColor = FLinearColor(0.92f, 0.94f, 0.96f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization")
	FLinearColor SuitAccentColor = FLinearColor(0.12f, 0.42f, 0.88f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization")
	TArray<uint8> FaceTexturePNG;

	/** Legacy Feet color (pre 5-part scheme); folded into Hands on migrate. */
	UPROPERTY()
	FLinearColor FeetColor = FLinearColor(0.92f, 0.94f, 0.96f, 1.f);

	FLinearColor GetPartColor(EMDCharacterCustomizationPart Part) const;
	void SetPartColor(EMDCharacterCustomizationPart Part, FLinearColor NewColor);
	void MigrateLegacyFieldsIfNeeded();
};

USTRUCT(BlueprintType)
struct FMDCharacterCustomizationSlotSummary
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Customization")
	int32 SlotIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Customization")
	bool bOccupied = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Customization")
	FString SlotLabel;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Customization")
	FDateTime LastModified;
};

USTRUCT(BlueprintType)
struct FMDCharacterCustomizationSlotData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization")
	int32 SlotIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization")
	FString SlotLabel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization")
	FDateTime LastModified;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization")
	FMDCharacterCustomizationAppearance Appearance;
};
