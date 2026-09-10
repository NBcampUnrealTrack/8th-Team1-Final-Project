// Fill out your copyright notice in the Description page of Project Settings.


#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Data/Equipment/MD_ItemDefinition.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "Data/Equipment/MD_ConsumeItemDefinition.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"
#include "GameplayEffectComponents/AbilitiesGameplayEffectComponent.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"
#include "Util/MD_LogUtility.h"

UMD_EquipHotbarComponent::UMD_EquipHotbarComponent()
{
	// 입력과 상태 변경에 의해서만 동작하는 컴포넌트 구성
	PrimaryComponentTick.bCanEverTick = false;
	// 서버 아이템 상태를 클라이언트에 복제하는 기본 설정
	SetIsReplicatedByDefault(true);
	// 기본 아이템 설정 수와 무관한 네 개의 공통 슬롯 생성
	ToolEntries.SetNum(4);
}

void UMD_EquipHotbarComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	// 상위 컴포넌트의 복제 항목 등록
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMD_EquipHotbarComponent, ToolEntries);
	DOREPLIFETIME(UMD_EquipHotbarComponent, CurrentSlotIndex);
}

void UMD_EquipHotbarComponent::BeginPlay()
{
	// 상위 컴포넌트 시작 처리
	Super::BeginPlay();
	// 유효한 서버 소유자에 한해서 기본 아이템 구성
	if (IsValid(GetOwner()) == true && GetOwner()->HasAuthority() == true)
	{
		// 기본 설정이 슬롯 수를 늘리지 않도록 복사 범위 제한
		int32 CopyCount = FMath::Min(DefaultTools.Num(), ToolEntries.Num());
		// 기본 설정의 슬롯 순서를 유지하는 지급 순회
		for (int32 Index = 0; Index < CopyCount; ++Index)
		{
			// 비어 있지 않은 기본 아이템만 한 개씩 등록
			if (IsValid(DefaultTools[Index]) == true)
			{
				// 잘못된 중복 도구 설정은 공통 슬롯 검증에서 거부
				SetItemAtSlot(Index, DefaultTools[Index], 1);
			}
		}
		// 빈 기본 설정도 UI에 초기 상태 전달
		OnHotbarChanged.Broadcast();
	}
}

void UMD_EquipHotbarComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// PlayerState에 남는 이전 캐릭터의 장비 GE 정리
	RemoveAllItemAbilities();
	// 상위 컴포넌트의 종료 처리
	Super::EndPlay(EndPlayReason);
}

UMD_ItemDefinition* UMD_EquipHotbarComponent::GetCurrentItem() const
{
	// 선택 슬롯을 공통 조회 경로로 전달
	return GetItemAtSlot(CurrentSlotIndex);
}

UMD_ItemDefinition* UMD_EquipHotbarComponent::GetItemAtSlot(int32 SlotIndex) const
{
	// 범위를 벗어나거나 소진된 슬롯의 아이템 접근 차단
	if (ToolEntries.IsValidIndex(SlotIndex) == false || ToolEntries[SlotIndex].Count <= 0)
	{
		// 보관 아이템 부재 전달
		return nullptr;
	}
	// 실제 보관 중인 공통 정의 반환
	return ToolEntries[SlotIndex].ToolDefinition;
}

int32 UMD_EquipHotbarComponent::GetItemCountAtSlot(int32 SlotIndex) const
{
	// 유효한 아이템이 있는 슬롯의 수량만 제공
	return IsValid(GetItemAtSlot(SlotIndex)) == true ? ToolEntries[SlotIndex].Count : 0;
}

int32 UMD_EquipHotbarComponent::FindItemEntry(UMD_ItemDefinition* ItemDefinition) const
{
	// 식별할 수 없는 정의의 조회 차단
	if (IsValid(ItemDefinition) == false || ItemDefinition->GetPrimaryAssetId().IsValid() == false)
	{
		// 일치 위치 부재 전달
		return INDEX_NONE;
	}
	// 네 개 슬롯에서 동일한 전체 AssetId 탐색
	for (int32 Index = 0; Index < ToolEntries.Num(); ++Index)
	{
		// 빈 슬롯과 소진 상태를 제외한 공통 정의 조회
		UMD_ItemDefinition* EntryDefinition = GetItemAtSlot(Index);
		// AssetType까지 일치하는 아이템의 보관 위치 확인
		if (IsValid(EntryDefinition) == true && EntryDefinition->GetPrimaryAssetId() == ItemDefinition->GetPrimaryAssetId())
		{
			// 동일 아이템 위치 전달
			return Index;
		}
	}
	// 일치 아이템 부재 전달
	return INDEX_NONE;
}

int32 UMD_EquipHotbarComponent::GetItemCount(UMD_ItemDefinition* ItemDefinition) const
{
	// 동일 AssetId 슬롯의 보유 수량 조회
	return GetItemCountAtSlot(FindItemEntry(ItemDefinition));
}

bool UMD_EquipHotbarComponent::FindAvailableItemEntry(UMD_ItemDefinition* ItemDefinition, int32 Amount, int32& OutEntryIndex) const
{
	// 실패 시 호출부가 오래된 슬롯 위치를 사용하지 않도록 초기화
	OutEntryIndex = INDEX_NONE;
	// 공통 정의와 식별자 및 양의 지급 수량 확인
	if (IsValid(ItemDefinition) == false || ItemDefinition->GetPrimaryAssetId().IsValid() == false || Amount <= 0)
	{
		// 잘못된 지급 데이터의 프로젝트 로그 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 아이템 정의, AssetId 또는 지급 수량이 유효하지 않음"), *UE_CALL_LOCATIONINFO);
		// 지급 요청 검증 실패 전달
		return false;
	}
	// 동일 도구의 중복 보관과 수량 합산을 제한할 종류 확인
	bool bIsTool = ItemDefinition->IsA<UMD_ToolDefinition>();
	// 도구의 일괄 지급 및 비정상 슬롯 배열 차단
	if ((bIsTool == true && Amount != 1) || ToolEntries.Num() != 4)
	{
		// 슬롯 또는 도구 수량 제약 실패 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s Hotbar는 네 슬롯이며 같은 도구는 한 번에 한 개만 지급 가능"), *UE_CALL_LOCATIONINFO);
		// 지급 수량 및 보관 구조 검증 실패 전달
		return false;
	}
	// 동일 아이템의 합산 대상 위치
	int32 SameEntryIndex = INDEX_NONE;
	// 새로운 아이템을 넣을 최초 빈 위치
	int32 EmptyEntryIndex = INDEX_NONE;
	// 모든 슬롯의 상태를 변경 없이 확인
	for (int32 Index = 0; Index < ToolEntries.Num(); ++Index)
	{
		// 해당 위치의 실제 보관 아이템 조회
		UMD_ItemDefinition* EntryDefinition = GetItemAtSlot(Index);
		// 최초 빈 슬롯만 후보로 보관
		if (IsValid(EntryDefinition) == false)
		{
			// 다른 빈 슬롯이 있어도 첫 위치 유지
			if (EmptyEntryIndex == INDEX_NONE)
			{
				// 비어 있는 지급 후보 위치 저장
				EmptyEntryIndex = Index;
			}
			// 빈 슬롯의 종류 및 AssetId 비교 생략
			continue;
		}
		// 동일한 전체 AssetId의 합산 후보 확인
		if (EntryDefinition->GetPrimaryAssetId() == ItemDefinition->GetPrimaryAssetId())
		{
			// 같은 ID의 중복 슬롯 및 서로 다른 종류 설정 충돌 차단
			if (SameEntryIndex != INDEX_NONE || EntryDefinition->IsA<UMD_ToolDefinition>() != bIsTool)
			{
				// AssetId 중복 또는 종류 충돌 기록
				UE_LOG(MarineDemolition, Warning, TEXT("%s 동일 AssetId의 중복 슬롯 또는 아이템 종류 충돌"), *UE_CALL_LOCATIONINFO);
				// 충돌 상태의 지급 실패 전달
				return false;
			}
			// 동일 아이템의 보관 위치 저장
			SameEntryIndex = Index;
		}
	}
	// 같은 도구의 수량 증가와 일반 아이템의 정수 오버플로 차단
	if (SameEntryIndex != INDEX_NONE && (bIsTool == true || ToolEntries[SameEntryIndex].Count > MAX_int32 - Amount))
	{
		// 중복 도구 또는 수량 표현 범위 초과 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 이미 소지한 도구이거나 아이템 수량의 정수 표현 범위를 초과함"), *UE_CALL_LOCATIONINFO);
		// 추가 지급 실패 전달
		return false;
	}
	// 동일 일반 아이템의 수량 합산 위치를 우선 사용
	OutEntryIndex = SameEntryIndex;
	// 합산 대상이 없는 새로운 도구와 일반 아이템은 빈 슬롯 사용
	if (OutEntryIndex == INDEX_NONE)
	{
		// 최초 빈 슬롯을 지급 위치로 선택
		OutEntryIndex = EmptyEntryIndex;
	}
	// 모든 슬롯이 차 있는 신규 품목의 지급 차단
	if (ToolEntries.IsValidIndex(OutEntryIndex) == false)
	{
		// 보관 공간 부족 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 아이템을 보관할 빈 Hotbar 슬롯이 없음"), *UE_CALL_LOCATIONINFO);
		// 보관 공간 부족 결과 전달
		return false;
	}
	// 상태를 변경하지 않은 지급 위치 검증 성공 전달
	return true;
}

bool UMD_EquipHotbarComponent::CanAddItem(UMD_ItemDefinition* ItemDefinition, int32 Amount) const
{
	// 공통 검사가 채울 지급 후보 위치
	int32 EntryIndex = INDEX_NONE;
	// 상태 변경 없는 지급 가능 여부 반환
	return FindAvailableItemEntry(ItemDefinition, Amount, EntryIndex);
}

bool UMD_EquipHotbarComponent::AddItem(UMD_ItemDefinition* ItemDefinition, int32 Amount)
{
	// 실제 상태와 복제 갱신에 사용할 소유 액터 조회
	AActor* OwnerActor = GetOwner();
	// 클라이언트의 직접 아이템 지급 차단
	if (IsValid(OwnerActor) == false || OwnerActor->HasAuthority() == false)
	{
		// 지급 권한 실패 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s Hotbar 아이템 지급의 서버 소유자가 유효하지 않음"), *UE_CALL_LOCATIONINFO);
		// 직접 지급 실패 전달
		return false;
	}
	// 실제 지급할 위치의 공통 검증 결과
	int32 EntryIndex = INDEX_NONE;
	// 보관 가능 검사와 능력 부여를 순서대로 완료한 경우에만 아이템 변경
	if (FindAvailableItemEntry(ItemDefinition, Amount, EntryIndex) == false || GrantItemAbility(ItemDefinition) == false)
	{
		// 보관 또는 GE 부여 실패에 따른 서버 구매 실패 전달
		return false;
	}
	// 실제 수량과 정의를 함께 갱신할 슬롯 참조
	FMD_HotbarToolEntry& Entry = ToolEntries[EntryIndex];
	// 일반 아이템만 같은 AssetId 수량을 합산하고 새 도구는 한 개로 추가
	int32 NewCount = ItemDefinition->IsA<UMD_ToolDefinition>() == false && IsValid(GetItemAtSlot(EntryIndex)) == true ? Entry.Count + Amount : Amount;
	// 새로 지급할 도구의 초기 충전 설정 조회
	UMD_ToolDefinition* NewTool = Cast<UMD_ToolDefinition>(ItemDefinition);
	// 새 도구의 초기 충전량을 해당 빈 슬롯에만 반영
	Entry.Charge = IsValid(NewTool) == true ? NewTool->InitialCharge : 0.0f;
	// 새 정의 또는 합산 대상 정의를 슬롯에 반영
	Entry.ToolDefinition = ItemDefinition;
	// 검증을 통과한 보유 수량 반영
	Entry.Count = NewCount;
	// 완성된 슬롯 변경을 장착 표시와 UI 및 복제에 전달
	NotifyEntriesChanged();
	// 실제 지급 성공 전달
	return true;
}

bool UMD_EquipHotbarComponent::TryConsumeItem(UMD_ItemDefinition* ItemDefinition, int32 Amount)
{
	// 소유자가 없거나 서버 권한 및 차감 수량이 올바르지 않은 요청 차단
	if (IsValid(GetOwner()) == false || GetOwner()->HasAuthority() == false || Amount <= 0)
	{
		// 차감 권한 또는 수량 실패 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s Hotbar 차감의 서버 권한 또는 수량이 유효하지 않음"), *UE_CALL_LOCATIONINFO);
		// 차감 요청 검증 실패 전달
		return false;
	}
	// 실제 정의 AssetId에 해당하는 차감 위치 조회
	int32 EntryIndex = FindItemEntry(ItemDefinition);
	// 미보유 품목 및 보유 수량보다 큰 차감 요청 차단
	if (ToolEntries.IsValidIndex(EntryIndex) == false || ToolEntries[EntryIndex].Count < Amount)
	{
		// 차감 대상 또는 잔여 수량 부족 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 차감할 아이템이 없거나 보유 수량이 부족함"), *UE_CALL_LOCATIONINFO);
		// 수량을 변경하지 않은 차감 실패 전달
		return false;
	}
	// 해당 슬롯의 보유 수량만 차감
	ToolEntries[EntryIndex].Count -= Amount;
	// 소진 시 배열 위치를 유지하면서 정의 참조 제거
	if (ToolEntries[EntryIndex].Count == 0)
	{
		// 실행 중인 소비 GA는 GE의 종료 후 제거 정책으로 마무리까지 유지
		RemoveItemAbility(ItemDefinition);
		// 다음 아이템이 사용할 수 있도록 슬롯 비우기
		ToolEntries[EntryIndex].ToolDefinition = nullptr;
	}
	// 수량과 정의 변경을 함께 전달
	NotifyEntriesChanged();
	// 실제 수량 차감 성공 전달
	return true;
}

bool UMD_EquipHotbarComponent::SetItemAtSlot(int32 SlotIndex, UMD_ItemDefinition* ItemDefinition, int32 Amount)
{
	// 서버 소유자와 지정 슬롯 및 수량의 공통 검사
	if (IsValid(GetOwner()) == false || GetOwner()->HasAuthority() == false || ToolEntries.IsValidIndex(SlotIndex) == false || Amount < 0)
	{
		// 직접 슬롯 설정의 요청 오류 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s Hotbar 슬롯 설정의 권한, 위치 또는 수량이 유효하지 않음"), *UE_CALL_LOCATIONINFO);
		// 슬롯 설정 실패 전달
		return false;
	}
	// 양의 수량을 배치할 때만 정의와 단일 소지 조건 확인
	if (Amount > 0)
	{
		// 미지정 정의 및 무효 ID와 도구 다중 수량 차단
		if (IsValid(ItemDefinition) == false || ItemDefinition->GetPrimaryAssetId().IsValid() == false
			|| (ItemDefinition->IsA<UMD_ToolDefinition>() == true && Amount != 1))
		{
			// 배치할 아이템 설정 오류 기록
			UE_LOG(MarineDemolition, Warning, TEXT("%s 슬롯의 아이템 정의, AssetId 또는 도구 수량이 유효하지 않음"), *UE_CALL_LOCATIONINFO);
			// 슬롯 내용 검증 실패 전달
			return false;
		}
		// 서로 다른 도구는 허용하고 같은 아이템의 중복 슬롯만 검사
		for (int32 Index = 0; Index < ToolEntries.Num(); ++Index)
		{
			// 현재 변경할 슬롯은 기존 내용과 무관하게 교체 가능
			if (Index == SlotIndex)
			{
				// 자기 슬롯과의 중복 비교 생략
				continue;
			}
			// 다른 위치의 실제 아이템 조회
			UMD_ItemDefinition* OtherItem = GetItemAtSlot(Index);
			// 같은 전체 AssetId가 여러 슬롯에 보관되는 상태 차단
			if (IsValid(OtherItem) == true && OtherItem->GetPrimaryAssetId() == ItemDefinition->GetPrimaryAssetId())
			{
				// 직접 슬롯 설정을 통한 동일 아이템 중복 보관 기록
				UE_LOG(MarineDemolition, Warning, TEXT("%s 다른 슬롯에 동일 AssetId의 아이템이 이미 존재함"), *UE_CALL_LOCATIONINFO);
				// 기존 슬롯들을 유지한 배치 실패 전달
				return false;
			}
		}
	}
	// 교체할 아이템의 능력 부여를 완료한 경우에만 기존 슬롯 변경
	if (Amount > 0 && GrantItemAbility(ItemDefinition) == false)
	{
		// 기존 도구와 GE를 보존한 교체 실패 전달
		return false;
	}
	// 수량만 바뀐 같은 품목의 GE는 유지하고 교체되는 품목만 회수
	UMD_ItemDefinition* PreviousItem = GetItemAtSlot(SlotIndex);
	// 비우기 요청 또는 서로 다른 전체 AssetId의 교체 검사
	if (IsValid(PreviousItem) == true && (Amount == 0 || PreviousItem->GetPrimaryAssetId() != ItemDefinition->GetPrimaryAssetId()))
	{
		// 교체 전 품목의 잔존 능력 정리
		RemoveItemAbility(PreviousItem);
	}
	// 빈 슬롯 요청에서는 정의도 함께 제거
	ToolEntries[SlotIndex].ToolDefinition = Amount > 0 ? ItemDefinition : nullptr;
	// 직접 설정 또는 저장 복구한 도구의 초기 충전량 조회
	UMD_ToolDefinition* SlotTool = Amount > 0 ? Cast<UMD_ToolDefinition>(ItemDefinition) : nullptr;
	// 명시적 슬롯 재구성 시 정의의 초기 충전량 적용
	ToolEntries[SlotIndex].Charge = IsValid(SlotTool) == true ? SlotTool->InitialCharge : 0.0f;
	// 지정 슬롯의 수량 확정
	ToolEntries[SlotIndex].Count = Amount;
	// 완성된 슬롯 상태의 변경 알림
	NotifyEntriesChanged();
	// 지정 슬롯 설정 성공 전달
	return true;
}

UMD_ToolDefinition* UMD_EquipHotbarComponent::GetCurrentTool() const
{
	// 현재 슬롯에서 도구 정의만 기존 장착 코드에 제공
	return Cast<UMD_ToolDefinition>(GetCurrentItem());
}

void UMD_EquipHotbarComponent::SelectSlot(int32 SlotIndex)
{
	// 범위 밖 위치와 현재 위치의 중복 선택 차단
	if (ToolEntries.IsValidIndex(SlotIndex) == false || SlotIndex == CurrentSlotIndex)
	{
		// 변경이 없는 선택 요청 종료
		return;
	}
	// 유효한 서버 소유자에 한해서 선택 상태 직접 확정
	if (IsValid(GetOwner()) == true && GetOwner()->HasAuthority() == true)
	{
		// 서버의 선택 위치 갱신
		CurrentSlotIndex = SlotIndex;
		// 선택 변경에 따른 장착 표시 갱신
		OnRep_CurrentSlotIndex();
		// 서버에서 처리한 입력의 RPC 중복 요청 생략
		return;
	}
	// 로컬 입력 지연을 줄이기 위한 예측 선택 반영
	CurrentSlotIndex = SlotIndex;
	// 로컬 장착 표시와 UI에 예측 상태 전달
	OnHotbarChanged.Broadcast();
	// 서버에 실제 선택 위치 확정 요청
	ServerSelectSlot(SlotIndex);
}

void UMD_EquipHotbarComponent::CycleSlot(int32 Direction)
{
	// 이동 방향이 없거나 순환할 슬롯이 부족한 입력 차단
	if (ToolEntries.Num() <= 1 || Direction == 0)
	{
		// 순환하지 않는 입력 종료
		return;
	}
	// 입력 크기와 무관하게 한 칸씩 이동
	int32 Step = Direction > 0 ? 1 : -1;
	// 배열 양 끝을 연결하는 다음 선택 위치 계산
	SelectSlot((CurrentSlotIndex + Step + ToolEntries.Num()) % ToolEntries.Num());
}

TArray<TSoftObjectPtr<UDataAsset>> UMD_EquipHotbarComponent::BuildEquipmentSaveData() const
{
	// 기존 저장 형식으로 반환할 공통 아이템 경로 목록
	TArray<TSoftObjectPtr<UDataAsset>> Result;
	// 서버 소유자만 저장용 보관 상태 조회 허용
	if (IsValid(GetOwner()) == false || GetOwner()->HasAuthority() == false)
	{
		// 비권위 저장 요청에 빈 결과 전달
		return Result;
	}
	// 빈 슬롯을 포함한 네 위치의 순서 유지
	for (int32 Index = 0; Index < ToolEntries.Num(); ++Index)
	{
		// 도구와 일반 아이템을 같은 저장 형식에 기록
		Result.Add(GetItemAtSlot(Index));
	}
	// 기존 저장 시스템에 경로 목록 반환
	return Result;
}

bool UMD_EquipHotbarComponent::RestoreEquipmentSaveData(const TArray<TSoftObjectPtr<UDataAsset>>& SaveData)
{
	// 서버 권한 없는 저장 상태 복구 차단
	if (IsValid(GetOwner()) == false || GetOwner()->HasAuthority() == false)
	{
		// 복구 권한 실패 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s Hotbar 저장 복구의 서버 소유자가 유효하지 않음"), *UE_CALL_LOCATIONINFO);
		// 보관 상태 복구 실패 전달
		return false;
	}
	// 기존 저장 정보가 없으면 기본 장비 상태 유지
	if (SaveData.Num() == 0)
	{
		// 복구할 정보가 없는 기존 저장 형식의 성공 전달
		return true;
	}
	// 저장 복구로 사라질 기존 품목의 GE와 GA 정리
	RemoveAllItemAbilities();
	// 기존 보관 상태를 제거한 고정 네 슬롯 생성
	ToolEntries.Empty(4);
	// 저장 데이터 길이와 무관하게 슬롯 개수 고정
	ToolEntries.SetNum(4);
	// 구형 저장의 긴 슬롯 목록을 현재 최대 범위로 제한
	int32 CopyCount = FMath::Min(SaveData.Num(), ToolEntries.Num());
	// 저장 위치를 유지하는 공통 아이템 복구 순회
	for (int32 Index = 0; Index < CopyCount; ++Index)
	{
		// 빈 저장 위치의 불필요한 로드 생략
		if (SaveData[Index].IsNull() == true)
		{
			// 다음 저장 위치 확인
			continue;
		}
		// 소프트 경로에서 도구 또는 일반 아이템 정의 복구
		UMD_ItemDefinition* Item = Cast<UMD_ItemDefinition>(SaveData[Index].LoadSynchronous());
		// 로드 가능한 공통 정의만 현재 슬롯 제약에 따라 복구
		if (IsValid(Item) == true)
		{
			// 저장된 슬롯 순서대로 서로 다른 도구와 일반 아이템 복구
			SetItemAtSlot(Index, Item, 1);
		}
	}
	// 복구 후 선택 위치가 유효한 네 슬롯 범위에 있도록 보정
	CurrentSlotIndex = FMath::Clamp(CurrentSlotIndex, 0, ToolEntries.Num() - 1);
	// 복구된 최종 보관 상태 전달
	NotifyEntriesChanged();
	// 기존 저장 데이터의 적용 완료 전달
	return true;
}

void UMD_EquipHotbarComponent::NotifyEntriesChanged()
{
	// 네트워크 갱신에 사용할 소유자 조회
	AActor* OwnerActor = GetOwner();
	// UI 콜백에 앞서 서버 복제 갱신 예약
	if (IsValid(OwnerActor) == true && OwnerActor->HasAuthority() == true)
	{
		// 변경된 아이템과 수량의 빠른 복제 요청
		OwnerActor->ForceNetUpdate();
	}
	// 리슨 서버에도 동일한 상태 변경 알림 적용
	OnRep_ToolEntries();
}

void UMD_EquipHotbarComponent::OnRep_ToolEntries()
{
	// 복제된 아이템과 수량으로 장착 표시 및 UI 갱신
	OnHotbarChanged.Broadcast();
}

void UMD_EquipHotbarComponent::OnRep_CurrentSlotIndex()
{
	// 복제된 선택 상태로 장착 표시 및 UI 갱신
	OnHotbarChanged.Broadcast();
}

void UMD_EquipHotbarComponent::ServerSelectSlot_Implementation(int32 SlotIndex)
{
	// 소유자 권한과 선택 위치 범위의 서버 재검증
	if (IsValid(GetOwner()) == false || GetOwner()->HasAuthority() == false || ToolEntries.IsValidIndex(SlotIndex) == false)
	{
		// 유효하지 않은 선택 요청 종료
		return;
	}
	// 검증된 클라이언트 선택의 서버 확정
	CurrentSlotIndex = SlotIndex;
	// 서버의 장착 표시와 UI 갱신
	OnRep_CurrentSlotIndex();
	// 선택 상태의 빠른 클라이언트 복제 요청
	GetOwner()->ForceNetUpdate();
}

float UMD_EquipHotbarComponent::GetToolCharge(UMD_ToolDefinition* ToolDefinition)
{
	// 동일한 전체 식별자의 보관 위치 조회
	int32 Index = FindItemEntry(ToolDefinition);
	// 미보유 도구는 사용 가능한 충전량이 없는 상태로 전달
	return ToolEntries.IsValidIndex(Index) == true ? ToolEntries[Index].Charge : 0.0f;
}

bool UMD_EquipHotbarComponent::CanAddToolCharge(UMD_ToolDefinition* ToolDefinition, float Amount)
{
	// 충전 설정과 보유 수량 및 잔량을 함께 확인
	if (IsValid(ToolDefinition) == false || FMath::IsFinite(Amount) == false || Amount <= 0.0f
		|| FMath::IsFinite(ToolDefinition->MaxCharge) == false || ToolDefinition->MaxCharge <= 0.0f
		|| GetItemCount(ToolDefinition) <= 0 || GetToolCharge(ToolDefinition) >= ToolDefinition->MaxCharge)
	{
		// 보충할 수 없는 대상 또는 만충 상태 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 보유하지 않은 도구이거나 충전 설정 및 잔량 조건이 충족되지 않음"), *UE_CALL_LOCATIONINFO);
		// 충전 상태를 변경하지 않은 거절 결과 전달
		return false;
	}
	// 대상 도구의 보충 가능 결과 전달
	return true;
}

bool UMD_EquipHotbarComponent::AddToolCharge(UMD_ToolDefinition* ToolDefinition, float Amount)
{
	// 실제 충전 상태 변경 전 서버 권한 확인
	if (IsValid(GetOwner()) == false || GetOwner()->HasAuthority() == false)
	{
		// 충전 요청 권한 오류 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 도구 충전의 서버 권한이 없음"), *UE_CALL_LOCATIONINFO);
		// 권한 없는 충전 요청 실패 전달
		return false;
	}
	// 최종 보유 상태와 충전 상한 재검증
	if (CanAddToolCharge(ToolDefinition, Amount) == false)
	{
		// 선행 검사에서 기록한 충전 실패 전달
		return false;
	}
	// 검증된 실제 보관 슬롯 참조
	FMD_HotbarToolEntry& Entry = ToolEntries[FindItemEntry(ToolDefinition)];
	// 덧셈 오버플로 없이 남은 용량만큼 충전
	Entry.Charge += FMath::Min(Amount, ToolDefinition->MaxCharge - Entry.Charge);
	// 변경된 잔량을 소유자와 UI 및 복제에 전달
	NotifyEntriesChanged();
	// 실제 충전 완료 결과 전달
	return true;
}

bool UMD_EquipHotbarComponent::IsItemAbilityGrantValid(UMD_ItemDefinition* ItemDefinition)
{
	// 도구 또는 소모품 정의의 기능 설정 접근 전 유효성 검사
	if (IsValid(ItemDefinition) == false)
	{
		// 정의 부재의 프로젝트 로그 기록
		UE_LOG(MarineDemolition, Warning, TEXT("Item ability definition is invalid."));
		// 구매 설정 검증 실패 전달
		return false;
	}
	// 아이템 유형이 제공한 구매 GE 클래스 조회
	TSubclassOf<UGameplayEffect> EffectClass = ItemDefinition->GetAbilityGrantEffect();
	// 사용 효과를 아직 지정하지 않은 소모품의 기존 보관 및 별도 사용 경로 유지
	if (IsValid(EffectClass) == false && ItemDefinition->GetUseAbilityTag().IsValid() == false && ItemDefinition->IsA<UMD_ConsumeItemDefinition>() == true)
	{
		// 등록된 구매 GE가 없는 소모품의 보관 허용
		return true;
	}
	// 부여 GE와 사용 태그가 없는 장비의 구매 차단
	if (IsValid(EffectClass) == false || ItemDefinition->GetUseAbilityTag().IsValid() == false)
	{
		// 아이템의 누락된 구매 GE 또는 실행 태그 기록
		UE_LOG(MarineDemolition, Warning, TEXT("Item requires AbilityGrantEffect and use ability tag: %s"), *ItemDefinition->GetPathName());
		// 잘못된 지급 설정 결과 전달
		return false;
	}
	// 보관 기간에 한정해 독립적으로 GA를 부여할 GE 기본값 조회
	UGameplayEffect* Effect = EffectClass.GetDefaultObject();
	// Instant 부여와 아이템 간 GE 스택 합산으로 발생할 수명 및 SourceObject 충돌 차단
	if (Effect->DurationPolicy != EGameplayEffectDurationType::Infinite || Effect->GetStackingType() != EGameplayEffectStackingType::None
		|| IsValid(Effect->FindComponent<UAbilitiesGameplayEffectComponent>()) == false)
	{
		// 지원하지 않는 구매 GE 구성의 프로젝트 로그 기록
		UE_LOG(MarineDemolition, Warning, TEXT("Item GE requires Infinite, no stacking and Grant Gameplay Abilities: %s"), *ItemDefinition->GetPathName());
		// 부여 계약을 충족하지 못한 설정 거부
		return false;
	}
	// 구매자 인터페이스를 통해 PlayerState의 ASC 조회
	UAbilitySystemComponent* AbilitySystem = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
	// 준비된 구매자에게만 실제 구매 허용
	if (IsValid(AbilitySystem) == false || AbilitySystem->GetAvatarActor() != GetOwner())
	{
		// 아직 연결되지 않은 구매자 ASC 기록
		UE_LOG(MarineDemolition, Warning, TEXT("Item buyer AbilitySystem is not initialized."));
		// ASC 연결 전 구매 차단
		return false;
	}
	// 아이템 구매 GE 설정과 수신자 준비 완료 전달
	return true;
}

FGameplayAbilitySpecHandle UMD_EquipHotbarComponent::FindItemAbility(UMD_ItemDefinition* ItemDefinition)
{
	// 입력 소유자의 능력 시스템 조회
	UAbilitySystemComponent* AbilitySystem = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
	// 미연결 시스템 또는 아이템 없는 입력 차단
	if (IsValid(AbilitySystem) == false || IsValid(ItemDefinition) == false || ItemDefinition->GetPrimaryAssetId().IsValid() == false)
	{
		// 실행할 능력 부재 전달
		return FGameplayAbilitySpecHandle();
	}
	// 동일 태그를 공유하더라도 선택한 아이템에 부여된 한 개의 GA만 탐색
	FGameplayAbilitySpecHandle FoundHandle;
	// ASC에 실제 부여된 능력 목록 순회
	for (FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
	{
		// 구매 GE의 문맥에서 전달된 공통 아이템 정의 조회
		UMD_ItemDefinition* SourceItem = Cast<UMD_ItemDefinition>(Spec.SourceObject.Get());
		// 전체 AssetId와 정확한 사용 태그로 선택 품목의 능력 식별
		if (IsValid(SourceItem) == true && SourceItem->GetPrimaryAssetId() == ItemDefinition->GetPrimaryAssetId()
			&& Spec.PendingRemove == false && Spec.RemoveAfterActivation == false
			&& IsValid(Spec.Ability) == true && Spec.Ability->GetAssetTags().HasTagExact(ItemDefinition->GetUseAbilityTag()) == true)
		{
			// 같은 아이템에 중복 등록된 입력 능력의 임의 실행 차단
			if (FoundHandle.IsValid() == true)
			{
				// 중복된 능력 설정의 프로젝트 로그 기록
				UE_LOG(MarineDemolition, Warning, TEXT("Multiple use abilities granted for item: %s"), *ItemDefinition->GetPrimaryAssetId().ToString());
				// 모호한 실행 대상 거부
				return FGameplayAbilitySpecHandle();
			}
			// 현재 품목에만 귀속된 실행 핸들 저장
			FoundHandle = Spec.Handle;
		}
	}
	// 실제 부여된 능력의 식별 결과 전달
	return FoundHandle;
}

bool UMD_EquipHotbarComponent::GrantItemAbility(UMD_ItemDefinition* ItemDefinition)
{
	// GA를 사용하지 않는 공통 보관 아이템은 기존 지급만 수행
	if (IsValid(ItemDefinition) == true && ItemDefinition->IsA<UMD_ToolDefinition>() == false && ItemDefinition->IsA<UMD_ConsumeItemDefinition>() == false)
	{
		// 비장비 품목의 능력 부여 생략 성공 전달
		return true;
	}
	// 선택적 GE가 미지정된 소모품은 이전과 같이 보관만 수행
	if (IsValid(ItemDefinition) == true && ItemDefinition->IsA<UMD_ConsumeItemDefinition>() == true
		&& IsValid(ItemDefinition->GetAbilityGrantEffect()) == false && ItemDefinition->GetUseAbilityTag().IsValid() == false)
	{
		// 소비 기능을 지정하기 전 수량 보관의 성공 전달
		return true;
	}
	// 기본 장비가 먼저 생성되는 초기화 시점의 ASC 조회
	UAbilitySystemComponent* AbilitySystem = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
	// BeginPlay의 기본 슬롯은 보존하고 PossessedBy의 ASC 연결 시 능력 부여
	if (IsValid(GetOwner()) == true && GetOwner()->HasActorBegunPlay() == false
		&& (IsValid(AbilitySystem) == false || AbilitySystem->GetAvatarActor() != GetOwner()))
	{
		// 초기 보관만 완료하고 능력 연결은 초기화 진입점으로 연기
		return true;
	}
	// 구매 GE의 형식과 구매자 ASC 검증
	if (IsItemAbilityGrantValid(ItemDefinition) == false)
	{
		// 잘못된 구매 효과의 지급 차단
		return false;
	}
	// 재구매에 의한 같은 품목의 GE 중복 생성 차단
	FActiveGameplayEffectHandle* ExistingHandle = ItemAbilityEffects.Find(ItemDefinition->GetPrimaryAssetId());
	// 기존 능력 부여가 유지되는 수량 증가 요청 확인
	if (ExistingHandle != nullptr && AbilitySystem->GetActiveGameplayEffect(*ExistingHandle) != nullptr)
	{
		// 기존 품목의 유효한 능력 등록 상태 전달
		return FindItemAbility(ItemDefinition).IsValid();
	}
	// GA의 SourceObject로 구매한 아이템을 전달할 GE 문맥 생성
	FGameplayEffectContextHandle Context = AbilitySystem->MakeEffectContext();
	// 같은 태그를 공유하는 도구 사이에서 아이템 귀속 구분
	Context.AddSourceObject(ItemDefinition);
	// BP GE에 지정된 GA 구성으로 실제 적용 사양 생성
	FGameplayEffectSpecHandle Spec = AbilitySystem->MakeOutgoingSpec(ItemDefinition->GetAbilityGrantEffect(), 1.0f, Context);
	// 유효한 사양에서만 부여 효과 적용
	if (Spec.IsValid() == false)
	{
		// GE 사양 생성 실패의 프로젝트 로그 기록
		UE_LOG(MarineDemolition, Warning, TEXT("Failed to create item grant GE spec."));
		// 실제 지급 없는 실패 전달
		return false;
	}
	// 서버 구매자에게 GE를 적용하여 엔진의 Grant Gameplay Abilities 실행
	FActiveGameplayEffectHandle AppliedHandle = AbilitySystem->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	// 요구된 사용 GA가 실제로 부여된 경우에만 지급 성공 허용
	if (AppliedHandle.IsValid() == true && FindItemAbility(ItemDefinition).IsValid() == true)
	{
		// 마지막 소비나 업그레이드에서 회수할 GE 핸들 등록
		ItemAbilityEffects.Add(ItemDefinition->GetPrimaryAssetId(), AppliedHandle);
		// 구매 GE의 실제 능력 부여 성공 전달
		return true;
	}
	// 적용은 되었으나 필요한 GA가 없는 잘못된 GE의 회수
	if (AppliedHandle.IsValid() == true)
	{
		// 실패한 지급으로 남는 능력 및 효과 제거
		AbilitySystem->RemoveActiveGameplayEffect(AppliedHandle);
	}
	// 적용 거부 또는 사용 태그에 해당하는 GA 부재 기록
	UE_LOG(MarineDemolition, Warning, TEXT("Item GE failed to grant its use ability: %s"), *ItemDefinition->GetPathName());
	// 아이템과 결제를 변경하지 않는 구매 실패 전달
	return false;
}

void UMD_EquipHotbarComponent::RemoveItemAbility(UMD_ItemDefinition* ItemDefinition)
{
	// 빈 슬롯이나 초기화 전 아이템의 제거 생략
	if (IsValid(ItemDefinition) == false)
	{
		// 제거할 아이템 부재 시 처리 종료
		return;
	}
	// 아이템과 함께 회수할 구매 GE 핸들 수신
	FActiveGameplayEffectHandle Handle;
	// 동일 아이템의 정리 요청 중복 처리 방지
	if (ItemAbilityEffects.RemoveAndCopyValue(ItemDefinition->GetPrimaryAssetId(), Handle) == true)
	{
		// 소유 캐릭터가 종료되어도 GE가 남은 실제 ASC 조회
		UAbilitySystemComponent* AbilitySystem = Handle.GetOwningAbilitySystemComponent();
		// 유효한 시스템에서 GE의 능력 회수 정책 실행
		if (IsValid(AbilitySystem) == true)
		{
			// RemoveAbilityOnEnd로 마지막 소모품 GA의 정상 종료 보장
			AbilitySystem->RemoveActiveGameplayEffect(Handle);
		}
	}
}

void UMD_EquipHotbarComponent::RemoveAllItemAbilities()
{
	// GE 제거 콜백의 재진입에도 순회가 유지되도록 핸들 목록 분리
	TArray<FActiveGameplayEffectHandle> Handles;
	// 현재 캐릭터가 부여한 아이템 GE만 회수 대상으로 수집
	ItemAbilityEffects.GenerateValueArray(Handles);
	// 능력 종료 콜백에서 동일 핸들을 다시 제거하지 않도록 먼저 등록 해제
	ItemAbilityEffects.Reset();
	// 저장 복구 또는 캐릭터 종료 이전에 적용된 효과 순회
	for (FActiveGameplayEffectHandle Handle : Handles)
	{
		// GE가 적용된 PlayerState ASC 조회
		UAbilitySystemComponent* AbilitySystem = Handle.GetOwningAbilitySystemComponent();
		// 수명이 끝난 능력 시스템의 접근 차단
		if (IsValid(AbilitySystem) == true)
		{
			// 보관 품목에 부여된 효과와 GA의 수명 종료
			AbilitySystem->RemoveActiveGameplayEffect(Handle);
		}
	}
}

void UMD_EquipHotbarComponent::InitializeItemAbilities()
{
	// 서버의 ASC 연결 이후에만 기본 아이템 능력 초기화
	if (IsValid(GetOwner()) == false || GetOwner()->HasAuthority() == false)
	{
		// 클라이언트의 직접 GE 부여 차단
		return;
	}
	// 네 공통 슬롯의 보관 아이템만 초기화 대상으로 순회
	for (int32 Index = 0; Index < ToolEntries.Num(); ++Index)
	{
		// 소진된 아이템을 제외한 현재 보관 정의 조회
		UMD_ItemDefinition* Item = GetItemAtSlot(Index);
		// 유효한 보관 품목의 GE 연결 실패 검사
		if (IsValid(Item) == true && GrantItemAbility(Item) == false)
		{
			// 기본 또는 저장 복구 장비의 설정 실패 기록
			UE_LOG(MarineDemolition, Warning, TEXT("Failed to initialize stored item ability: %s"), *Item->GetPathName());
		}
	}
}

bool UMD_EquipHotbarComponent::TryActivateCurrentItemAbility()
{
	// 실제 선택 품목의 SourceObject와 태그에 해당하는 부여 GA 조회
	FGameplayAbilitySpecHandle Handle = FindItemAbility(GetCurrentItem());
	// 클라이언트 입력의 서버 활성화 요청을 지원하는 ASC 조회
	UAbilitySystemComponent* AbilitySystem = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
	// 구매로 부여받은 능력이 있는 경우에만 실행 요청
	if (IsValid(AbilitySystem) == true && Handle.IsValid() == true)
	{
		// GA에 설정된 실행 정책으로 클라이언트 또는 서버 실행 전달
		return AbilitySystem->TryActivateAbility(Handle);
	}
	// 미보유 또는 아직 부여되지 않은 능력의 입력 결과 전달
	return false;
}
