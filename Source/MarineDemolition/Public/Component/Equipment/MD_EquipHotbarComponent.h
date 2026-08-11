// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "MD_EquipHotbarComponent.generated.h"

class UMD_ToolDefinition;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMDOnEquipHotbarChanged);

/** 네트워크 복제를 지원하는 배열에 상점 품목 식별자와 수량을 함께 담는 항목 구조체 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_HotbarShopItemEntry
{
	GENERATED_BODY()

	/** 카탈로그 품목과 핫바 보유 수량을 연결하는 고유 식별자 */
	UPROPERTY(BlueprintReadOnly, Category = "Hotbar")
	FName ItemId;

	/** 해당 플레이어가 현재 보유한 품목 수량 */
	UPROPERTY(BlueprintReadOnly, Category = "Hotbar")
	int32 Count = 0;
};

UCLASS( ClassGroup=(MarineDemolition), meta=(BlueprintSpawnableComponent) )
class MARINEDEMOLITION_API UMD_EquipHotbarComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UMD_EquipHotbarComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:
	// 슬롯 내용 또는 선택 인덱스가 바뀔 때 브로드캐스트. UI가 구독한다
	UPROPERTY(BlueprintAssignable, Category = "Hotbar")
	FMDOnEquipHotbarChanged OnHotbarChanged;

private:
	UPROPERTY(ReplicatedUsing = OnRep_Slots)
	TArray<TObjectPtr<UMD_ToolDefinition>> Slots;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentSlotIndex)
	int32 CurrentSlotIndex = 0;

	/** TMap 복제 제약을 피하면서 플레이어별 장비와 소모품을 공유하기 위한 복제 품목 배열 */
	UPROPERTY(ReplicatedUsing = OnRep_ShopItemEntries)
	TArray<FMD_HotbarShopItemEntry> ShopItemEntries;

public:
	UFUNCTION(BlueprintPure, Category = "Hotbar")
	UMD_ToolDefinition* GetCurrentTool() const;

	UFUNCTION(BlueprintPure, Category = "Hotbar")
	UMD_ToolDefinition* GetToolAtSlot(int32 SlotIndex) const;

	UFUNCTION(BlueprintPure, Category = "Hotbar")
	int32 GetSlotCount() const { return Slots.Num(); }

	UFUNCTION(BlueprintPure, Category = "Hotbar")
	int32 GetCurrentSlotIndex() const { return CurrentSlotIndex; }

	/** 별도 가방 인벤토리 없이 핫바가 보관하는 상점 구매 품목 수량 조회 함수 */
	UFUNCTION(BlueprintPure, Category = "Hotbar")
	int32 GetShopItemCount(FName ItemId) const;

	/** 서버가 검증한 상점 구매 품목을 핫바 소유 원장에 추가하는 함수 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Hotbar")
	bool AddShopItem(FName ItemId, int32 Amount = 1);

	/** 상점 장비 업그레이드 구매 시 GameplayTag가 정확히 일치하는 기존 장비 슬롯을 교체하는 함수 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Hotbar")
	bool ReplaceToolByGameplayTag(const FGameplayTag& TargetToolTag, UMD_ToolDefinition* Tool);

	/** 소모품 사용 시 핫바 소유 원장에서 지정 수량을 차감하는 함수 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Hotbar")
	bool TryConsumeShopItem(FName ItemId, int32 Amount = 1);

	// 슬롯 직접 선택 (숫자키)
	UFUNCTION(BlueprintCallable, Category = "Hotbar")
	void SelectSlot(int32 SlotIndex);

	// 다음/이전 슬롯으로 순환 (휠)
	UFUNCTION(BlueprintCallable, Category = "Hotbar")
	void CycleSlot(int32 Direction);

	// 슬롯에 도구 등록 (획득 시스템에서 호출)
	UFUNCTION(BlueprintCallable, Category = "Hotbar")
	void SetToolAtSlot(int32 SlotIndex, UMD_ToolDefinition* Tool);

	// 서버에서 플레이어의 현재 장비 목록을 저장하기 위한 데이터를 반환하는 함수
	TArray<TSoftObjectPtr<UDataAsset>> BuildEquipmentSaveData() const;

	// 서버에서 저장된 장비 목록을 불러오는 함수
	bool RestoreEquipmentSaveData(const TArray<TSoftObjectPtr<UDataAsset>>& SaveData);

protected:

	// HotBar 칸수. 이 값을 바꿔 UI도 따라가게
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hotbar", meta = (ClampMin = "1"))
	int32 InitialSlotCount = 1;

	// BP에서 미리 채워둘 기본 도구들 (프로토 테스트용)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hotbar")
	TArray<TObjectPtr<UMD_ToolDefinition>> DefaultTools;

private:
	/** 클라이언트 슬롯 선택을 서버 권위 상태에 반영하는 RPC */
	UFUNCTION(Server, Reliable)
	void ServerSelectSlot(int32 SlotIndex);

	/** 슬롯 배열 복제 후 UI를 갱신하는 알림 함수 */
	UFUNCTION()
	void OnRep_Slots();

	/** 선택 인덱스 복제 후 UI를 갱신하는 알림 함수 */
	UFUNCTION()
	void OnRep_CurrentSlotIndex();

	/** 상점 구매 품목 배열 복제 후 핫바 UI 갱신 알림 함수 */
	UFUNCTION()
	void OnRep_ShopItemEntries();
};
