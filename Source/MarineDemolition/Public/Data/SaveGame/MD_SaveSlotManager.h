// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Types/SaveGame/MD_SaveDataTypes.h"
#include "MD_SaveSlotManager.generated.h"

// 게임 데이터 비동기 저장 완료 시 성공 여부를 호출부에 전달한다.
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnSaveMDGameDataComplete, bool, bWasSuccessful);
// 게임 데이터 비동기 제거 완료 시 성공 여부를 호출부에 전달한다.
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnRemoveMDGameDataComplete, bool, bWasSuccessful);
// 게임 데이터 목록을 비동기 로드 완료 시 데이터 목록과 함께 성공 여부를 호출부에 전달한다.
DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnLoadMDGameDataListComplete, const TArray<FMD_GameData>&, GameDataList, bool, bWasSuccessful);

// 맵 이동 이후 실행할 게임 데이터 초기화 방식을 구분하는 상태
enum class EMD_PendingGameDataOperation : uint8
{
	// 목적지 맵에서 실행할 초기화 작업이 없는 상태
	None,

	// 목적지 맵의 기본 상태로 임시 게임 데이터를 생성하는 작업
	NewGame,

	// 현재 진행중인 임시 게임 데이터를 목적지 맵에 적용하는 작업
	ContinueGame,

	// 목적지 맵에 선택한 저장 데이터를 적용하는 작업
	LoadGame
};

/**
 * Host의 서버 권위 GameSession 상태를 슬롯 파일로 저장하고 현재 게임에 로드한다.
 * PostSeamlessTravel에서 플레이어 캐릭터에게 데이터 적용하기
 */
UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_SaveSlotManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;

public:
	// 자동 저장 목록에 할당할 수 있는 최대 슬롯 개수이며 0이면 자동 저장 목록을 사용하지 않는다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save GameSession", meta = (ClampMin = "1", ClampMax = "9"))
	int32 MaxAutoSaveSlotCount = 3;

	// 사용자 저장 목록에 할당할 수 있는 최대 슬롯 개수이며 0이면 사용자 저장 목록을 사용하지 않는다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save GameSession", meta = (ClampMin = "1", ClampMax = "20"))
	int32 MaxUserSaveSlotCount = 5;

protected:
	// 세이브 파일 이름
	static const FString GameSaveFileName;

	// 비동기 함수 실행 상태를 나타내는 플래그
	bool bAsyncOperationInProgress = false;

	// 자동 저장 목록에서 다음 저장이 덮어쓸 위치
	int32 AutoSaveIndex = 0;

	// 불러오기로 선택되어 검증을 통과한 게임 데이터
	TSharedPtr<FMD_GameData> SelectedLoadGameData;

	// 현재 진행중인 게임의 데이터를 저장(기록)하는 변수
	// 저장되기 이전의 게임 데이터가 기록됨 (로그인/로그아웃 플레이어 등)
	TSharedPtr<FMD_GameData> TemporaryGameData;

	// 목적지 맵에서 실행할 게임 데이터 초기화 작업
	EMD_PendingGameDataOperation PendingGameDataOperation = EMD_PendingGameDataOperation::None;

public:
	// 현재의 게임 데이터를 수집하고 세이브 파일에 저장하는 함수
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Save Game")
	bool SaveGameData(const FString& DataName, bool bIsAuto = false);

	// 세이브 파일에 저장된 임의 게임 데이터를 제거하는 함수
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Save Game")
	bool RemoveGameData(const FMD_GameData& GameData);

	// 세이브 파일에 저장된 전체 게임 데이터 목록을 불러오는 함수
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Save Game")
	TArray<FMD_GameData> LoadAllGameDataList();

	// 현재의 게임 데이터를 수집하고 비동기로 세이브 파일에 저장하는 함수
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Save Game")
	void AsyncSaveGameData(const FString& DataName, bool bIsAuto, const FOnSaveMDGameDataComplete& CompletionDelegate);

	// 세이브 파일에 저장된 임의 게임 데이터를 비동기로 제거하는 함수
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Save Game")
	void AsyncRemoveGameData(const FMD_GameData& GameData, const FOnRemoveMDGameDataComplete& CompletionDelegate);

	// 세이브 파일에 저장된 전체 게임 데이터 목록을 비동기로 불러오는 함수
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Save Game")
	void AsyncLoadAllGameDataList(const FOnLoadMDGameDataListComplete& CompletionDelegate);

	// 저장된 게임 데이터 내용을 현재 서버의 GameState와 접속 플레이어에게 적용한다.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Save Game")
	bool LoadGameDataToWorld(const FMD_GameData& GameData);

	// 현재 플레이어로부터 플레이어 데이터를 추출하는 함수
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Save Game")
	bool ParseDataFromPlayer(const APlayerState* PlayerState);

	// 로드된 게임 데이터에 존재하는 플레이어의 데이터를 현재 플레이어에게 덮어쓰는 함수
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Save Game")
	bool LoadPlayerData(const APlayerState* PlayerState) const;

	// 불러오기 여부와 현재 진행 데이터 존재 여부에 따라 맵 적용 작업을 예약하고 목적지 맵을 반환하는 함수
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Save Game")
	bool OnTryStartGameWithGameData(const FMD_GameData& GameData, bool bLoadGame, FString& OutMapPackagePath);

	// 현재 진행중인 게임 데이터의 존재 여부를 반환하는 함수
	UFUNCTION(BlueprintPure, Category = "Save Game")
	bool HasTemporaryGameData() const;

	// Session 시작 실패 시 현재 진행 데이터는 유지하고 보류 작업만 취소하는 함수
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Save Game")
	void CancelPendingGameDataOperation();

	// 현재 기록되고 있던 게임 데이터를 초기화하는 함수
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Save Game")
	void ResetTemporaryGameData();

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Save Game")
	// 목적지 게임 맵의 액터 초기화 이후 임시 데이터 생성 또는 저장 데이터 적용을 완료하는 함수
	bool ApplyPendingGameDataOperation();

protected:
	// 저장 슬롯 작업을 실행할 수 있는 Host 또는 서버 World인지 확인하는 함수
	bool HasServerAuthority() const;

	// 저장해야 하는 GameSession 데이터를 수집하는 함수
	bool CollectGameData();

	// 게임 데이터에 존재하는 플레이어의 데이터를 현재 플레이어에게 덮어쓰는 함수
	bool OverwriteDataToPlayerInternal(const APlayerState* PlayerState, const FMD_GameData& GameData) const;

	// 저장 슬롯과 저장되지 않은 현재 회차에 필요한 검증 기준을 구분하여 World에 적용하는 함수
	bool ApplyGameDataToWorldInternal(const FMD_GameData& GameData, bool bRequireSavedSlotIdentity);

};
