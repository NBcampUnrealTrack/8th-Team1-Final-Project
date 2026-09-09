#include "World/Contamination/MD_ContaminationItem.h"

#include "GameplayTags/MD_GameplayTags.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Gameplay/Scrape/MD_ContaminationDebrisSubsystem.h"

AMD_ContaminationItem::AMD_ContaminationItem()
{
	StorageContentTag = MDGameplayTags::Resource_Storage_Content_Contamination;
}

void AMD_ContaminationItem::BeginPlay()
{
    if (!PhysicalItemId.IsValid()) PhysicalItemId = FGuid::NewGuid();
    if (HasAuthority() && GetWorld())
        if (auto* Budget = GetWorld()->GetSubsystem<UMD_ContaminationDebrisSubsystem>()) Budget->RegisterItem(this);
    Super::BeginPlay();
}

void AMD_ContaminationItem::EndPlay(const EEndPlayReason::Type Reason)
{
    if (HasAuthority() && GetWorld())
        if (auto* Budget = GetWorld()->GetSubsystem<UMD_ContaminationDebrisSubsystem>()) Budget->UnregisterItem(this);
    Super::EndPlay(Reason);
}

void AMD_ContaminationItem::InitializeRecoveryRecord(FMD_ContaminationPendingDrop&& Record)
{
    check(Record.bSinglePhysicalItem && Record.IsStructurallyValid());
    PhysicalItemId = Record.BatchId;
    RecoveryRecord = MoveTemp(Record);
    bHasRecoveryRecord = true;
}

bool AMD_ContaminationItem::CaptureRecoveryRecord(FMD_ContaminationPendingDrop& Out) const
{
    if (!HasAuthority() || IsActorBeingDestroyed()) return false;
    Out = bHasRecoveryRecord ? RecoveryRecord : FMD_ContaminationPendingDrop();
    Out.BatchId = PhysicalItemId;
    if (!Out.BatchId.IsValid()) return false;
    if (Out.SourceActorName.IsNone() && Out.SourceComponentName.IsNone()) Out.SourceActorName = GetFName();
    Out.ItemClass = GetClass();
    Out.SpawnTransform = GetActorTransform();
    Out.bSinglePhysicalItem = true;
    Out.bSpawnConfigurationReady = true;
    if (Mesh && Mesh->IsSimulatingPhysics())
    {
        Out.LinearVelocity = Mesh->GetPhysicsLinearVelocity();
        Out.AngularVelocityRadians = Mesh->GetPhysicsAngularVelocityInRadians();
    }
    return Out.IsStructurallyValid();
}

void AMD_ContaminationItem::RestoreSavedVelocity()
{
    if (bHasRecoveryRecord && Mesh && Mesh->IsSimulatingPhysics())
    {
        Mesh->SetPhysicsLinearVelocity(RecoveryRecord.LinearVelocity);
        Mesh->SetPhysicsAngularVelocityInRadians(RecoveryRecord.AngularVelocityRadians);
    }
}
