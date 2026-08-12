// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "MD_EquipHotbarComponent.generated.h"

class UMD_ToolDefinition;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMDOnEquipHotbarChanged);

/** 고정 Hotbar 위치에 ToolDefinition과 보유 수량을 함께 담는 복제 항목 구조체 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_HotbarToolEntry
{
	GENERATED_BODY()

	/** Hotbar 품목의 식별과 표시 데이터를 함께 제공하는 도구 정의 */
	UPROPERTY(BlueprintReadOnly, Category = "Hotbar")
	TObjectPtr<UMD_ToolDefinition> ToolDefinition;

	/** 해당 슬롯의 ToolDefinition 보유 수량 */
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

protected:
	/** 에디터에서 지정하는 최소 Hotbar 슬롯 수 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hotbar", meta = (ClampMin = "1"))
	int32 ConfiguredSlotCount = 4;

	// BP에서 미리 채워둘 기본 도구들 (프로토 테스트용)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hotbar")
	TArray<TObjectPtr<UMD_ToolDefinition>> DefaultTools;

private:
	/** 슬롯 위치와 ToolDefinition 및 수량을 단일 상태로 유지하는 고정 길이 복제 배열 */
	UPROPERTY(ReplicatedUsing = OnRep_ToolEntries)
	TArray<FMD_HotbarToolEntry> ToolEntries;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentSlotIndex)
	int32 CurrentSlotIndex = 0;

public:
	UFUNCTION(BlueprintPure, Category = "Hotbar")
	UMD_ToolDefinition* GetCurrentTool() const;

	UFUNCTION(BlueprintPure, Category = "Hotbar")
	UMD_ToolDefinition* GetToolAtSlot(int32 SlotIndex) const;

	UFUNCTION(BlueprintPure, Category = "Hotbar")
	int32 GetSlotCount() const { return ToolEntries.Num(); }

	UFUNCTION(BlueprintPure, Category = "Hotbar")
	int32 GetCurrentSlotIndex() const { return CurrentSlotIndex; }

	/** ToolDefinition을 기준으로 Hotbar가 보관하는 수량을 반환하는 함수 */
	UFUNCTION(BlueprintPure, Category = "Hotbar")
	int32 GetToolCount(UMD_ToolDefinition* ToolDefinition) const;

	/** 동일 정의 수량 증가, 동일 GameplayTag 교체, 빈 항목 추가 순서로 반영 가능성을 확인하는 통합 함수 */
	UFUNCTION(BlueprintPure, Category = "Hotbar")
	bool CanAddTool(UMD_ToolDefinition* ToolDefinition) const;

	/** 서버가 검증한 ToolDefinition을 빈 슬롯에 추가하거나 기존 수량을 증가시키는 함수 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Hotbar")
	bool AddTool(UMD_ToolDefinition* ToolDefinition, int32 Amount = 1);

	/** 동일 GameplayTag 슬롯을 교체하고 일치 슬롯이 없으면 빈 장비 슬롯에 구매 장비를 배치하는 함수 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Hotbar")
	bool ReplaceTool(UMD_ToolDefinition* Tool);

	/** 소모품 사용 시 ToolDefinition 수량을 차감하고 소진된 슬롯을 비우는 함수 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Hotbar")
	bool TryConsumeTool(UMD_ToolDefinition* ToolDefinition, int32 Amount = 1);

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

private:
	/** 설정 슬롯 수와 기본 도구 수 중 큰 값으로 런타임 고정 슬롯 수를 결정하는 함수 */
	int32 ResolveSlotCount() const;

	/** 동일 정의, 동일 태그, 빈 항목 우선순위로 반영할 고정 항목 위치를 반환하는 함수 */
	bool FindAvailableToolEntry(UMD_ToolDefinition* ToolDefinition, int32& OutEntryIndex) const;

	/** 클라이언트 슬롯 선택을 서버 권위 상태에 반영하는 RPC */
	UFUNCTION(Server, Reliable)
	void ServerSelectSlot(int32 SlotIndex);

	/** ToolEntries 복제 후 Hotbar UI를 갱신하는 알림 함수 */
	UFUNCTION()
	void OnRep_ToolEntries();

	/** 선택 인덱스 복제 후 UI를 갱신하는 알림 함수 */
	UFUNCTION()
	void OnRep_CurrentSlotIndex();
};
