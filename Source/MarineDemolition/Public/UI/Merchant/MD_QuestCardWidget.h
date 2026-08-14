// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/Merchant/MD_MerchantTypes.h"
#include "MD_QuestCardWidget.generated.h"

class UButton;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMD_QuestCardActionRequest, int32, QuestIndex);

/**
 * 반복 퀘스트 카드 1장 (`04` §5).
 *
 * 카드는 자기 배열 인덱스를 모른다 — 컨테이너(UMD_QuestWidget)가 RepeatQuests 배열의
 * 인덱스를 SetQuest()로 넣어주고, 그 인덱스를 그대로 액션 델리게이트에 실어 돌려준다.
 * 서버 판정은 여기서 하지 않는다. AMD_PlayerController::ServerAcceptRepeat/ServerDeclineRepeat가
 * 실제 처리를 맡고, 이 카드는 요청만 올린다 (상점 카드와 동일한 설계 원칙).
 *
 * 점수·호감도 트랙(`09` §1.5 등급별 값)은 FMD_QuestDefinitionData에 아직 필드가 없어
 * 이 카드에는 넣지 않는다 — 데이터가 생기면 그때 추가한다.
 */
UCLASS()
class MARINEDEMOLITION_API UMD_QuestCardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "MD|Quest")
	void SetQuest(int32 InQuestIndex, const FMD_RepeatQuestState& InState);

	UFUNCTION(BlueprintPure, Category = "MD|Quest")
	const FMD_RepeatQuestState& GetQuestState() const { return QuestState; }

	UFUNCTION(BlueprintCallable, Category = "MD|Quest")
	void RefreshDisplay();

	/** 제시된 카드에서 '수락'을 눌렀을 때 */
	UPROPERTY(BlueprintAssignable, Category = "MD|Quest")
	FMD_QuestCardActionRequest OnAcceptRequested;

	/** 제시된 카드에서 '거절'을 눌렀을 때 */
	UPROPERTY(BlueprintAssignable, Category = "MD|Quest")
	FMD_QuestCardActionRequest OnDeclineRequested;

	/** 수락한 카드에서 '포기'를 눌렀을 때 발생하는 이벤트 */
	UPROPERTY(BlueprintAssignable, Category = "MD|Quest")
	FMD_QuestCardActionRequest OnAbandonRequested;

protected:
	virtual void NativeConstruct() override;

	UFUNCTION()
	void HandleAcceptClicked();

	UFUNCTION()
	void HandleDeclineClicked();

	UFUNCTION()
	void HandleAbandonClicked();

	/** 디자이너가 에디터 미리보기에서 값을 넣어볼 수 있도록 노출한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Quest")
	FMD_RepeatQuestState QuestState;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Quest")
	int32 QuestIndex = 0;

	// ───────────────── 카드 표시 항목 (`04` §5) ─────────────────

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_QuestName;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_Description;

	/** 목표 샘플 등급 배지: C / B / A / S */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_GradeBadge;

	/** 회수 / 손상 허용 회수 / 청소 계약 / 마감 계약 / 복합 (enum DisplayName) */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_ObjectiveType;

	/** `N/M` 진행도 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_Progress;

	/** 퀘스트 보수 (`450₡`) */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_RewardCredits;

	/** 제시 / 수락 / 거절 / 완료 / 실패 (enum DisplayName) */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_Status;

	// ───────────────── 버튼 (상태별로 하나만 보인다) ─────────────────

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Accept;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Decline;

	/** 확정된 포기 페널티를 서버 상인 원장에 요청하는 버튼 (호감도만 하락) */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Abandon;
};
