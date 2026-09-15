#include "Gameplay/Customization/MD_CharacterAppearanceLibrary.h"

#include "Data/Customization/MD_CharacterRosterData.h"
#include "Gameplay/Customization/MD_CharacterAppearanceSettings.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	constexpr int32 AppearanceUserIndex = 0;
}

UMD_CharacterRosterData* UMD_CharacterAppearanceLibrary::GetCharacterRoster()
{
	const UMD_CharacterAppearanceSettings* Settings = UMD_CharacterAppearanceSettings::Get();
	return IsValid(Settings) == true ? Settings->GetRoster() : nullptr;
}

bool UMD_CharacterAppearanceLibrary::LoadLocalAppearance(FMD_CharacterAppearance& OutAppearance)
{
	const UMD_CharacterAppearanceSettings* Settings = UMD_CharacterAppearanceSettings::Get();
	if (IsValid(Settings) == false || Settings->SaveSlotName.IsEmpty() == true)
	{
		return false;
	}

	if (UGameplayStatics::DoesSaveGameExist(Settings->SaveSlotName, AppearanceUserIndex) == false)
	{
		return false;
	}

	UMD_CharacterAppearanceSaveGame* SaveGame =
		Cast<UMD_CharacterAppearanceSaveGame>(UGameplayStatics::LoadGameFromSlot(Settings->SaveSlotName, AppearanceUserIndex));
	if (IsValid(SaveGame) == false)
	{
		return false;
	}

	OutAppearance = SaveGame->Appearance;
	return true;
}

bool UMD_CharacterAppearanceLibrary::SaveLocalAppearance(const FMD_CharacterAppearance& Appearance)
{
	const UMD_CharacterAppearanceSettings* Settings = UMD_CharacterAppearanceSettings::Get();
	if (IsValid(Settings) == false || Settings->SaveSlotName.IsEmpty() == true)
	{
		return false;
	}

	UMD_CharacterAppearanceSaveGame* SaveGame =
		Cast<UMD_CharacterAppearanceSaveGame>(UGameplayStatics::CreateSaveGameObject(UMD_CharacterAppearanceSaveGame::StaticClass()));
	if (IsValid(SaveGame) == false)
	{
		return false;
	}

	SaveGame->Appearance = Appearance;
	return UGameplayStatics::SaveGameToSlot(SaveGame, Settings->SaveSlotName, AppearanceUserIndex);
}
