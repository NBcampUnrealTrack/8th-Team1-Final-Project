// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/Merchant/MD_MerchantTypes.h"

#include "MD_QuestWidget.generated.h"

class UImage;
class UCommonButtonGroupBase;
class UVerticalBox;
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

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

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

	// 메인 퀘스트 카드를 배치하는 목록 영역
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> VB_MainQuest;

	// 반복 퀘스트 카드를 배치하는 목록 영역
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> VB_RepeatQuest;

	// 선택된 퀘스트의 카드와 동일한 상태 문구 표시
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Detail_State;

	// 선택된 퀘스트 이름 표시
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Detail_Name;

	// 현재 페이즈의 목표 종류 표시
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_DetailProgress;

	// 현재 페이즈의 누적 진행 수량 표시
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_DetailProgress_Current;

	// 현재 페이즈의 요구 수량 표시
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_DetailProgress_Total;

	// 현재 페이즈 목표 달성 시 체크 표시
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_DetailProgress;

	// 현재 페이즈의 목표 설명 표시
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Detail_Description;

	// 보상 수령 가능 상태에서만 표시할 안내 문구
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_CompletionMessage;

	// 성공 보상 호감도 표시
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Reward_Affinity;

	// 성공 보상 점수 표시
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Reward_Score;

	// 성공 보상 크레딧 표시
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Reward_Credit;

	// 제시된 반복 퀘스트 수락 버튼
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UMD_CommonButtonBase> Btn_Accept;

	// 제시된 반복 퀘스트 거절 버튼
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UMD_CommonButtonBase> Btn_Reject;

	// 목표를 달성한 반복 퀘스트 보상 수령 버튼
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UMD_CommonButtonBase> Btn_Reward;

	// 진행 중인 반복 퀘스트 포기 버튼
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
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

	// 선택된 반복 퀘스트의 수락 요청을 전달하는 함수
	void HandleAcceptClicked();

	// 선택된 반복 퀘스트의 거절 요청을 전달하는 함수
	void HandleRejectClicked();

	// 선택된 반복 퀘스트의 보상 수령 요청을 전달하는 함수
	void HandleRewardClicked();

	// 선택된 반복 퀘스트의 포기 요청을 전달하는 함수
	void HandleGiveupClicked();
};
