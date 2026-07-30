#include "Customization/MD_CharacterCustomizationSettings.h"

#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "UI/MD_CharacterCustomizationWidget.h"

namespace MDCharacterCustomizationSettingsPrivate
{
	static TArray<FLinearColor> MakeBuiltinPresets()
	{
		return {
			FLinearColor(0.00f, 0.00f, 0.00f), FLinearColor(0.45f, 0.45f, 0.45f), FLinearColor(0.75f, 0.75f, 0.75f), FLinearColor(1.00f, 1.00f, 1.00f),
			FLinearColor(0.55f, 0.00f, 0.00f), FLinearColor(1.00f, 0.00f, 0.00f), FLinearColor(1.00f, 0.55f, 0.55f), FLinearColor(0.55f, 0.00f, 0.27f),
			FLinearColor(0.82f, 0.00f, 0.00f), FLinearColor(1.00f, 0.27f, 0.00f), FLinearColor(1.00f, 0.65f, 0.00f), FLinearColor(1.00f, 1.00f, 0.00f),
			FLinearColor(0.55f, 0.82f, 0.00f), FLinearColor(0.00f, 0.55f, 0.00f), FLinearColor(0.00f, 0.82f, 0.27f), FLinearColor(0.00f, 0.82f, 0.55f),
			FLinearColor(0.00f, 0.55f, 0.55f), FLinearColor(0.00f, 0.82f, 0.82f), FLinearColor(0.00f, 0.55f, 0.82f), FLinearColor(0.00f, 0.27f, 0.82f),
			FLinearColor(0.00f, 0.00f, 0.55f), FLinearColor(0.27f, 0.00f, 0.82f), FLinearColor(0.55f, 0.00f, 0.82f), FLinearColor(0.82f, 0.00f, 0.82f),
			FLinearColor(0.55f, 0.27f, 0.07f), FLinearColor(0.82f, 0.55f, 0.27f), FLinearColor(0.92f, 0.78f, 0.55f), FLinearColor(0.92f, 0.94f, 0.96f),
			FLinearColor(0.12f, 0.42f, 0.88f), FLinearColor(0.10f, 0.78f, 0.86f), FLinearColor(0.08f, 0.18f, 0.32f), FLinearColor(0.92f, 0.45f, 0.12f),
			FLinearColor(0.92f, 0.78f, 0.68f), FLinearColor(0.75f, 0.55f, 0.42f), FLinearColor(0.35f, 0.25f, 0.20f), FLinearColor(0.20f, 0.22f, 0.28f),
			FLinearColor(0.45f, 0.50f, 0.58f), FLinearColor(0.65f, 0.70f, 0.78f), FLinearColor(0.15f, 0.55f, 0.35f), FLinearColor(0.85f, 0.15f, 0.20f),
			FLinearColor(0.95f, 0.85f, 0.20f), FLinearColor(0.55f, 0.35f, 0.75f), FLinearColor(0.25f, 0.65f, 0.85f), FLinearColor(0.90f, 0.40f, 0.65f),
			FLinearColor(0.40f, 0.20f, 0.15f), FLinearColor(0.15f, 0.15f, 0.18f), FLinearColor(0.30f, 0.32f, 0.36f), FLinearColor(0.98f, 0.98f, 1.00f),
		};
	}
}

UMD_CharacterCustomizationSettings::UMD_CharacterCustomizationSettings()
{
	CustomizationWidgetClass = TSoftClassPtr<UMD_CharacterCustomizationWidget>(FSoftClassPath(
		TEXT("/Game/Characters/Customization/UI/WBP_MD_CharacterCustomization.WBP_MD_CharacterCustomization_C")));
	PreviewSkeletalMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(
		TEXT("/Game/Characters/Customization/SK_MD_TripoCharacter.SK_MD_TripoCharacter")));
	PreviewMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(
		TEXT("/Game/Characters/Customization/MI_MD_CharacterCustom.MI_MD_CharacterCustom")));
}

const UMD_CharacterCustomizationSettings* UMD_CharacterCustomizationSettings::Get()
{
	return GetDefault<UMD_CharacterCustomizationSettings>();
}

#if WITH_EDITOR
FText UMD_CharacterCustomizationSettings::GetSectionText() const
{
	return NSLOCTEXT("MDCustomization", "SettingsSection", "MD Character Customization");
}

FText UMD_CharacterCustomizationSettings::GetSectionDescription() const
{
	return NSLOCTEXT(
		"MDCustomization",
		"SettingsSectionDesc",
		"Engine/project defaults for the character customization widget. These override WBP instance defaults at runtime.");
}
#endif

const TArray<FLinearColor>& UMD_CharacterCustomizationSettings::GetPresetSwatchColorsOrDefault() const
{
	if (PresetSwatchColors.Num() > 0)
	{
		return PresetSwatchColors;
	}

	if (CachedBuiltinPresets.Num() == 0)
	{
		CachedBuiltinPresets = MDCharacterCustomizationSettingsPrivate::MakeBuiltinPresets();
	}

	return CachedBuiltinPresets;
}
