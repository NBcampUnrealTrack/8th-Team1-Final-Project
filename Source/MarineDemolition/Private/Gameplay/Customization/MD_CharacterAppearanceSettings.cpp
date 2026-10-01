#include "Gameplay/Customization/MD_CharacterAppearanceSettings.h"

#include "Data/Customization/MD_CharacterRosterData.h"

const UMD_CharacterAppearanceSettings* UMD_CharacterAppearanceSettings::Get()
{
	return GetDefault<UMD_CharacterAppearanceSettings>();
}

UMD_CharacterRosterData* UMD_CharacterAppearanceSettings::GetRoster() const
{
	return CharacterRoster.IsNull() ? nullptr : CharacterRoster.LoadSynchronous();
}
