#pragma once
#include "CoreMinimal.h"
#include "MDPlacementTools.h"
#include "Widgets/SCompoundWidget.h"

class SMDPlacementTools : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SMDPlacementTools) {} SLATE_END_ARGS()
    void Construct(const FArguments& Args);
private:
    void Build();
    void ClearPreview();
    FReply Preview();
    FReply RunQuick(MDPlacement::EQuickAction Action);
    EActiveTimerReturnType Update(double, float);
    MDPlacement::FOptions& Options = MDPlacement::SessionOptions();
    TWeakObjectPtr<AActor> Target;
    bool bPinned = false;
    TArray<MDPlacement::FCandidate> Candidates;
    int32 CandidateIndex = 0;
    FText Feedback;
    uint32 LanguageRevision = 0;
};
