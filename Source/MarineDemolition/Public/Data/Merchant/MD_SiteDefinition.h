// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MD_SiteDefinition.generated.h"

class UMD_QuestDefinition;

 // 작업 현장 1곳의 모든 퀘스트를 묶는 데이터에셋

UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_SiteDefinition : public UDataAsset
{
	GENERATED_BODY()
	
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Site")
	FText SiteName;

	/** 발주처 이름. 캐릭터화하지 않는 가벼운 텍스트. (09 §1) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Site")
	FText ClientName;

	//계약일수 기본3일, 일단 비상4일차는 포함하지 않았습니다. 점검 후 진행시스템에서 판단
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,Category = "MD|Site")
	int32 ContractDayCount = 3;

	//Day별 메인퀘스트. 배열 인덱스 순서대로 Day1,2,3
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Site|Main")
	TArray<TObjectPtr<UMD_QuestDefinition>> MainQuestLine;

	//반복 퀘스트. Day마다 이 풀에서 2~3개를 뽑는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Site|Repeat")
	TArray<TObjectPtr<UMD_QuestDefinition>> RepeatQuestPool;

	//Day별 반복퀘스트 개수 범위(기획상 2~3개)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Site|Repeat")
	int32 MinRepeatCount = 2;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Site|Repeat")
	int32 MaxRepeatCount = 3;
};
