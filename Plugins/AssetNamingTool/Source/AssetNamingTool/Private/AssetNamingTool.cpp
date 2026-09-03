#include "AssetNamingTool.h"

#include "ToolMenus.h"

#include "Editor.h"
#include "EditorUtilitySubsystem.h"
#include "EditorUtilityWidgetBlueprint.h"

#include "Interfaces/IPluginManager.h"

#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Brushes/SlateImageBrush.h"


#define LOCTEXT_NAMESPACE "FAssetNamingToolModule"


// ============================================================
// Module Startup
// ============================================================

void FAssetNamingToolModule::StartupModule()
{
    RegisterStyle();

    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateRaw(
            this,
            &FAssetNamingToolModule::RegisterToolbar
        )
    );
}


// ============================================================
// Module Shutdown
// ============================================================

void FAssetNamingToolModule::ShutdownModule()
{
    UToolMenus::UnRegisterStartupCallback(this);

    UToolMenus::UnregisterOwner(this);

    UnregisterStyle();
}


// ============================================================
// Icon 등록
// ============================================================

void FAssetNamingToolModule::RegisterStyle()
{
    if (StyleSet.IsValid())
    {
        return;
    }


    const TSharedPtr<IPlugin> Plugin =
        IPluginManager::Get().FindPlugin(
            TEXT("AssetNamingTool")
        );


    if (!Plugin.IsValid())
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT(
                "AssetNamingTool: "
                "Could not find plugin."
            )
        );

        return;
    }


    StyleSet =
        MakeShared<FSlateStyleSet>(
            TEXT("AssetNamingToolStyle")
        );


    const FString ResourcesPath =
        Plugin->GetBaseDir()
        / TEXT("Resources");


    StyleSet->SetContentRoot(
        ResourcesPath
    );


    StyleSet->Set(
        TEXT("AssetNamingTool.ToolbarIcon"),

        new FSlateImageBrush(
            StyleSet->RootToContentDir(
                TEXT("Icon40"),
                TEXT(".png")
            ),

            FVector2D(
                20.0f,
                20.0f
            )
        )
    );


    FSlateStyleRegistry::RegisterSlateStyle(
        *StyleSet
    );
}


// ============================================================
// Icon 등록 해제
// ============================================================

void FAssetNamingToolModule::UnregisterStyle()
{
    if (!StyleSet.IsValid())
    {
        return;
    }


    FSlateStyleRegistry::UnRegisterSlateStyle(
        *StyleSet
    );


    StyleSet.Reset();
}


// ============================================================
// Toolbar 버튼 등록
// ============================================================

void FAssetNamingToolModule::RegisterToolbar()
{
    FToolMenuOwnerScoped OwnerScoped(this);


    UToolMenu* Toolbar =
        UToolMenus::Get()->ExtendMenu(
            TEXT("LevelEditor.LevelEditorToolBar.ModesToolBar")
        );


    if (Toolbar == nullptr)
    {
        return;
    }


    FToolMenuSection& Section =
        Toolbar->FindOrAddSection(
            TEXT("File")
        );


    FToolMenuEntry ToolbarEntry =
        FToolMenuEntry::InitToolBarButton(

            TEXT("AssetNamingToolButton"),

            FUIAction(
                FExecuteAction::CreateRaw(
                    this,
                    &FAssetNamingToolModule::OpenAssetNamingTool
                )
            ),

            LOCTEXT(
                "AssetNamingToolLabel",
                "Naming Validator"
            ),

            LOCTEXT(
                "AssetNamingToolTooltip",
                "Open Asset Naming Validator"
            ),

            FSlateIcon(
                StyleSet->GetStyleSetName(),
                TEXT("AssetNamingTool.ToolbarIcon")
            )
        );


    Section.AddEntry(
        ToolbarEntry
    );

    UE_LOG(
        LogTemp,
        Log,
        TEXT("AssetNamingTool: Toolbar button registered.")
    );

    UToolMenus::Get()->RefreshAllWidgets();
}


// ============================================================
// EUW 열기
// ============================================================

void FAssetNamingToolModule::OpenAssetNamingTool()
{
    if (GEditor == nullptr)
    {
        return;
    }


    UEditorUtilitySubsystem* EditorUtilitySubsystem =
        GEditor->GetEditorSubsystem<
        UEditorUtilitySubsystem
        >();


    if (EditorUtilitySubsystem == nullptr)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT(
                "AssetNamingTool: "
                "EditorUtilitySubsystem not found."
            )
        );

        return;
    }


    static const TCHAR* WidgetPath =
        TEXT(
            "/AssetNamingTool/Editor/"
            "EUW_AssetNamingTool."
            "EUW_AssetNamingTool"
        );


    UEditorUtilityWidgetBlueprint* WidgetBlueprint =
        LoadObject<UEditorUtilityWidgetBlueprint>(
            nullptr,
            WidgetPath
        );


    if (WidgetBlueprint == nullptr)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT(
                "AssetNamingTool: "
                "Failed to load EUW: %s"
            ),
            WidgetPath
        );

        return;
    }


    EditorUtilitySubsystem->SpawnAndRegisterTab(
        WidgetBlueprint
    );
}


#undef LOCTEXT_NAMESPACE


IMPLEMENT_MODULE(
    FAssetNamingToolModule,
    AssetNamingTool
)