// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Types/Score/MD_ScoreTypes.h"
#include "MD_ScoreTableData.generated.h"

// 평가 점수 단가표. 기본값은 기획 기본수치, 밸런스 튜닝은 에셋에서 가능
UCLASS()
class MARINEDEMOLITION_API UMD_ScoreTableData : public UDataAsset
{
	GENERATED_BODY()
	
public:
	UMD_ScoreTableData();

	// 이벤트 태그의 단가. 미등록 태그는 0점 + 경고 로그
	UFUNCTION(BlueprintPure, Category = "Score")
	int32 GetEventValue(FGameplayTag EventTag) const;

	// 샘플 등급·상태의 단가
	UFUNCTION(BlueprintPure, Category = "Score")
	int32 GetSampleValue(EMD_SampleGrade Grade, EMD_SampleScoreType Type) const;

	// 해당 Day의 점검 기준 (Day1 등 점검 없는 날은 0)
	UFUNCTION(BlueprintPure, Category = "Score")
	int32 GetDayThreshold(int32 Day) const;

	// 인원수별 점검 기준 배율 (표에 없으면 1.0)
	UFUNCTION(BlueprintPure, Category = "Score")
	float GetCrewMultiplier(int32 Crew) const;

	// 경고 상태에서 다음 점검 기준에 곱하는 배율 (기획 : 1.1)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Score|Inspection")
	float WarnedThresholdMultiplier = 1.1f;

	// 수도 미밀봉 확산 감점의 사고당 상한 (기획 : -80)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Score|Penalty")
	int32 PipeLeakCapPerIncident = -80;
	
protected:
	// 플랫 이벤트 단가 (Score.* 태그 → 점수)
	UPROPERTY(EditAnywhere, Category = "Score")
	TMap<FGameplayTag, int32> EventValues;

	// 샘플 등급별 단가 4종
	UPROPERTY(EditAnywhere, Category = "Score")
	TMap<EMD_SampleGrade, FMD_SampleScoreRow> SampleValues;

	// Day별 점검 기준 (표준·4인 기준선)
	UPROPERTY(EditAnywhere, Category = "Score|Inspection")
	TMap<int32, int32> DayThresholds;

	// 인원 배율 (1인 0.40 / 2인 0.65 / 3인 0.85 / 4인 1.00)
	UPROPERTY(EditAnywhere, Category = "Score|Inspection")
	TMap<int32, float> CrewMultipliers;
};
