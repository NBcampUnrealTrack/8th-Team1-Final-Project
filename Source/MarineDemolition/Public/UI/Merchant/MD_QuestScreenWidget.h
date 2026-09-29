// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/PrimaryAssetId.h"
#include "UI/Foundation/MD_CommonGameMenuWidget.h"
#include "UI/Merchant/MD_QuestWidget.h"

#include "MD_QuestScreenWidget.generated.h"

class UMD_CommonButtonBase;
class UWidgetSwitcher;
class UMD_LoreCatalog;
class UMD_LoreQuestTabWidget;

/** 퀘스트와 로어 탭 전환 및 화면 수명을 관리하는 상인 퀘스트 화면 */
UCLASS()
class MARINEDEMOLITION_API UMD_QuestScreenWidget : public UMD_CommonGameMenuWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

public:
	// 퀘스트 탭의 반복 퀘스트 수락 또는 보상 요청 전달 이벤트
	UPROPERTY(BlueprintAssignable, Category = "MD|Quest")
	FMD_QuestRepeatActionRequest OnRepeatAcceptRequested;

	// 퀘스트 탭의 반복 퀘스트 거절 요청 전달 이벤트
	UPROPERTY(BlueprintAssignable, Category = "MD|Quest")
	FMD_QuestRepeatActionRequest OnRepeatDeclineRequested;

	// 퀘스트 탭의 반복 퀘스트 포기 요청 전달 이벤트
	UPROPERTY(BlueprintAssignable, Category = "MD|Quest")
	FMD_QuestRepeatActionRequest OnRepeatAbandonRequested;

protected:
	// 현재 로어 탭을 표시 중인지 저장하는 선택 상태
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	bool bShowingLoreTab = false;

	// 현재 디자이너의 퀘스트 선택 버튼
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UMD_CommonButtonBase> Btn_QuestTab_1;

	// 현재 디자이너의 기록 선택 버튼
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UMD_CommonButtonBase> Btn_QuestTab_2;

	// 퀘스트 목록과 상세 정보를 표시하는 디자이너 하위 위젯
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UMD_QuestWidget> WBP_MD_QuestTab;

	// 수집한 로어 목록과 상세 정보를 표시하는 디자이너 하위 위젯
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore", meta = (BindWidgetOptional))
	TObjectPtr<UMD_LoreQuestTabWidget> WBP_MD_LoreTab;

	// 선택 탭에 맞는 하위 화면을 표시하는 디자이너 전환 영역
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UWidgetSwitcher> WS_Contents;

public:
	// 퀘스트 탭에 메인 퀘스트 상태를 전달하는 함수
	UFUNCTION(BlueprintCallable, Category = "MD|Quest")
	void SetMainQuests(const TArray<FMD_MainQuestState>& InMainQuests);

	// 퀘스트 탭에 반복 퀘스트 상태를 전달하는 함수
	UFUNCTION(BlueprintCallable, Category = "MD|Quest")
	void SetRepeatQuests(const TArray<FMD_RepeatQuestState>& InRepeatQuests);

	// 현재 현장의 카탈로그와 로어 수집 원장을 함께 적용하는 함수
	UFUNCTION(BlueprintCallable, Category = "MD|Lore")
	void SetLoreData(UMD_LoreCatalog* InLoreCatalog, const TArray<FPrimaryAssetId>& InDiscoveredLoreAssetIds, FText InSiteName);

	// 퀘스트 목록 및 상세 탭을 표시하는 함수
	UFUNCTION(BlueprintCallable, Category = "MD|Quest")
	void ShowQuestTab();

	// 로어 수집 목록 및 상세 탭을 표시하는 함수
	UFUNCTION(BlueprintCallable, Category = "MD|Quest")
	void ShowLoreTab();

protected:
	// 현재 탭의 표시 페이지와 버튼 선택 상태를 동기화하는 함수
	void ApplySelectedTab();

	// 퀘스트 탭의 수락 또는 보상 요청을 화면 외부로 전달하는 함수
	UFUNCTION()
	void HandleRepeatAcceptRequested(int32 QuestIndex);

	// 퀘스트 탭의 거절 요청을 화면 외부로 전달하는 함수
	UFUNCTION()
	void HandleRepeatDeclineRequested(int32 QuestIndex);

	// 퀘스트 탭의 포기 요청을 화면 외부로 전달하는 함수
	UFUNCTION()
	void HandleRepeatAbandonRequested(int32 QuestIndex);
};
