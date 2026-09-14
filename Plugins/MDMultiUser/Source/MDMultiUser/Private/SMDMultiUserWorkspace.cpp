#include "SMDMultiUserWorkspace.h"
#include "MDWorkspacePreferences.h"
#include "Widgets/Images/SImage.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "ConcertClientSettings.h"
#include "Editor.h"
#include "Engine/World.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "IConcertClient.h"
#include "IConcertSession.h"
#include "IConcertSyncClient.h"
#include "IMultiUserClientModule.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MDMultiUser"

namespace MDWorkspace
{
    // Small, shared palette; brush lifetime exceeds every widget using it.
    const FLinearColor Ink = FLinearColor::FromSRGBColor(FColor(233, 237, 242));
    const FLinearColor Muted = FLinearColor::FromSRGBColor(FColor(156, 168, 184));
    const FLinearColor Accent = FLinearColor::FromSRGBColor(FColor(86, 214, 185));
    const FLinearColor Amber = FLinearColor::FromSRGBColor(FColor(242, 191, 106));
    const FSlateRoundedBoxBrush Background(FLinearColor::FromSRGBColor(FColor(24, 27, 33)), 0.0f);
    const FSlateRoundedBoxBrush Card(FLinearColor::FromSRGBColor(FColor(34, 39, 47)), 8.0f);
    const FSlateRoundedBoxBrush Inset(FLinearColor::FromSRGBColor(FColor(28, 32, 39)), 6.0f);
    const FSlateRoundedBoxBrush Avatar(FLinearColor::White, 6.0f);

    TSharedRef<STextBlock> Label(const FText& Text, int32 Size = 10, bool bBold = false, FLinearColor Color = Ink)
    {
        return SNew(STextBlock).Text(Text).Font(FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size))
            .ColorAndOpacity(Color).AutoWrapText(true);
    }

    FReply OpenBrowser()
    {
        IMultiUserClientModule::Get().OpenBrowser();
        return FReply::Handled();
    }
}

void SMDMultiUserWorkspace::Construct(const FArguments& InArgs)
{
    using namespace MDWorkspace;
    PreferencesRevision = FMDWorkspacePreferences::Revision();
    Refresh(0, 0);
    BuildContent();
    RegisterActiveTimer(0.25f, FWidgetActiveTimerDelegate::CreateSP(this, &SMDMultiUserWorkspace::Refresh));
}

void SMDMultiUserWorkspace::BuildContent()
{
    using namespace MDWorkspace;
    ChildSlot
    [
        SNew(SBorder).BorderImage(&Background).Padding(20)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 18)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()[Label(MDLOC("Eyebrow", "REVSTUDIO  /  MULTI-USER"), 9, true, Accent)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)[Label(MDLOC("Title", "함께 편집"), 22, true)]
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton").ContentPadding(FMargin(10, 7))
                    .ToolTipText(MDLOC("SettingsTip", "작업 공간 설정과 언어 선택을 엽니다."))
                    .OnClicked_Lambda([] { FMDWorkspacePreferences::OpenWindow(); return FReply::Handled(); })
                    [SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
                        [SNew(STextBlock).Text_Lambda([this] { return FText::FromString(LocalDisplayName); }).ColorAndOpacity(Muted)]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [SNew(SImage).Image(FAppStyle::GetBrush("Icons.Settings"))]]
                ]
            ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SBorder).BorderImage(&Card).Padding(18)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [SNew(STextBlock).Text(this, &SMDMultiUserWorkspace::StatusText)
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10)).ColorAndOpacity(this, &SMDMultiUserWorkspace::StatusColor)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 6)
                    [SNew(STextBlock).Text(this, &SMDMultiUserWorkspace::SessionTitle).ToolTipText_Lambda([this] { return FText::FromString(SessionName); })
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 18)).ColorAndOpacity(Ink).AutoWrapText(true)]
                    + SVerticalBox::Slot().AutoHeight()
                    [SNew(STextBlock).Text(this, &SMDMultiUserWorkspace::StatusDescription).AutoWrapText(true).ColorAndOpacity(Muted)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 16, 0, 0)
                    [
                        SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8, 8))
                        + SWrapBox::Slot()
                        [
                            SNew(SButton).ButtonStyle(FAppStyle::Get(), "PrimaryButton").ContentPadding(FMargin(18, 9))
                            .IsEnabled_Lambda([this] { return bClientAvailable; })
                            .OnClicked_Lambda([] { return OpenBrowser(); })
                            .ToolTipText(MDLOC("BrowserTip", "기존 Multi-User 브라우저에서 서버와 세션을 선택하거나 활동 내역을 확인합니다."))
                            [SNew(STextBlock).Text_Lambda([this] {
                                if (IsBusy()) { return MDLOC("Progress", "연결 진행 확인"); }
                                return IsConnected() ? MDLOC("Details", "세션 상세 열기") : MDLOC("Choose", "세션 선택");
                            })]
                        ]
                        + SWrapBox::Slot()
                        [
                            SNew(SButton).ContentPadding(FMargin(12, 9))
                            .Visibility_Lambda([this] { return IsConnected() ? EVisibility::Visible : EVisibility::Collapsed; })
                            .OnClicked_Lambda([] { IMultiUserClientModule::Get().DisconnectSession(true); return FReply::Handled(); })
                            .ToolTipText(MDLOC("LeaveTip", "Unreal의 변경사항 저장 확인 창을 거쳐 세션을 나갑니다."))
                            [Label(MDLOC("Leave", "세션 나가기"))]
                        ]
                    ]
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 14)
            [
                SNew(SBorder).BorderImage(&Inset).Padding(12)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()[Label(MDLOC("LocalLevel", "내 편집 레벨"), 9, false, Muted)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
                    [SNew(STextBlock).Text_Lambda([this] { return FText::FromString(MapName); })
                        .ToolTipText_Lambda([this] { return FText::FromString(MapPath); }).ColorAndOpacity(Ink).AutoWrapText(true)]
                ]
            ]
            + SVerticalBox::Slot().FillHeight(1)
            [
                SNew(SOverlay)
                + SOverlay::Slot()
                [
                    SNew(SVerticalBox).Visibility_Lambda([this] { return IsConnected() ? EVisibility::Visible : EVisibility::Collapsed; })
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
                    [SNew(STextBlock).Text_Lambda([this] { return FText::Format(MDLOC("Participants", "참여자  {0}"), FText::AsNumber(Participants.Num())); })
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12)).ColorAndOpacity(Ink)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
                    [SNew(SSearchBox).InitialText(FText::FromString(Search)).HintText(MDLOC("Search", "이름 또는 기기 검색"))
                        .OnTextChanged_Lambda([this](const FText& Text) { Search = Text.ToString(); FilterParticipants(); })]
                    + SVerticalBox::Slot().FillHeight(1)
                    [
                        SNew(SOverlay)
                        + SOverlay::Slot()
                        [SAssignNew(ParticipantList, SListView<TSharedPtr<FMDParticipant>>)
                            .ListItemsSource(&FilteredParticipants).SelectionMode(ESelectionMode::None)
                            .OnGenerateRow(this, &SMDMultiUserWorkspace::MakeParticipantRow)]
                        + SOverlay::Slot().VAlign(VAlign_Center).HAlign(HAlign_Center)
                        [SNew(STextBlock).Text(MDLOC("NoResults", "검색 결과가 없습니다."))
                            .ColorAndOpacity(Muted).Visibility_Lambda([this] { return FilteredParticipants.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })]
                    ]
                ]
                + SOverlay::Slot()
                [
                    SNew(SScrollBox).Visibility_Lambda([this] { return IsConnected() ? EVisibility::Collapsed : EVisibility::Visible; })
                    + SScrollBox::Slot()
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 12)[Label(MDLOC("StartTitle", "같은 레벨에서, 같은 팀으로"), 14, true)]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)[Label(MDLOC("Step1", "01   팀과 같은 프로젝트 커밋으로 준비하세요."), 10, false, Muted)]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)[Label(MDLOC("Step2", "02   Tailscale 연결 후 팀 세션을 선택하세요."), 10, false, Muted)]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)[Label(MDLOC("Step3", "03   참여자를 확인하고 레벨 편집을 시작하세요."), 10, false, Muted)]
                        + SVerticalBox::Slot().AutoHeight()
                        [
                            SNew(SBorder).BorderImage(&Card).Padding(14)
                            [SNew(SVerticalBox)
                                + SVerticalBox::Slot().AutoHeight()[Label(MDLOC("NoServerTitle", "세션이 보이지 않나요?"), 10, true)]
                                + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
                                [Label(MDLOC("NoServerHelp", "Tailscale에서 팀 호스트가 보이는지 확인하세요. 멀티유저 런처로 에디터를 실행하면 연결 주소가 자동으로 적용됩니다."), 10, false, Muted)]]
                        ]
                    ]
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 0)
            [
                SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8, 6))
                + SWrapBox::Slot()
                [SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton").ContentPadding(FMargin(6, 5))
                    .OnClicked_Lambda([] {
                        const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Docs/MultiUser/멀티유저 편집 운영 규칙.md"));
                        FPlatformProcess::LaunchFileInDefaultExternalApplication(*Path);
                        return FReply::Handled();
                    })[Label(MDLOC("Guide", "팀 작업 가이드"), 10, false, Muted)]]
                + SWrapBox::Slot()
                [SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton").ContentPadding(FMargin(6, 5))
                    .Visibility_Lambda([this] { return IsConnected() ? EVisibility::Visible : EVisibility::Collapsed; })
                    .ToolTipText(MDLOC("CopyTip", "현재 세션 이름을 클립보드에 복사합니다."))
                    .OnClicked_Lambda([this] {
                        FPlatformApplicationMisc::ClipboardCopy(*SessionName);
                        CopyFeedbackUntil = FPlatformTime::Seconds() + 2.0;
                        return FReply::Handled();
                    })
                    [SNew(STextBlock).ColorAndOpacity(Muted).Text_Lambda([this] {
                        return FPlatformTime::Seconds() < CopyFeedbackUntil ? MDLOC("Copied", "복사했습니다") : MDLOC("Copy", "세션 이름 복사");
                    })]]
            ]
        ]
    ];
}

EActiveTimerReturnType SMDMultiUserWorkspace::Refresh(double CurrentTime, float DeltaTime)
{
    Status = EConcertConnectionStatus::Disconnected;
    bClientAvailable = false;
    SessionName.Reset();
    ServerName.Reset();
    TArray<TSharedPtr<FMDParticipant>> Updated;
    if (IMultiUserClientModule::IsAvailable())
    {
        if (const TSharedPtr<IConcertSyncClient> Sync = IMultiUserClientModule::Get().GetClient())
        {
            bClientAvailable = true;
            const auto Client = Sync->GetConcertClient();
            const auto& LocalInfo = Client->GetClientInfo();
            LocalDisplayName = LocalInfo.DisplayName.IsEmpty() ? LocalInfo.UserName : LocalInfo.DisplayName;
            Status = Client->GetSessionConnectionStatus();
            if (const UConcertClientConfig* Config = Client->GetConfiguration())
            {
                ServerName = Config->DefaultServerURL;
            }
            if (const auto Session = Client->GetCurrentSession(); Session && IsConnected())
            {
                SessionName = Session->GetSessionInfo().SessionName;
                // Resolve the actual session server; the configured default may be a different host.
                ServerName.Reset();
                for (const auto& Server : Client->GetKnownServers())
                {
                    if (Server.InstanceInfo.InstanceId == Session->GetSessionInfo().ServerInstanceId)
                    {
                        ServerName = Server.ServerName;
                        break;
                    }
                }
                auto Local = MakeShared<FMDParticipant>();
                Local->Id = Session->GetSessionClientEndpointId();
                Local->Info = Session->GetLocalClientInfo();
                Local->bLocal = true;
                Updated.Add(Local);
                for (const auto& Remote : Session->GetSessionClients())
                {
                    if (Remote.ClientEndpointId == Local->Id) { continue; }
                    auto Participant = MakeShared<FMDParticipant>();
                    Participant->Id = Remote.ClientEndpointId;
                    Participant->Info = Remote.ClientInfo;
                    Updated.Add(Participant);
                }
                Updated.Sort([](const auto& A, const auto& B) {
                    if (A->bLocal != B->bLocal) { return A->bLocal; }
                    const int32 Order = A->Info.DisplayName.Compare(B->Info.DisplayName, ESearchCase::IgnoreCase);
                    return Order != 0 ? Order < 0 : A->Id.ToString() < B->Id.ToString();
                });
            }
        }
    }
    bool bChanged = Participants.Num() != Updated.Num();
    for (int32 Index = 0; !bChanged && Index < Updated.Num(); ++Index)
    {
        bChanged = Participants[Index]->Id != Updated[Index]->Id || Participants[Index]->Info != Updated[Index]->Info;
    }
    if (bChanged)
    {
        Participants = MoveTemp(Updated);
        FilterParticipants();
    }
    const UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    MapName = World ? World->GetName() : MDLOC("NoLevel", "열린 레벨 없음").ToString();
    MapPath = World ? World->GetOutermost()->GetName() : FString();
    if (PreferencesRevision != FMDWorkspacePreferences::Revision())
    {
        PreferencesRevision = FMDWorkspacePreferences::Revision();
        BuildContent();
        ParticipantList->RequestListRefresh();
    }
    return EActiveTimerReturnType::Continue;
}

void SMDMultiUserWorkspace::FilterParticipants()
{
    FilteredParticipants.Reset();
    for (const auto& Participant : Participants)
    {
        if (Search.IsEmpty() || Participant->Info.DisplayName.Contains(Search) || Participant->Info.UserName.Contains(Search)
            || Participant->Info.DeviceName.Contains(Search))
        {
            FilteredParticipants.Add(Participant);
        }
    }
    if (ParticipantList) { ParticipantList->RequestListRefresh(); }
}

TSharedRef<ITableRow> SMDMultiUserWorkspace::MakeParticipantRow(TSharedPtr<FMDParticipant> Item, const TSharedRef<STableViewBase>& Owner)
{
    using namespace MDWorkspace;
    const FString Name = Item->Info.DisplayName.IsEmpty() ? Item->Info.UserName : Item->Info.DisplayName;
    return SNew(STableRow<TSharedPtr<FMDParticipant>>, Owner).Padding(FMargin(0, 3))
    [
        SNew(SBorder).BorderImage(&Card).Padding(12)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
            [SNew(SBox).WidthOverride(4).HeightOverride(32)
                [SNew(SBorder).BorderImage(&Avatar).BorderBackgroundColor(Item->Info.AvatarColor)]]
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
            [SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()[Label(FText::FromString(Name), 11, true)]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
                [SNew(SBox).Visibility(FMDWorkspacePreferences::ShowDevices() ? EVisibility::Visible : EVisibility::Collapsed)
                    [Label(FText::FromString(Item->Info.DeviceName + TEXT("  /  ") + Item->Info.PlatformName), 9, false, Muted)]]]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0)
            [Label(Item->bLocal ? MDLOC("Me", "나") : MDLOC("Online", "참여 중"), 9, true, Item->bLocal ? Accent : Muted)]
        ]
    ];
}

FText SMDMultiUserWorkspace::StatusText() const
{
    if (!bClientAvailable) { return MDLOC("Unavailable", "클라이언트 준비 중"); }
    switch (Status)
    {
    case EConcertConnectionStatus::Connected: return MDLOC("Connected", "연결됨");
    case EConcertConnectionStatus::Connecting: return MDLOC("Connecting", "연결 중…");
    case EConcertConnectionStatus::Disconnecting: return MDLOC("Disconnecting", "연결 종료 중…");
    default: return MDLOC("Disconnected", "연결 안 됨");
    }
}

FText SMDMultiUserWorkspace::SessionTitle() const
{
    if (IsConnected()) { return FText::FromString(SessionName); }
    return IsBusy() ? MDLOC("Wait", "잠시만 기다려 주세요") : MDLOC("Ready", "팀 세션에 참여하세요");
}

FText SMDMultiUserWorkspace::StatusDescription() const
{
    if (!bClientAvailable) { return MDLOC("UnavailableHelp", "Multi-User 클라이언트가 준비되면 세션을 선택할 수 있습니다."); }
    if (IsBusy()) { return MDLOC("BusyHelp", "진행 상황과 오류는 Multi-User 브라우저에서 확인할 수 있습니다."); }
    if (IsConnected())
    {
        return ServerName.IsEmpty() ? MDLOC("LiveHelp", "팀 세션에 참여 중입니다. 활동 내역은 세션 상세에서 확인하세요.")
            : FText::Format(MDLOC("ServerLive", "{0} 서버 · 팀 세션에 참여 중입니다."), FText::FromString(ServerName));
    }
    return MDLOC("OfflineHelp", "세션을 선택하면 함께 작업하는 팀원이 여기에 표시됩니다.");
}

FSlateColor SMDMultiUserWorkspace::StatusColor() const
{
    return IsConnected() ? MDWorkspace::Accent : IsBusy() ? MDWorkspace::Amber : MDWorkspace::Muted;
}

#undef LOCTEXT_NAMESPACE
