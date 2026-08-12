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

	DOREPLIFETIME(UMD_EquipHotbarComponent, ToolEntries);
	DOREPLIFETIME(UMD_EquipHotbarComponent, CurrentSlotIndex);
}


void UMD_EquipHotbarComponent::BeginPlay()
{
	Super::BeginPlay();

	if (IsValid(GetOwner()) == true && GetOwner()->HasAuthority() == true)
	{
		// 설정 슬롯 수와 기본 도구 수 중 큰 값으로 런타임 고정 항목 배열을 구성하는 처리
		ToolEntries.SetNum(ResolveSlotCount());

		// 기본 도구가 고정 항목 배열 범위를 벗어나지 않도록 제한하는 복사 수
		const int32 CopyCount = FMath::Min(DefaultTools.Num(), ToolEntries.Num());
		for (int32 Index = 0; Index < CopyCount; ++Index)
		{
			ToolEntries[Index].ToolDefinition = DefaultTools[Index];
			ToolEntries[Index].Count = IsValid(DefaultTools[Index]) == true ? 1 : 0;
		}

		OnHotbarChanged.Broadcast();
	}
}

UMD_ToolDefinition* UMD_EquipHotbarComponent::GetCurrentTool() const
{
	return GetToolAtSlot(CurrentSlotIndex);
}

int32 UMD_EquipHotbarComponent::ResolveSlotCount() const
{
	return FMath::Max(ConfiguredSlotCount, DefaultTools.Num());
}

UMD_ToolDefinition* UMD_EquipHotbarComponent::GetToolAtSlot(int32 SlotIndex) const
{
	if (ToolEntries.IsValidIndex(SlotIndex) == false)
	{
		return nullptr;
	}

	return ToolEntries[SlotIndex].ToolDefinition;
}

int32 UMD_EquipHotbarComponent::GetToolCount(UMD_ToolDefinition* ToolDefinition) const
{
	// 고정 항목 배열에서 동일 ToolDefinition의 수량을 조회하는 처리
	const FMD_HotbarToolEntry* SameToolEntry = ToolEntries.FindByPredicate([ToolDefinition](const FMD_HotbarToolEntry& Entry) 
		{ 
			return Entry.ToolDefinition == ToolDefinition;
		});
	return SameToolEntry != nullptr ? SameToolEntry->Count : 0;
}

bool UMD_EquipHotbarComponent::FindAvailableToolEntry(UMD_ToolDefinition* ToolDefinition, int32& OutEntryIndex) const
{
	OutEntryIndex = INDEX_NONE;
	if (IsValid(ToolDefinition) == false || ToolDefinition->ToolAbilityTag.IsValid()== false)
	{
		return false;
	}

	int32 SameToolEntryIndex = INDEX_NONE;
	int32 EmptyEntryIndex = INDEX_NONE;
	for (int32 EntryIndex = 0; EntryIndex < ToolEntries.Num(); ++EntryIndex)
	{
		const UMD_ToolDefinition* EntryToolDefinition = ToolEntries[EntryIndex].ToolDefinition;
		if (IsValid(EntryToolDefinition) == false || ToolEntries[EntryIndex].Count <= 0)
		{
			if (EmptyEntryIndex == INDEX_NONE)
			{
				EmptyEntryIndex = EntryIndex;
			}
			continue;
		}

		if (EntryToolDefinition->ToolAbilityTag.MatchesTagExact(ToolDefinition->ToolAbilityTag) == true)
		{
			// 동일한 아이템이 여러 슬롯에 존재하는 잘못된 상태에서 임의 항목을 변경하지 않기 위한 검사
			if (SameToolEntryIndex != INDEX_NONE)
			{
				return false;
			}

			SameToolEntryIndex = EntryIndex;
			continue;
		}
	}

	// 동일 정의 수량 증가, 동일 태그 교체, 빈 항목 추가 순서로 반영 위치를 선택하는 처리
	OutEntryIndex = ToolEntries.IsValidIndex(SameToolEntryIndex) == true ? SameToolEntryIndex : EmptyEntryIndex;
	return ToolEntries.IsValidIndex(OutEntryIndex) == true;
}

bool UMD_EquipHotbarComponent::CanAddTool(UMD_ToolDefinition* ToolDefinition) const
{
	if (IsValid(ToolDefinition) == false)
	{
		return false;
	}

	int32 AvailableEntryIndex = INDEX_NONE;
	return FindAvailableToolEntry(ToolDefinition, AvailableEntryIndex);
}

bool UMD_EquipHotbarComponent::AddTool(UMD_ToolDefinition* ToolDefinition, int32 Amount)
{
	// 클라이언트 환경 실행 제한
	AActor* OwnerActor = GetOwner();
	if (IsValid(OwnerActor) == false || OwnerActor->HasAuthority() == false)
	{
		return false;
	}

	// 잘못된 ToolDefinition 또는 수량으로 인한 지급 방지
	if (IsValid(ToolDefinition) == false || Amount <= 0)
	{
		return false;
	}

	// 아이템을 추가할 수 있는 Slot의 인덱스 찾기
	int32 AvailableEntryIndex = INDEX_NONE;
	bool bIsAvailable = FindAvailableToolEntry(ToolDefinition, AvailableEntryIndex);

	if (bIsAvailable == false)
	{
		return false;
	}

	// 동일 정의 항목의 현재 수량을 정수 범위 검사에 사용하기 위한 값
	const int32 CurrentCount = ToolEntries[AvailableEntryIndex].Count;

	// 비정상적으로 큰 반복 구매로 int32 수량이 순환하는 상황 방지
	if (CurrentCount > MAX_int32 - Amount)
	{
		return false;
	}

	// ToolDefinition과 수량을 동일 고정 항목에 원자적으로 반영하는 처리
	ToolEntries[AvailableEntryIndex].ToolDefinition = ToolDefinition;
	ToolEntries[AvailableEntryIndex].Count = CurrentCount + Amount;
	OnRep_ToolEntries();
	OwnerActor->ForceNetUpdate();
	return true;
}

bool UMD_EquipHotbarComponent::ReplaceTool(UMD_ToolDefinition* Tool)
{
	// 클라이언트 환경 실행 제한
	AActor* OwnerActor = GetOwner();
	if (IsValid(OwnerActor) == false || OwnerActor->HasAuthority() == false)
	{
		return false;
	}

	// 유효하지 않은 장비 지급을 방지하기 위한 검사
	if (IsValid(Tool) == false || Tool->ToolAbilityTag.IsValid() == false)
	{
		return false;
	}

	// 아이템 교체 또는 추가할 수 있는 인덱스 위치 찾기
	int32 AvailableEntryIndex = INDEX_NONE;
	bool bIsAvailable = FindAvailableToolEntry(Tool, AvailableEntryIndex);

	// 가능한 인덱스가 존재하지 않거나, 동일한 아이템으로 교체 시도 시 실패 반환
	if (bIsAvailable == false || ToolEntries[AvailableEntryIndex].ToolDefinition == Tool)
	{
		return false;
	}

	// 새로운 ToolDefinition 할당
	ToolEntries[AvailableEntryIndex].ToolDefinition = Tool;
	ToolEntries[AvailableEntryIndex].Count = 1;
	OnRep_ToolEntries();
	OwnerActor->ForceNetUpdate();
	return true;
}

bool UMD_EquipHotbarComponent::TryConsumeTool(UMD_ToolDefinition* ToolDefinition, int32 Amount)
{
	// 클라이언트 환경 실행 제한
	AActor* OwnerActor = GetOwner();
	if (IsValid(OwnerActor) == false || OwnerActor->HasAuthority() == false)
	{
		return false;
	}

	// 사용하려는 아이템 및 수량 유효성 확인
	if (IsValid(ToolDefinition) == false || Amount <= 0)
	{
		return false;
	}

	// 차감할 ToolDefinition의 고정 항목 위치 조회
	int32 ToolEntryIndex = ToolEntries.IndexOfByPredicate([ToolDefinition](const FMD_HotbarToolEntry& Entry)
		{
			bool bIsMatch = Entry.ToolDefinition->ToolAbilityTag.MatchesTagExact(ToolDefinition->ToolAbilityTag);
			bIsMatch = bIsMatch && Entry.ToolDefinition == ToolDefinition;
			return bIsMatch;
		});

	if (ToolEntries.IsValidIndex(ToolEntryIndex) == false || ToolEntries[ToolEntryIndex].Count < Amount)
	{
		return false;
	}

	ToolEntries[ToolEntryIndex].Count -= Amount;
	if (ToolEntries[ToolEntryIndex].Count == 0)
	{
		// 배열 길이를 유지하면서 소진된 위치를 재사용할 수 있도록 항목만 초기화하는 처리
		ToolEntries[ToolEntryIndex].ToolDefinition = nullptr;
	}

	OnRep_ToolEntries();
	OwnerActor->ForceNetUpdate();
	return true;
}

void UMD_EquipHotbarComponent::SelectSlot(int32 SlotIndex)
{
	if (ToolEntries.IsValidIndex(SlotIndex) == false || SlotIndex == CurrentSlotIndex)
	{
		return;
	}

	if (IsValid(GetOwner()) == true && GetOwner()->HasAuthority() == true)
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
	if (ToolEntries.Num() <= 1 || Direction == 0)
	{
		return;
	}

	// 입력 방향을 한 칸 이동 단위로 정규화한 값
	const int32 Step = (Direction > 0) ? 1 : -1;
	// 배열 양 끝에서 순환하도록 계산한 다음 선택 인덱스
	const int32 NextIndex = (CurrentSlotIndex + Step + ToolEntries.Num()) % ToolEntries.Num();

	SelectSlot(NextIndex);
}

void UMD_EquipHotbarComponent::SetToolAtSlot(int32 SlotIndex, UMD_ToolDefinition* Tool)
{
	if (IsValid(GetOwner()) == false || GetOwner()->HasAuthority() == false || ToolEntries.IsValidIndex(SlotIndex) == false)
	{
		return;
	}

	// 별도 도구 배열 없이 지정 고정 항목에 ToolDefinition과 기본 수량을 함께 설정하는 처리
	ToolEntries[SlotIndex].ToolDefinition = Tool;
	ToolEntries[SlotIndex].Count = IsValid(Tool) == true ? 1 : 0;
	OnRep_ToolEntries();
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

	// 고정 항목 순서를 보존하면서 ToolDefinition만 기존 저장 형식으로 복사하는 처리
	for (const FMD_HotbarToolEntry& ToolEntry : ToolEntries)
	{
		OutEquipmentList.Add(IsValid(ToolEntry.ToolDefinition) == true ? ToolEntry.ToolDefinition.Get() : nullptr);
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

	// 저장 데이터 길이와 무관하게 에디터에서 결정한 고정 항목 수를 유지하는 초기화
	ToolEntries.Reset();
	ToolEntries.SetNum(ResolveSlotCount());

	// 저장 데이터가 고정 항목 수를 넘지 않도록 제한하는 복사 수
	const int32 CopyCount = FMath::Min(SavedToolSlots.Num(), ToolEntries.Num());
	for (int32 Index = 0; Index < CopyCount; ++Index)
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

		ToolEntries[Index].ToolDefinition = Tool;
		ToolEntries[Index].Count = 1;
	}

	// Listen Server 환경에서도 통합 항목 변경을 UI에 즉시 전달하는 처리
	OnRep_ToolEntries();

	// 클라이언트에 변경 사항 즉시 전달
	GetOwner()->ForceNetUpdate();

	return true;
}

/** ToolEntries 복제 완료 후 Hotbar UI 갱신을 요청하는 함수 */
void UMD_EquipHotbarComponent::OnRep_ToolEntries()
{
	OnHotbarChanged.Broadcast();
}

void UMD_EquipHotbarComponent::OnRep_CurrentSlotIndex()
{
	OnHotbarChanged.Broadcast();
}

void UMD_EquipHotbarComponent::ServerSelectSlot_Implementation(int32 SlotIndex)
{
	if (ToolEntries.IsValidIndex(SlotIndex) == false)
	{
		return;
	}

	CurrentSlotIndex = SlotIndex;
	OnRep_CurrentSlotIndex();
}
