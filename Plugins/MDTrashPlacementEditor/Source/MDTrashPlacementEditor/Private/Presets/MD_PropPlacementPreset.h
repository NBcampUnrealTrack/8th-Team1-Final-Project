#pragma once

#include "CoreMinimal.h"
#include "Palette/MD_PropPaletteSettings.h"
#include "MD_PropPlacementPreset.generated.h"

/** Value snapshot only. Presets never contain level actors or run construction scripts. */
USTRUCT()
struct FMD_PropPlacementPresetData
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FMD_PropPaletteEntry> Entries;

	UPROPERTY()
	FGuid SelectedEntryId;

	/** Explicit source for a prop selected outside the palette. */
	UPROPERTY()
	FMD_PropPaletteEntry DirectEntry;

	UPROPERTY()
	bool bBlueprintSource = false;

	UPROPERTY()
	bool bPaletteMix = false;

	UPROPERTY()
	bool bContinuous = true;

	UPROPERTY()
	double Spacing = 100.0;

	UPROPERTY()
	int32 MaxPerStroke = 500;

	UPROPERTY()
	int32 MixSeed = 1337;

	UPROPERTY()
	EMD_PropRotationAxis RotationAxis = EMD_PropRotationAxis::Yaw;

	UPROPERTY()
	double RotationStep = 15.0;

	void Sanitize();
	bool operator==(const FMD_PropPlacementPresetData& Other) const;
};

USTRUCT()
struct FMD_PropPlacementPreset
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid Id;

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FMD_PropPlacementPresetData Data;
};
