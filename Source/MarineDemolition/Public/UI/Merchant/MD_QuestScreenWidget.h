// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Types/Lore/MD_LoreTypes.h"
#include "UI/Foundation/MD_CommonGameMenuWidget.h"
#include "UI/Merchant/MD_QuestWidget.h"

#include "MD_QuestScreenWidget.generated.h"

class UMD_CommonButtonBase;
class UMD_LoreCatalog;
class UMD_LoreQuestTabWidget;
class UMD_MerchantMenuWidget;

/** 퀘스트와 로어 탭 전환 및 화면 수명을 관리하는 상인 퀘스트 화면 */
UCLASS()
class MARINEDEMOLITION_API UMD_QuestScreenWidget : public UMD_CommonGameMenuWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;

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
	// 퀘스트 화면 뒤에 남아 있는 상인 메뉴 표시 상태를 관리하기 위한 참조
	UPROPERTY()
	TObjectPtr<UMD_MerchantMenuWidget> OwningMerchantMenu;

	// 화면 생성 전후에 동일한 메인 퀘스트 데이터를 유지하기 위한 캐시
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	TArray<FMD_MainQuestState> MainQuests;

	// 화면 생성 전후에 동일한 반복 퀘스트 데이터를 유지하기 위한 캐시
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	TArray<FMD_RepeatQuestState> RepeatQuests;

	// 로어 탭에 전달할 현재 현장의 정적 카탈로그
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore")
	TObjectPtr<UMD_LoreCatalog> LoreCatalog;

	// 선택한 사이트의 이름을 위젯 재구성 이후에도 유지하기 위한 보관
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore")
	FText LoreSiteName;

	// 로어 탭에 전달할 플레이어의 전역 수집 원장
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore")
	TArray<FPrimaryAssetId> DiscoveredLoreAssetIds;

	// 현재 로어 탭을 표시 중인지 저장하는 선택 상태
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	bool bShowingLoreTab = false;

	// 퀘스트 탭 선택과 화면 전환에 사용하는 공통 버튼
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidget))
	TObjectPtr<UMD_CommonButtonBase> Btn_QuestTab;

	// 로어 탭 선택과 화면 전환에 사용하는 공통 버튼
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UMD_CommonButtonBase> Btn_LoreTab;

	// 퀘스트 목록과 상세 정보를 표시하는 디자이너 하위 위젯
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidget))
	TObjectPtr<UMD_QuestWidget> WBP_MD_QuestTab;

	// 수집한 로어 목록과 상세 정보를 표시하는 디자이너 하위 위젯
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore", meta = (BindWidgetOptional))
	TObjectPtr<UMD_LoreQuestTabWidget> WBP_MD_LoreTab;

public:
	// 화면 뒤에 표시할 상인 메뉴 참조를 적용하는 함수
	UFUNCTION(BlueprintCallable, Category = "MD|Quest")
	void SetOwningMerchantMenu(UMD_MerchantMenuWidget* InMenuWidget);

	// 퀘스트 탭에 메인 퀘스트 상태를 전달하는 함수
	UFUNCTION(BlueprintCallable, Category = "MD|Quest")
	void SetMainQuests(const TArray<FMD_MainQuestState>& InMainQuests);

	// 퀘스트 탭에 반복 퀘스트 상태를 전달하는 함수
	UFUNCTION(BlueprintCallable, Category = "MD|Quest")
	void SetRepeatQuests(const TArray<FMD_RepeatQuestState>& InRepeatQuests);

	// 현재 현장의 카탈로그와 로어 수집 원장을 함께 적용하는 함수
	UFUNCTION(BlueprintCallable, Category = "MD|Lore")
	void SetLoreData(UMD_LoreCatalog* InLoreCatalog, const TArray<FPrimaryAssetId>& InDiscoveredLoreAssetIds, FText InSiteName);

	// 퀘스트 하위 위젯과 로어 하위 위젯을 현재 캐시로 갱신하는 함수
	UFUNCTION(BlueprintCallable, Category = "MD|Quest")
	void RefreshDisplay();

	// 퀘스트 목록 및 상세 탭을 표시하는 함수
	UFUNCTION(BlueprintCallable, Category = "MD|Quest")
	void ShowQuestTab();

	// 로어 수집 목록 및 상세 탭을 표시하는 함수
	UFUNCTION(BlueprintCallable, Category = "MD|Quest")
	void ShowLoreTab();

protected:
	// 현재 탭 선택 상태를 두 하위 위젯과 상단 버튼에 적용하는 함수
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
