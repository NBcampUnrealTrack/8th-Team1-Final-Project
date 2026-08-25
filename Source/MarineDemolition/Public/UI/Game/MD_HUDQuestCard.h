#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/Merchant/MD_MerchantTypes.h"
#include "MD_HUDQuestCard.generated.h"

class UTextBlock;
enum class EMD_QuestTab : uint8;

/** 퀘스트 한 건의 종류·이름·설명·진행률을 표시하는 HUD 카드 */
UCLASS()
class MARINEDEMOLITION_API UMD_HUDQuestCard : public UUserWidget
{
	GENERATED_BODY()

protected:
	// 메인 또는 반복 퀘스트 종류 표시
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_QuestKind;

	// 퀘스트 종류 글자의 외곽 그림자 표시
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_QuestKind_Shadow;

	// 퀘스트 이름 표시
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_QuestName;

	// 퀘스트 이름 글자의 외곽 그림자 표시
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_QuestName_Shadow;

	// 퀘스트 목표 설명 표시
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_QuestDescription;

	// 퀘스트 목표 설명 글자의 외곽 그림자 표시
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_QuestDescription_Shadow;

	// 현재 진행도와 목표 수량을 숫자 형식으로 표시
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Progress;

	// 진행도 글자의 외곽 그림자 표시
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Progress_Shadow;

public:
	// 메인 퀘스트 상태를 카드 표시 정보로 적용하는 함수
	void SetMainQuest(const FMD_MainQuestState& QuestState);

	// 반복 퀘스트 상태를 카드 표시 정보로 적용하는 함수
	void SetRepeatQuest(const FMD_RepeatQuestState& QuestState);

protected:
	// 퀘스트 종류에 맞는 카드 아이콘과 색상을 Blueprint에서 적용하는 이벤트
	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
	void SetQuestCardType(EMD_QuestTab QuestType);

	/** 퀘스트 종류와 관계없이 공통 카드 표시 정보를 적용하는 함수 */
	void SetQuest(FPrimaryAssetId DefinitionId, int32 PhaseIndex, int32 CurrentProgress, EMD_QuestTab QuestType);

	/** 본문과 그림자 텍스트에 같은 내용을 적용하는 함수 */
	void SetTextPair(UTextBlock* TextWidget, UTextBlock* ShadowWidget, FText Text);

	/** 현재 진행도와 목표 수량을 새 카드의 숫자 형식으로 적용하는 함수 */
	void SetProgressText(int32 CurrentProgress, int32 RequiredCount);
};
