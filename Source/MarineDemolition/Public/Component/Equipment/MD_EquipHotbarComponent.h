// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MD_EquipHotbarComponent.generated.h"

struct FMD_EquipHotbarDataWrapper;
class UMD_ToolDefinition;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMDOnEquipHotbarChanged);

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

	UFUNCTION(BlueprintPure, Category = "Hotbar")
	UMD_ToolDefinition* GetCurrentTool() const;

	UFUNCTION(BlueprintPure, Category = "Hotbar")
	UMD_ToolDefinition* GetToolAtSlot(int32 SlotIndex) const;

	UFUNCTION(BlueprintPure, Category = "Hotbar")
	int32 GetSlotCount() const { return Slots.Num(); }

	UFUNCTION(BlueprintPure, Category = "Hotbar")
	int32 GetCurrentSlotIndex() const { return CurrentSlotIndex; }

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
	struct FMD_EquipHotbarDataWrapper BuildEquipmentSaveData() const;

	// 서버에서 저장된 장비 목록을 불러오는 함수
	bool RestoreEquipmentSaveData(const struct FMD_EquipHotbarDataWrapper& SaveData);

protected:

	// HotBar 칸수. 이 값을 바꿔 UI도 따라가게
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hotbar", meta = (ClampMin = "1"))
	int32 InitialSlotCount = 1;

	// BP에서 미리 채워둘 기본 도구들 (프로토 테스트용)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hotbar")
	TArray<TObjectPtr<UMD_ToolDefinition>> DefaultTools;

private:
	UFUNCTION(Server, Reliable)
	void ServerSelectSlot(int32 SlotIndex);

	UFUNCTION()
	void OnRep_Slots();

	UFUNCTION()
	void OnRep_CurrentSlotIndex();

	UPROPERTY(ReplicatedUsing = OnRep_Slots)
	TArray<TObjectPtr<UMD_ToolDefinition>> Slots;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentSlotIndex)
	int32 CurrentSlotIndex = 0;

};
