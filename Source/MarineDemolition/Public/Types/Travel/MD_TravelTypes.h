// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "MD_TravelTypes.generated.h"

/* 출발 콘솔에서 고르는 계약 난이도. 평가 점검 기준에 곱해지는 배율이다 (05 §0.1) */
UENUM(BlueprintType)
enum class EMD_Difficulty : uint8
{
	Safe      UMETA(DisplayName = "안전"),
	Standard  UMETA(DisplayName = "표준"),
	Dangerous UMETA(DisplayName = "위험")
};

/* 출발 콘솔 목록에 뜨는 현장 한 칸. bAvailable이 false면 목록에는 남되 선택할 수 없다 (04 §3) */
USTRUCT(BlueprintType)
struct FMD_TravelSiteEntry
{
	GENERATED_BODY()

	/** 현장 표시명. UI 버튼 라벨로 쓴다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Travel")
	FText DisplayName;

	/** 출발 시 ServerTravel할 레벨 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Travel")
	TSoftObjectPtr<UWorld> Level;

	/** false면 준비 중 현장. 위젯에서 잠기고 서버도 요청을 거부한다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Travel")
	bool bAvailable = false;
};

namespace MDTravel
{
	/** 난이도 배율 (05 §0.1) */
	inline float GetDifficultyMultiplier(EMD_Difficulty Difficulty)
	{
		switch (Difficulty)
		{
		case EMD_Difficulty::Safe:      return 0.8f;
		case EMD_Difficulty::Dangerous: return 1.2f;
		case EMD_Difficulty::Standard:
		default:                        return 1.0f;
		}
	}
}
