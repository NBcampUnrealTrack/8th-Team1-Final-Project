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
	TArray<TSoftObjectPtr<class UMD_ToolDefinition>> ToolSlots;

	// Add More If Need
};

/**
 * 
 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_SaveSlot
{
	GENERATED_BODY()

public:
	FMD_SaveSlot()
	{
		SlotName = TEXT("");
		QuestID = FPrimaryAssetId();
		MapPackagePath = TEXT("");
		SaveTime = FDateTime::Now();
	}

	FMD_SaveSlot(const FString& SlotName) : SlotName(SlotName)
	{
		QuestID = FPrimaryAssetId();
		MapPackagePath = TEXT("");
		SaveTime = FDateTime::Now();
	}

	FMD_SaveSlot(const FMD_SaveSlot& OtherSaveSlot)
		: SlotName(OtherSaveSlot.SlotName), QuestID(OtherSaveSlot.QuestID), MapPackagePath(OtherSaveSlot.MapPackagePath), SaveTime(OtherSaveSlot.SaveTime)
	{
	}

public:
	const static FMD_SaveSlot Empty;

	// Game Session의 데이터가 저장된 Slot 이름
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Slot")
	FString SlotName;

	// 저장 시점에 진행중인 퀘스트의 ID
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Slot")
	FPrimaryAssetId QuestID;

	// 저장 시점에 플레이 중인 Map의 패키지 경로
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Slot")
	FString MapPackagePath;

	// 저장한 시각
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Slot")
	FDateTime SaveTime = FDateTime::Now();

public:
	const bool IsEmpty() const;

	bool operator==(const FMD_SaveSlot& OtherSlotInfo)
	{
		return SlotName == OtherSlotInfo.SlotName;
	}
	bool operator!=(const FMD_SaveSlot& OtherSlotInfo)
	{
		return SlotName != OtherSlotInfo.SlotName;
	}

	USaveGame* LoadGameSessionData() const;

	bool SaveGameSessionData(USaveGame* GameData, bool bOverwrite = true);
};

/**
 * 저장되는 Game Session 데이터
 */
UCLASS()
class MARINEDEMOLITION_API UMD_SaveGameSession : public USaveGame
{
	GENERATED_BODY()

public:
	// 누계 점수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save GameSession")
	int32 Score;

	// 보유 돈
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save GameSession")
	int32 Credit;

	// 상인 호감도
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save GameSession")
	float MerchantLikeability;

	// 플레이어 데이터 목록 { UniqueId.ToString(), FMD_PlayerData() }
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save GameSession")
	TMap<FString, FMD_PlayerData> PlayerDataMap;

	// 3일 루프 중 경과 일수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save GameSession")
	int32 ElapsedDays;
};

/**
 * 자동 저장과 사용자 저장 슬롯 목록을 구분하여 Host 로컬에 보존한다.
 */
UCLASS()
class MARINEDEMOLITION_API UMD_SaveSlotList : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Save GameSession")
	TArray<FMD_SaveSlot> UserSaveSlotList;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Save GameSession")
	TArray<FMD_SaveSlot> AutoSaveSlotList;
};
