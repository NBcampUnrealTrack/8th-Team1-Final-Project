#pragma once

#include "Framework/Commands/Commands.h"

class FMDTrashPlacementEditorModeCommands : public TCommands<FMDTrashPlacementEditorModeCommands>
{
public:
	FMDTrashPlacementEditorModeCommands();

	virtual void RegisterCommands() override;
	static TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> GetCommands();

	TSharedPtr<FUICommandInfo> PlacementTool;
	TSharedPtr<FUICommandInfo> StainPlacementTool;

private:
	TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> Commands;
};
