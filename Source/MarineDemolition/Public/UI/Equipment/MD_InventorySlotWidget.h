// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/MD_CommonButtonBase.h"
#include "Styling/SlateColor.h"
#include "MD_InventorySlotWidget.generated.h"

class UBorder;
class UTextBlock;
class UMD_InventoryDragDropOperation;
class UMD_InventoryWidget;
class UMD_ItemDefinition;

// 공통 버튼 입력과 인벤토리 표시 및 슬롯 간 드래그 교환을 제공하는 클래스
UCLASS(Abstract, Blueprintable)
class MARINEDEMOLITION_API UMD_InventorySlotWidget : public UMD_CommonButtonBase
{
	GENERATED_BODY()

protected:
	virtual void NativeDestruct() override;
	virtual FReply HandleButtonDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;
	virtual void NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual void NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual bool NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

protected:
	// 드롭 가능한 카드의 외곽선 색상을 변경할 배경 테두리
	UPROPERTY(BlueprintReadOnly, Category = "MD|Inventory", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> BDR_Root;

	// 카드 상단에 표시할 1부터 시작하는 Hotbar 번호
	UPROPERTY(BlueprintReadOnly, Category = "MD|Inventory", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_SlotNumber;

	// 아이템과 함께 이동하는 보유 수량 표시
	UPROPERTY(BlueprintReadOnly, Category = "MD|Inventory", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_ToolCount;

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

	// 슬롯 연결 시 생성하고 반복 갱신에서 재사용할 번호 텍스트
	UPROPERTY(Transient)
	FText SlotNumberText;

	// 보유 수량이 달라질 때만 생성할 수량 텍스트
	UPROPERTY(Transient)
	FText ItemCountText;

	// 기본 설정 또는 실행 중 변경으로 드롭 대상에 적용할 테두리 색상
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetDropHighlightColor, Category = "MD|Inventory")
	FLinearColor DropHighlightColor = FLinearColor::Yellow;

	// 현재 카드에 드롭 대상 테두리 강조가 적용된 상태
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MD|Inventory")
	bool bIsDropHighlighted = false;

	// 드래그 강조 중에도 BP의 선택 상태에 맞춰 갱신할 복원용 외곽선 색상
	UPROPERTY(Transient, BlueprintReadWrite, Category = "MD|Inventory")
	FSlateColor DefaultOutlineColor;

	// 커서가 카드 위에 있는 동안 종료 알림을 받을 드래그 작업
	UPROPERTY(Transient)
	TWeakObjectPtr<UMD_InventoryDragDropOperation> HoveredDragOperation;

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

	// 강조 중인 카드에도 지정한 색상을 즉시 반영하고 복원 색상 유지
	UFUNCTION(BlueprintCallable, BlueprintSetter, Category = "MD|Inventory")
	void SetDropHighlightColor(FLinearColor InColor);

	// 현재 드래그의 교환 가능 여부에 맞춰 대상 테두리 갱신
	UFUNCTION(BlueprintCallable, Category = "MD|Inventory")
	bool UpdateDropHighlight(UMD_InventoryDragDropOperation* Operation);

	// 드롭과 취소 및 화면 종료 시 기존 테두리 복원
	UFUNCTION(BlueprintCallable, Category = "MD|Inventory")
	void ClearDropHighlight();

	// 기본 마우스 입력과 사용자 지정 입력이 공유하는 드래그 생성
	UFUNCTION(BlueprintCallable, Category = "MD|Inventory")
	UMD_InventoryDragDropOperation* CreateItemDragOperation();

	// 검증된 드래그 원본과 현재 슬롯의 교환 요청
	UFUNCTION(BlueprintCallable, Category = "MD|Inventory")
	bool TryDropItem(UMD_InventoryDragDropOperation* Operation);

	// 실제 보유 여부를 전달하여 BP에서 빈 슬롯 본문과 카드 디자인 갱신
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|Inventory")
	void OnSlotRefreshed(bool bHasItem);

protected:
	// 표시 내용과 텍스트 이력이 달라진 경우에만 텍스트 위젯 갱신
	void SetSlotText(UTextBlock* TextBlock, FText NewText);

	// 배경과 모서리 및 두께를 유지한 외곽선 강조 전환
	void SetDropHighlighted(bool bHighlight);

	// 드래그 완료 또는 취소 알림에 따른 테두리 정리
	UFUNCTION()
	void HandleDragEnded(UDragDropOperation* Operation);
};
