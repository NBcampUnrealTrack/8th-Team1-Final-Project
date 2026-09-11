#include "MDTrashPlacementEditorMode.h"

#include "InteractiveToolManager.h"
#include "Tools/EdModeInteractiveToolsContext.h"
#include "MDTrashPlacementEditorModeCommands.h"
#include "MDTrashPlacementEditorModeToolkit.h"
#include "Tools/MDTrashPlacementTool.h"
#include "Tools/MD_StainPlacementTool.h"

#define LOCTEXT_NAMESPACE "MDTrashPlacementEditorMode"

const FEditorModeID UMDTrashPlacementEditorMode::EM_MDTrashPlacementEditorModeId =
	TEXT("EM_MDTrashPlacementEditorMode");
const FString UMDTrashPlacementEditorMode::PlacementToolName = TEXT("MDTrashPlacementTool");

UMDTrashPlacementEditorMode::UMDTrashPlacementEditorMode()
{
	Info = FEditorModeInfo(
		EM_MDTrashPlacementEditorModeId,
		LOCTEXT("ModeName", "Placement"),
		FSlateIcon(),
		true);
}

void UMDTrashPlacementEditorMode::Enter()
{
	UEdMode::Enter();

	const FMDTrashPlacementEditorModeCommands& ToolCommands = FMDTrashPlacementEditorModeCommands::Get();
	RegisterTool(
		ToolCommands.PlacementTool,
		PlacementToolName,
		NewObject<UMDTrashPlacementToolBuilder>(this));

	RegisterTool(ToolCommands.StainPlacementTool, TEXT("MDStainPlacementTool"), NewObject<UMD_StainPlacementToolBuilder>(this));
	GetInteractiveToolsContext()->StartTool(PlacementToolName);
}

void UMDTrashPlacementEditorMode::ActorSelectionChangeNotify()
{
}

void UMDTrashPlacementEditorMode::CreateToolkit()
{
	Toolkit = MakeShared<FMDTrashPlacementEditorModeToolkit>();
}

TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> UMDTrashPlacementEditorMode::GetModeCommands() const
{
	return FMDTrashPlacementEditorModeCommands::GetCommands();
}

#undef LOCTEXT_NAMESPACE
