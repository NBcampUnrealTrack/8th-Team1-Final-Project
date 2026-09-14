#include "Data/Customization/MD_CharacterRosterData.h"

FMD_CharacterAppearance UMD_CharacterRosterData::Sanitize(const FMD_CharacterAppearance& Appearance) const
{
	FMD_CharacterAppearance Result = Appearance;
	if (Characters.IsValidIndex(Result.CharacterIndex) == false)
	{
		Result.CharacterIndex = 0;
	}
	if (Palette.IsValidIndex(Result.ColorIndex) == false)
	{
		Result.ColorIndex = 0;
	}
	return Result;
}

FLinearColor UMD_CharacterRosterData::GetPaletteColor(int32 Index) const
{
	return Palette.IsValidIndex(Index) ? Palette[Index].Color : FLinearColor::White;
}
