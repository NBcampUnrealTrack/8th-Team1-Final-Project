// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MD_LoreQuestTabWidget.generated.h"

class UProgressBar;
class UScrollBox;
class UTextBlock;

// 현장별 로어 수집 목록과 선택 기록물의 상세 내용을 표시하는 탭 클래스
UCLASS()
class MARINEDEMOLITION_API UMD_LoreQuestTabWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

protected:
	// 현재 열람 중인 현장을 식별하기 위한 현장 이름 텍스트 참조
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore", meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> TXT_SiteName;

	// 전체 기록물 대비 수집 완료 수량을 표시하기 위한 진행 텍스트 참조
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore", meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> TXT_LoreProgress;

	// 로어 수집 비율을 시각적으로 표시하기 위한 진행 막대 참조
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore", meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UProgressBar> PB_LoreProgress;

	// 기록물 카드를 세로 목록으로 제공하기 위한 스크롤 영역 참조
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore", meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UScrollBox> SL_LoreList;

	// 선택된 기록물의 이름을 강조하기 위한 상세 제목 텍스트 참조
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore", meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> TXT_DetailTitle;

	// 선택된 기록물의 번호와 발견 위치를 안내하기 위한 상세 정보 텍스트 참조
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore", meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> TXT_DetailMeta;

	// 선택된 기록물의 로어 내용을 출력하기 위한 상세 본문 텍스트 참조
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore", meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> TXT_DetailBody;

	// 선택된 기록물의 수집 보상을 안내하기 위한 보상 텍스트 참조
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore", meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> TXT_DetailReward;
};
