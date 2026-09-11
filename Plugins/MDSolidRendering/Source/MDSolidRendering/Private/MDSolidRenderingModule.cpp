#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

class FMDSolidRenderingModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MDSolidRendering"));
        check(Plugin.IsValid());
        AddShaderSourceDirectoryMapping(TEXT("/Plugin/MDSolidRendering"), FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders")));
    }
};

IMPLEMENT_MODULE(FMDSolidRenderingModule, MDSolidRendering)
