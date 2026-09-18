// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MD_EquipHotbarWidget.generated.h"

class UMD_EquipHotbarComponent;
class UMD_ItemDefinition;

UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_EquipHotbarWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

protected:
	// 월드 전환 시 컨트롤러 교체 알림을 제공하는 로컬 플레이어
	UPROPERTY(Transient)
	TWeakObjectPtr<ULocalPlayer> BoundLocalPlayer;

	// 소유 정보 변경 후에도 기존 Pawn 알림을 정확히 해제할 컨트롤러
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> BoundController;

	// UMG의 컨텍스트 갱신과 독립적으로 해제할 컨트롤러 변경 구독
	FDelegateHandle PlayerControllerChangedHandle;

public:
	UFUNCTION(BlueprintImplementableEvent, Category ="Hotbar")
	void OnHotbarRefreshed();

	// 현재 선택 위치의 표시 동기화이며 유효한 선택이 없으면 INDEX_NONE 전달
	UFUNCTION(BlueprintImplementableEvent, Category = "Hotbar")
	void OnHotbarSelectionChanged(int32 SelectedSlotIndex);

	UFUNCTION(BlueprintPure, Category = "Hotbar")
	int32 GetSlotCount() const;

	UFUNCTION(BlueprintPure, Category = "Hotbar")
	int32 GetCurrentSlotIndex() const;

	// 도구와 소모품 등 모든 슬롯 아이템 제공
	UFUNCTION(BlueprintPure, Category = "Hotbar")
	UMD_ItemDefinition* GetItemAtSlot(int32 SlotIndex);

	// 슬롯의 현재 보관 수량 제공
	UFUNCTION(BlueprintPure, Category = "Hotbar")
	int32 GetItemCountAtSlot(int32 SlotIndex);
private:

	UFUNCTION()
	void HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn);

	UFUNCTION()
	void HandleHotbarContentsChanged();

	void BindToPawn(APawn* NewPawn);

	void UnbindHotbar();

	// 유지된 위젯이 이전 월드의 장비 컴포넌트 수명을 늘리지 않는 참조
	UPROPERTY(Transient)
	TWeakObjectPtr<UMD_EquipHotbarComponent> BoundHotbar;

protected:
	// 기존 위젯을 유지하는 월드 전환의 소유 정보 및 장비 연결 갱신
	void HandlePlayerControllerChanged(APlayerController* NewController);

	// 이전 Pawn 알림 해제 후 현재 컨트롤러와 장비 연결
	void BindToController(APlayerController* NewController);

	// 위젯 재구성 및 종료 시 로컬 플레이어와 컨트롤러 구독 정리
	void UnbindPlayer();

	// 슬롯 내용 갱신과 구분한 선택 변경 알림 처리
	UFUNCTION()
	void HandleHotbarSelectionChanged();

	// 유지된 컨트롤러의 실제 월드와 위젯 조회 컨텍스트 동기화
	void RefreshPlayerContext();
};
