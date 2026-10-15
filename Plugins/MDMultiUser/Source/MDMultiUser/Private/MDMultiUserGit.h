#pragma once
#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformProcess.h"
class FJsonObject;

struct FMDGitFile
{
    FString Path, State, OtherPath;
    bool bUnpublished = false, bNeedsPublish = true;
};

class FMDMultiUserGit
{
public:
    static FMDMultiUserGit& Get();
    void Start();
    void Stop();
    bool Run(const FString& Operation, const FString& Message = {});
    void AutoSelectPlaced();
    bool Busy() const { return Process.IsValid(); }
    bool Selected(const FString& Path) const { return Selection.Contains(Path); }
    void Select(const FString& Path, bool bSelect);
    FText Status() const;
    TArray<TSharedPtr<FMDGitFile>> Files;
    FString Branch, Head, Upstream, Feedback;
    int32 Ahead = 0, Behind = 0, Revision = 0;
private:
    bool Tick(float Delta);
    bool CanWrite(const FString& Operation, FString& Error) const;
    FTSTicker::FDelegateHandle Ticker;
    FDelegateHandle PlacedHandle, DroppedHandle;
    FProcHandle Process;
    FString Job, Operation;
    double RefreshAt = 0;
    bool bPlacementDirty = false;
    TSet<FString> Selection, ManualSelection, Excluded, PlacedPaths;
};
