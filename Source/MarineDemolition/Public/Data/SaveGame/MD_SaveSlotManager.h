// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Data/SaveGame/MD_SaveSlotInfoList.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MD_SaveSlotManager.generated.h"

class AController;
class UMD_SaveGameSession;

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

	// 자동 저장 세이브 슬롯의 이름 앞에 붙은 문구
	const FString AutoSaveSlotPrefix = TEXT("AutoSave");

	// 저장된 세이브 슬롯들에 대한 정보 목록
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save GameSession")
	TArray<FMD_SaveSlotInfo> SlotInfoList;

	// 자동 저장으로 저장된 세이브 슬롯들에 대한 정보 목록
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save GameSession")
	TArray<FMD_SaveSlotInfo> AutoSlotInfoList;

	// 자동 저장 시 사용되는 인덱스 번호
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 NextAutoSaveIndex;

private:
	// 선택된 세이브 슬롯 정보
	TSharedPtr<FMD_SaveSlotInfo> PendingSaveSlotInfo;

	// 호스팅할 맵으로 이동하는 동안 로드된 GameSession 객체의 수명을 유지하는 변수
	UPROPERTY(Transient)
	TObjectPtr<UMD_SaveGameSession> PendingGameSessionData;

	// 현재 호스팅 중인 Session에서 재접속 플레이어의 Equipment를 복원하는 기준 데이터
	UPROPERTY(Transient)
	TObjectPtr<UMD_SaveGameSession> ActiveGameSessionData;

public:
	// 저장되어있는 세이브 슬롯의 이름 목록을 반환하는 함수
	UFUNCTION(BlueprintCallable, Category = "Save GameSession")
	TArray<FMD_SaveSlotInfo> GetSaveSlotInfoList() const;

	// 세이브 슬롯들에 대한 정보 목록을 저장하는 함수
	UFUNCTION(BlueprintCallable, Category = "Save GameSession")
	bool SaveSlotInfoList();

	// 현재 GameSession의 정보를 특정 이름으로 저장하는 함수
	UFUNCTION(BlueprintCallable, Category = "Save GameSession")
	bool SaveCurrentGameSessionWithName(const FString& SlotName, bool bIsOverWrite = false);

	// 현재 GameSession의 정보를 자동으로 저장하는 함수
	UFUNCTION(BlueprintCallable, Category = "Save GameSession")
	bool SaveCurrentGamesessionByAuto();

	// 임의 이름으로 저장된 세이브 슬롯을 호스팅될 Session에서 사용할 데이터로써 로드 및 보관 처리하는 함수
	UFUNCTION(BlueprintCallable, Category = "Save GameSession")
	bool PrepareGameSessionForHosting(const FString& SlotName);

	// 호스팅 맵 진입 후 대기 중인 GameSession 데이터를 현재 Session 데이터로 전환하는 함수
	bool ActivatePendingGameSession();

	// Player의 Pawn이 제거되기 전에 서버 권위 Equipment 상태를 현재 Session 데이터에 기록하는 함수
	bool SavePlayerEquipment(AController* PlayerController);

	// Player의 새 Pawn이 생성된 후 저장된 Equipment 상태를 서버 권위로 복원하는 함수
	bool LoadPlayerEquipment(AController* PlayerController);

private:
	// 세이브 슬롯들에 대한 정보 목록을 불러오는 함수
	bool LoadSlotInfoList();

	// 현재 GameSession의 정보를 실제로 저장하는 함수
	TSharedPtr<FMD_SaveSlotInfo> SaveCurrentGameSessionInternal(const FString& SlotName, bool bIsOverWrite = false);

	// 임의 이름으로 저장된 세이브 슬롯의 데이터를 실제로 로드하는 함수
	UMD_SaveGameSession* LoadGameSessionDataInternal(const FString& SlotName);

	// Online Subsystem의 Player ID를 저장 데이터 조회에 사용할 안정적인 문자열 키로 변환하는 함수
	bool BuildPlayerSaveKey(const AController* PlayerController, FString& OutPlayerSaveKey) const;

	// Equipment 데이터를 기록할 현재 또는 대기 중인 GameSession 객체를 반환하는 함수
	UMD_SaveGameSession* GetMutableEquipmentSessionData();

	// Equipment 데이터를 복원할 현재 또는 대기 중인 GameSession 객체를 반환하는 함수
	const UMD_SaveGameSession* GetEquipmentSessionData() const;

	// 세션 저장 직전에 접속 중인 모든 Player의 최신 Equipment 상태를 수집하는 함수
	void CaptureAllConnectedPlayerEquipment();
};
