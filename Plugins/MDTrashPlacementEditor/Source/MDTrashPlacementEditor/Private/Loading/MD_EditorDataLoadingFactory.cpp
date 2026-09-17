#include "Loading/MD_EditorDataLoadingFactory.h"

#include "Components/ActorComponent.h"
#include "Elements/Interfaces/TypedElementDataStorageCompatibilityInterface.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "UI/Loading/MD_LoadingScreenSubsystem.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "UObject/Package.h"

namespace
{
    TAutoConsoleVariable<int32> CVarEditorDataBatchSize(
        TEXT("MD.EditorData.RegistrationBatchSize"), 512,
        TEXT("Maximum queued map-loading editor metadata registrations per frame. 0 uses immediate registration."));
    constexpr int32 MaximumPendingObjects = 65536;
}

void UMD_EditorDataLoadingFactory::RegisterRegistrationFilters(
    UE::Editor::DataStorage::ICompatibilityProvider& Compatibility) const
{
    auto* MutableThis = const_cast<UMD_EditorDataLoadingFactory*>(this);
    MutableThis->Provider = &Compatibility;
    Compatibility.RegisterRegistrationFilter(
        [WeakThis = TWeakObjectPtr<UMD_EditorDataLoadingFactory>(MutableThis)](
            const UE::Editor::DataStorage::ICompatibilityProvider&, const UObject* Object)
        {
            auto* Factory = WeakThis.Get();
            return !Factory || Factory->FilterRegistration(Object);
        });
}

void UMD_EditorDataLoadingFactory::PostRegister(UE::Editor::DataStorage::ICoreProvider& Storage)
{
    BeginFrameHandle = FCoreDelegates::OnBeginFrame.AddUObject(this, &ThisClass::DrainRegistrations);
}

void UMD_EditorDataLoadingFactory::PreShutdown(UE::Editor::DataStorage::ICoreProvider& Storage)
{
    FCoreDelegates::OnBeginFrame.Remove(BeginFrameHandle);
    Provider = nullptr;
    ReleaseQueue();
}

bool UMD_EditorDataLoadingFactory::FilterRegistration(const UObject* Object)
{
    // Background object loading must retain the engine's own async-registration rules.
    if (!IsInGameThread() || bDraining || CVarEditorDataBatchSize.GetValueOnGameThread() <= 0)
    { return true; }
    const AActor* Actor = Cast<AActor>(Object);
    const UActorComponent* Component = Actor ? nullptr : Cast<UActorComponent>(Object);
    const UWorld* World = Actor ? Actor->GetWorld() : (Component ? Component->GetWorld() : nullptr);
    // Editor world edits and live gameplay changes retain normal metadata visibility.
    // Include deserialized editor map actors before world initialization. Normal
    // editing, newly spawned editor actors and preview worlds remain immediate.
    const auto* Loading = World && World->GetGameInstance()
        ? World->GetGameInstance()->GetSubsystem<UMD_LoadingScreenSubsystem>() : nullptr;
    const bool bPreparingWorld = World && (!World->HasBegunPlay() || (Loading && Loading->IsLoadingScreenActive()));
    const bool bEditorMapLoad = World && World->WorldType == EWorldType::Editor
        && !World->IsInitialized() && Object->HasAnyFlags(RF_WasLoaded);
    if (!World || World->bIsTearingDown
        || !(bEditorMapLoad || (World->WorldType == EWorldType::PIE && bPreparingWorld)))
    { return true; }
    const TWeakObjectPtr<UObject> WeakObject(const_cast<UObject*>(Object));
    if (PendingSet.Contains(WeakObject)) { return false; }
    // Bound metadata memory even for maps beyond the validated production scale.
    // Overflow retains normal engine behavior instead of dropping registrations.
    if (PendingObjects.Num() >= MaximumPendingObjects) { return true; }
    PendingSet.Add(WeakObject);
    PendingObjects.Add(WeakObject);
    return false;
}

void UMD_EditorDataLoadingFactory::DrainRegistrations()
{
    if (!Provider || GetPendingCount() == 0) { return; }
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_EditorData_RegistrationSlice);
    const int32 Requested = CVarEditorDataBatchSize.GetValueOnGameThread();
    const int32 Limit = Requested > 0 ? FMath::Clamp(Requested, 1, 2048) : MaximumPendingObjects;
    TGuardValue<bool> DrainGuard(bDraining, true);
    const double Deadline = FPlatformTime::Seconds() + 0.001;
    int32 Processed = 0;
    while (NextObject < PendingObjects.Num() && Processed++ < Limit)
    {
        const TWeakObjectPtr<UObject> WeakObject = PendingObjects[NextObject++];
        PendingSet.Remove(WeakObject);
        if (UObject* Object = WeakObject.Get())
        {
            const UWorld* World = Object->GetWorld();
            if (World && !World->bIsTearingDown)
            { Provider->AddCompatibleObject(Object); }
        }
        if ((Processed & 31) == 0 && Requested > 0 && FPlatformTime::Seconds() >= Deadline) { break; }
    }
    if (NextObject == PendingObjects.Num()) { ReleaseQueue(); }
}

void UMD_EditorDataLoadingFactory::ReleaseQueue()
{
    PendingObjects.Empty();
    PendingSet.Empty();
    NextObject = 0;
}
