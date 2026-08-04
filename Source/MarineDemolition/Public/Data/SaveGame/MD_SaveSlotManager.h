// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MD_SaveSlotManager.generated.h"

/**
 * 
 */
UCLASS()
class MARINEDEMOLITION_API UMD_SaveSlotManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()
	
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

protected:
	// 자동 저장으로 할당되는 Save Slot의 최대 개수
	const int32 MaxAutoSaveSlotCount = 3;

	// 사용자의 임의로 저장할 수 있는 SaveSlot의 최대 개수
	const int32 MaxSaveSlotCount = 5;

	const FString AutoSaveSlotPrefix = TEXT("AutoSave");

	// 저장된 Save Slot의 이름 목록
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save GameSession")
	TArray<FString> SaveSlotNames;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 AutoSaveSlotIndex;

public:
	TArray<FString> GetSaveSlotNames() const;

	bool LoadSaveSlotNames();

	bool SaveSlotNameSaveData();

	class UMD_SaveGameSession* LoadGameSessionData(const FString& SlotName);

	// 현재 GameSession의 정보를 특정 이름으로 저장하는 함수
	bool SaveCurrentGameSessionWithName(const FString& SlotName, bool bIsOverWrite = false);

	// 현재 GameSession의 정보를 자동으로 저장하는 함수
	bool SaveCurrentGamesessionByAuto();

private:
	// 현재 GameSession의 정보를 실제로 저장하는 함수
	bool SaveCurrentGameSessionInternal(const FString& SlotName);
};
