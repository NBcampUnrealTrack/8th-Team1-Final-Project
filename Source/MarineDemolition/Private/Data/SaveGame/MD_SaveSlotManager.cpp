// Fill out your copyright notice in the Description page of Project Settings.


#include "Data/SaveGame/MD_SaveSlotManager.h"
#include "Kismet/GameplayStatics.h"
#include "Data/SaveGame/MD_SaveSlotName.h"
#include "Data/SaveGame/MD_SaveGameSession.h"
#include "Game/MD_GameStateBase.h"
#include "Util/MD_LogUtility.h"

void UMD_SaveSlotManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	AutoSaveSlotIndex = 0;
	LoadSaveSlotNames();
}

TArray<FString> UMD_SaveSlotManager::GetSaveSlotNames() const
{
	// 전체 세이브 슬롯의 이름을 저장하는 배열
	TArray<FString> TotalSaveSlotNames;

	// 자동으로 저장된 세이브 슬롯 존재 검사
	for (int32 i = 0; i < MaxAutoSaveSlotCount; ++i)
	{
		// 자동 저장 슬롯의 이름 설정
		FString AutoSlotName = FString::Printf(TEXT("%s_%d"), *AutoSaveSlotPrefix, i + 1);

		// 이름이 일치하는 SaveSlot 존재 검사
		bool bAutoSaveExist = UGameplayStatics::DoesSaveGameExist(AutoSlotName, 0);
		if (bAutoSaveExist == true)
		{
			// 세이브 슬롯 이름 목록에 추가
			TotalSaveSlotNames.Add(AutoSlotName);
		}
	}

	// 사용자가 임의로 설정한 세이브 슬롯 이름 목록 추가
	TotalSaveSlotNames.Append(SaveSlotNames);

	return TotalSaveSlotNames;
}

bool UMD_SaveSlotManager::LoadSaveSlotNames()
{
	// Save Slot 이름 목록이 저장된 SaveGame 데이터 불러오기
	USaveGame* SaveData = UGameplayStatics::LoadGameFromSlot(TEXT("SaveSlotNames"), 0);
	UMD_SaveSlotName* SlotNameSaveData = Cast<UMD_SaveSlotName>(SaveData);
	if (IsValid(SlotNameSaveData) == false)
	{
		// 로그 출력
		UE_LOG(MarineDemolition, Warning, TEXT("%s (SaveSlotName)이름으로 저장된 Save 데이터가 존재하지 않습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}
	
	// 세이브 슬롯 이름 목록 저장
	SaveSlotNames = SlotNameSaveData->SaveSlotNames;
	return true;
}

bool UMD_SaveSlotManager::SaveSlotNameSaveData()
{
	// Save Slot 이름 목록을 저장하는 SaveGame 객체 생성
	UMD_SaveSlotName* SlotNameSaveData = NewObject<UMD_SaveSlotName>(this);
	if (IsValid(SlotNameSaveData) == false)
	{
		// 로그 출력
		UE_LOG(MarineDemolition, Warning, TEXT("%s UMD_SaveSlotName 타입의 세이브 데이터 객체가 생성되지 않았습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// 저장 데이터 설정
	SlotNameSaveData->SaveSlotNames = SaveSlotNames;

	// 데이터 저장
	bool bSaveSuccess = UGameplayStatics::SaveGameToSlot(SlotNameSaveData, TEXT("SaveSlotNames"), 0);

	// 데이터 저장 성공 여부 검사
	if (bSaveSuccess == false)
	{
		// 로그 출력
		UE_LOG(MarineDemolition, Warning, TEXT("%s 세이브 슬롯 이름 목록 저장에 실패하였습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	return true;
}

UMD_SaveGameSession* UMD_SaveSlotManager::LoadGameSessionData(const FString& SlotName)
{
	bool bIsSaveSlotExist = UGameplayStatics::DoesSaveGameExist(SlotName, 0);
	if (bIsSaveSlotExist == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s (%s)이름으로 저장된 Save 데이터가 존재하지 않습니다."), *UE_CALL_LOCATIONINFO, *SlotName);
		return nullptr;
	}

	// SlotName 이름의 Slot으로 부터 저장된 데이터 불러오기
	USaveGame* SaveData = UGameplayStatics::LoadGameFromSlot(SlotName, 0);

	// 불러온 데이터를 MD_SaveGameSession으로 변환 및 유효성 검사
	UMD_SaveGameSession* GameSessionData = Cast<UMD_SaveGameSession>(SaveData);
	if (IsValid(GameSessionData) == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 불러온 데이터를 MD_SaveGameSession 타입으로 변환할 수 없습니다."), *UE_CALL_LOCATIONINFO);
		return nullptr;
	}

	return GameSessionData;
}

bool UMD_SaveSlotManager::SaveCurrentGameSessionWithName(const FString& SlotName, bool bIsOverWrite)
{
	// 동일한 이름의 세이브 슬롯 존재 검사
	bool bIsExist = UGameplayStatics::DoesSaveGameExist(SlotName, 0);

	// 덮어쓰기 설정 검사
	if (bIsOverWrite == false && bIsExist == true)
	{
		// 로그 출력
		UE_LOG(MarineDemolition, Warning, TEXT("%s 동일한 이름으로 저장된 세이브 데이터기 존재합니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// GameSession 데이터 저장
	bool bSaveWasSuccess = SaveCurrentGameSessionInternal(SlotName);

	// 데이터 저장 성공 여부 검사
	if (bSaveWasSuccess == false)
	{
		// 로그 출력
		UE_LOG(MarineDemolition, Warning, TEXT("%s GameSession 데이터 저장에 실패하였습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// 덮어쓰기의 경우 기존 중복되는 이름의 세이브 슬롯 삭제
	if (bIsExist == true)
	{
		SaveSlotNames.Remove(SlotName);
	}

	// 저장된 슬롯의 이름 추가
	SaveSlotNames.Insert(SlotName, 0);

	// 저장된 슬롯의 개수가 기준치를 초과하는 경우
	if (SaveSlotNames.Num() > MaxSaveSlotCount)
	{
		// 기존 Save Slot 제거
		FString DeletedSlotName = SaveSlotNames.Pop();
		UGameplayStatics::DeleteGameInSlot(DeletedSlotName, 0);
	}

	return true;
}

bool UMD_SaveSlotManager::SaveCurrentGamesessionByAuto()
{
	// 자동 저장 슬롯의 이름 설정
	FString AutoSlotName = FString::Printf(TEXT("%s_%d"), *AutoSaveSlotPrefix, AutoSaveSlotIndex + 1);

	// GameSession 데이터 저장
	bool bSaveWasSuccess = SaveCurrentGameSessionInternal(AutoSlotName);

	// 데이터 저장 성공 여부 검사
	if (bSaveWasSuccess == false)
	{
		// 로그 출력
		UE_LOG(MarineDemolition, Warning, TEXT("%s 자동 저장에 실패하였습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// 자동저장 인덱스 증가
	AutoSaveSlotIndex = (AutoSaveSlotIndex + 1) % MaxAutoSaveSlotCount;
	return true;
}

bool UMD_SaveSlotManager::SaveCurrentGameSessionInternal(const FString& SlotName)
{
	// 현재 Session의 GameState 얻기 및 유효성 확인
	UWorld* CurrentWorld = GetWorld();
	AMD_GameStateBase* SessionGameState = IsValid(CurrentWorld) == true ? CurrentWorld->GetGameState<AMD_GameStateBase>() : nullptr;
	if (IsValid(SessionGameState) == false)
	{
		// 로그 출력
		UE_LOG(MarineDemolition, Error, TEXT("%s 현재 World의 GameState[AMD_GameStateBase]가 존재하지 않습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// 클라이언트 환경 실행 제한
	if (SessionGameState->HasAuthority() == false)
	{
		// 로그 출력
		UE_LOG(MarineDemolition, Warning, TEXT("%s Server에서만 Session 데이터를 저장할 수 있습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// 저장할 데이터 Instance 생성
	UMD_SaveGameSession* GameSessionData = NewObject<UMD_SaveGameSession>(this);

	// 저장되는 데이터 설정
	GameSessionData->Score = SessionGameState->GetScore();
	GameSessionData->Credit = 0;
	GameSessionData->MerchantLikeability = 0;
	GameSessionData->MissionID = FPrimaryAssetId();
	GameSessionData->ElapsedDays = SessionGameState->GetCurrentDay();

	// 데이터 저장
	bool bSaveSuccess = UGameplayStatics::SaveGameToSlot(GameSessionData, SlotName, 0);

	// 데이터 저장 성공 여부 검사
	if (bSaveSuccess == false)
	{
		// 로그 출력
		UE_LOG(MarineDemolition, Warning, TEXT("%s Session 데이터 저장에 실패하였습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	return true;
}
