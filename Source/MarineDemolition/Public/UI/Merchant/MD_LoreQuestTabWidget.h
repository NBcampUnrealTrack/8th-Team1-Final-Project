// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/Lore/MD_LoreTypes.h"
#include "MD_LoreQuestTabWidget.generated.h"

class UProgressBar;
class UScrollBox;
class UTextBlock;
class UMD_LoreCardWidget;
class UMD_LoreCatalog;

// 현장별 로어 수집 목록과 선택 기록물의 상세 내용을 표시하는 탭 클래스
UCLASS()
class MARINEDEMOLITION_API UMD_LoreQuestTabWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

protected:
	// 정적 정의와 복제 진행 상태를 결합하기 위한 현재 로어 카탈로그
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore")
	TObjectPtr<UMD_LoreCatalog> LoreCatalog;

	// 스크롤 목록에 반복 생성할 로어 카드 위젯 클래스
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Lore")
	TSubclassOf<UMD_LoreCardWidget> LoreCardClass;

	// 현재 카탈로그의 발견 상태를 카드와 상세 화면에 적용하기 위한 전역 로어 원장
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore")
	TArray<FPrimaryAssetId> DiscoveredLoreAssetIds;

	// 카드 선택과 상세 정보 표시를 연결하기 위한 카탈로그 배열 인덱스
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore")
	int32 SelectedLoreIndex = INDEX_NONE;

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

public:
	// 카탈로그와 복제된 전역 로어 원장을 목록 및 상세 화면에 적용하는 함수
	void SetLoreData(UMD_LoreCatalog* InLoreCatalog, const TArray<FPrimaryAssetId>& InDiscoveredLoreAssetIds);

protected:
	// 카탈로그 순서대로 카드 위젯을 다시 생성하는 함수
	void RebuildLoreList();

	// 현장 이름과 수집 수량 및 진행 막대를 갱신하는 함수
	void RefreshLoreHeader();

	// 선택된 공개 상태에 따라 제목과 본문을 갱신하는 함수
	void RefreshLoreDetails();

	// 카드 선택 인덱스를 상세 화면에 반영하는 콜백
	UFUNCTION()
	void HandleLoreCardSelected(int32 LoreIndex);
};
