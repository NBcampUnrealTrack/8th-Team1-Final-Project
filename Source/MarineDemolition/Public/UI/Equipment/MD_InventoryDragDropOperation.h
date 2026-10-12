// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/DragDropOperation.h"
#include "MD_InventoryDragDropOperation.generated.h"

class UMD_InventoryWidget;
class UMD_ItemDefinition;

// 드래그 시작 시의 화면과 보관 공간 및 아이템을 드롭 시점까지 식별하는 작업 데이터
UCLASS()
class MARINEDEMOLITION_API UMD_InventoryDragDropOperation : public UDragDropOperation
{
	GENERATED_BODY()

public:
	// 같은 화면을 다시 열었을 때 이전 드래그를 구분할 세션 식별자
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MD|Inventory")
	FGuid SourceDragSessionId;

	// 다른 인벤토리 화면으로의 잘못된 드롭 차단용 출처
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MD|Inventory")
	TWeakObjectPtr<UMD_InventoryWidget> SourceInventory;

	// 드래그 중 교체되거나 소진된 원본 아이템의 확인 기준
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MD|Inventory")
	TObjectPtr<UMD_ItemDefinition> SourceItem;

	// 드래그를 시작한 Hotbar의 고정 슬롯 인덱스
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MD|Inventory")
	int32 SourceSlotIndex = INDEX_NONE;
};
