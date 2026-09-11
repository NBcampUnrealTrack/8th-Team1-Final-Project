// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Margin.h"
#include "UI/Foundation/MD_CommonGameMenuWidget.h"
#include "MD_InventoryWidget.generated.h"

class APlayerController;
class APawn;
class UHorizontalBox;
class UMD_EquipHotbarComponent;
class UMD_InventorySlotWidget;
class UMD_ItemDefinition;

// Hotbar의 보관 상태 표시와 슬롯 교환을 제공하는 인벤토리 부모 클래스
UCLASS(Abstract, Blueprintable)
class MARINEDEMOLITION_API UMD_InventoryWidget : public UMD_CommonGameMenuWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	virtual void NativeDestruct() override;

protected:
	// Hotbar 순서대로 아이템 카드를 가로 배치할 컨테이너
	UPROPERTY(BlueprintReadOnly, Category = "MD|Inventory", meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> HB_Slots;

	// 번호와 아이콘 및 이름과 수량을 구성할 슬롯 위젯 클래스
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Inventory")
	TSubclassOf<UMD_InventorySlotWidget> SlotWidgetClass;

	// 자동 생성 카드에 적용할 좌우 및 상하 여백
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Inventory")
	FMargin SlotPadding = FMargin(64.0f, 8.0f);

	// 동일 슬롯의 드래그 대상을 유지할 카드 인스턴스 목록
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MD|Inventory")
	TArray<TObjectPtr<UMD_InventorySlotWidget>> SlotWidgets;

	// 화면 재활성화와 Pawn 교체 이전의 드래그를 구분할 세션 식별자
	UPROPERTY(Transient)
	FGuid DragSessionId;

	// 화면 조회와 재배치 요청이 공유하는 실제 Hotbar
	UPROPERTY(Transient)
	TObjectPtr<UMD_EquipHotbarComponent> BoundHotbar;

	// Pawn 교체 알림을 해제할 기존 소유 컨트롤러
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> BoundController;

public:
	// 연결된 Hotbar 반환
	UFUNCTION(BlueprintPure, Category = "MD|Inventory")
	FORCEINLINE UMD_EquipHotbarComponent* GetHotbarComponent() { return BoundHotbar; }

	// 현재 화면 연결에서 시작한 드래그의 세션 식별자 반환
	FORCEINLINE FGuid GetDragSessionId() { return DragSessionId; }

	// 빈칸을 포함한 인벤토리 슬롯 수 조회
	UFUNCTION(BlueprintPure, Category = "MD|Inventory")
	int32 GetSlotCount();

	// 현재 장착한 아이템의 Hotbar 위치 조회
	UFUNCTION(BlueprintPure, Category = "MD|Inventory")
	int32 GetCurrentSlotIndex();

	// 슬롯의 이름과 아이콘을 제공하는 아이템 정의 조회
	UFUNCTION(BlueprintPure, Category = "MD|Inventory")
	UMD_ItemDefinition* GetItemAtSlot(int32 SlotIndex);

	// 슬롯별 보유 수량 조회
	UFUNCTION(BlueprintPure, Category = "MD|Inventory")
	int32 GetItemCountAtSlot(int32 SlotIndex);

	// 드롭 대상의 슬롯 조합 검사
	UFUNCTION(BlueprintPure, Category = "MD|Inventory")
	bool CanSwapSlots(int32 FromSlotIndex, int32 ToSlotIndex);

	// 0부터 시작하는 슬롯 인덱스로 빈칸 이동 또는 위치 교환, 클라이언트의 true는 서버 요청 전송 성공
	UFUNCTION(BlueprintCallable, Category = "MD|Inventory")
	bool SwapSlots(int32 FromSlotIndex, int32 ToSlotIndex);

	// 화면 활성화와 Pawn 교체 및 Hotbar 변경 후 슬롯 표시 갱신 이벤트
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|Inventory")
	void OnInventoryRefreshed();

protected:
	// 고정 슬롯 카드를 가로 생성하고 기존 인스턴스의 표시 갱신
	void RefreshSlots();
	// 현재 소유 Pawn의 Hotbar로 알림 연결 교체
	void BindToPawn(APawn* NewPawn);
	// 기존 Hotbar의 변경 알림 해제
	void UnbindHotbar();
	// 화면 종료 시 Hotbar와 컨트롤러 알림 연결 정리
	void UnbindInventory();

	// 리스폰과 빙의 변경 후 새 인벤토리 연결
	UFUNCTION()
	void HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn);

	// 실제 보관 상태와 선택 위치의 변경을 화면에 전달
	UFUNCTION()
	void HandleHotbarChanged();
};
