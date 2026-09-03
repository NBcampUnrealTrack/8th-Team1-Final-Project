#include "MDTrashPlacementEditorModeCommands.h"

#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "MDTrashPlacementEditorModeCommands"

FMDTrashPlacementEditorModeCommands::FMDTrashPlacementEditorModeCommands()
	: TCommands<FMDTrashPlacementEditorModeCommands>(
		TEXT("MDTrashPlacementEditorMode"),
		LOCTEXT("ContextName", "MD Trash Placement Editor Mode"),
		NAME_None,
		FAppStyle::GetAppStyleSetName())
{
}

void FMDTrashPlacementEditorModeCommands::RegisterCommands()
{
	TArray<TSharedPtr<FUICommandInfo>>& ToolCommands = Commands.FindOrAdd(NAME_Default);

	UI_COMMAND(
		PlacementTool,
		"Trash Placement",
		"Activates the designer-authored gameplay trash placement tool.",
		EUserInterfaceActionType::ToggleButton,
		FInputChord());

	ToolCommands.Add(PlacementTool);
}

TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> FMDTrashPlacementEditorModeCommands::GetCommands()
{
	return FMDTrashPlacementEditorModeCommands::Get().Commands;
}

#undef LOCTEXT_NAMESPACE
