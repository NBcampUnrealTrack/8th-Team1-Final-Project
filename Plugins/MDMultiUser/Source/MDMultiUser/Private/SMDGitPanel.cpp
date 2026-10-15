#include "SMDGitPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Misc/MessageDialog.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

void SMDGitPanel::Construct(const FArguments&)
{
    auto Button = [](const TCHAR* Label, const TCHAR* Operation)
    {
        return SNew(SButton).Text(FText::FromString(Label)).IsEnabled_Lambda([] { return !FMDMultiUserGit::Get().Busy(); })
            .OnClicked_Lambda([Operation] {
                if (FString(Operation) == TEXT("pull") && FMessageDialog::Open(EAppMsgType::YesNo,
                    FText::FromString(TEXT("체크포인트 생성 후 에디터를 종료하고 ff-only Pull을 실행합니다. C++ 변경이 있으면 빌드 후 현재 맵을 다시 엽니다. 진행할까요?"))) != EAppReturnType::Yes) return FReply::Handled();
                if (FString(Operation) == TEXT("push") && FMessageDialog::Open(EAppMsgType::YesNo,
                    FText::FromString(TEXT("현재 브랜치의 모든 미푸시 커밋을 표시된 원격 브랜치로 게시합니다. 선택 파일만 따로 푸시하는 동작은 아닙니다. 진행할까요?"))) != EAppReturnType::Yes) return FReply::Handled();
                FMDMultiUserGit::Get().Run(Operation); return FReply::Handled();
            });
    };
    ChildSlot
    [SNew(SExpandableArea).InitiallyCollapsed(false)
        .HeaderContent()[SNew(STextBlock).Text(FText::FromString(TEXT("Git · 배치 작업과 참조 파일")))]
        .BodyContent()
        [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0,6)
            [SNew(STextBlock).AutoWrapText(true).Text_Lambda([] { return FMDMultiUserGit::Get().Status(); })]
            + SVerticalBox::Slot().AutoHeight()
            [SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(5,5))
                + SWrapBox::Slot()[Button(TEXT("상태"), TEXT("status"))]
                + SWrapBox::Slot()[Button(TEXT("Fetch"), TEXT("fetch"))]
                + SWrapBox::Slot()[Button(TEXT("안전한 Pull"), TEXT("pull"))]
                + SWrapBox::Slot()[SNew(SButton).Text(FText::FromString(TEXT("배치 참조 재선택"))).OnClicked_Lambda([] {
                    FMDMultiUserGit::Get().AutoSelectPlaced(); return FReply::Handled(); })]
                + SWrapBox::Slot()[SNew(SButton).Text(FText::FromString(TEXT("Git 로그"))).OnClicked_Lambda([] {
                    FPlatformProcess::ExploreFolder(*FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("MultiUserGit"))); return FReply::Handled(); })]]
            + SVerticalBox::Slot().AutoHeight().Padding(0,6)
            [SNew(SBox).HeightOverride(180)
                [SAssignNew(List,SListView<TSharedPtr<FMDGitFile>>).ListItemsSource(&FMDMultiUserGit::Get().Files)
                    .OnGenerateRow_Lambda([](TSharedPtr<FMDGitFile> File,const TSharedRef<STableViewBase>& Owner) {
                        return SNew(STableRow<TSharedPtr<FMDGitFile>>,Owner)
                            [SNew(SCheckBox).IsEnabled_Lambda([] { return !FMDMultiUserGit::Get().Busy(); })
                                .IsChecked_Lambda([File] { return FMDMultiUserGit::Get().Selected(File->Path) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                                .OnCheckStateChanged_Lambda([File](ECheckBoxState State) { FMDMultiUserGit::Get().Select(File->Path,State==ECheckBoxState::Checked); })
                                [SNew(STextBlock).ToolTipText(FText::FromString(File->Path)).Text(FText::FromString(File->State + (File->bUnpublished ? TEXT(" 미푸시 · ") : TEXT(" · ")) + FPaths::GetCleanFilename(File->Path)))]];
                    })]]
            + SVerticalBox::Slot().AutoHeight()
            [SAssignNew(Message,SEditableTextBox).HintText(FText::FromString(TEXT("선택 파일 커밋 메시지")))]
            + SVerticalBox::Slot().AutoHeight().Padding(0,5)
            [SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(5,5))
                + SWrapBox::Slot()[SNew(SButton).Text(FText::FromString(TEXT("선택 변경 파일 커밋"))).IsEnabled_Lambda([] { return !FMDMultiUserGit::Get().Busy(); })
                    .OnClicked_Lambda([this] { FMDMultiUserGit::Get().Run(TEXT("commit"),Message->GetText().ToString()); return FReply::Handled(); })]
                + SWrapBox::Slot()[Button(TEXT("브랜치 푸시"),TEXT("push"))]]
            + SVerticalBox::Slot().AutoHeight()
            [SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("배치된 액터·참조 에셋 중 변경/미푸시 파일을 자동 선택합니다. 수동 해제는 유지됩니다. 원격 기준을 갱신하려면 Fetch를 누르세요. Pull·커밋은 세션 종료 및 미저장 작업 보호 후 진행합니다.")))]]];
    RegisterActiveTimer(.25f,FWidgetActiveTimerDelegate::CreateSP(this,&SMDGitPanel::Refresh));
}
EActiveTimerReturnType SMDGitPanel::Refresh(double,float)
{
    if (Revision != FMDMultiUserGit::Get().Revision)
    { Revision=FMDMultiUserGit::Get().Revision; List->RequestListRefresh(); }
    return EActiveTimerReturnType::Continue;
}
