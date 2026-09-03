#pragma once

#include "CoreMinimal.h"
#include "Tools/UEdMode.h"
#include "MDTrashPlacementEditorMode.generated.h"

UCLASS()
class MDTRASHPLACEMENTEDITOR_API UMDTrashPlacementEditorMode : public UEdMode
{
	GENERATED_BODY()

public:
	static const FEditorModeID EM_MDTrashPlacementEditorModeId;
	static const FString PlacementToolName;

	UMDTrashPlacementEditorMode();

	virtual void Enter() override;
	virtual void ActorSelectionChangeNotify() override;
	virtual void CreateToolkit() override;
	virtual TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> GetModeCommands() const override;
};
