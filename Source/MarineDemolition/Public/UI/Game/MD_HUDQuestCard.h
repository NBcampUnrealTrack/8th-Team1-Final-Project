#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/Merchant/MD_MerchantTypes.h"
#include "MD_HUDQuestCard.generated.h"

class UProgressBar;
class UTextBlock;

/** 퀘스트 한 건의 종류·이름·설명·진행률을 표시하는 HUD 카드 */
UCLASS()
class MARINEDEMOLITION_API UMD_HUDQuestCard : public UUserWidget
{
	GENERATED_BODY()

protected:
	// 메인 또는 반복 퀘스트 종류 표시
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_QuestKind;

	// 퀘스트 이름 표시
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_QuestName;

	// 퀘스트 목표 설명 표시
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_QuestDescription;

	// 현재 진행도를 목표 수량에 대한 비율로 표시
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> PB_QuestProgressRate;

public:
	// 메인 퀘스트 상태를 카드 표시 정보로 적용하는 함수
	void SetMainQuest(const FMD_MainQuestState& QuestState);

	// 반복 퀘스트 상태를 카드 표시 정보로 적용하는 함수
	void SetRepeatQuest(const FMD_RepeatQuestState& QuestState);

protected:
	// 퀘스트 종류와 공통 정의 및 진행도를 실제 UI 요소에 적용하는 함수
	void ApplyQuestData(EMD_QuestKind QuestKind, const FMD_QuestDefinitionData& Definition, int32 CurrentProgress);
};
