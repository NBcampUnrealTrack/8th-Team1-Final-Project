// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Data/Shop/MD_ShopCatalog.h"
#include "UI/MD_CommonButtonBase.h"
#include "MD_ShopItemCardWidget.generated.h"

class UTextBlock;

/** 상점 목록에서 품목의 이름, 아이콘과 가격을 표시하는 선택 카드 */
UCLASS()
class MARINEDEMOLITION_API UMD_ShopItemCardWidget : public UMD_CommonButtonBase
{
	GENERATED_BODY()

public:
	// 카드에 표시할 품목을 설정
	UFUNCTION(BlueprintCallable, Category = "MD|Shop")
	void SetItem(const FMD_ShopItem& InItem);

	// 카드에 저장된 품목을 반환
	UFUNCTION(BlueprintPure, Category = "MD|Shop")
	const FMD_ShopItem& GetItem() const { return Item; }

	// 저장된 품목 데이터를 현재 카드 UI에 반영
	UFUNCTION(BlueprintCallable, Category = "MD|Shop")
	void RefreshDisplay();

protected:
	/** 디자이너가 에디터 미리보기에서 값을 넣어볼 수 있도록 노출한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Shop")
	FMD_ShopItem Item;

	// 카드 하단의 크레딧 가격 표시
	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_CostCredit;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	// 가격 숫자와 분리된 크레딧 기호 및 개당 단위 표시
	TObjectPtr<UTextBlock> Txt_Credit_Suffix;
};
