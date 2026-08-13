// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "MD_LoreCardWidget.generated.h"

class UTextBlock;

// 로어 기록물 한 건의 번호와 제목 및 수집 상태를 표시하는 카드 클래스
UCLASS()
class MARINEDEMOLITION_API UMD_LoreCardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

protected:
	// 목록에서 기록물 순서를 식별하기 위한 번호 텍스트 참조
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore", meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> TXT_LoreNumber;

	// 목록에서 기록물의 표시 이름을 제공하기 위한 제목 텍스트 참조
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore", meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> TXT_LoreTitle;

	// 해독 여부와 발견 여부를 구분하기 위한 상태 텍스트 참조
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore", meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> TXT_LoreState;
};
