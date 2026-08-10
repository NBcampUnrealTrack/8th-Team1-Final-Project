// Fill out your copyright notice in the Description page of Project Settings.


#include "Types/SaveGame/MD_SaveDataTypes.h"
#include "GameplayTags/MD_GameplayTags.h"

const FGameplayTag FMD_GameStateData::DataFieldKey = MDGameplayTags::Save_Data_GameState;
const FGameplayTag FMD_MerchantData::DataFieldKey = MDGameplayTags::Save_Data_Merchant;
const FGameplayTag FMD_WorldData::DataFieldKey = MDGameplayTags::Save_Data_World;

void UMD_SaveGame::ApplyMaxSaveCountLimit(const int32& MaxAutoSaveCount, const int32& MaxUserSaveCount)
{
	if (AutoSaveList.Num() > MaxAutoSaveCount)
	{
		AutoSaveList.SetNum(MaxAutoSaveCount);
	}
	if (UserSaveList.Num() > MaxUserSaveCount)
	{
		UserSaveList.SetNum(MaxUserSaveCount);
	}
	
	if (AutoSaveList2.Num() > MaxAutoSaveCount)
	{
		AutoSaveList2.SetNum(MaxAutoSaveCount);
	}
	if (UserSaveList2.Num() > MaxUserSaveCount)
	{
		UserSaveList2.SetNum(MaxUserSaveCount);
	}

	return;
}
