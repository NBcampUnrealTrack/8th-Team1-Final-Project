#pragma once

#include "CoreMinimal.h"
#include "ConcertMessages.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

struct FMDParticipant
{
    FGuid Id;
    FConcertClientInfo Info;
    bool bLocal = false;
};

/** Presentation only: connection, persistence and session ownership stay in Concert. */
class SMDMultiUserWorkspace : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SMDMultiUserWorkspace) {}
    SLATE_END_ARGS()
    void Construct(const FArguments& InArgs);

private:
    void BuildContent();
    EActiveTimerReturnType Refresh(double CurrentTime, float DeltaTime);
    void FilterParticipants();
    TSharedRef<ITableRow> MakeParticipantRow(TSharedPtr<FMDParticipant> Item, const TSharedRef<STableViewBase>& Owner);
    FText StatusText() const;
    FText SessionTitle() const;
    FText StatusDescription() const;
    FSlateColor StatusColor() const;
    bool IsConnected() const { return Status == EConcertConnectionStatus::Connected; }
    bool IsBusy() const { return Status == EConcertConnectionStatus::Connecting || Status == EConcertConnectionStatus::Disconnecting; }

    EConcertConnectionStatus Status = EConcertConnectionStatus::Disconnected;
    bool bClientAvailable = false;
    FString SessionName;
    FString ServerName;
    FString MapName;
    FString MapPath;
    FString Search;
    FString LocalDisplayName;
    uint32 PreferencesRevision = 0;
    double CopyFeedbackUntil = 0.0;
    TArray<TSharedPtr<FMDParticipant>> Participants;
    TArray<TSharedPtr<FMDParticipant>> FilteredParticipants;
    TSharedPtr<SListView<TSharedPtr<FMDParticipant>>> ParticipantList;
};
