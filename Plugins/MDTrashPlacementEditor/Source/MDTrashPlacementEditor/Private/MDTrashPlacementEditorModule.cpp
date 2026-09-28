#include "MDTrashPlacementEditorModule.h"

#include "MDTrashPlacementEditorModeCommands.h"
#include "Details/MD_StainCustomization.h"
#include "Tools/MD_StainRefit.h"
#include "World/Contamination/MD_SurfaceStain.h"
#include "Palette/MD_PropPaletteCustomization.h"
#include "Palette/MD_PropPaletteSettings.h"
#include "Presets/MD_PropPresetSettings.h"
#include "Tools/MD_PropPlacementTool.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "CoreGlobals.h"
#include "PropertyEditorModule.h"

void FMDTrashPlacementEditorModule::StartupModule()
{
	FMDTrashPlacementEditorModeCommands::Register();
	if (IsRunningCommandlet())
	{
		return;
	}
	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	PropertyEditor.RegisterCustomClassLayout(UMD_PropPlacementProperties::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FMD_PropPaletteCustomization::MakeInstance));
	PropertyEditor.RegisterCustomClassLayout(AMD_SurfaceStain::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FMD_StainCustomization::MakeInstance));
	PropertyEditor.NotifyCustomizationModuleChanged();
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	PropAssetRenamedHandle = Registry.OnAssetRenamed().AddUObject(
		GetMutableDefault<UMD_PropPaletteSettings>(), &UMD_PropPaletteSettings::HandleAssetRenamed);
	PropPresetAssetRenamedHandle = Registry.OnAssetRenamed().AddUObject(
		GetMutableDefault<UMD_PropPresetSettings>(), &UMD_PropPresetSettings::HandleAssetRenamed);
}

void FMDTrashPlacementEditorModule::ShutdownModule()
{
	FMD_StainRefit::Shutdown();
	if (FAssetRegistryModule* Registry = FModuleManager::GetModulePtr<FAssetRegistryModule>(TEXT("AssetRegistry")))
	{
		if (IAssetRegistry* ActiveRegistry = Registry->TryGet())
		{
			ActiveRegistry->OnAssetRenamed().Remove(PropAssetRenamedHandle);
			ActiveRegistry->OnAssetRenamed().Remove(PropPresetAssetRenamedHandle);
		}
	}
	if (FPropertyEditorModule* PropertyEditor = FModuleManager::GetModulePtr<FPropertyEditorModule>(TEXT("PropertyEditor")))
	{
		PropertyEditor->UnregisterCustomClassLayout(UMD_PropPlacementProperties::StaticClass()->GetFName());
		PropertyEditor->UnregisterCustomClassLayout(AMD_SurfaceStain::StaticClass()->GetFName());
		PropertyEditor->NotifyCustomizationModuleChanged();
	}
	FMDTrashPlacementEditorModeCommands::Unregister();
}

IMPLEMENT_MODULE(FMDTrashPlacementEditorModule, MDTrashPlacementEditor)
