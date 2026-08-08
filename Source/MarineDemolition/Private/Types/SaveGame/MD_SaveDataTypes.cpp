// Fill out your copyright notice in the Description page of Project Settings.


#include "Types/SaveGame/MD_SaveDataTypes.h"

void UMD_SaveGame::ApplyMasSaveCountLimit(const int32& MaxAutoSaveCount, const int32& MaxUserSaveCount)
{
	if (AutoSaveList.Num() > MaxAutoSaveCount)
	{
		AutoSaveList.SetNum(MaxAutoSaveCount);
	}
	if (UserSaveList.Num() > MaxUserSaveCount)
	{
		UserSaveList.SetNum(MaxUserSaveCount);
	}
	return;
}
