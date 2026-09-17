#pragma once

#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "MD_EditorDataLoadingFactory.generated.h"

/** Spread editor metadata registration during map loading over frames; gameplay objects stay untouched. */
UCLASS()
class UMD_EditorDataLoadingFactory : public UEditorDataStorageFactory
{
    GENERATED_BODY()
public:
    virtual void RegisterRegistrationFilters(UE::Editor::DataStorage::ICompatibilityProvider& Compatibility) const override;
    virtual void PostRegister(UE::Editor::DataStorage::ICoreProvider& Storage) override;
    virtual void PreShutdown(UE::Editor::DataStorage::ICoreProvider& Storage) override;

    int32 GetPendingCount() const { return PendingObjects.Num() - NextObject; }

private:
    bool FilterRegistration(const UObject* Object);
    void DrainRegistrations();
    void ReleaseQueue();

    UE::Editor::DataStorage::ICompatibilityProvider* Provider = nullptr;
    TArray<TWeakObjectPtr<UObject>> PendingObjects;
    TSet<TWeakObjectPtr<UObject>> PendingSet;
    int32 NextObject = 0;
    bool bDraining = false;
    FDelegateHandle BeginFrameHandle;
};
