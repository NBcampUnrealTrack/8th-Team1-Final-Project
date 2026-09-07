// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/MD_CommonButtonBase.h"
#include "MD_LoreCardWidget.generated.h"

class UTextBlock;

// 기록물의 번호와 제목 및 획득 여부를 표시하는 공통 버튼 클래스
UCLASS()
class MARINEDEMOLITION_API UMD_LoreCardWidget : public UMD_CommonButtonBase
{
	GENERATED_BODY()

protected:
	// 획득 상태 문구에 사용할 에디터 지정 색상
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FLinearColor AcquiredStateTextColor = FLinearColor::White;

	// 미획득 상태 문구에 사용할 에디터 지정 색상
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FLinearColor UnacquiredStateTextColor = FLinearColor::White;

	// 카탈로그 순서 표시용 텍스트 바인딩
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_LoreNumber;

	// 획득한 기록물의 이름 표시용 텍스트 바인딩
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_LoreTitle;

	// 획득 여부 표시용 텍스트 바인딩
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_LoreState;

public:
	// 카탈로그 순서와 이름 및 획득 여부를 카드에 적용하는 함수
	void SetLoreEntry(int32 InLoreIndex, FText EntryName, bool bIsAcquired);
};
