#include "Gameplay/Interaction/MD_InteractionSettings.h"

#include "Engine/DataTable.h"

#define LOCTEXT_NAMESPACE "MD_InteractionSettings"

const UMD_InteractionSettings* UMD_InteractionSettings::Get()
{
	return GetDefault<UMD_InteractionSettings>();
}

#if WITH_EDITOR
FText UMD_InteractionSettings::GetSectionText() const
{
	return LOCTEXT("InteractionSettingsSection", "MD Interaction");
}

FText UMD_InteractionSettings::GetSectionDescription() const
{
	return LOCTEXT("InteractionSettingsDescription", "조준 상호작용 대상의 공용 표시 이름 테이블");
}
#endif

const UDataTable* UMD_InteractionSettings::GetInteractionPresentationTable() const
{
	return InteractionPresentationTable.LoadSynchronous();
}

#undef LOCTEXT_NAMESPACE
