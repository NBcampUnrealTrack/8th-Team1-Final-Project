#include "MDWorkspacePreferences.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "IMultiUserClientModule.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    constexpr const TCHAR* Section = TEXT("MDMultiUser.Workspace");
    struct FPreferences
    {
        bool bEnglish = false;
        bool bShowDevices = true;
        uint32 Revision = 0;
        FPreferences()
        {
            FConfigFile Config;
            Config.Read(FPaths::ProjectSavedDir() / TEXT("Config/MDMultiUser.ini"));
            FString Language;
            Config.GetString(Section, TEXT("Language"), Language);
            bEnglish = Language == TEXT("en");
            Config.GetBool(Section, TEXT("ShowDevices"), bShowDevices);
        }
    };
    FPreferences& Preferences() { static FPreferences Value; return Value; }
    TWeakPtr<SWindow> SettingsWindow;

    const TMap<FString, FString>& English()
    {
        static const TMap<FString, FString> Values = {
            {TEXT("Eyebrow"), TEXT("MULTI-USER")},
            {TEXT("Title"), TEXT("Team workspace")}, {TEXT("Tab"), TEXT("Team workspace")},
            {TEXT("TabTip"), TEXT("Multi-User sessions and teammates")},
            {TEXT("Open"), TEXT("Team workspace")}, {TEXT("OpenTip"), TEXT("Open the Multi-User workspace")},
            {TEXT("OpenToolbar"), TEXT("Team workspace")}, {TEXT("ToolbarTip"), TEXT("View connection status and teammates")},
            {TEXT("Settings"), TEXT("Settings")}, {TEXT("SettingsTip"), TEXT("Open workspace settings and language preferences")},
            {TEXT("BrowserTip"), TEXT("Choose a server and session, or view activity in the Multi-User browser.")},
            {TEXT("Progress"), TEXT("View connection progress")}, {TEXT("Details"), TEXT("Session details")},
            {TEXT("Choose"), TEXT("Choose a session")}, {TEXT("Leave"), TEXT("Leave session")},
            {TEXT("LeaveTip"), TEXT("Leave using Unreal's confirmation and save workflow.")},
            {TEXT("LocalLevel"), TEXT("MY OPEN LEVEL")}, {TEXT("Participants"), TEXT("Teammates  {0}")},
            {TEXT("Search"), TEXT("Search names or devices")}, {TEXT("NoResults"), TEXT("No matching teammates.")},
            {TEXT("StartTitle"), TEXT("One level. One team.")},
            {TEXT("Step1"), TEXT("01   Start from the same project commit.")},
            {TEXT("Step2"), TEXT("02   Connect to Tailscale and choose your session.")},
            {TEXT("Step3"), TEXT("03   Check your teammates and start editing.")},
            {TEXT("NoServerTitle"), TEXT("Can't find your session?")},
            {TEXT("NoServerHelp"), TEXT("Check that your team host appears in Tailscale. Start the editor with the Multi-User launcher to configure your connection address.")},
            {TEXT("Guide"), TEXT("Team guide (Korean)")}, {TEXT("Copy"), TEXT("Copy session name")},
            {TEXT("CopyTip"), TEXT("Copy the current session name to the clipboard.")}, {TEXT("Copied"), TEXT("Copied")},
            {TEXT("NoLevel"), TEXT("No level open")}, {TEXT("Me"), TEXT("You")}, {TEXT("Online"), TEXT("In session")},
            {TEXT("Unavailable"), TEXT("CLIENT STARTING")}, {TEXT("Connected"), TEXT("CONNECTED")},
            {TEXT("Connecting"), TEXT("CONNECTING…")}, {TEXT("Disconnecting"), TEXT("DISCONNECTING…")},
            {TEXT("Disconnected"), TEXT("NOT CONNECTED")}, {TEXT("Wait"), TEXT("Please wait a moment")},
            {TEXT("Ready"), TEXT("Join your team's session")},
            {TEXT("UnavailableHelp"), TEXT("You can choose a session when the Multi-User client is ready.")},
            {TEXT("BusyHelp"), TEXT("View progress and errors in the Multi-User browser.")},
            {TEXT("LiveHelp"), TEXT("You're in a team session. Open session details to view activity.")},
            {TEXT("OfflineHelp"), TEXT("Choose a session to see the teammates working with you.")},
            {TEXT("SettingsTitle"), TEXT("Workspace settings")},
            {TEXT("SettingsIntro"), TEXT("Make this workspace yours.")},
            {TEXT("Language"), TEXT("Display language")},
            {TEXT("LanguageHelp"), TEXT("Applies to this workspace. Unreal Editor and its built-in browser keep their own language.")},
            {TEXT("Devices"), TEXT("Show device and platform names")},
            {TEXT("DevicesHelp"), TEXT("Display these details below each teammate's name.")},
            {TEXT("LocalOnly"), TEXT("Saved for you on this computer and project.")},
            {TEXT("Advanced"), TEXT("Multi-User profile & connection settings")},
            {TEXT("AdvancedHelp"), TEXT("Open Unreal's settings for your display name, avatar color and connection.")},
            {TEXT("Apply"), TEXT("Apply")}, {TEXT("Cancel"), TEXT("Cancel")},
            {TEXT("Applied"), TEXT("Settings applied")},
            {TEXT("SaveFailed"), TEXT("Couldn't save settings. Check that the project's Saved folder is writable, then try again.")}
        };
        return Values;
    }

    const FSlateRoundedBoxBrush PrefsBackground(FLinearColor::FromSRGBColor(FColor(24, 27, 33)), 0.0f);
    const FSlateRoundedBoxBrush PrefsCard(FLinearColor::FromSRGBColor(FColor(34, 39, 47)), 8.0f);

    class SMDWorkspaceSettings : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SMDWorkspaceSettings) {}
        SLATE_END_ARGS()

        void Construct(const FArguments& Args)
        {
            bDraftEnglish = FMDWorkspacePreferences::IsEnglish();
            bDraftDevices = FMDWorkspacePreferences::ShowDevices();
            Languages = {MakeShared<FString>(TEXT("한국어")), MakeShared<FString>(TEXT("English"))};
            Build();
        }

    private:
        TSharedRef<STextBlock> Label(const FText& Text, int32 Size = 10, bool bBold = false)
        {
            return SNew(STextBlock).Text(Text).AutoWrapText(true)
                .Font(FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size));
        }

        void Build()
        {
            ChildSlot
            [
                SNew(SBorder).BorderImage(&PrefsBackground).Padding(24)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()[Label(MDLOC("SettingsTitle", "작업 공간 설정"), 20, true)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 22)[Label(MDLOC("SettingsIntro", "내 작업 방식에 맞게 설정하세요."))]
                    + SVerticalBox::Slot().FillHeight(1)
                    [
                        SNew(SScrollBox)
                        + SScrollBox::Slot()
                        [
                            SNew(SBorder).BorderImage(&PrefsCard).Padding(16)
                            [
                                SNew(SVerticalBox)
                                + SVerticalBox::Slot().AutoHeight()[Label(MDLOC("Language", "표시 언어"), 11, true)]
                                + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 10)
                                [SNew(SComboBox<TSharedPtr<FString>>).OptionsSource(&Languages)
                                    .InitiallySelectedItem(Languages[bDraftEnglish ? 1 : 0])
                                    .ContentPadding(FMargin(10, 7))
                                    .OnGenerateWidget_Lambda([this](TSharedPtr<FString> Value) { return Label(FText::FromString(*Value)); })
                                    .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Value, ESelectInfo::Type) {
                                        if (Value) { bDraftEnglish = Value == Languages[1]; bApplied = false; }
                                    })
                                    [SNew(STextBlock).Text_Lambda([this] { return FText::FromString(*Languages[bDraftEnglish ? 1 : 0]); })]]
                                + SVerticalBox::Slot().AutoHeight()[Label(MDLOC("LanguageHelp", "이 협업 도구에 적용됩니다. Unreal 에디터와 기본 세션 브라우저의 언어는 유지됩니다."))]
                                + SVerticalBox::Slot().AutoHeight().Padding(0, 24, 0, 6)
                                [SNew(SCheckBox).IsChecked_Lambda([this] { return bDraftDevices ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                                    .OnCheckStateChanged_Lambda([this](ECheckBoxState Value) { bDraftDevices = Value == ECheckBoxState::Checked; bApplied = false; })
                                    [Label(MDLOC("Devices", "기기와 플랫폼 이름 표시"), 11, true)]]
                                + SVerticalBox::Slot().AutoHeight()[Label(MDLOC("DevicesHelp", "참여자 이름 아래에 기기 정보를 표시합니다."))]
                            ]
                        ]
                        + SScrollBox::Slot().Padding(0, 16, 0, 0)
                        [SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton").ContentPadding(FMargin(0, 8))
                            .ToolTipText(MDLOC("AdvancedHelp", "표시 이름, 아바타 색상, 연결 설정을 변경하는 Unreal 설정을 엽니다."))
                            .OnClicked_Lambda([] { IMultiUserClientModule::Get().OpenSettings(); return FReply::Handled(); })
                            [Label(MDLOC("Advanced", "Multi-User 프로필·연결 설정 열기"))]]
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 16, 0, 10)
                    [SNew(STextBlock).AutoWrapText(true).Text_Lambda([this] {
                        if (bSaveFailed) { return MDLOC("SaveFailed", "설정을 저장하지 못했습니다. 프로젝트 Saved 폴더의 쓰기 권한을 확인하고 다시 시도하세요."); }
                        return bApplied ? MDLOC("Applied", "설정을 적용했습니다") : MDLOC("LocalOnly", "이 컴퓨터의 내 프로젝트 설정으로 저장됩니다.");
                    })]
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(1)
                        + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
                        [SNew(SButton).ContentPadding(FMargin(18, 8))
                            .OnClicked_Lambda([] { FMDWorkspacePreferences::CloseWindow(); return FReply::Handled(); })
                            [Label(MDLOC("Cancel", "취소"))]]
                        + SHorizontalBox::Slot().AutoWidth()
                        [SNew(SButton).ButtonStyle(FAppStyle::Get(), "PrimaryButton").ContentPadding(FMargin(22, 8))
                            .OnClicked_Lambda([this] {
                                bSaveFailed = !FMDWorkspacePreferences::Apply(bDraftEnglish, bDraftDevices);
                                bApplied = !bSaveFailed;
                                if (!bSaveFailed)
                                {
                                    // Rebuild after the input callback returns, preserving widget lifetime during routing.
                                    RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SMDWorkspaceSettings::Rebuild));
                                }
                                return FReply::Handled();
                            })[Label(MDLOC("Apply", "적용"))]]
                    ]
                ]
            ];
        }

        EActiveTimerReturnType Rebuild(double, float) { Build(); return EActiveTimerReturnType::Stop; }
        bool bDraftEnglish = false;
        bool bDraftDevices = true;
        bool bApplied = false;
        bool bSaveFailed = false;
        TArray<TSharedPtr<FString>> Languages;
    };
}

bool FMDWorkspacePreferences::IsEnglish() { return Preferences().bEnglish; }
bool FMDWorkspacePreferences::ShowDevices() { return Preferences().bShowDevices; }
uint32 FMDWorkspacePreferences::Revision() { return Preferences().Revision; }

bool FMDWorkspacePreferences::Apply(bool bEnglish, bool bShowDevices)
{
    const FString Path = FPaths::ProjectSavedDir() / TEXT("Config/MDMultiUser.ini");
    FConfigFile Config;
    Config.Read(Path);
    Config.SetString(Section, TEXT("Language"), bEnglish ? TEXT("en") : TEXT("ko"));
    Config.SetBool(Section, TEXT("ShowDevices"), bShowDevices);
    Config.Dirty = true;
    if (!Config.Write(Path)) { return false; }

    // Read the file independently before reporting success or updating the visible UI.
    FConfigFile Readback;
    Readback.Read(Path);
    FString Language;
    bool bDevices = !bShowDevices;
    if (!Readback.GetString(Section, TEXT("Language"), Language) || Language != (bEnglish ? TEXT("en") : TEXT("ko"))
        || !Readback.GetBool(Section, TEXT("ShowDevices"), bDevices) || bDevices != bShowDevices) { return false; }
    Preferences().bEnglish = bEnglish;
    Preferences().bShowDevices = bShowDevices;
    ++Preferences().Revision;
    return true;
}

FText FMDWorkspacePreferences::Text(const TCHAR* Key, const TCHAR* Korean)
{
    if (IsEnglish())
    {
        if (const FString* Value = English().Find(Key)) { return FText::FromString(*Value); }
    }
    return FText::FromString(Korean);
}

void FMDWorkspacePreferences::OpenWindow()
{
    if (const auto Existing = SettingsWindow.Pin()) { Existing->BringToFront(); return; }
    const auto Window = SNew(SWindow)
        .Title_Lambda([] { return MDLOC("SettingsTitle", "작업 공간 설정"); })
        .ClientSize(FVector2D(460, 520)).MinWidth(400).MinHeight(480)
        .SupportsMaximize(false).SupportsMinimize(false)
        [SNew(SMDWorkspaceSettings)];
    SettingsWindow = Window;
    FSlateApplication::Get().AddWindow(Window);
}

void FMDWorkspacePreferences::CloseWindow()
{
    if (const auto Window = SettingsWindow.Pin()) { Window->RequestDestroyWindow(); }
    SettingsWindow.Reset();
}
