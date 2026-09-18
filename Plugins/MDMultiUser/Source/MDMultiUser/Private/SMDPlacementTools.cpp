#include "SMDPlacementTools.h"
#include "MDWorkspacePreferences.h"
#include "MDHoldSnap.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Editor.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    const FSlateRoundedBoxBrush Background(FLinearColor::FromSRGBColor(FColor(24,27,33)),0.f);
    const FSlateRoundedBoxBrush Card(FLinearColor::FromSRGBColor(FColor(34,39,47)),8.f);
    TSharedRef<STextBlock> Label(const FText& Text, bool Bold=false)
    { return SNew(STextBlock).Text(Text).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle(Bold ? "Bold" : "Regular", 10)); }
}

void SMDPlacementTools::Construct(const FArguments& Args)
{
    LanguageRevision = FMDWorkspacePreferences::Revision();
    Build();
    RegisterActiveTimer(.25f, FWidgetActiveTimerDelegate::CreateSP(this, &SMDPlacementTools::Update));
}

void SMDPlacementTools::Build()
{
    using namespace MDPlacement;
    auto Quick = [this](EQuickAction Action, const FText& Name) -> TSharedRef<SWidget> {
        return SNew(SButton).ContentPadding(FMargin(12,8))
            .IsEnabled_Lambda([] { FText Reason; return CanMove(SelectedActor(),Reason); })
            .OnClicked_Lambda([this,Action] { return RunQuick(Action); })[Label(Name)];
    };
    ChildSlot
    [SNew(SBorder).BorderImage(&Background).Padding(20)
        [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,16)
            [SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(Text(TEXT("배치 도구"),TEXT("Placement tools"))).Font(FCoreStyle::GetDefaultFontStyle("Bold",22))]
                + SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)
                [SNew(STextBlock).AutoWrapText(true).Text_Lambda([] {
                    AActor* Actor = SelectedActor();
                    return Actor ? FText::FromString(Actor->GetActorLabel()) : Text(TEXT("레벨에서 이동할 액터 하나를 선택하세요."),TEXT("Select one actor to move in the level."));
                })]
            ]
            + SVerticalBox::Slot().FillHeight(1)
            [SNew(SScrollBox)
                + SScrollBox::Slot()
                [SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)
                    [SNew(STextBlock).AutoWrapText(true).Text_Lambda([] { return MDHoldSnap::Shortcuts(); })
                        .ToolTipText(Text(TEXT("에디터 개인설정 → 키보드 단축키 → Multi-User Placement에서 변경할 수 있습니다. 액터 선택 후 키를 누른 채 대상을 클릭하세요. U/Y는 한 번 누릅니다."),TEXT("Change keys in Editor Preferences > Keyboard Shortcuts > Multi-User Placement. Hold a key and click a target. Press U/Y once.")))]
                    + SVerticalBox::Slot().AutoHeight()[Label(Text(TEXT("빠른 배치"),TEXT("Quick placement")),true)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,10,0,18)
                    [SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6,6))
                        + SWrapBox::Slot()[Quick(EQuickAction::Floor,Text(TEXT("수직 바닥 스냅 · U"),TEXT("Vertical drop · U")))]
                        + SWrapBox::Slot()[Quick(EQuickAction::Grid,Text(TEXT("그리드 맞춤 · Y"),TEXT("Snap to grid · Y")))]]
                    + SVerticalBox::Slot().AutoHeight()
                    [SNew(SBorder).BorderImage(&Card).Padding(16)
                        [SNew(SVerticalBox)
                            + SVerticalBox::Slot().AutoHeight()[Label(Text(TEXT("모서리 스냅"),TEXT("Edge snap")),true)]
                            + SVerticalBox::Slot().AutoHeight().Padding(0,6,0,12)
                            [Label(Text(TEXT("메시 루트 액터 1개 · Blueprint 지원 · 실제 메시 모서리에 맞춥니다."),TEXT("One actor with a static mesh root, including Blueprints. Align real mesh edges.")))]
                            + SVerticalBox::Slot().AutoHeight()
                            [SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6,6))
                                + SWrapBox::Slot()
                                [SNew(SButton).ContentPadding(FMargin(10,7))
                                    .IsEnabled_Lambda([] { return SelectedActor()!=nullptr; })
                                    .OnClicked_Lambda([this] {
                                        Target=SelectedActor(); bPinned=true; ClearPreview();
                                        Feedback=Text(TEXT("기준 액터 지정 완료. 이제 이동할 액터를 선택하세요."),TEXT("Target pinned. Now select the actor to move.")); return FReply::Handled();
                                    })[Label(Text(TEXT("선택을 기준으로 지정"),TEXT("Pin selected as target")))]]
                                + SWrapBox::Slot()
                                [SNew(SButton).ContentPadding(FMargin(10,7))
                                    .OnClicked_Lambda([this] { Target.Reset(); bPinned=false; ClearPreview(); return FReply::Handled(); })
                                    [Label(Text(TEXT("주변 자동 탐색"),TEXT("Search nearby")))]]]
                            + SVerticalBox::Slot().AutoHeight().Padding(0,8,0,16)
                            [SNew(STextBlock).AutoWrapText(true).Text_Lambda([this] {
                                if (!bPinned) { return Text(TEXT("기준: 가까운 메시 최대 16개"),TEXT("Target: up to 16 nearby mesh actors")); }
                                return Target.IsValid() ? FText::FromString(Target->GetActorLabel()) : Text(TEXT("기준 액터가 사라졌습니다. 다시 지정하세요."),TEXT("Pinned target is gone. Pin another target."));
                            })]
                            + SVerticalBox::Slot().AutoHeight()[Label(Text(TEXT("최대 이동 거리 (cm)"),TEXT("Maximum movement (cm)")))]
                            + SVerticalBox::Slot().AutoHeight().Padding(0,6,0,12)
                            [SNew(SNumericEntryBox<double>).MinValue(1).MaxValue(1000).MinSliderValue(1).MaxSliderValue(500)
                                .Value_Lambda([this] { return TOptional<double>(Options.Distance); })
                                .OnValueChanged_Lambda([this](double Value) { Options.Distance=FMath::Clamp(Value,1.,1000.); ClearPreview(); })]
                            + SVerticalBox::Slot().AutoHeight()[Label(Text(TEXT("끝점 맞춤 · 액터 회전 유지"),TEXT("Endpoint matching · Keep actor rotation")))]
                            + SVerticalBox::Slot().AutoHeight().Padding(0,16,0,0)
                            [SNew(SButton).ButtonStyle(FAppStyle::Get(),"PrimaryButton").ContentPadding(FMargin(14,9))
                                .IsEnabled_Lambda([] { FText Reason; return CanMove(SelectedActor(),Reason); })
                                .OnClicked(this,&SMDPlacementTools::Preview)[Label(Text(TEXT("모서리 후보 미리보기"),TEXT("Preview edge matches")))]]
                        ]
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,14,0,0)
                    [Label(Text(TEXT("주황: 원래 모서리 · 청록: 기준 · 초록: 적용 위치\n끝점을 일치시킵니다. 면 겹침·충돌은 미리보기에서 확인하세요."),TEXT("Orange: source · Cyan: target · Green: result\nMatches edge endpoints. Check preview for face overlap / collisions.")))]
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,12,0,8)
            [SNew(STextBlock).AutoWrapText(true).Text_Lambda([this] {
                if (!Candidates.IsValidIndex(CandidateIndex)) { return Feedback; }
                const FCandidate& C=Candidates[CandidateIndex];
                return FText::Format(Text(TEXT("후보 {0}/{1} · {2} cm · {3}°\n{4}\n{5}"),TEXT("Match {0}/{1} · {2} cm · {3}°\n{4}\n{5}")),
                    CandidateIndex+1,Candidates.Num(),FText::AsNumber(FMath::RoundToDouble(C.Distance*10)/10),FText::AsNumber(FMath::RoundToDouble(C.Angle*10)/10),
                    C.Target.IsValid() ? FText::FromString(C.Target->GetActorLabel()) : FText::GetEmpty(),Feedback);
            })]
            + SVerticalBox::Slot().AutoHeight()
            [SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6,6))
                + SWrapBox::Slot()[SNew(SButton).ContentPadding(FMargin(10,8)).IsEnabled_Lambda([this] { return Candidates.Num()>1; })
                    .OnClicked_Lambda([this] { CandidateIndex=(CandidateIndex+Candidates.Num()-1)%Candidates.Num(); return FReply::Handled(); })[Label(Text(TEXT("이전"),TEXT("Previous")))]]
                + SWrapBox::Slot()[SNew(SButton).ContentPadding(FMargin(10,8)).IsEnabled_Lambda([this] { return Candidates.Num()>1; })
                    .OnClicked_Lambda([this] { CandidateIndex=(CandidateIndex+1)%Candidates.Num(); return FReply::Handled(); })[Label(Text(TEXT("다음"),TEXT("Next")))]]
                + SWrapBox::Slot()[SNew(SButton).ContentPadding(FMargin(10,8))
                    .OnClicked_Lambda([this] { ClearPreview(); return FReply::Handled(); })[Label(Text(TEXT("취소"),TEXT("Cancel")))]]
                + SWrapBox::Slot()[SNew(SButton).ButtonStyle(FAppStyle::Get(),"PrimaryButton").ContentPadding(FMargin(18,8))
                    .IsEnabled_Lambda([this] { FText Reason; return Candidates.IsValidIndex(CandidateIndex) && IsSnapshotCurrent(Candidates[CandidateIndex]) &&
                        Candidates[CandidateIndex].Source.Get()==SelectedActor() && CanMove(SelectedActor(),Reason); })
                    .OnClicked_Lambda([this] { if (Candidates.IsValidIndex(CandidateIndex)) { Apply(Candidates[CandidateIndex],Feedback); Candidates.Reset(); } return FReply::Handled(); })
                    [Label(Text(TEXT("적용"),TEXT("Apply")))]]]
        ]
    ];
}

void SMDPlacementTools::ClearPreview() { Candidates.Reset(); CandidateIndex=0; Feedback=FText::GetEmpty(); }
FReply SMDPlacementTools::Preview()
{
    ClearPreview();
    Options.Anchor=MDPlacement::EAnchor::Endpoint; Options.bRotate=false;
    if (bPinned && (!Target.IsValid() || Target.Get()==MDPlacement::SelectedActor()))
    { Feedback=MDPlacement::Text(TEXT("기준과 다른 이동 액터를 선택하세요. 기준이 없어졌다면 다시 지정하세요."),TEXT("Select a different source actor, or re-pin the missing target.")); return FReply::Handled(); }
    MDPlacement::FindCandidates(MDPlacement::SelectedActor(),bPinned ? Target.Get() : nullptr,Options,Candidates,Feedback);
    return FReply::Handled();
}
FReply SMDPlacementTools::RunQuick(MDPlacement::EQuickAction Action)
{
    ClearPreview(); MDPlacement::QuickAction(Action,Feedback); return FReply::Handled();
}
EActiveTimerReturnType SMDPlacementTools::Update(double, float)
{
    if (LanguageRevision!=FMDWorkspacePreferences::Revision()) { LanguageRevision=FMDWorkspacePreferences::Revision(); ClearPreview(); Build(); }
    if (Candidates.IsValidIndex(CandidateIndex))
    {
        FText Reason;
        if (!MDPlacement::IsSnapshotCurrent(Candidates[CandidateIndex]) || Candidates[CandidateIndex].Source.Get()!=MDPlacement::SelectedActor()
            || !MDPlacement::CanMove(MDPlacement::SelectedActor(),Reason))
        { ClearPreview(); Feedback=MDPlacement::Text(TEXT("선택·위치·편집 상태가 바뀌어 미리보기를 취소했습니다."),TEXT("Preview canceled because selection, transforms or editing state changed.")); }
        else { MDPlacement::DrawPreview(Candidates[CandidateIndex]); }
    }
    return EActiveTimerReturnType::Continue;
}
