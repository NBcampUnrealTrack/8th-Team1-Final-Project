#include "Hotbar/MD_HotbarComponent.h"

#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

UMD_HotbarComponent::UMD_HotbarComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UMD_HotbarComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !Owner->HasAuthority()) return;

	MaxSlots = FMath::Max(1, MaxSlots);
	Slots.SetNum(MaxSlots);
}

void UMD_HotbarComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(UMD_HotbarComponent, Slots, COND_OwnerOnly);
}

bool UMD_HotbarComponent::TryAddItem(
	TSubclassOf<AActor> ItemClass,
	int32& OutSlotIndex)
{
	OutSlotIndex = INDEX_NONE;

	AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !Owner->HasAuthority() || !ItemClass) return false;

	for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
	{
		FMD_HotbarSlot& Slot = Slots[SlotIndex];
		if (!Slot.IsEmpty()) continue;

		Slot.ItemClass = ItemClass;
		OutSlotIndex = SlotIndex;
		NotifyHotbarChanged();
		Owner->ForceNetUpdate();
		return true;
	}

	return false;
}

bool UMD_HotbarComponent::TryRemoveItemAt(int32 SlotIndex)
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner)
		|| !Owner->HasAuthority()
		|| !Slots.IsValidIndex(SlotIndex)
		|| Slots[SlotIndex].IsEmpty()) return false;

	Slots[SlotIndex].ItemClass = nullptr;
	NotifyHotbarChanged();
	Owner->ForceNetUpdate();
	return true;
}

TSubclassOf<AActor> UMD_HotbarComponent::GetItemClassAt(int32 SlotIndex) const
{
	return Slots.IsValidIndex(SlotIndex) ? Slots[SlotIndex].ItemClass : nullptr;
}

void UMD_HotbarComponent::OnRep_Slots()
{
	NotifyHotbarChanged();
}

void UMD_HotbarComponent::NotifyHotbarChanged()
{
	OnHotbarChanged.Broadcast();
}
