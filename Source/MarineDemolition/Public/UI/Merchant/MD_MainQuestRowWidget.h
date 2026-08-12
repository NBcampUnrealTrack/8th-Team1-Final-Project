// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/Merchant/MD_MerchantTypes.h"
#include "MD_MainQuestRowWidget.generated.h"

class UTextBlock;
class UWidget;

/**
 * 메인 퀘스트 Day 1행 (`04` §5.5 탭1).
 *
 * 메인퀘는 수락/거절이 없다 — Day가 되면 자동으로 Active가 된다 (`09` §1).
 * 그래서 이 위젯에는 버튼이 없다. 지난 Day는 완료 체크, 다음 Day는 잠김으로
 * 표시하고, 현재 Day만 설명·진행도를 펼쳐 보여준다.
 */
UCLASS()
class MARINEDEMOLITION_API UMD_MainQuestRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "MD|Quest")
	void SetMainQuest(const FMD_MainQuestState& InState);

	UFUNCTION(BlueprintPure, Category = "MD|Quest")
	const FMD_MainQuestState& GetMainQuestState() const { return QuestState; }

	UFUNCTION(BlueprintCallable, Category = "MD|Quest")
	void RefreshDisplay();

protected:
	virtual void NativeConstruct() override;

	/** 디자이너가 에디터 미리보기에서 값을 넣어볼 수 있도록 노출한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Quest")
	FMD_MainQuestState QuestState;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_DayLabel;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_QuestName;

	/** 잠김 / 진행중 / 완료 (enum DisplayName) */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_Status;

	/**
	 * 현재 Day 행만 펼쳐서 보여줄 상세 컨테이너(설명+진행도).
	 * 지난/다음 Day는 이 컨테이너를 숨긴다. 구체 타입을 묶지 않아 디자이너가
	 * 컨테이너 종류를 바꿔도 바인딩이 유지된다.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> BDR_ExpandedDetail;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_Description;

	/** `N/M` 진행도 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_Progress;

	/** 미완료 배지. `09` §1 원칙: 완료하지 않아도 다음 Day 진행에 영향 없다 — 강제로 막지 않는다. */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_IncompleteBadge;
};
