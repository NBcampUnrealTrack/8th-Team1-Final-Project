// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Trash/MD_TrashSettings.h"

#include "Engine/DataTable.h"

#define LOCTEXT_NAMESPACE "MD_Trash"

const UMD_TrashSettings* UMD_TrashSettings::Get()
{
	return GetDefault<UMD_TrashSettings>();
}

#if WITH_EDITOR
FText UMD_TrashSettings::GetSectionText() const
{
	return LOCTEXT("TrashSettingsSection", "MD Trash");
}

FText UMD_TrashSettings::GetSectionDescription() const
{
	return LOCTEXT("TrashSettingsDescription", "쓰레기 분류 칸 수치와 산재 쓰레기 종류 테이블");
}
#endif

const UDataTable* UMD_TrashSettings::GetTrashCategoryTable() const
{
	return TrashCategoryTable.LoadSynchronous();
}

const UDataTable* UMD_TrashSettings::GetTrashKindTable() const
{
	return TrashKindTable.LoadSynchronous();
}

#undef LOCTEXT_NAMESPACE
