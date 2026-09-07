// Fill out your copyright notice in the Description page of Project Settings.

// 중복 헤더 포함 방지
#pragma once

// 기본 엔진 타입 사용
#include "CoreMinimal.h"
// 탭 위젯 기반 클래스 사용
#include "Blueprint/UserWidget.h"
// 로어 식별자 타입 사용
#include "Types/Lore/MD_LoreTypes.h"
// 리플렉션 생성 코드 포함
#include "MD_LoreQuestTabWidget.generated.h"

// 수집 진행률 표시 타입 전방 선언
class UProgressBar;
// 카드 목록 컨테이너 타입 전방 선언
class UScrollBox;
// 현장명과 진행 수량 표시 타입 전방 선언
class UTextBlock;
// 반복 생성할 로어 카드 타입 전방 선언
class UMD_LoreCardWidget;
// 현장별 로어 목록 타입 전방 선언
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

	// 현장 이름 표시용 텍스트 바인딩
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_SiteName;

	// 전체 기록물 대비 획득 수량 표시용 텍스트 바인딩
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_LoreProgress;

	// 로어 수집 비율 표시용 진행 막대 바인딩
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore", meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> PB_LoreProgress;

	// 로어 카드 목록 표시용 스크롤 영역 바인딩
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore", meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> SL_LoreList;

public:
	// 카탈로그와 복제된 획득 원장을 목록과 진행률에 적용하는 함수
	void SetLoreData(UMD_LoreCatalog* InLoreCatalog, const TArray<FPrimaryAssetId>& InDiscoveredLoreAssetIds, FText InSiteName);

protected:
	// 카탈로그 순서대로 획득 여부를 표시하는 카드 생성
	void RebuildLoreList();

	// 현장 이름과 획득 수량 및 진행 막대 갱신
	void RefreshLoreHeader();
};
