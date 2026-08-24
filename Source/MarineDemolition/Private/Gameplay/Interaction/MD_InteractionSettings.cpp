#include "Gameplay/Interaction/MD_InteractionSettings.h"

#include "Engine/DataTable.h"

const UMD_InteractionSettings* UMD_InteractionSettings::Get()
{
	return GetDefault<UMD_InteractionSettings>();
}

const UDataTable* UMD_InteractionSettings::GetInteractionPresentationTable() const
{
	return InteractionPresentationTable.LoadSynchronous();
}
