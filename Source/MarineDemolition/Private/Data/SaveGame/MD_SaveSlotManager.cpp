// Fill out your copyright notice in the Description page of Project Settings.


#include "Data/SaveGame/MD_SaveSlotManager.h"
#include "Kismet/GameplayStatics.h"
#include "Data/SaveGame/MD_SaveSlotInfoList.h"
#include "Data/SaveGame/MD_SaveGameSession.h"
#include "Game/MD_GameStateBase.h"
#include "Util/MD_LogUtility.h"

void UMD_SaveSlotManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 초기 Auto Save 인덱스는 0으로 설정
	NextAutoSaveIndex = 0;

	// 사용자가 설정한 세이브 슬롯 이름 목록 불러오기
	LoadSlotInfoList();
}

TArray<FMD_SaveSlotInfo> UMD_SaveSlotManager::GetSaveSlotInfoList() const
{
	// 전체 세이브 슬롯의 이름을 저장하는 배열
	TArray<FMD_SaveSlotInfo> OutSlotInfoList;

	// 자동 저장으로 생성된 세이브 슬롯 정보 추가
	OutSlotInfoList.Append(AutoSlotInfoList);

	// 사용자가 저장한 세이브 슬롯 정보 추가
	OutSlotInfoList.Append(SlotInfoList);

	return OutSlotInfoList;
}

bool UMD_SaveSlotManager::SaveSlotInfoList()
{
	// Save Slot 정보 목록이 저장되는 Slot 이름
	const FString SlotName = TEXT("SlotInfoList");

	// Save Slot 정보 목록을 저장하는 객체 생성 및 유효성 검사
	UMD_SaveSlotInfoList* SlotInfoListData = NewObject<UMD_SaveSlotInfoList>(this);
	if (IsValid(SlotInfoListData) == false)
	{
		// 로그 출력
		UE_LOG(MarineDemolition, Warning, TEXT("%s UMD_SaveSlotInfoList 타입의 객체가 생성되지 않았습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// 저장 데이터 설정
	SlotInfoListData->SlotInformations = SlotInfoList;
	SlotInfoListData->AutoSlotInformations = AutoSlotInfoList;

	// 데이터 저장
	bool bSaveSuccess = UGameplayStatics::SaveGameToSlot(SlotInfoListData, SlotName, 0);

	// 데이터 저장 성공 여부 검사
	if (bSaveSuccess == false)
	{
		// 로그 출력
		UE_LOG(MarineDemolition, Warning, TEXT("%s UMD_SaveSlotInfoList 데이터 저장에 실패하였습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	return true;
}

bool UMD_SaveSlotManager::SaveCurrentGameSessionWithName(const FString& SlotName, bool bIsOverWrite)
{
	// GameSession 데이터 저장
	TSharedPtr<FMD_SaveSlotInfo> SaveSlotInfo = SaveCurrentGameSessionInternal(SlotName, bIsOverWrite);

	// 데이터 저장 성공 여부 검사
	if (SaveSlotInfo.IsValid() == false)
	{
		// 로그 출력
		UE_LOG(MarineDemolition, Warning, TEXT("%s Game Session 데이터 저장에 실패하였습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// 덮어쓰기의 경우 기존 중복되는 세이브 슬롯 정보 삭제
	if (bIsOverWrite == true)
	{
		SlotInfoList.Remove(*SaveSlotInfo);
	}

	// Game Session의 데이터가 저장된 슬롯의 정보를 목록에 추가
	SlotInfoList.Insert(*SaveSlotInfo, 0);

	// 저장된 슬롯의 개수가 기준치를 초과하는 경우
	if (SlotInfoList.Num() > MaxSaveSlotCount)
	{
		// 가장 오래된 Save Slot 제거
		FMD_SaveSlotInfo OldestSlotInfo = SlotInfoList.Pop();
		UGameplayStatics::DeleteGameInSlot(OldestSlotInfo.SlotName, 0);
	}

	return true;
}

bool UMD_SaveSlotManager::SaveCurrentGamesessionByAuto()
{
	// 자동 저장 슬롯의 이름 설정
	FString SlotName = FString::Printf(TEXT("%s_%d"), *AutoSaveSlotPrefix, NextAutoSaveIndex + 1);

	// GameSession 데이터 저장
	TSharedPtr<FMD_SaveSlotInfo> SaveSlotInfo = SaveCurrentGameSessionInternal(SlotName, true);

	// 데이터 저장 성공 여부 검사
	if (SaveSlotInfo.IsValid() == false)
	{
		// 로그 출력
		UE_LOG(MarineDemolition, Warning, TEXT("%s 자동 저장에 실패하였습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// 자동 저장 인덱스가 유효한 경우 해당 인덱스 위치의 세이브 슬롯 정보 업데이트
	if (AutoSlotInfoList.IsValidIndex(NextAutoSaveIndex) == true)
	{
		AutoSlotInfoList[NextAutoSaveIndex] = *SaveSlotInfo;
	}
	else
	{
		// 목록의 뒤쪽에 세이브 슬롯 정보 추가
		AutoSlotInfoList.Add(*SaveSlotInfo);
	}

	// 자동저장 인덱스 증가
	NextAutoSaveIndex = (NextAutoSaveIndex + 1) % MaxAutoSaveSlotCount;

	return true;
}

bool UMD_SaveSlotManager::PrepareGameSessionForHosting(const FString& SlotName)
{
	// 슬롯 파일을 읽고 타입 검증까지 완료한 객체를 가져온다.
	UMD_SaveGameSession* const LoadedGameSession = LoadGameSessionDataInternal(SlotName);

	if (IsValid(LoadedGameSession) == false)
	{
		return false;
	}

	// GameInstanceSubsystem이 객체를 참조해 비동기 세션 생성과 맵 이동 동안 수명을 보장한다.
	return true;
}

bool UMD_SaveSlotManager::LoadSlotInfoList()
{
	// Save Slot 정보 목록이 저장된 Slot 이름
	const FString SlotName = TEXT("SlotInfoList");

	// Save Slot 정보가 저장된 데이터 불러오기
	USaveGame* SaveData = UGameplayStatics::LoadGameFromSlot(SlotName, 0);
	UMD_SaveSlotInfoList* SlotInfoListData = Cast<UMD_SaveSlotInfoList>(SaveData);

	// 불러온 데이터 유효성 검사
	if (IsValid(SlotInfoListData) == false)
	{
		// 로그 출력
		UE_LOG(MarineDemolition, Warning, TEXT("%s (%s)이름으로 저장된 Save 데이터가 존재하지 않습니다."), *UE_CALL_LOCATIONINFO, *SlotName);
		return false;
	}
	
	// 세이브 슬롯 정보 목록 저장
	SlotInfoList = SlotInfoListData->SlotInformations;
	AutoSlotInfoList = SlotInfoListData->AutoSlotInformations;

	return true;
}

TSharedPtr<FMD_SaveSlotInfo> UMD_SaveSlotManager::SaveCurrentGameSessionInternal(const FString& SlotName, bool bIsOverWrite)
{
	// 동일한 이름의 세이브 슬롯 존재 검사
	bool bIsExist = UGameplayStatics::DoesSaveGameExist(SlotName, 0);

	// 덮어쓰기 설정 검사
	if (bIsOverWrite == false && bIsExist == true)
	{
		// 로그 출력
		UE_LOG(MarineDemolition, Warning, TEXT("%s 동일한 이름(%s)으로 저장된 세이브 데이터가 존재합니다."), *UE_CALL_LOCATIONINFO, *SlotName);
		return nullptr;
	}

	// 현재 Session의 GameState 얻기 및 유효성 확인
	UWorld* CurrentWorld = GetWorld();
	AMD_GameStateBase* SessionGameState = IsValid(CurrentWorld) == true ? CurrentWorld->GetGameState<AMD_GameStateBase>() : nullptr;
	if (IsValid(SessionGameState) == false)
	{
		// 로그 출력
		UE_LOG(MarineDemolition, Error, TEXT("%s 현재 World의 GameState[AMD_GameStateBase]가 존재하지 않습니다."), *UE_CALL_LOCATIONINFO);
		return nullptr;
	}

	// 클라이언트 환경 실행 제한
	if (SessionGameState->HasAuthority() == false)
	{
		// 로그 출력
		UE_LOG(MarineDemolition, Warning, TEXT("%s Server에서만 Session 데이터를 저장할 수 있습니다."), *UE_CALL_LOCATIONINFO);
		return nullptr;
	}

	// Game Session의 데이터를 저장하는 객체 생성
	UMD_SaveGameSession* GameSessionData = NewObject<UMD_SaveGameSession>(this);

	// 저장되는 데이터 설정
	GameSessionData->Score = SessionGameState->GetScore();
	GameSessionData->Credit = 0;
	GameSessionData->MerchantLikeability = 0;
	GameSessionData->ElapsedDays = SessionGameState->GetCurrentDay();

	// 데이터 저장
	bool bSaveSuccess = UGameplayStatics::SaveGameToSlot(GameSessionData, SlotName, 0);

	// 데이터 저장 성공 여부 검사
	if (bSaveSuccess == false)
	{
		// 로그 출력
		UE_LOG(MarineDemolition, Warning, TEXT("%s Session 데이터 저장에 실패하였습니다."), *UE_CALL_LOCATIONINFO);
		return nullptr;
	}

	// Game Session 데이터를 저장한 세이브 슬롯의 정보를 담은 MD_SaveSlotInfo 객체 생성
	TSharedPtr<FMD_SaveSlotInfo> SaveSlotInfo = MakeShared<FMD_SaveSlotInfo>();
	SaveSlotInfo->SlotName = SlotName;
	SaveSlotInfo->QuestID = FPrimaryAssetId();

	return SaveSlotInfo;
}

UMD_SaveGameSession* UMD_SaveSlotManager::LoadGameSessionDataInternal(const FString& SlotName)
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
