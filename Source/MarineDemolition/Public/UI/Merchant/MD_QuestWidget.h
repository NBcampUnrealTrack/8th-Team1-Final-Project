// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "Types/Merchant/MD_MerchantTypes.h"

#include "MD_QuestWidget.generated.h"

class UImage;
class UCommonButtonGroupBase;
class UScrollBox;
class UTextBlock;
class UMD_CommonButtonBase;
class UMD_QuestCardWidget;

// 반복 퀘스트 액션과 원본 배열 인덱스를 상위 화면에 전달하는 이벤트
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMD_QuestRepeatActionRequest, int32, QuestIndex);

/** 메인과 반복 퀘스트 목록 및 선택된 퀘스트 상세 정보를 표시하는 탭 */
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

	/** 현재 현장의 카탈로그와 로어 수집 원장을 코덱스 탭에 함께 적용하는 함수 */
	UFUNCTION(BlueprintCallable, Category = "MD|Lore")
	void SetLoreData(UMD_LoreCatalog* InLoreCatalog, const TArray<FPrimaryAssetId>& InDiscoveredLoreAssetIds);

	UFUNCTION(BlueprintCallable, Category = "MD|Quest")
	void SelectTab(EMD_QuestTab InTab);

	UFUNCTION(BlueprintPure, Category = "MD|Quest")
	FORCEINLINE EMD_QuestTab GetSelectedTab() const { return SelectedTab; }

	/** 탭 표시 + 메인/반복 목록을 모두 다시 그린다. */
	UFUNCTION(BlueprintCallable, Category = "MD|Quest")
	void RefreshDisplay();

	UPROPERTY(BlueprintAssignable, Category = "MD|Quest")
	FMD_QuestBackRequest OnBackRequested;

public:
	// 선택된 반복 퀘스트의 수락 또는 보상 수령 요청 이벤트
	UPROPERTY(BlueprintAssignable, Category = "MD|Quest")
	FMD_QuestRepeatActionRequest OnRepeatAcceptRequested;

	// 선택된 반복 퀘스트의 거절 요청 이벤트
	UPROPERTY(BlueprintAssignable, Category = "MD|Quest")
	FMD_QuestRepeatActionRequest OnRepeatDeclineRequested;

	// 선택된 반복 퀘스트의 포기 요청 이벤트
	UPROPERTY(BlueprintAssignable, Category = "MD|Quest")
	FMD_QuestRepeatActionRequest OnRepeatAbandonRequested;

protected:
	// 목록에 생성할 퀘스트 카드 Widget Blueprint 클래스
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest")
	TSubclassOf<UMD_QuestCardWidget> QuestCardClass;

	// 목록과 상세 화면에 표시할 메인 퀘스트 복제 상태
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	TArray<FMD_MainQuestState> MainQuests;

	// 목록과 상세 화면에 표시할 반복 퀘스트 복제 상태
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	TArray<FMD_RepeatQuestState> RepeatQuests;

	// 선택 상태 갱신과 재구성 후 선택 복원에 사용하는 생성 카드 목록
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMD_QuestCardWidget>> QuestCards;

	// 동적으로 생성한 퀘스트 카드 중 항상 하나만 선택되도록 관리하는 그룹
	UPROPERTY(Transient)
	TObjectPtr<UCommonButtonGroupBase> QuestCardButtonGroup;

	// 우측 상세 패널에 현재 표시 중인 카드 참조
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MD|Quest")
	TObjectPtr<UMD_QuestCardWidget> SelectedQuestCard;

	// 디자이너가 지정한 메인 퀘스트 상세 아이콘 Brush
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Appearance")
	FSlateBrush MainQuestIcon;

	// 디자이너가 지정한 반복 퀘스트 상세 아이콘 Brush
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Appearance")
	FSlateBrush RepeatQuestIcon;

	// 동적으로 생성한 메인 및 반복 퀘스트 카드를 담는 스크롤 목록
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidget))
	TObjectPtr<UScrollBox> SB_QuestList;

	// 선택된 퀘스트 종류에 맞는 아이콘을 표시하는 이미지
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_DetailType;

	/** 메인·반복 퀘스트와 분리된 코덱스 탭 표시용 전역 수집 정보 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore")
	TArray<FPrimaryAssetId> DiscoveredLoreAssetIds;

	// 선택된 퀘스트 이름을 표시하는 텍스트
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidget))
	TObjectPtr<UTextBlock> Txt_DetailName;

	// 선택된 퀘스트의 현재 진행 수량을 표시하는 텍스트
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidget))
	TObjectPtr<UTextBlock> Txt_DetailProgress;

	// 선택된 퀘스트의 현재 목표 내용을 표시하는 텍스트
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidget))
	TObjectPtr<UTextBlock> Txt_DetailDescription;

	// 성공 보상 호감도 값을 표시하는 텍스트
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidget))
	TObjectPtr<UTextBlock> Txt_AffinityReward;

	// 성공 보상 점수 값을 표시하는 텍스트
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidget))
	TObjectPtr<UTextBlock> Txt_ScoreReward;

	// 성공 보상 크레딧 값을 표시하는 텍스트
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidget))
	TObjectPtr<UTextBlock> Txt_CreditReward;

	// 제시된 반복 퀘스트를 수락하는 공통 버튼
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidget))
	TObjectPtr<UMD_CommonButtonBase> Btn_Accept;

	// 제시된 반복 퀘스트를 거절하는 공통 버튼
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidget))
	TObjectPtr<UMD_CommonButtonBase> Btn_Reject;

	// 목표를 달성한 반복 퀘스트의 보상을 수령하는 공통 버튼
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidget))
	TObjectPtr<UMD_CommonButtonBase> Btn_Reward;

	// 진행 중인 반복 퀘스트를 포기하는 공통 버튼
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidget))
	TObjectPtr<UMD_CommonButtonBase> Btn_Giveup;

public:
	// 화면에 표시할 메인 퀘스트 상태를 적용하는 함수
	UFUNCTION(BlueprintCallable, Category = "MD|Quest")
	void SetMainQuests(const TArray<FMD_MainQuestState>& InMainQuests);

	// 화면에 표시할 반복 퀘스트 상태를 적용하는 함수
	UFUNCTION(BlueprintCallable, Category = "MD|Quest")
	void SetRepeatQuests(const TArray<FMD_RepeatQuestState>& InRepeatQuests);

	// 현재 퀘스트 목록과 선택 상세 정보를 다시 구성하는 함수
	UFUNCTION(BlueprintCallable, Category = "MD|Quest")
	void RefreshDisplay();

	// 선택한 카드를 우측 상세 화면에 적용하는 함수
	UFUNCTION(BlueprintCallable, Category = "MD|Quest")
	void SelectQuestCard(UMD_QuestCardWidget* QuestCard);

protected:
	// 메인과 반복 퀘스트 상태에서 카드 목록을 다시 생성하는 함수
	void RebuildQuestList();

	// 현재 선택 카드의 데이터로 우측 상세 및 액션 버튼을 갱신하는 함수
	void RefreshQuestDetail();

	// 공통 버튼 카드의 기본 클릭 이벤트를 선택 처리로 연결하는 함수
	void HandleQuestCardClicked(UMD_QuestCardWidget* QuestCard);

	// 선택된 반복 퀘스트의 수락 요청을 전달하는 함수
	void HandleAcceptClicked();

	// 선택된 반복 퀘스트의 거절 요청을 전달하는 함수
	void HandleRejectClicked();

	// 선택된 반복 퀘스트의 보상 수령 요청을 전달하는 함수
	void HandleRewardClicked();

	// 선택된 반복 퀘스트의 포기 요청을 전달하는 함수
	void HandleGiveupClicked();
};
