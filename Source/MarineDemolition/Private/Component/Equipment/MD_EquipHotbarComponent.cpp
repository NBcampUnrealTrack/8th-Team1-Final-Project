// Fill out your copyright notice in the Description page of Project Settings.


#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Net/UnrealNetwork.h"
#include "Data/Equipment/MD_ToolDefinition.h"


// Sets default values for this component's properties
UMD_EquipHotbarComponent::UMD_EquipHotbarComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UMD_EquipHotbarComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UMD_EquipHotbarComponent, Slots);
	DOREPLIFETIME(UMD_EquipHotbarComponent, CurrentSlotIndex);
}


void UMD_EquipHotbarComponent::BeginPlay()
{
	Super::BeginPlay();

	if (GetOwner()->HasAuthority())
	{
		Slots.SetNum(InitialSlotCount);

		const int32 CopyCount = FMath::Min(DefaultTools.Num(), Slots.Num());
		for (int32 Index = 0; Index < CopyCount; ++Index)
		{
			Slots[Index] = DefaultTools[Index];
		}

		OnHotbarChanged.Broadcast();
	}
}

UMD_ToolDefinition* UMD_EquipHotbarComponent::GetCurrentTool() const
{
	return GetToolAtSlot(CurrentSlotIndex);
}

UMD_ToolDefinition* UMD_EquipHotbarComponent::GetToolAtSlot(int32 SlotIndex) const
{
	if (!Slots.IsValidIndex(SlotIndex))
	{
		return nullptr;
	}

	return Slots[SlotIndex];
}

void UMD_EquipHotbarComponent::SelectSlot(int32 SlotIndex)
{
	if (!Slots.IsValidIndex(SlotIndex) || SlotIndex == CurrentSlotIndex)
	{
		return;
	}

	if (GetOwner()->HasAuthority())
	{
		CurrentSlotIndex = SlotIndex;
		OnRep_CurrentSlotIndex();
		return;
	}

	// 클라이언트에 바로 반영해 입력 지연 감소, 서버에 확정 요청
	CurrentSlotIndex = SlotIndex;
	OnHotbarChanged.Broadcast();
	ServerSelectSlot(SlotIndex);
}

void UMD_EquipHotbarComponent::CycleSlot(int32 Direction)
{
	if (Slots.Num() <= 1 || Direction == 0)
	{
		return;
	}

	const int32 Step = (Direction > 0) ? 1 : -1;
	const int32 NextIndex = (CurrentSlotIndex + Step + Slots.Num()) % Slots.Num();

	SelectSlot(NextIndex);
}

void UMD_EquipHotbarComponent::SetToolAtSlot(int32 SlotIndex, UMD_ToolDefinition* Tool)
{
	if (!GetOwner()->HasAuthority() || !Slots.IsValidIndex(SlotIndex))
	{
		return;
	}

	Slots[SlotIndex] = Tool;
	OnRep_Slots();
}

void UMD_EquipHotbarComponent::OnRep_Slots()
{
	OnHotbarChanged.Broadcast();
}

void UMD_EquipHotbarComponent::OnRep_CurrentSlotIndex()
{
	OnHotbarChanged.Broadcast();
}

void UMD_EquipHotbarComponent::ServerSelectSlot_Implementation(int32 SlotIndex)
{
	if (!Slots.IsValidIndex(SlotIndex))
	{
		return;
	}

	CurrentSlotIndex = SlotIndex;
	OnRep_CurrentSlotIndex();
}
