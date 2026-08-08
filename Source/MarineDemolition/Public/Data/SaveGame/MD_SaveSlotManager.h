// 프로젝트 설정의 저작권 안내 문구를 사용한다.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Types/SaveGame/MD_SaveDataTypes.h"
#include "MD_SaveSlotManager.generated.h"

// 비동기 저장 완료 시 생성된 슬롯과 성공 여부를 호출부에 전달한다.
DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnSaveMDGameDataComplete, const FMD_GameData&, GameData, bool, bSuccess);

// 비동기 로드 완료 시 적용한 슬롯과 성공 여부를 호출부에 전달한다.
DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnLoadMDGameDataComplete, const FMD_GameData&, GameData, bool, bSuccess);

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

	UPROPERTY()
	TObjectPtr<UMD_SaveGame> SaveFile;

	// 불러온 게임 데이터 또는 최근에 저장한 게임 데이터
	// 플레이어에게 이전 데이터가 적용되는 기준
	TSharedPtr<FMD_GameData> ActiveGameData;

	// 이후 게임 데이터 저장 또는 로드 전까지 있었던 모든 플레이어의 데이터를 저장하는 변수
	// Ex) 게임 데이터 저장 이전에 접속을 종료한 플레이어의 데이터
	TMap<FString, FMD_PlayerData> PlayerDataLogMap;

	int32 AutoSaveIndex = 0;

public:
	// 현재 서버 게임 내용을 Host 로컬 파일에 저장한다.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Save Game")
	bool SaveGameData(const FString& DataName, bool bIsAuto = false);

	// 저장된 게임 데이터 내용을 현재 서버의 GameState와 접속 플레이어에게 적용한다.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Save Game")
	bool LoadGameDataToWorld(const FMD_GameData& GameData);

	// 저장된 게임 데이터 내용을 현재 서버의 GameState와 접속 플레이어에게 적용한다.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Save Game")
	bool LoadGameData(const FString& DataName);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Save Game")
	bool RemoveGameData(const FMD_GameData& GameData);

	// 현재의 게임 데이터를 수집하고 비동기로 세이브 파일에 저장하는 함수
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Save GameSession")
	void AsyncSaveGameData(const FString& DataName, bool bIsAuto, const FOnSaveMDGameDataComplete& CompletionDelegate);

	// 저장되어있는 게임 데이터를 비동기로 읽은 뒤 현재 서버 GameState와 접속 플레이어에게 적용한다.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Save GameSession")
	void AsyncLoadGameData(const FString& DataName, const FOnLoadMDGameDataComplete& CompletionDelegate);

	// 현재 플레이어로부터 플레이어 데이터를 추출하는 함수
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Save Game")
	bool ParseDataFromPlayer(const APlayerState* PlayerState);

	// 로드된 게임 데이터에 존재하는 플레이어의 데이터를 현재 플레이어에게 덮어쓰는 함수
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Save Game")
	bool OverwriteDataToPlayer(const APlayerState* PlayerState) const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Save Game")
	TArray<FMD_GameData> LoadAllGameData();

protected:
	// 저장해야 하는 GameSession 데이터를 수집하는 함수
	bool CollectGameSessionData(FMD_GameData& OutGameData);

	// 비동기 방식으로 Game 데이터 저장이 완료된 경우의 콜백 함수
	UFUNCTION()
	void HandleLoadGameToSaveDataComplete(const FString& LoadedSlotName, const int32 UserIndex, USaveGame* LoadedSaveGame, const FMD_GameData CollectedGameData, const bool bIsAuto, const FOnSaveMDGameDataComplete SaveCompletionDelegate);

	// 비동기 방식으로 Game 데이터 로드가 완료된 경우의 콜백 함수
	UFUNCTION()
	void HandleLoadGameToLoadDataComplete(const FString& LoadedSlotName, const int32 UserIndex, USaveGame* LoadedSaveGame, const FString DataName, const FOnLoadMDGameDataComplete SaveCompletionDelegate);
};
