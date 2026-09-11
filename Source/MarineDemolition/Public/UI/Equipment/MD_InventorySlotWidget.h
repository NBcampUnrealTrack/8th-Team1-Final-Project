// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MD_InventorySlotWidget.generated.h"

class UHorizontalBox;
class UImage;
class UTextBlock;
class UMD_InventoryDragDropOperation;
class UMD_InventoryWidget;
class UMD_ItemDefinition;

// 가로 인벤토리 카드의 표시 데이터와 슬롯 간 드래그 교환을 제공하는 위젯 부모 클래스
UCLASS(Abstract, Blueprintable)
class MARINEDEMOLITION_API UMD_InventorySlotWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

protected:
	// 카드 상단에 표시할 1부터 시작하는 Hotbar 번호
	UPROPERTY(BlueprintReadOnly, Category = "MD|Inventory", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_SlotNumber;

	// 카드 중앙의 아이템 아이콘 표시
	UPROPERTY(BlueprintReadOnly, Category = "MD|Inventory", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_ToolIcon;

	// 카드 하단의 아이템 이름 표시
	UPROPERTY(BlueprintReadOnly, Category = "MD|Inventory", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_ToolName;

	// 아이템과 함께 이동하는 보유 수량 표시
	UPROPERTY(BlueprintReadOnly, Category = "MD|Inventory", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_ToolCount;

	// 빈 슬롯에서 보유 수량 문구와 숫자를 함께 숨길 영역
	UPROPERTY(BlueprintReadOnly, Category = "MD|Inventory", meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> HB_ToolCount;

	// 표시 조회와 교환 요청을 전달할 인벤토리 화면
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MD|Inventory")
	TObjectPtr<UMD_InventoryWidget> OwningInventory;

	// 빈칸에도 유지되는 Hotbar 고정 위치
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MD|Inventory")
	int32 SlotIndex = INDEX_NONE;

	// 카드 상세 표시에 사용할 현재 아이템 정의
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MD|Inventory")
	TObjectPtr<UMD_ItemDefinition> ItemDefinition;

	// 수량 표시와 빈 슬롯 표현에 사용할 현재 보유 수량
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MD|Inventory")
	int32 ItemCount = 0;

public:
	// 가로 목록에서 할당한 화면과 고정 인덱스 연결
	void InitializeSlot(UMD_InventoryWidget* Inventory, int32 InSlotIndex);

	// 기본 표시와 사용자 지정 디자인에 실제 보관 상태 반영
	UFUNCTION(BlueprintCallable, Category = "MD|Inventory")
	void RefreshSlot();

	// 드롭 위치 확인에 사용할 슬롯 인덱스 반환
	UFUNCTION(BlueprintPure, Category = "MD|Inventory")
	FORCEINLINE int32 GetSlotIndex() { return SlotIndex; }

	// 현재 화면에서 해당 슬롯의 아이템을 드래그할 수 있는지 검사
	UFUNCTION(BlueprintPure, Category = "MD|Inventory")
	bool CanDragItem();

	// 드롭 강조 표시와 실제 교환의 동일 조건 검사
	UFUNCTION(BlueprintPure, Category = "MD|Inventory")
	bool CanAcceptDrop(UMD_InventoryDragDropOperation* Operation);

	// 기본 마우스 입력과 사용자 지정 입력이 공유하는 드래그 생성
	UFUNCTION(BlueprintCallable, Category = "MD|Inventory")
	UMD_InventoryDragDropOperation* CreateItemDragOperation();

	// 검증된 드래그 원본과 현재 슬롯의 교환 요청
	UFUNCTION(BlueprintCallable, Category = "MD|Inventory")
	bool TryDropItem(UMD_InventoryDragDropOperation* Operation);

	// 아이템 상태에 따른 카드 배경 등의 표시 갱신 이벤트
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|Inventory")
	void OnSlotRefreshed();

	// 기본 아이콘을 대체할 드래그 미리보기 위젯
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|Inventory")
	UWidget* CreateItemDragVisual();
};
