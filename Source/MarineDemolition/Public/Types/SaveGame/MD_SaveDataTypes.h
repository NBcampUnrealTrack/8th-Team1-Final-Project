// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "MD_SaveDataTypes.generated.h"

/**
 * 플레이어 데이터
 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_PlayerData
{
	GENERATED_BODY()

public:
	// 플레이어의 핫 바의 슬롯 목록을 복사하여 저장하는 변수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save GameSession")
	TArray<TSoftObjectPtr<UDataAsset>> HotbarToolList;

	// Add More If Need
};

USTRUCT(BlueprintType, meta = (HasNativeMake = "/Script/MarineDemolition.MD_BlueprintFunctionLibrary.MakeMD_GameData"))
struct MARINEDEMOLITION_API FMD_GameData
{
	GENERATED_BODY()

public:
	// 서로 다른 게임 데이터와 구분하기 위한 고유 값(세이브 이름)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marine Demolition Game Data")
	FString UniqueName = TEXT("");

	// 플레이 맵
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marine Demolition Game Data")
	FString MapPackagePath = TEXT("");

	// 진행중인 퀘스트 아이디
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marine Demolition Game Data")
	FPrimaryAssetId QuestID = FPrimaryAssetId();

	// 게임 점수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marine Demolition Game Data")
	int32 Score = 0;

	// 인게임 재화
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marine Demolition Game Data")
	int32 Credit = 0;

	// 상인 우호도
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marine Demolition Game Data")
	float MerchantLevel = 0;

	// 인게임 경과 시간
	// Ex) 2.2 = 2일 + 2시간(인게임) 경과
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marine Demolition Game Data")
	double InGameTime = 0;

	// 플레이어 데이터
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marine Demolition Game Data")
	TMap<FString, FMD_PlayerData> PlayerData;

	// 현실 저장 시간
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marine Demolition Game Data")
	FDateTime SaveTime = FDateTime::Now();

public:
	bool operator==(const FMD_GameData& Other) const
	{
		return UniqueName == Other.UniqueName;
	}
	bool operator!=(const FMD_GameData& Other) const
	{
		return !(UniqueName == Other.UniqueName);
	}

	friend uint32 GetTypeHash(const FMD_GameData& Value)
	{
		return GetTypeHash(Value.UniqueName);
	}
};

UCLASS()
class MARINEDEMOLITION_API UMD_SaveGame : public USaveGame
{
	GENERATED_BODY()
	
public:
	// 자동 저장된 데이터 목록
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marine Demolition Game Data")
	TArray<FMD_GameData> AutoSaveList;

	// 사용자가 임의로 저장한 데이터 목록
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marine Demolition Game Data")
	TArray<FMD_GameData> UserSaveList;

public:
	void ApplyMaxSaveCountLimit(const int32& MaxAutoSaveCount, const int32& MaxUserSaveCount);
};
