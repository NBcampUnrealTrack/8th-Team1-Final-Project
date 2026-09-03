#include "MDTrashPlacementEditorModule.h"

#include "MDTrashPlacementEditorModeCommands.h"

void FMDTrashPlacementEditorModule::StartupModule()
{
	FMDTrashPlacementEditorModeCommands::Register();
}

void FMDTrashPlacementEditorModule::ShutdownModule()
{
	FMDTrashPlacementEditorModeCommands::Unregister();
}

IMPLEMENT_MODULE(FMDTrashPlacementEditorModule, MDTrashPlacementEditor)
