// 프로젝트 설정의 저작권 안내 문구를 사용한다.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Types/SaveGame/MD_SaveDataTypes.h"
#include "MD_SaveSlotManager.generated.h"

// 비동기 저장 완료 시 생성된 슬롯과 성공 여부를 호출부에 전달한다.
DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnSaveMDGameSessionAsyncComplete, const FMD_SaveSlot&, SaveSlot, bool, bSuccess);

// 비동기 로드 완료 시 적용한 슬롯과 성공 여부를 호출부에 전달한다.
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FOnLoadMDGameSessionAsyncComplete, const FMD_SaveSlot&, SaveSlot, const UMD_SaveGameSession*, GameSessionData, bool, bSuccess);

/**
 * Host의 서버 권위 GameSession 상태를 슬롯 파일로 저장하고 현재 게임에 복원한다.
 */
UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_SaveSlotManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()

// 변수
protected:
	// 마지막으로 비동기 저장 또는 로드에 성공한 슬롯을 후속 세션 처리에서 참조한다.
	TSharedPtr<FMD_SaveSlot> ActiveSlot;

	// 마지막으로 비동기 저장 또는 로드에 성공한 게임 데이터를 GC로부터 보호한다.
	UPROPERTY(Transient)
	TObjectPtr<UMD_SaveGameSession> ActiveGameSessionData;
	
	// 동시에 실행된 저장과 로드가 활성 슬롯을 서로 덮어쓰지 않도록 진행 상태를 제한한다.
	bool bAsyncOperationInProgress = false;

public:
	// 현재 서버 게임 내용을 Host 로컬 파일에 저장하고 생성된 슬롯 메타데이터를 반환한다.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Save GameSession")
	FMD_SaveSlot SaveGameSessionData(const FString& SaveSlotName) const;

	// 슬롯 파일의 게임 내용을 현재 서버 GameState와 접속 플레이어에게 복원한다.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Save GameSession")
	bool LoadGameSessionData(const FMD_SaveSlot& SaveSlot) const;

	// 현재 서버 게임 내용을 수집한 뒤 Host 로컬 파일 쓰기를 비동기로 실행한다.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Save GameSession")
	void SaveGameSessionDataAsync(const FString& SaveSlotName, const FOnSaveMDGameSessionAsyncComplete& CompletionDelegate);

	// 슬롯 파일을 비동기로 읽은 뒤 현재 서버 GameState와 접속 플레이어에게 복원한다.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Save GameSession")
	void LoadGameSessionDataAsync(const FMD_SaveSlot& SaveSlot, const FOnLoadMDGameSessionAsyncComplete& CompletionDelegate);

protected:
	// 저장해야 하는 GameSession 데이터를 수집하는 함수
	bool CollectGameSessionData(FMD_SaveSlot& OutSaveSlot, UMD_SaveGameSession* OutGameSessionData);

	// 비동기 방식으로 GameSession 데이터 저장이 완료된 경우의 콜백 함수
	UFUNCTION()
	void HandleSaveGameSessionDataComplete(const FString& SavedSlotName, const int32 UserIndex, bool bSaveSuccess, const FMD_SaveSlot SaveSlot, const FOnSaveMDGameSessionAsyncComplete CompletionDelegate);

	// 비동기 방식으로 GameSession 데이터 로드가 완료된 경우의 콜백 함수
	UFUNCTION()
	void HandleLoadGameSessionDataComplete(const FString& LoadedSlotName, const int32 UserIndex, USaveGame* LoadedSaveGame, const FMD_SaveSlot SaveSlot, const FOnLoadMDGameSessionAsyncComplete CompletionDelegate);
};
