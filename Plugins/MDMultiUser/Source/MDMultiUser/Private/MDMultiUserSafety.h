#pragma once
#include "CoreMinimal.h"
#include "Containers/Ticker.h"

struct FAssetData;
class UActorFactory;
class UObject;
class IConcertClientWorkspace;
class IConcertSyncClient;
class UWorld;
class FObjectPreSaveContext;

/** Local recovery only. Never saves a project package or writes back into Concert. */
class FMDMultiUserSafety
{
public:
    static FMDMultiUserSafety& Get();
    void Start();
    void Stop();
    bool Checkpoint(UWorld* World, FString& Error);
    bool PrepareRecovery(UWorld* World, FString& Directory, FString& Error);
    FText Status() const;
    FString RecoveryRoot() const;
    static constexpr double IntervalSeconds = 300.0;
private:
    friend class FMDSafetyRecoveryTest;
    bool Tick(float Delta);
    bool Connected() const;
    void AssetDrag(const TArray<FAssetData>& Assets, UActorFactory* Factory);
    void PreSave(UObject* Object, FObjectPreSaveContext Context);
    void BindWorkspace(const TSharedPtr<IConcertClientWorkspace>& Workspace);
    void Warn(const FString& Message);
    void CheckForMissingActors(UWorld* World);
    FTSTicker::FDelegateHandle TickHandle;
    FDelegateHandle DragHandle, SaveHandle, WorkspaceHandle, FinalizeHandle;
    TWeakPtr<IConcertSyncClient> SyncClient;
    TWeakPtr<IConcertClientWorkspace> Workspace;
    bool bSyncReady = true, bCheckedPeers = false;
    double NextCheckpoint = 0, LastWarning = -100;
    bool bWasConnected = false, bWriting = false;
    TWeakObjectPtr<UWorld> ObservedWorld;
    double StableAfter = 0;
    bool bCheckedRecovery = false, bPendingRecovery = false;
    FString LastStatus, LastRecoverySource;
};
