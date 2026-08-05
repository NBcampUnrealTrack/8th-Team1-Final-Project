// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Types/Merchant/MD_MerchantTypes.h"
#include "MD_QuestDefinition.generated.h"

//메인, 반복퀘스트 정의. 보상받는 방법은 같으므로 종류만 구분

UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_QuestDefinition : public UDataAsset
{
	GENERATED_BODY()
	
public:

	//퀘스트 이름(UI에 나타나는 이름)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest")
	FText QuestName;

	//퀘스트 설명("S급 샘플을 회수하세요")
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest")
	FText Description;

	//메인퀘스트 반복퀘스트 구분(Repeat을 기본값으로 둠)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest")
	EMD_QuestKind Kind = EMD_QuestKind::Repeat;
	
	//퀘스트 성공 유형(파손없이회수, 파손 회수...)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Objective")
	EMD_QuestObjectiveType ObjectiveType = EMD_QuestObjectiveType::RecoverIntact;

	//목표 샘플 등급(Tags)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Objective")
	FGameplayTag TargetGradeTag;

	//목표 달성 회수 개수
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Objective")
	int32 RequiredCount = 1;

	//성공보수 크레딧
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Reward")
	int32 RewardCredits = 80;

	//로드할 맵
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|QuestMap")
	TWeakObjectPtr<UWorld> QuestMap;
};
