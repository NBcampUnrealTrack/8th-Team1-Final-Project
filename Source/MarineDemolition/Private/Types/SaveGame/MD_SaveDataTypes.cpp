// Fill out your copyright notice in the Description page of Project Settings.


#include "Types/SaveGame/MD_SaveDataTypes.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/SaveGame.h"

#include "Util/MD_LogUtility.h"

const FMD_SaveSlot FMD_SaveSlot::Empty = FMD_SaveSlot();

const bool FMD_SaveSlot::IsEmpty() const
{
	if (SlotName.IsEmpty() == true || MapPackagePath.IsEmpty() == true)
	{
		return true;
	}

	bool bIsEmpty = UGameplayStatics::DoesSaveGameExist(SlotName, 0);

	return bIsEmpty == false;
}

USaveGame* FMD_SaveSlot::LoadGameSessionData() const
{
	if (SlotName.IsEmpty() == true)
	{
		return nullptr;
	}

	// SlotName으로 저장된 세이브 파일이 존재하는 지 검사
	bool bSaveGameExist = UGameplayStatics::DoesSaveGameExist(SlotName, 0);

	// 세이브 파일이 존재하지 않는 경우 함수 조기 종료
	if (bSaveGameExist == false)
	{
		// 로그 출력
		UE_LOG(MarineDemolition, Warning, TEXT("%s (%s)이름으로 저장된 세이브 파일이 존재하지 않습니다."), *UE_CALL_LOCATIONINFO, *SlotName);
		return nullptr;
	}

	// 세이브 데이터 불러오기
	USaveGame* GameData = UGameplayStatics::LoadGameFromSlot(SlotName, 0);

	return IsValid(GameData) == true ? GameData : nullptr;
}

bool FMD_SaveSlot::SaveGameSessionData(USaveGame* GameData, bool bOverwrite)
{
	if (SlotName.IsEmpty() == true || IsValid(GameData) == false)
	{
		return false;
	}

	// SlotName으로 저장된 세이브 파일이 존재하는 지 검사
	bool bSaveGameExist = UGameplayStatics::DoesSaveGameExist(SlotName, 0);

	// 덮어쓰기 검사
	if (bOverwrite == false && bSaveGameExist == true)
	{
		// 로그 출력
		UE_LOG(MarineDemolition, Warning, TEXT("%s (%s)이름으로 저장된 세이브 파일이 중복됩니다."), *UE_CALL_LOCATIONINFO, *SlotName);
		return false;
	}

	// 세이브 데이터 저장
	bool bSaveSuccess = UGameplayStatics::SaveGameToSlot(GameData, SlotName, 0);

	// 데이터 저장 시각 갱신
	SaveTime = bSaveSuccess == true ? FDateTime::Now() : SaveTime;

	return bSaveSuccess;
}
