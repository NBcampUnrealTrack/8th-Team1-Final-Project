#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FSlateStyleSet;

class FAssetNamingToolModule : public IModuleInterface
{
public:

    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:

    void RegisterToolbar();
    void RegisterStyle();
    void UnregisterStyle();

    void OpenAssetNamingTool();

private:

    TSharedPtr<FSlateStyleSet> StyleSet;
};