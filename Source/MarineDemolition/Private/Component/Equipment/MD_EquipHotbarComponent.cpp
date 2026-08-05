// Fill out your copyright notice in the Description page of Project Settings.


#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Net/UnrealNetwork.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "Data/SaveGame/MD_SaveGameSession.h"


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

FMD_EquipHotbarDataWrapper UMD_EquipHotbarComponent::BuildEquipmentSaveData() const
{
	// 기본 값 생성
	FMD_EquipHotbarDataWrapper HotbarDataWrapper;

	// 컴포넌트 소유 액터가 유효하지 않거나 클라이언트 환경 실행 제한
	if (IsValid(GetOwner()) == false || GetOwner()->HasAuthority() == false)
	{
		return HotbarDataWrapper;
	}

	// 슬롯 정보 복사
	for (UMD_ToolDefinition* Tool : Slots)
	{
		HotbarDataWrapper.ToolSlots.Add(IsValid(Tool) == true ? Tool : nullptr);
	}

	return HotbarDataWrapper;
}

bool UMD_EquipHotbarComponent::RestoreEquipmentSaveData(const FMD_EquipHotbarDataWrapper& SaveData)
{
	// 컴포넌트 소유 액터가 유효하지 않거나 클라이언트 환경 실행 제한
	if (IsValid(GetOwner()) == false || GetOwner()->HasAuthority() == false)
	{
		return false;
	}

	// 복구할 데이터가 없으면 함수 조기 종료
	if (SaveData.ToolSlots.Num() <= 0)
	{
		return true;
	}

	// 슬롯 목록 초기화(Local 데이터 무시) 및 크기 설정
	Slots.Reset();
	Slots.SetNum(SaveData.ToolSlots.Num());

	// 저장된 데이터 복사
	for (int32 Index = 0; Index < SaveData.ToolSlots.Num(); ++Index)
	{
		if (SaveData.ToolSlots[Index].IsValid() == false)
		{
			continue;
		}

		Slots[Index] = SaveData.ToolSlots[Index].LoadSynchronous();
	}

	// Listen Server 환경 고려 Slot 정보 변경 이벤트 실행
	OnRep_Slots();

	// 클라이언트에 변경 사항 즉시 전달
	GetOwner()->ForceNetUpdate();

	return true;
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
