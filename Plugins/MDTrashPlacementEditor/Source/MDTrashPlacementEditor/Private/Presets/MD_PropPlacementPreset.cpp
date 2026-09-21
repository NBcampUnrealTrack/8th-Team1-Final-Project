#include "Presets/MD_PropPlacementPreset.h"

void FMD_PropPlacementPresetData::Sanitize()
{
	Spacing = FMath::IsFinite(Spacing) ? FMath::Clamp(Spacing, 1.0, 100000.0) : 100.0;
	MaxPerStroke = FMath::Clamp(MaxPerStroke, 1, 5000);
	RotationStep = FMath::IsFinite(RotationStep) ? FMath::Clamp(RotationStep, 1.0, 180.0) : 15.0;
	if (static_cast<uint8>(RotationAxis) > static_cast<uint8>(EMD_PropRotationAxis::Yaw))
	{
		RotationAxis = EMD_PropRotationAxis::Yaw;
	}
	for (FMD_PropPaletteEntry& Entry : Entries)
	{
		Entry.Placement.Sanitize();
	}
	DirectEntry.Placement.Sanitize();
}

bool FMD_PropPlacementPresetData::operator==(const FMD_PropPlacementPresetData& Other) const
{
	return Entries == Other.Entries && SelectedEntryId == Other.SelectedEntryId && DirectEntry == Other.DirectEntry
		&& bBlueprintSource == Other.bBlueprintSource && bPaletteMix == Other.bPaletteMix
		&& bContinuous == Other.bContinuous && Spacing == Other.Spacing
		&& MaxPerStroke == Other.MaxPerStroke && MixSeed == Other.MixSeed
		&& RotationAxis == Other.RotationAxis && RotationStep == Other.RotationStep;
}
