#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "MDMultiUserGit.h"
class SEditableTextBox;
template<typename T> class SListView;

class SMDGitPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SMDGitPanel) {} SLATE_END_ARGS()
    void Construct(const FArguments& Args);
private:
    EActiveTimerReturnType Refresh(double Now, float Delta);
    TSharedPtr<SListView<TSharedPtr<FMDGitFile>>> List;
    TSharedPtr<SEditableTextBox> Message;
    int32 Revision = -1;
};
