#include "Component/Facility/MD_FacilityFaultManagerComponent.h"

#include "Engine/World.h"
#include "TimerManager.h"

UMD_FacilityFaultManagerComponent::UMD_FacilityFaultManagerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UMD_FacilityFaultManagerComponent::HasServerAuthority() const
{
	return IsValid(GetOwner()) && GetOwner()->HasAuthority() && GetWorld()
		&& GetWorld()->IsGameWorld() && GetWorld()->GetNetMode() != NM_Client;
}

void UMD_FacilityFaultManagerComponent::BeginPlay()
{
	Super::BeginPlay();
	Random.Initialize(RandomSeed != 0 ? RandomSeed : FMath::Rand());
	StartFaultSchedules();
}

void UMD_FacilityFaultManagerComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	StopFaultSchedules();
	for (const auto& Weak : Facilities)
	{
		if (UMD_FacilityFaultComponent* Facility = Weak.Get())
			Facility->OnStateChanged.RemoveDynamic(this, &UMD_FacilityFaultManagerComponent::HandleFacilityStateChanged);
	}
	Facilities.Empty();
	Super::EndPlay(Reason);
}

bool UMD_FacilityFaultManagerComponent::RegisterFacility(UMD_FacilityFaultComponent* Facility)
{
	if (!HasServerAuthority() || !IsValid(Facility) || Facility->GetWorld() != GetWorld()
		|| !IsValid(Facility->GetOwner()) || !Facility->GetOwner()->HasAuthority()) return false;
	Facilities.RemoveAll([](const auto& Weak) { return !Weak.IsValid(); });
	Facilities.AddUnique(Facility);
	Facility->OnStateChanged.AddUniqueDynamic(this, &UMD_FacilityFaultManagerComponent::HandleFacilityStateChanged);
	return true;
}

void UMD_FacilityFaultManagerComponent::UnregisterFacility(UMD_FacilityFaultComponent* Facility)
{
	if (!HasServerAuthority()) return;
	if (IsValid(Facility))
		Facility->OnStateChanged.RemoveDynamic(this, &UMD_FacilityFaultManagerComponent::HandleFacilityStateChanged);
	Facilities.RemoveAll([Facility](const auto& Weak) { return !Weak.IsValid() || Weak.Get() == Facility; });
}

TArray<UMD_FacilityFaultComponent*> UMD_FacilityFaultManagerComponent::GetRegisteredFacilities() const
{
	TArray<UMD_FacilityFaultComponent*> Result;
	for (const auto& Weak : Facilities)
		if (UMD_FacilityFaultComponent* Facility = Weak.Get()) Result.Add(Facility);
	return Result;
}

TArray<UMD_FacilityFaultComponent*> UMD_FacilityFaultManagerComponent::GetActiveFaults() const
{
	TArray<UMD_FacilityFaultComponent*> Result;
	for (UMD_FacilityFaultComponent* Facility : GetRegisteredFacilities())
		if (Facility->IsFaultActive()) Result.Add(Facility);
	return Result;
}

void UMD_FacilityFaultManagerComponent::HandleFacilityStateChanged(UMD_FacilityFaultComponent* Facility,
	const FMD_FacilitySnapshot& Previous, const FMD_FacilitySnapshot& Current)
{
	OnFacilityStateChanged.Broadcast(Facility, Previous, Current);
}

void UMD_FacilityFaultManagerComponent::StartFaultSchedules()
{
	if (!HasServerAuthority()) return;
	StopFaultSchedules();
	TSet<FName> UsedIds;
	for (const FMD_FacilityFaultSchedule& Schedule : Schedules)
	{
		if (!Schedule.bEnabled) continue;
		if (Schedule.ScheduleId.IsNone() || UsedIds.Contains(Schedule.ScheduleId) || !Schedule.HasValidInterval())
		{
			UE_LOG(LogTemp, Warning, TEXT("[MD_Facility] Ignoring invalid/duplicate fault schedule: %s"), *Schedule.ScheduleId.ToString());
			continue;
		}
		UsedIds.Add(Schedule.ScheduleId);
		RunningSchedules.Add(Schedule);
	}
	ScheduleTimers.SetNum(RunningSchedules.Num());
	for (int32 Index = 0; Index < RunningSchedules.Num(); ++Index) ArmSchedule(Index);
}

void UMD_FacilityFaultManagerComponent::StopFaultSchedules()
{
	if (!HasServerAuthority()) return;
	ScheduleGeneration = ScheduleGeneration == MAX_int32 ? 1 : ScheduleGeneration + 1;
	for (FTimerHandle& Timer : ScheduleTimers) GetWorld()->GetTimerManager().ClearTimer(Timer);
	ScheduleTimers.Empty();
	RunningSchedules.Empty();
	// Stopping new occurrences never repairs existing incidents or resets their work.
}

void UMD_FacilityFaultManagerComponent::ArmSchedule(int32 Index)
{
	if (!RunningSchedules.IsValidIndex(Index)) return;
	const FMD_FacilityFaultSchedule& Schedule = RunningSchedules[Index];
	const float Delay = Random.FRandRange(Schedule.MinIntervalSeconds, Schedule.MaxIntervalSeconds);
	GetWorld()->GetTimerManager().SetTimer(ScheduleTimers[Index],
		FTimerDelegate::CreateUObject(this, &UMD_FacilityFaultManagerComponent::Occur, Index, ScheduleGeneration), Delay, false);
}

void UMD_FacilityFaultManagerComponent::Occur(int32 Index, int32 Generation)
{
	if (!HasServerAuthority() || Generation != ScheduleGeneration || !RunningSchedules.IsValidIndex(Index)) return;
	// Copy: event listeners may stop/reconfigure schedules during the fault transition.
	const FMD_FacilityFaultSchedule Schedule = RunningSchedules[Index];
	TriggerRandomFault(Schedule.Kind, Schedule.Group);
	if (Generation == ScheduleGeneration) ArmSchedule(Index);
}

UMD_FacilityFaultComponent* UMD_FacilityFaultManagerComponent::TriggerRandomFault(EMD_FacilityKind Kind, FName Group)
{
	if (!HasServerAuthority()) return nullptr;
	TArray<UMD_FacilityFaultComponent*> Candidates;
	for (UMD_FacilityFaultComponent* Facility : GetRegisteredFacilities())
	{
		if (Facility->Kind == Kind && (Group.IsNone() || Facility->Group == Group) && Facility->CanReceiveRandomFault())
			Candidates.Add(Facility);
	}
	if (Candidates.IsEmpty()) return nullptr;
	UMD_FacilityFaultComponent* Chosen = Candidates[Random.RandRange(0, Candidates.Num() - 1)];
	return Chosen->TryBreak() ? Chosen : nullptr;
}
