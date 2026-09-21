#pragma once

#include "Modules/ModuleManager.h"
#include "Delegates/Delegate.h"

class FMDTrashPlacementEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	FDelegateHandle PropAssetRenamedHandle;
	FDelegateHandle PropPresetAssetRenamedHandle;
};
