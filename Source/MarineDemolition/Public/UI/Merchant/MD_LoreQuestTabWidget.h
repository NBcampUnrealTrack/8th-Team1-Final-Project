// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/PrimaryAssetId.h"
#include "Blueprint/UserWidget.h"
#include "MD_LoreQuestTabWidget.generated.h"

class UMD_FilledSlider;
class UWrapBox;
class UTextBlock;
class UMD_LoreCardWidget;
class UMD_LoreCatalog;

// 현장별 로어의 획득 여부와 수집 진행률을 표시하는 탭 클래스
UCLASS()
class MARINEDEMOLITION_API UMD_LoreQuestTabWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

protected:
	// 현재 표시할 현장의 정적 목록 보관
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore")
	TObjectPtr<UMD_LoreCatalog> LoreCatalog;

	// 선택한 사이트의 이름을 위젯 재구성 이후에도 유지하기 위한 보관
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore")
	FText LoreSiteName;

	// 스크롤 목록에 반복 생성할 카드 클래스 설정
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Lore")
	TSubclassOf<UMD_LoreCardWidget> LoreCardClass;

	// 카드별 획득 여부 판단을 위한 전역 획득 원장 보관
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore")
	TArray<FPrimaryAssetId> DiscoveredLoreAssetIds;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore", meta = (BindWidgetOptional))
	// 현재 구성의 현장 이름 표시
	TObjectPtr<UTextBlock> Txt_Location;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore", meta = (BindWidgetOptional))
	// 별도 수집 접두사 옆에 표시할 획득 수량과 전체 수량
	TObjectPtr<UTextBlock> Txt_Progress;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore", meta = (BindWidgetOptional))
	// 획득 수량과 전체 수량으로 채움 길이를 표시하는 슬라이더
	TObjectPtr<UMD_FilledSlider> Slider_Progress;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore", meta = (BindWidgetOptional))
	// 기존 스크롤 구조 안에서 카드만 배치하는 WrapBox
	TObjectPtr<UWrapBox> WB_LoreList;

public:
	UFUNCTION(BlueprintCallable, Category = "MD|Lore")
	// 카탈로그와 복제된 획득 원장을 목록과 진행률에 적용
	void SetLoreData(UMD_LoreCatalog* InLoreCatalog, const TArray<FPrimaryAssetId>& InDiscoveredLoreAssetIds, FText InSiteName);

protected:
	// 목록과 진행률을 한 번 집계하고 필요한 카드 및 헤더만 갱신
	void RefreshDisplay(bool bUpdateCards);
};
