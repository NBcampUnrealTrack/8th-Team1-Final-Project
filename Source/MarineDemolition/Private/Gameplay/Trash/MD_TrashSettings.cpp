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

float UMD_TrashSettings::GetPlayerCountTrashMultiplier(const int32 PlayerCount) const
{
	if (PlayerCountTrashMultipliers.IsEmpty())
	{
		return 1.0f;
	}
	const int32 Index = FMath::Clamp(PlayerCount - 1, 0, PlayerCountTrashMultipliers.Num() - 1);
	const float Multiplier = PlayerCountTrashMultipliers[Index];
	return FMath::IsFinite(Multiplier) && Multiplier > 0.0f ? Multiplier : 1.0f;
}

#undef LOCTEXT_NAMESPACE
