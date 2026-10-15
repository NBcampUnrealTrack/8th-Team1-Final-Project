#include "Modules/ModuleManager.h"

#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "SMDMultiUserWorkspace.h"
#include "SMDPlacementTools.h"
#include "MDHoldSnap.h"
#include "MDMultiUserSafety.h"
#include "MDMultiUserGit.h"
#include "MDWorkspacePreferences.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "MDMultiUser"

class FMDMultiUserModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        MDHoldSnap::Register();
        FMDMultiUserSafety::Get().Start();
        FMDMultiUserGit::Get().Start();
        FGlobalTabmanager::RegisterDefaultTabWindowSize(TabName, FVector2D(460, 720));
        FGlobalTabmanager::RegisterDefaultTabWindowSize(FName(TEXT("MDPlacementTools")), FVector2D(480, 850));
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner("MDPlacementTools", FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs&) {
            const auto Tab = SNew(SDockTab).TabRole(ETabRole::NomadTab)
                .Label_Lambda([] { return MDPlacement::Text(TEXT("배치 도구"), TEXT("Placement tools")); })[SNew(SMDPlacementTools)];
            PlacementTab = Tab;
            return Tab;
        })).SetDisplayName(MDPlacement::Text(TEXT("배치 도구"), TEXT("Placement tools"))).SetMenuType(ETabSpawnerMenuType::Hidden);
        PlacementCommand = IConsoleManager::Get().RegisterConsoleCommand(TEXT("MD.MultiUser.Placement"), TEXT("Open placement tools."),
            FConsoleCommandDelegate::CreateLambda([] { FGlobalTabmanager::Get()->TryInvokeTab(FName(TEXT("MDPlacementTools"))); }), ECVF_Default);
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabName,
            FOnSpawnTab::CreateRaw(this, &FMDMultiUserModule::SpawnTab))
            .SetDisplayName(MDLOC("Tab", "함께 편집"))
            .SetTooltipText(MDLOC("TabTip", "팀 세션과 참여자를 확인하는 Multi-User 작업 공간"))
            .SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Outliner"))
            .SetMenuType(ETabSpawnerMenuType::Hidden);
        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FMDMultiUserModule::RegisterMenus));
        OpenCommand = IConsoleManager::Get().RegisterConsoleCommand(TEXT("MD.MultiUser.Open"),
            TEXT("Open the Multi-User workspace."), FConsoleCommandDelegate::CreateRaw(this, &FMDMultiUserModule::Open), ECVF_Default);
        SettingsCommand = IConsoleManager::Get().RegisterConsoleCommand(TEXT("MD.MultiUser.Settings"),
            TEXT("Open workspace preferences."), FConsoleCommandDelegate::CreateStatic(&FMDWorkspacePreferences::OpenWindow), ECVF_Default);
        LanguageCommand = IConsoleManager::Get().RegisterConsoleCommand(TEXT("MD.MultiUser.Language"),
            TEXT("Set workspace language: ko or en."), FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args) {
                if (Args.Num() != 1 || (Args[0] != TEXT("ko") && Args[0] != TEXT("en")))
                {
                    UE_LOG(LogTemp, Warning, TEXT("MD.MultiUser.Language expects ko or en."));
                    return;
                }
                const bool bSaved = FMDWorkspacePreferences::Apply(Args[0] == TEXT("en"), FMDWorkspacePreferences::ShowDevices());
                UE_LOG(LogTemp, Display, TEXT("MDMultiUser language=%s saved=%d"), *Args[0], bSaved);
            }), ECVF_Default);
    }

    virtual void ShutdownModule() override
    {
        FMDMultiUserGit::Get().Stop();
        FMDMultiUserSafety::Get().Stop();
        MDHoldSnap::Unregister();
        if (PlacementCommand) { IConsoleManager::Get().UnregisterConsoleObject(PlacementCommand); }
        if (const auto Tab = PlacementTab.Pin()) { Tab->RequestCloseTab(); }
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner("MDPlacementTools");
        FMDWorkspacePreferences::CloseWindow();
        if (LanguageCommand) { IConsoleManager::Get().UnregisterConsoleObject(LanguageCommand); }
        if (SettingsCommand) { IConsoleManager::Get().UnregisterConsoleObject(SettingsCommand); }
        if (OpenCommand) { IConsoleManager::Get().UnregisterConsoleObject(OpenCommand); }
        UToolMenus::UnRegisterStartupCallback(this);
        UToolMenus::UnregisterOwner(this);
        if (const auto Tab = WorkspaceTab.Pin()) { Tab->RequestCloseTab(); }
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabName);
    }

private:
    void Open() { FGlobalTabmanager::Get()->TryInvokeTab(TabName); }

    TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args)
    {
        const auto Tab = SNew(SDockTab).TabRole(ETabRole::NomadTab).Label_Lambda([] { return MDLOC("Tab", "함께 편집"); })[SNew(SMDMultiUserWorkspace)];
        WorkspaceTab = Tab;
        return Tab;
    }

    void RegisterMenus()
    {
        FToolMenuOwnerScoped Owner(this);
        const FUIAction Action(FExecuteAction::CreateRaw(this, &FMDMultiUserModule::Open));
        const FSlateIcon Icon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Outliner");
        UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window")->FindOrAddSection("WindowLayout")
            .AddMenuEntry("MDMultiUser", MakeAttributeLambda([] { return MDLOC("Open", "함께 편집"); }), MakeAttributeLambda([] { return MDLOC("OpenTip", "Multi-User 팀 작업 공간 열기"); }), Icon, Action);
        UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.User")->FindOrAddSection("MDMultiUser")
            .AddEntry(FToolMenuEntry::InitToolBarButton("MDMultiUser", Action, MakeAttributeLambda([] { return MDLOC("OpenToolbar", "함께 편집"); }),
                MakeAttributeLambda([] { return MDLOC("ToolbarTip", "세션 연결 상태와 참여자 확인"); }), Icon));
    }

    inline static const FName TabName = TEXT("MDMultiUserWorkspace");
    TWeakPtr<SDockTab> PlacementTab;
    IConsoleObject* PlacementCommand = nullptr;
    IConsoleObject* SettingsCommand = nullptr;
    IConsoleObject* LanguageCommand = nullptr;
    IConsoleObject* OpenCommand = nullptr;
    TWeakPtr<SDockTab> WorkspaceTab;
};

IMPLEMENT_MODULE(FMDMultiUserModule, MDMultiUser)

#undef LOCTEXT_NAMESPACE
