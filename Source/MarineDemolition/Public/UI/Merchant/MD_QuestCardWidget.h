// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Types/Merchant/MD_MerchantTypes.h"
#include "UI/MD_CommonButtonBase.h"

#include "MD_QuestCardWidget.generated.h"

class UImage;
class UTextBlock;

/** 메인 또는 반복 퀘스트 하나를 선택하는 공통 버튼 카드 */
UCLASS()
class MARINEDEMOLITION_API UMD_QuestCardWidget : public UMD_CommonButtonBase
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;

protected:
	// 목록에서 선택된 퀘스트를 원본 배열과 연결하기 위한 인덱스
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	int32 QuestIndex = INDEX_NONE;

	// 현재 카드가 메인 퀘스트를 표시하는지 구분하기 위한 상태
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	bool bMainQuest = true;

	// 메인 퀘스트 카드 표시와 상세 화면 전달에 사용하는 상태
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	FMD_MainQuestState MainQuestState;

	// 반복 퀘스트 카드 표시와 상세 화면 전달에 사용하는 상태
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	FMD_RepeatQuestState RepeatQuestState;

	// 디자이너가 지정한 메인 퀘스트 아이콘 Brush
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Appearance")
	FSlateBrush MainQuestIcon;

	// 디자이너가 지정한 반복 퀘스트 아이콘 Brush
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Appearance")
	FSlateBrush RepeatQuestIcon;

	// 퀘스트 종류에 맞는 아이콘을 표시하기 위한 이미지
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_QuestType;

	// 메인 또는 반복 퀘스트 종류를 표시하기 위한 텍스트
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_QuestType;

	// 퀘스트 원본의 표시 이름을 출력하기 위한 텍스트
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_QuestName;

	// 현재 페이즈 진행도를 수량 형식으로 출력하기 위한 텍스트
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_QuestProgress;

	// 현재 페이즈의 목표 내용을 출력하기 위한 텍스트
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_QuestDescription;

public:
	// 메인 퀘스트 배열 인덱스와 상태를 카드에 적용하는 함수
	UFUNCTION(BlueprintCallable, Category = "MD|Quest")
	void SetMainQuest(int32 InQuestIndex, const FMD_MainQuestState& InQuestState);

	// 반복 퀘스트 배열 인덱스와 상태를 카드에 적용하는 함수
	UFUNCTION(BlueprintCallable, Category = "MD|Quest")
	void SetRepeatQuest(int32 InQuestIndex, const FMD_RepeatQuestState& InQuestState);

	// 카드 종류 적용 후 Blueprint에서 추가 연출을 구현하는 이벤트
	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent, Category = "MD|Quest")
	void SetQuestType(bool bInMainQuest);

	// 현재 카드가 메인 퀘스트를 표시하는지 반환하는 함수
	UFUNCTION(BlueprintPure, Category = "MD|Quest")
	FORCEINLINE bool GetIsMainQuest() { return bMainQuest; }

	// 현재 카드와 원본 퀘스트 배열을 연결하는 인덱스를 반환하는 함수
	UFUNCTION(BlueprintPure, Category = "MD|Quest")
	FORCEINLINE int32 GetQuestIndex() { return QuestIndex; }

	// 현재 카드의 메인 퀘스트 상태를 반환하는 함수
	UFUNCTION(BlueprintPure, Category = "MD|Quest")
	FORCEINLINE FMD_MainQuestState GetMainQuestState() { return MainQuestState; }

	// 현재 카드의 반복 퀘스트 상태를 반환하는 함수
	UFUNCTION(BlueprintPure, Category = "MD|Quest")
	FORCEINLINE FMD_RepeatQuestState GetRepeatQuestState() { return RepeatQuestState; }

	// 현재 종류와 상태를 카드 위젯에 다시 반영하는 함수
	UFUNCTION(BlueprintCallable, Category = "MD|Quest")
	void RefreshDisplay();
};
