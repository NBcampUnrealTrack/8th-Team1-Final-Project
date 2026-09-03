#include "MDTrashPlacementEditorModeToolkit.h"

#define LOCTEXT_NAMESPACE "MDTrashPlacementEditorModeToolkit"

void FMDTrashPlacementEditorModeToolkit::Init(
	const TSharedPtr<IToolkitHost>& InitToolkitHost,
	TWeakObjectPtr<UEdMode> InOwningMode)
{
	FModeToolkit::Init(InitToolkitHost, InOwningMode);
}

void FMDTrashPlacementEditorModeToolkit::GetToolPaletteNames(TArray<FName>& PaletteNames) const
{
	PaletteNames.Add(NAME_Default);
}

FName FMDTrashPlacementEditorModeToolkit::GetToolkitFName() const
{
	return FName(TEXT("MDTrashPlacementEditorMode"));
}

FText FMDTrashPlacementEditorModeToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("DisplayName", "MD Trash Placement Editor Mode");
}

#undef LOCTEXT_NAMESPACE
