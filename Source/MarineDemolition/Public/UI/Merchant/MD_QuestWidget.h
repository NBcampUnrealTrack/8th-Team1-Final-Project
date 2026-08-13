// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/Lore/MD_LoreTypes.h"
#include "Types/Merchant/MD_MerchantTypes.h"
#include "UI/Merchant/MD_MerchantUITypes.h"

#include "MD_QuestWidget.generated.h"

class UButton;
class UPanelWidget;
class UTextBlock;
class UWidget;
class UMD_MainQuestRowWidget;
class UMD_QuestCardWidget;
class UMD_MerchantMenuWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMD_QuestBackRequest);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMD_QuestRepeatActionRequest, int32, QuestIndex);

/**
 * 퀘스트 목록 화면. 메인 / 반복 / 로어 3탭 (`04` §5.5).
 *
 * 탭마다 데이터 원천·리스트 형태·상호작용 모델이 다르다는 게 기획 확정 내용이라
 * 하나의 카드로 셋을 억지로 재사용하지 않는다. 메인은 UMD_MainQuestRowWidget,
 * 반복은 UMD_QuestCardWidget을 쓰고, 로어는 LoreComponent의 영구 수집 원장을 사용한다.
 *
 * 반복 퀘스트 수락/거절/포기 처리는 여기서 하지 않는다. 상점 위젯과 같은 원칙으로,
 * 요청만 델리게이트로 올리고 실제 서버 판정(AMD_PlayerController::ServerAcceptRepeat 등)은
 * 소유 액터(AMD_Merchant)가 맡는다.
 */
UCLASS()
class MARINEDEMOLITION_API UMD_QuestWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "MD|Quest")
	void SetOwningMerchantMenu(UMD_MerchantMenuWidget* InMenuWidget) { OwningMerchantMenu = InMenuWidget; }

	UFUNCTION(BlueprintCallable, Category = "MD|Quest")
	void SetMainQuests(const TArray<FMD_MainQuestState>& InMainQuests);

	UFUNCTION(BlueprintCallable, Category = "MD|Quest")
	void SetRepeatQuests(const TArray<FMD_RepeatQuestState>& InRepeatQuests);

	/** 로어 전용 복제 원장에서 받은 현장별 수집 정보를 코덱스 탭에 적용하는 함수 */
	UFUNCTION(BlueprintCallable, Category = "MD|Lore")
	void SetLoreProgress(const TArray<FMD_LoreSiteProgress>& InSiteProgressList);

	UFUNCTION(BlueprintCallable, Category = "MD|Quest")
	void SelectTab(EMD_QuestTab InTab);

	UFUNCTION(BlueprintPure, Category = "MD|Quest")
	EMD_QuestTab GetSelectedTab() const { return SelectedTab; }

	/** 탭 표시 + 메인/반복 목록을 모두 다시 그린다. */
	UFUNCTION(BlueprintCallable, Category = "MD|Quest")
	void RefreshDisplay();

	UPROPERTY(BlueprintAssignable, Category = "MD|Quest")
	FMD_QuestBackRequest OnBackRequested;

	UPROPERTY(BlueprintAssignable, Category = "MD|Quest")
	FMD_QuestRepeatActionRequest OnRepeatAcceptRequested;

	UPROPERTY(BlueprintAssignable, Category = "MD|Quest")
	FMD_QuestRepeatActionRequest OnRepeatDeclineRequested;

	/** 백엔드에 파기 처리가 아직 없다 (`09` 김동연 TODO 대응). 요청만 올라가며 처리는 호출부가 안내로 대신한다. */
	/** 반복 퀘스트 카드의 포기 요청을 상인 액터까지 전달하는 델리게이트 */
	UPROPERTY(BlueprintAssignable, Category = "MD|Quest")
	FMD_QuestRepeatActionRequest OnRepeatAbandonRequested;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	void RebuildMainQuestList();
	void RebuildRepeatQuestList();
	/** 전달받은 로어 수집 원장만 사용하여 코덱스 표시를 다시 구성하는 함수 */
	void RefreshLoreDisplay();
	void ApplyTabVisibility();

	UFUNCTION()
	void HandleTabMainClicked();

	UFUNCTION()
	void HandleTabRepeatClicked();

	UFUNCTION()
	void HandleTabLoreClicked();

	UFUNCTION()
	void HandleBackClicked();

	UFUNCTION()
	void HandleCardAcceptRequested(int32 QuestIndex);

	UFUNCTION()
	void HandleCardDeclineRequested(int32 QuestIndex);

	UFUNCTION()
	void HandleCardAbandonRequested(int32 QuestIndex);

	UPROPERTY()
	TObjectPtr<UMD_MerchantMenuWidget> OwningMerchantMenu;

	/** 메인퀘 Day 행으로 쓸 WBP */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Quest")
	TSubclassOf<UMD_MainQuestRowWidget> MainQuestRowClass;

	/** 반복퀘 카드로 쓸 WBP */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Quest")
	TSubclassOf<UMD_QuestCardWidget> RepeatQuestCardClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Quest")
	TArray<FMD_MainQuestState> MainQuests;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Quest")
	TArray<FMD_RepeatQuestState> RepeatQuests;

	/** 메인·반복 퀘스트와 분리된 코덱스 탭 표시용 현장별 수집 정보 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore")
	TArray<FMD_LoreSiteProgress> LoreSiteProgressList;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Quest")
	EMD_QuestTab SelectedTab = EMD_QuestTab::Main;

	// ───────────────── 탭 3종 ─────────────────

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_TabMain;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_TabRepeat;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_TabLore;

	/** 탭마다 바뀐다 — "메인 퀘스트" / "반복 퀘스트" / "기록 보관함(코덱스)" (`04` §5.5 공통 주의) */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_PanelTitle;

	/** 세 탭 다 점검(Day2/3/4) 통과와 무관한 보너스라는 안내 (`04` §5.5 공통 주의) */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_NoInspectionImpactNotice;

	// ───────────────── 탭1: 메인퀘 ─────────────────

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> PNL_MainQuestList;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_MainEmptyNotice;

	// ───────────────── 탭2: 반복퀘 ─────────────────

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> PNL_RepeatQuestList;

	/** 현장을 아직 클리어하지 않아 반복퀘가 없을 때 (`09` §4) */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_RepeatEmptyNotice;

	/** "샘플 납품 자체에는 점수도 돈도 없다. 퀘스트를 끝내야 받는다." (`04` §5) */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_RepeatDeliveryNotice;

	// ───────────────── 탭3: 로어퀘 (코덱스) ─────────────────

	/** 로어 전용 복제 원장의 수집 현황을 표시하는 코덱스 탭 패널 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> PNL_LoreTab;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_LoreEmptyNotice;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Back;
};
