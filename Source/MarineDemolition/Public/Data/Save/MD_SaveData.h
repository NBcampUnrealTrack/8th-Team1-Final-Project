// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MD_SaveData.generated.h"

USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_InventoryDataWraper
{
	GENERATED_BODY()
	
public:
	UPROPERTY()
	TArray<UObject*> temp;
};

/*
 * 합산(누계) 점수
 * 돈(크레딧)
 * 상인 호감도
 * 수주 의뢰(진행/완료 상태 포함)
 * 플레이어 별 보유 아이템
 * 경과 일 수(3일 루프 진행도)
*/
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_SaveData
{
	GENERATED_BODY()

public:
	// 점수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	int32 Score;

	// 돈
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	int32 Credit;

	// 상인 호감도
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	float MerchantLikeability;

	// 플레이어 별 보유 아이템 목록
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	TMap<FUniqueNetIdRepl, FMD_InventoryDataWraper> temp;

	// 경과 일수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	int32 ElapsedDays;

public:
	static bool SaveDataToJson(const FString& FileName, const FMD_SaveData& Data);

	static bool LoadDataFromJson(const FString& FileName, FMD_SaveData& Data);
};
