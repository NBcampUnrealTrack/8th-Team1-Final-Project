// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayEffectTypes.h"
#include "GameplayAbilitySpecHandle.h"
#include "MD_EquipHotbarComponent.generated.h"

class UMD_ItemDefinition;
class UMD_ToolDefinition;
class UAbilitySystemComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMDOnEquipHotbarChanged);

// 기존 직렬화 이름을 유지하면서 모든 아이템 정의와 수량을 보관하는 슬롯
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_HotbarToolEntry
{
	GENERATED_BODY()

	// 도구 및 소모품 등 슬롯에 보관한 공통 아이템 정의
	UPROPERTY(BlueprintReadOnly, Category = "Hotbar")
	TObjectPtr<UMD_ItemDefinition> ToolDefinition;

	// 해당 슬롯의 보유 수량이며 빈 슬롯은 영
	UPROPERTY(BlueprintReadOnly, Category = "Hotbar")
	int32 Count = 0;

	// 보관 중인 일반 충전 도구의 현재 잔량
	UPROPERTY(BlueprintReadOnly, Category = "Hotbar")
	float Charge = 0.0f;
};

UCLASS(ClassGroup=(MarineDemolition), meta=(BlueprintSpawnableComponent))
class MARINEDEMOLITION_API UMD_EquipHotbarComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMD_EquipHotbarComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// 슬롯 내용 또는 선택 변경을 캐릭터와 UI에 전달하는 알림
	UPROPERTY(BlueprintAssignable, Category = "MD|HotBar")
	FMDOnEquipHotbarChanged OnHotbarChanged;

protected:
	// 기존 BP 설정 이름을 유지하는 기본 아이템 목록이며 앞의 네 위치만 사용
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hotbar", meta = (DisplayName = "Default Items"))
	TArray<TObjectPtr<UMD_ItemDefinition>> DefaultTools;

	// 종류별 전용 슬롯 없이 네 위치에 아이템과 수량을 함께 복제하는 상태
	UPROPERTY(ReplicatedUsing = OnRep_ToolEntries)
	TArray<FMD_HotbarToolEntry> ToolEntries;

	// 현재 사용 입력과 장착 표시가 참조할 슬롯 위치
	UPROPERTY(ReplicatedUsing = OnRep_CurrentSlotIndex)
	int32 CurrentSlotIndex = 0;

	// 수량 증가 시 중복 부여를 방지하고 소진 시 제거할 아이템별 활성 GE
	UPROPERTY(Transient)
	TMap<FPrimaryAssetId, FActiveGameplayEffectHandle> ItemAbilityEffects;

public:
	// 선택한 슬롯의 공통 아이템 조회
	UFUNCTION(BlueprintPure, Category = "Hotbar")
	UMD_ItemDefinition* GetCurrentItem() const;

	// 지정한 슬롯의 공통 아이템 조회
	UFUNCTION(BlueprintPure, Category = "Hotbar")
	UMD_ItemDefinition* GetItemAtSlot(int32 SlotIndex) const;

	// 고정된 슬롯 배열의 현재 길이 조회
	UFUNCTION(BlueprintPure, Category = "Hotbar")
	FORCEINLINE int32 GetSlotCount() const { return ToolEntries.Num(); }

	// 현재 선택한 슬롯 위치 조회
	UFUNCTION(BlueprintPure, Category = "MD|HotBar")
	FORCEINLINE int32 GetCurrentSlotIndex() const { return CurrentSlotIndex; }

	// 지정한 슬롯의 보유 수량 조회
	UFUNCTION(BlueprintPure, Category = "Hotbar")
	int32 GetItemCountAtSlot(int32 SlotIndex) const;

	// 공통 AssetId에 해당하는 보유 수량 조회
	UFUNCTION(BlueprintPure, Category = "Hotbar")
	int32 GetItemCount(UMD_ItemDefinition* ItemDefinition) const;

	// 같은 도구의 중복 보관을 제외한 일반 아이템 합산 및 빈 슬롯 지급 검사
	UFUNCTION(BlueprintPure, Category = "Hotbar")
	bool CanAddItem(UMD_ItemDefinition* ItemDefinition, int32 Amount = 1) const;

	// 서로 다른 도구는 빈 슬롯에 추가하고 같은 일반 아이템은 수량 합산
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Hotbar")
	bool AddItem(UMD_ItemDefinition* ItemDefinition, int32 Amount = 1);

	// 보관 중인 도구의 현재 충전 잔량 조회
	UFUNCTION(BlueprintPure, Category = "Hotbar|Charge")
	float GetToolCharge(UMD_ToolDefinition* ToolDefinition);

	// 도구 보유 상태와 충전 상한을 기준으로 보충 가능 여부 확인
	UFUNCTION(BlueprintPure, Category = "Hotbar|Charge")
	bool CanAddToolCharge(UMD_ToolDefinition* ToolDefinition, float Amount);

	// 서버에서 보관 중인 도구의 충전을 상한까지 보충
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Hotbar|Charge")
	bool AddToolCharge(UMD_ToolDefinition* ToolDefinition, float Amount);

	// 효과 적용과 분리하여 보유 수량만 차감하는 서버 처리
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Hotbar")
	bool TryConsumeItem(UMD_ItemDefinition* ItemDefinition, int32 Amount = 1);

	// 같은 AssetId의 중복 슬롯을 막는 지정 슬롯 설정이며 수량 영은 비우기
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Hotbar")
	bool SetItemAtSlot(int32 SlotIndex, UMD_ItemDefinition* ItemDefinition, int32 Amount = 1);

	// 기존 캐릭터 장착 코드에 선택 슬롯의 도구만 제공하는 호환 조회
	UFUNCTION(BlueprintPure, Category = "MD|Hotbar")
	UMD_ToolDefinition* GetCurrentTool() const;

	// 숫자키 등 BP 입력에 따른 슬롯 선택
	UFUNCTION(BlueprintCallable, Category = "MD|HotBar")
	void SelectSlot(int32 SlotIndex);

	// 휠 등 BP 입력에 따른 슬롯 순환
	UFUNCTION(BlueprintCallable, Category = "MD|HotBar")
	void CycleSlot(int32 Direction);

	// 기존 저장 형식에 슬롯 순서를 보존한 공통 정의 경로 제공
	TArray<TSoftObjectPtr<UDataAsset>> BuildEquipmentSaveData() const;

	// 기존 경로 저장 데이터에서 공통 아이템을 슬롯당 한 개씩 복구
	bool RestoreEquipmentSaveData(const TArray<TSoftObjectPtr<UDataAsset>>& SaveData);

	// 구매 전에 무효 GE와 GA 태그 및 ASC 준비 상태 검사
	bool IsItemAbilityGrantValid(UMD_ItemDefinition* ItemDefinition);

	// PlayerState ASC 연결 이후 기본 또는 복구된 아이템의 구매 GE 부여
	void InitializeItemAbilities();

	// 아이템의 전체 AssetId와 사용 태그에 해당하는 부여 GA 조회
	FGameplayAbilitySpecHandle FindItemAbility(UMD_ItemDefinition* ItemDefinition);

	// 선택한 아이템에 부여된 GA 한 개에 사용 입력 전달
	UFUNCTION(BlueprintCallable, Category = "Hotbar|Ability")
	bool TryActivateCurrentItemAbility();

protected:
	// 새 품목의 GE를 먼저 적용하여 능력 부여 실패 시 보관과 결제 차단
	bool GrantItemAbility(UMD_ItemDefinition* ItemDefinition);

	// 마지막 수량 소진 또는 교체된 품목의 GE와 부여 능력 회수
	void RemoveItemAbility(UMD_ItemDefinition* ItemDefinition);

	// 캐릭터 종료 또는 전체 장비 복구 이전의 아이템 GE 정리
	void RemoveAllItemAbilities();

	// 공통 AssetId와 도구 소지 상태 및 수량 범위로 지급 위치 결정
	bool FindAvailableItemEntry(UMD_ItemDefinition* ItemDefinition, int32 Amount, int32& OutEntryIndex) const;

	// 정의의 전체 AssetId에 일치하는 보관 위치 조회
	int32 FindItemEntry(UMD_ItemDefinition* ItemDefinition) const;

	// 서버 슬롯 변경을 캐릭터와 UI 및 복제 대상에 전달
	void NotifyEntriesChanged();

	// 클라이언트의 선택 요청을 서버 상태에 반영
	UFUNCTION(Server, Reliable)
	void ServerSelectSlot(int32 SlotIndex);

	// 아이템과 수량의 복제 완료 알림
	UFUNCTION()
	void OnRep_ToolEntries();

	// 선택 위치의 복제 완료 알림
	UFUNCTION()
	void OnRep_CurrentSlotIndex();
};
