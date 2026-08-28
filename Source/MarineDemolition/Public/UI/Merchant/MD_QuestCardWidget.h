// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/Merchant/MD_MerchantTypes.h"
#include "MD_QuestCardWidget.generated.h"

class UButton;
class UTextBlock;
struct FGameplayTagContainer;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMD_QuestCardActionRequest, int32, QuestIndex);

/**
 * 반복 퀘스트 카드 1장 (`04` §5).
 *
 * 카드는 자기 배열 인덱스를 모른다 — 컨테이너(UMD_QuestWidget)가 RepeatQuests 배열의
 * 인덱스를 SetQuest()로 넣어주고, 그 인덱스를 그대로 액션 델리게이트에 실어 돌려준다.
 * 서버 판정은 여기서 하지 않는다. AMD_PlayerController의 반복 퀘스트 서버 RPC가
 * 실제 처리를 맡고 이 카드는 상태에 맞는 요청만 전달하는 표시 책임.
 */
UCLASS()
class MARINEDEMOLITION_API UMD_QuestCardWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	/** 위젯 생성 시 상태별 액션 버튼을 C++ 요청 함수와 연결하는 초기화 */
	virtual void NativeConstruct() override;

public:
	/** 제시 카드의 수락 또는 목표 달성 카드의 보상 받기 동작을 요청하는 이벤트 */
	UPROPERTY(BlueprintAssignable, Category = "MD|Quest")
	FMD_QuestCardActionRequest OnAcceptRequested;

	/** 제시된 카드에서 '거절'을 눌렀을 때 */
	UPROPERTY(BlueprintAssignable, Category = "MD|Quest")
	FMD_QuestCardActionRequest OnDeclineRequested;

	/** 수락한 카드에서 '포기'를 눌렀을 때 발생하는 이벤트 */
	UPROPERTY(BlueprintAssignable, Category = "MD|Quest")
	FMD_QuestCardActionRequest OnAbandonRequested;

protected:
	/** 디자이너 미리보기와 런타임 갱신이 공유하는 반복 퀘스트 상태 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Quest")
	FMD_RepeatQuestState QuestState;

	/** 서버 요청이 반복 퀘스트 원장의 동일 항목을 찾기 위한 배열 인덱스 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Quest")
	int32 QuestIndex = 0;

	/** 반복 퀘스트의 공통 표시 이름 영역 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_QuestName;

	/** 제시 대사 또는 직전에 완료된 페이즈 대사 영역 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_Description;

	/** 목표 샘플 등급을 C·B·A·S 형식으로 표시하는 영역 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_GradeBadge;

	/** 현재 페이즈의 회수 또는 손상 허용 회수 조건 표시 영역 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_ObjectiveType;

	/** 현재 페이즈의 제출 수량을 N/M 형식으로 표시하는 영역 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_Progress;

	/** 퀘스트 성공 크레딧을 통화 형식으로 표시하는 영역 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_RewardCredits;

	/** 제시·수락·거절·완료·실패와 진행도에서 파생한 보상 수령 대기 상태 표시 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_Status;

	/** 목표 달성 후 상인에게 성공 보상 수령을 요청하는 버튼 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Reward;

	/** 제시 상태의 반복 퀘스트 수락을 요청하는 버튼 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Accept;

	/** 제시 상태의 반복 퀘스트 거절을 요청하는 버튼 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Decline;

	/** 미회수 실패 보상에 정의된 크레딧·점수·호감도 패널티를 요청하는 포기 버튼 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Abandon;

public:
	/** 배열 인덱스와 반복 퀘스트 상태를 카드에 적용하고 표시를 갱신하는 함수 */
	UFUNCTION(BlueprintCallable, Category = "MD|Quest")
	void SetQuest(int32 InQuestIndex, const FMD_RepeatQuestState& InState);

	/** 현재 카드가 보관한 반복 퀘스트 상태를 외부 컨테이너에 제공하는 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Quest")
	const FMD_RepeatQuestState& GetQuestState() const
	{
		const FMD_RepeatQuestState& OutQuestState = QuestState;
		return OutQuestState;
	}

	/** 현재 상태와 원본 에셋을 기준으로 텍스트와 버튼 노출을 다시 계산하는 함수 */
	UFUNCTION(BlueprintCallable, Category = "MD|Quest")
	void RefreshDisplay();

protected:
	/** 별도 보상 버튼 입력을 기존 서버 수락·정산 요청 경로로 전달하는 함수 */
	UFUNCTION()
	void HandleRewardClicked();

	/** 수락 버튼 입력을 상위 퀘스트 메뉴에 전달하는 함수 */
	UFUNCTION()
	void HandleAcceptClicked();

	/** 거절 버튼 입력을 상위 퀘스트 메뉴에 전달하는 함수 */
	UFUNCTION()
	void HandleDeclineClicked();

	/** 포기 버튼 입력을 상위 퀘스트 메뉴에 전달하는 함수 */
	UFUNCTION()
	void HandleAbandonClicked();

private:
	/** 허용 등급 태그 목록을 카드 배지용 C/B/A/S 문자열로 변환하는 함수 */
	FText BuildGradeBadgeText(const FGameplayTagContainer& GradeTags) const;
};
