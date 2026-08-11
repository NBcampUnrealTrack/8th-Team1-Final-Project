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
	DOREPLIFETIME(UMD_EquipHotbarComponent, ShopItemEntries);
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

/** 별도 가방 없이 핫바가 보유한 상점 품목 수량을 반환하는 조회 함수 */
int32 UMD_EquipHotbarComponent::GetShopItemCount(FName ItemId) const
{
	// 소규모 카탈로그 배열을 순회하여 복제 불가능한 TMap 없이 품목 수량을 조회하는 처리
	const FMD_HotbarShopItemEntry* ItemEntry = ShopItemEntries.FindByPredicate([ItemId](const FMD_HotbarShopItemEntry& Entry) { return Entry.ItemId == ItemId; });
	return ItemEntry != nullptr ? ItemEntry->Count : 0;
}

/** 서버에서 검증을 마친 상점 품목을 플레이어 핫바 원장에 지급하는 함수 */
bool UMD_EquipHotbarComponent::AddShopItem(FName ItemId, int32 Amount)
{
	AActor* OwnerActor = GetOwner();

	// 클라이언트 임의 지급 방지
	if (IsValid(OwnerActor) == false || OwnerActor->HasAuthority() == false)
	{
		return false;
	}

	// 잘못된 식별자 또는 수량으로 인한 지급 방지
	if (ItemId.IsNone() == true || Amount <= 0)
	{
		return false;
	}

	const int32 CurrentCount = GetShopItemCount(ItemId);

	// 비정상적으로 큰 반복 구매로 int32 수량이 순환하는 상황 방지
	if (CurrentCount > MAX_int32 - Amount)
	{
		return false;
	}

	FMD_HotbarShopItemEntry* ItemEntry = ShopItemEntries.FindByPredicate([ItemId](const FMD_HotbarShopItemEntry& Entry) 
		{ 
			return Entry.ItemId == ItemId; 
		});

	// Hotbar에 아이템 추가
	if (ItemEntry == nullptr)
	{
		FMD_HotbarShopItemEntry& NewEntry = ShopItemEntries.AddDefaulted_GetRef();
		NewEntry.ItemId = ItemId;
		NewEntry.Count = Amount;
	}
	else
	{
		ItemEntry->Count = CurrentCount + Amount;
	}

	OnRep_ShopItemEntries();
	OwnerActor->ForceNetUpdate();
	return true;
}

/** 서버에서 GameplayTag로 장비 계열 슬롯을 찾아 업그레이드를 원자적으로 반영하는 함수 */
bool UMD_EquipHotbarComponent::ReplaceToolByGameplayTag(const FGameplayTag& TargetToolTag, UMD_ToolDefinition* Tool)
{
	AActor* OwnerActor = GetOwner();

	// 클라이언트의 임의 장비 변경과 유효하지 않은 태그 또는 장비 교체를 방지하기 위한 검사
	if (IsValid(OwnerActor) == false || OwnerActor->HasAuthority() == false || TargetToolTag.IsValid() == false || IsValid(Tool) == false)
	{
		return false;
	}

	int32 MatchingSlotIndex = INDEX_NONE;
	for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
	{
		const UMD_ToolDefinition* SlotTool = Slots[SlotIndex];
		if (IsValid(SlotTool) == false || SlotTool->ToolAbilityTag.MatchesTagExact(TargetToolTag) == false)
		{
			continue;
		}

		// 동일 태그 슬롯이 여러 개인 잘못된 로드아웃에서 임의 슬롯 교체를 방지하기 위한 검사
		if (MatchingSlotIndex != INDEX_NONE)
		{
			return false;
		}

		MatchingSlotIndex = SlotIndex;
	}

	// 교체할 장비 계열이 현재 핫바에 없는 경우 결제를 막기 위한 검사
	if (Slots.IsValidIndex(MatchingSlotIndex) == false)
	{
		return false;
	}

	// 동일 장비를 다시 지급하여 결제만 발생하는 상황을 방지하기 위한 검사
	if (Slots[MatchingSlotIndex] == Tool)
	{
		return false;
	}

	// 슬롯 배열 순서와 무관하게 태그로 찾은 기존 장비를 업그레이드 장비로 교체하는 처리
	Slots[MatchingSlotIndex] = Tool;
	OnRep_Slots();
	OwnerActor->ForceNetUpdate();
	return true;
}

/** 실제 사용이 승인된 소모품을 플레이어 핫바 원장에서 차감하는 함수 */
bool UMD_EquipHotbarComponent::TryConsumeShopItem(FName ItemId, int32 Amount)
{
	AActor* OwnerActor = GetOwner();

	// 서버 권위와 유효한 소비 요청만 허용하기 위한 검사
	if (IsValid(OwnerActor) == false || OwnerActor->HasAuthority() == false || ItemId.IsNone() == true || Amount <= 0)
	{
		return false;
	}

	const int32 ItemIndex = ShopItemEntries.IndexOfByPredicate([ItemId](const FMD_HotbarShopItemEntry& Entry) 
		{ 
			return Entry.ItemId == ItemId; 
		});

	if (ShopItemEntries.IsValidIndex(ItemIndex) == false || ShopItemEntries[ItemIndex].Count < Amount)
	{
		return false;
	}

	ShopItemEntries[ItemIndex].Count -= Amount;
	if (ShopItemEntries[ItemIndex].Count == 0)
	{
		ShopItemEntries.RemoveAt(ItemIndex);
	}

	OnRep_ShopItemEntries();
	OwnerActor->ForceNetUpdate();
	return true;
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

TArray<TSoftObjectPtr<UDataAsset>> UMD_EquipHotbarComponent::BuildEquipmentSaveData() const
{
	// 기본 값 생성
	TArray<TSoftObjectPtr<UDataAsset>> OutEquipmentList;

	// 컴포넌트 소유 액터가 유효하지 않거나 클라이언트 환경 실행 제한
	if (IsValid(GetOwner()) == false || GetOwner()->HasAuthority() == false)
	{
		return OutEquipmentList;
	}

	// 슬롯 정보 복사
	for (UMD_ToolDefinition* Tool : Slots)
	{
		OutEquipmentList.Add(IsValid(Tool) == true ? Tool : nullptr);
	}

	return OutEquipmentList;
}

bool UMD_EquipHotbarComponent::RestoreEquipmentSaveData(const TArray<TSoftObjectPtr<UDataAsset>>& SavedToolSlots)
{
	// 컴포넌트 소유 액터가 유효하지 않거나 클라이언트 환경 실행 제한
	if (IsValid(GetOwner()) == false || GetOwner()->HasAuthority() == false)
	{
		return false;
	}

	// 복구할 데이터가 없으면 함수 조기 종료
	if (SavedToolSlots.Num() <= 0)
	{
		return true;
	}

	// 슬롯 목록 초기화(Local 데이터 무시) 및 크기 설정
	Slots.Reset();
	Slots.SetNum(SavedToolSlots.Num());

	// 저장된 데이터 복사
	for (int32 Index = 0; Index < SavedToolSlots.Num(); ++Index)
	{
		if (SavedToolSlots[Index].IsNull() == true)
		{
			continue;
		}

		UDataAsset* DataAsset = SavedToolSlots[Index].LoadSynchronous();
		UMD_ToolDefinition* Tool = Cast<UMD_ToolDefinition>(DataAsset);
		if (IsValid(Tool) == false)
		{
			continue;
		}

		Slots[Index] = Tool;
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

void UMD_EquipHotbarComponent::OnRep_ShopItemEntries()
{
	// 슬롯과 구매 품목 수량을 하나의 핫바 갱신 경로로 전달하기 위한 알림
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
