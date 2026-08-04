// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "MD_SaveSlotInfoList.generated.h"

USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_SaveSlotInfo
{
	GENERATED_BODY()

public:
	// Game Session의 데이터가 저장된 Slot 이름
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Slot Info")
	FString SlotName;

	// 진행하는 퀘스트의 ID
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Slot Info")
	FPrimaryAssetId QuestID;

	// 저장한 시각
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Slot Info")
	FDateTime SaveTime = FDateTime::Now();

public:
	bool operator==(const FMD_SaveSlotInfo& OtherSlotInfo)
	{
		return SlotName == OtherSlotInfo.SlotName;
	}
	bool operator!=(const FMD_SaveSlotInfo& OtherSlotInfo)
	{
		return SlotName != OtherSlotInfo.SlotName;
	}
};

/**
 * 
 */
UCLASS()
class MARINEDEMOLITION_API UMD_SaveSlotInfoList : public USaveGame
{
	GENERATED_BODY()
	
public:
	// 저장된 Game Session에 대한 정보 목록
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Slot")
	TArray<FMD_SaveSlotInfo> SlotInformations;

	// 저장된 Game Session에 대한 정보 목록
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Slot")
	TArray<FMD_SaveSlotInfo> AutoSlotInformations;
};
