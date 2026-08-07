// 프로젝트 설정의 저작권 안내 문구를 사용한다.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Types/SaveGame/MD_SaveDataTypes.h"
#include "MD_SaveSlotManager.generated.h"

class FSQLiteDatabase;

// 비동기 저장 완료 시 생성된 슬롯과 성공 여부를 호출부에 전달한다.
DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnSaveMDGameSessionAsyncComplete, const FMD_SaveSlot&, SaveSlot, bool, bSuccess);

// 비동기 로드 완료 시 적용한 슬롯과 성공 여부를 호출부에 전달한다.
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FOnLoadMDGameSessionAsyncComplete, const FMD_SaveSlot&, SaveSlot, const UMD_SaveGameSession*, GameSessionData, bool, bSuccess);

// 저장 슬롯을 사용자 저장과 자동 저장 목록 중 어디에 등록할지 구분한다.
UENUM(BlueprintType)
enum class EMD_SaveSlotType : uint8
{
	User UMETA(DisplayName = "User Save"),
	Auto UMETA(DisplayName = "Auto Save")
};

/**
 * Host의 서버 권위 GameSession 상태를 슬롯 파일로 저장하고 현재 게임에 로드한다.
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
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Save GameSession", meta = (ClampMin = "0", ClampMax = "3"))
	int32 MaxAutoSaveSlotCount = 3;

	// 사용자 저장 목록에 할당할 수 있는 최대 슬롯 개수이며 0이면 사용자 저장 목록을 사용하지 않는다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Save GameSession", meta = (ClampMin = "0", ClampMax = "5"))
	int32 MaxUserSaveSlotCount = 5;

protected:
	// 개별 게임 데이터 슬롯과 충돌하지 않도록 슬롯 목록에 사용하는 고정 내부 파일명이다.
	static const FString SlotListSaveFileName;

	// SQLite GameSession 데이터베이스가 Host의 Saved 디렉터리에서 사용할 고정 파일명이다.
	static const FString SQL_DatabaseFileName;

	// 저장 행과 데이터베이스에 기록하여 향후 구조 변경 시 호환 여부를 판단하는 현재 스키마 버전이다.
	static constexpr int32 SQL_SchemaVersion = 1;

	// 동시에 실행된 저장과 로드가 활성 슬롯을 서로 덮어쓰지 않도록 진행 상태를 제한한다.
	bool bAsyncOperationInProgress = false;

	// 세이브 슬롯 목록을 보관하고 있는 USaveGame 객체
	UPROPERTY(Transient)
	TObjectPtr<UMD_SaveSlotList> SlotList;

	// 마지막으로 비동기 저장 또는 로드에 성공한 슬롯을 후속 세션 처리에서 참조한다.
	TSharedPtr<FMD_SaveSlot> ActiveSlot;

	// 마지막으로 비동기 저장 또는 로드에 성공한 게임 데이터를 GC로부터 보호한다.
	UPROPERTY(Transient)
	TObjectPtr<UMD_SaveGameSession> ActiveGameSessionData;

public:
	// 현재 서버 게임 내용을 Host 로컬 파일에 저장하고 생성된 슬롯 메타데이터를 반환한다.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Save GameSession")
	FMD_SaveSlot SaveGameSessionData(const FString& SaveSlotName, EMD_SaveSlotType SaveSlotType = EMD_SaveSlotType::User) const;

	// 슬롯 파일의 게임 내용을 현재 서버 GameState와 접속 플레이어에게 복원한다.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Save GameSession")
	bool LoadGameSessionData(const FMD_SaveSlot& SaveSlot) const;

	// 현재 서버 게임 내용을 수집한 뒤 Host 로컬 파일 쓰기를 비동기로 실행한다.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Save GameSession")
	void SaveGameSessionDataAsync(const FString& SaveSlotName, EMD_SaveSlotType SaveSlotType, const FOnSaveMDGameSessionAsyncComplete& CompletionDelegate);

	// 슬롯 파일을 비동기로 읽은 뒤 현재 서버 GameState와 접속 플레이어에게 복원한다.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Save GameSession")
	void LoadGameSessionDataAsync(const FMD_SaveSlot& SaveSlot, const FOnLoadMDGameSessionAsyncComplete& CompletionDelegate);

	// 로드된 게임 데이터에 존재하는 플레이어의 데이터를 현재 플레이어에게 덮어쓰는 함수
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Save GameSession")
	bool OverwriteDataToPlayer(const APlayerState* PlayerState) const;

	// 로드된 게임 데이터에 존재하는 플레이어의들의 데이터를 전체 플레이어에게 덮어쓰는 함수
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Save GameSession")
	bool OverwriteDataToAllPlayer() const;

protected:
	// 저장된 슬롯을 종류별 목록에 등록하고 최대 개수 초과 시 가장 오래된 슬롯 파일을 삭제한다.
	bool AddSlotToList(const FMD_SaveSlot& SaveSlot, EMD_SaveSlotType SaveSlotType) const;

	// 세이브 슬롯 목록이 저장된 세이브 파일을 불러오는 함수
	void LoadSlotList();

	// 저장해야 하는 GameSession 데이터를 수집하는 함수
	bool CollectGameSessionData(FMD_SaveSlot& OutSaveSlot, UMD_SaveGameSession* OutGameSessionData) const;

	// 비동기 방식으로 GameSession 데이터 저장이 완료된 경우의 콜백 함수
	UFUNCTION()
	void HandleSaveGameSessionDataComplete(const FString& SavedSlotName, const int32 UserIndex, bool bSaveSuccess, const FMD_SaveSlot SaveSlot, EMD_SaveSlotType SaveSlotType, const FOnSaveMDGameSessionAsyncComplete CompletionDelegate);

	// 비동기 방식으로 GameSession 데이터 로드가 완료된 경우의 콜백 함수
	UFUNCTION()
	void HandleLoadGameSessionDataComplete(const FString& LoadedSlotName, const int32 UserIndex, USaveGame* LoadedSaveGame, const FMD_SaveSlot SaveSlot, const FOnLoadMDGameSessionAsyncComplete CompletionDelegate);
};
