#pragma once

#include "Modules/ModuleManager.h"

class FMDTrashPlacementEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
