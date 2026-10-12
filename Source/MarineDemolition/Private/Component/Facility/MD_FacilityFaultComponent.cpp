#include "Component/Facility/MD_FacilityFaultComponent.h"

#include "Component/Facility/MD_FacilityFaultManagerComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

const FName UMD_FacilityFaultComponent::FaultExemptActorTag(TEXT("MD.Facility.FaultExempt"));

bool UMD_FacilityFaultComponent::IsFaultExempt() const
{
	return IsValid(GetOwner()) && GetOwner()->ActorHasTag(FaultExemptActorTag);
}

UMD_FacilityFaultComponent::UMD_FacilityFaultComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UMD_FacilityFaultComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMD_FacilityFaultComponent, Snapshot);
}

bool UMD_FacilityFaultComponent::HasServerAuthority() const
{
	return IsValid(GetOwner()) && GetOwner()->HasAuthority() && GetWorld()
		&& GetWorld()->IsGameWorld() && GetWorld()->GetNetMode() != NM_Client;
}

double UMD_FacilityFaultComponent::GetServerTime() const
{
	const UWorld* World = GetWorld();
	if (!World) return 0.0;
	const AGameStateBase* GS = World->GetGameState();
	return GS ? GS->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

void UMD_FacilityFaultComponent::BeginPlay()
{
	Super::BeginPlay();
	if (HasServerAuthority())
	{
		RegisterWithManager();
		if (bStartBroken) TryBreak();
	}
}

void UMD_FacilityFaultComponent::RegisterWithManager()
{
	if (!HasServerAuthority() || Manager.IsValid()) return;
	AGameStateBase* GS = GetWorld()->GetGameState();
	UMD_FacilityFaultManagerComponent* Found = GS ? GS->FindComponentByClass<UMD_FacilityFaultManagerComponent>() : nullptr;
	if (Found && Found->RegisterFacility(this))
	{
		Manager = Found;
		GetWorld()->GetTimerManager().ClearTimer(RegistrationTimer);
	}
	else if (!GetWorld()->GetTimerManager().IsTimerActive(RegistrationTimer))
	{
		GetWorld()->GetTimerManager().SetTimer(RegistrationTimer, this,
			&UMD_FacilityFaultComponent::RegisterWithManager, 0.5f, true);
	}
}

void UMD_FacilityFaultComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(RegistrationTimer);
	if (Manager.IsValid()) Manager->UnregisterFacility(this);
	Manager.Reset();
	Super::EndPlay(Reason);
}

bool UMD_FacilityFaultComponent::CanReceiveRandomFault() const
{
	return HasServerAuthority() && !IsFaultExempt() && bRandomFaultEligible && !Snapshot.IsFaultActive()
		&& FMath::IsFinite(RefaultGraceSeconds) && RefaultGraceSeconds >= 0.f
		&& (Snapshot.State != EMD_FacilityState::Repaired
			|| GetServerTime() - Snapshot.RepairedAt >= RefaultGraceSeconds);
}

bool UMD_FacilityFaultComponent::TryBreak()
{
	return TryBreakInternal(false);
}

bool UMD_FacilityFaultComponent::TryBreakInternal(bool bSkipDeviceCondition)
{
	if (!HasServerAuthority() || IsFaultExempt() || Snapshot.IsFaultActive()
		|| (!bSkipDeviceCondition && !CanStartFault())) return false;
	const FMD_FacilitySnapshot Previous = Snapshot;
	Snapshot = FMD_FacilitySnapshot();
	Snapshot.FaultId = Previous.FaultId == MAX_int32 ? 1 : Previous.FaultId + 1;
	Snapshot.State = EMD_FacilityState::Broken;
	Snapshot.FaultStartedAt = GetServerTime();
	PublishChange(Previous);
	return true;
}

bool UMD_FacilityFaultComponent::BeginRepair(AActor* Repairer, int32 ExpectedFaultId)
{
	if (!HasServerAuthority() || !IsValid(Repairer) || Repairer->GetWorld() != GetWorld()
		|| !Snapshot.IsFaultActive() || Snapshot.FaultId != ExpectedFaultId) return false;
	if (Snapshot.State == EMD_FacilityState::Repairing && IsValid(Snapshot.Repairer)) return false;
	const FMD_FacilitySnapshot Previous = Snapshot;
	Snapshot.State = EMD_FacilityState::Repairing;
	Snapshot.Repairer = Repairer;
	PublishChange(Previous);
	return true;
}

bool UMD_FacilityFaultComponent::AddRepairProgress(AActor* Repairer, int32 ExpectedFaultId, float ProgressDelta)
{
	if (!HasServerAuthority() || !IsValid(Repairer) || Snapshot.State != EMD_FacilityState::Repairing
		|| Snapshot.FaultId != ExpectedFaultId || Snapshot.Repairer != Repairer
		|| !FMath::IsFinite(ProgressDelta) || ProgressDelta <= 0.f) return false;
	const FMD_FacilitySnapshot Previous = Snapshot;
	// Min before adding prevents overflow from otherwise finite untrusted work amounts.
	Snapshot.RepairProgress = FMath::Min(1.f, Snapshot.RepairProgress + FMath::Min(ProgressDelta, 1.f));
	if (Snapshot.RepairProgress >= 1.f)
	{
		Snapshot.State = EMD_FacilityState::Repaired;
		Snapshot.RepairedAt = GetServerTime();
		Snapshot.Repairer = nullptr;
	}
	PublishChange(Previous);
	return true;
}

bool UMD_FacilityFaultComponent::CancelRepair(AActor* Repairer, int32 ExpectedFaultId)
{
	if (!HasServerAuthority() || Snapshot.State != EMD_FacilityState::Repairing
		|| Snapshot.FaultId != ExpectedFaultId || Snapshot.Repairer != Repairer) return false;
	const FMD_FacilitySnapshot Previous = Snapshot;
	Snapshot.State = EMD_FacilityState::Broken;
	Snapshot.Repairer = nullptr;
	if (bResetProgressOnCancel) Snapshot.RepairProgress = 0.f;
	PublishChange(Previous);
	return true;
}

void UMD_FacilityFaultComponent::PublishChange(const FMD_FacilitySnapshot& Previous)
{
	GetOwner()->FlushNetDormancy();
	GetOwner()->ForceNetUpdate();
	OnRep_Snapshot(Previous); // Listen server receives the same presentation event as clients.
}

bool UMD_FacilityFaultComponent::CompleteRepairFromValidatedSource(int32 ExpectedFaultId)
{
	if (!HasServerAuthority() || !Snapshot.IsFaultActive() || Snapshot.FaultId != ExpectedFaultId) return false;
	const FMD_FacilitySnapshot Previous = Snapshot;
	Snapshot.State = EMD_FacilityState::Repaired;
	Snapshot.RepairProgress = 1.f;
	Snapshot.RepairedAt = GetServerTime();
	Snapshot.Repairer = nullptr;
	PublishChange(Previous);
	return true;
}

void UMD_FacilityFaultComponent::OnRep_Snapshot(const FMD_FacilitySnapshot& Previous)
{
	// Copy before broadcasting so a listener's subsequent transition cannot change this event.
	const FMD_FacilitySnapshot Current = Snapshot;
	OnStateChanged.Broadcast(this, Previous, Current);
}
