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
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore", meta = (BindWidgetOptional))
	// 현재 카드 구성의 카탈로그 순서 표시
	TObjectPtr<UTextBlock> Txt_Index;

public:
	UFUNCTION(BlueprintCallable, Category = "MD|Lore")
	// 카탈로그 순서와 공개 가능한 제목을 적용하고 획득 연출 갱신
	void SetLoreEntry(int32 InLoreIndex, FText EntryName, bool bIsAcquired);

	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent, Category = "MD|Lore", meta = (ForceAsFunction))
	// 기존 Blueprint 함수에서 획득 상태 문구와 완료 아이콘 및 이미지 선택
	void SetLoreIsFound(bool bNewIsFound);
};
