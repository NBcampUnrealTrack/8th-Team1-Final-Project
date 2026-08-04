// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "GameplayTags.h"
#include "MD_SaveGameSession.generated.h"

USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_InventoryDataWraper
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<FGameplayTag> InventoryData;
};


/*
 * 합산(누계) 점수
 * 돈(크레딧)
 * 상인 호감도
 * 수주 의뢰(진행/완료 상태 포함)
 * 플레이어 별 보유 아이템
 * 경과 일 수(3일 루프 진행도)
*/
UCLASS()
class MARINEDEMOLITION_API UMD_SaveGameSession : public USaveGame
{
	GENERATED_BODY()
	
public:
	// 점수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save GameSession")
	int32 Score;

	// 돈
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save GameSession")
	int32 Credit;

	// 상인 호감도
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save GameSession")
	float MerchantLikeability;

	// 수주한 의뢰
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save GameSession")
	FPrimaryAssetId MissionID;

	// 플레이어 별 보유 아이템 목록
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save GameSession")
	TMap<FUniqueNetIdRepl, FMD_InventoryDataWraper> InventoryData;

	// 경과 일수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save GameSession")
	int32 ElapsedDays;
};
