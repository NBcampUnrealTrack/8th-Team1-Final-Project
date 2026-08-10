// Fill out your copyright notice in the Description page of Project Settings.

#include "Data/SaveGame/MD_SaveSlotManager.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerState.h"

#include "Game/MD_GameStateBase.h"
#include "Character/MD_Character.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "Gameplay/Persistence/MD_DayPersistenceSubsystem.h"
#include "Util/MD_LogUtility.h"

const FString UMD_SaveSlotManager::GameSaveFileName = TEXT("MarineDemolitionGame");

void UMD_SaveSlotManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

}

void UMD_SaveSlotManager::Deinitialize()
{
	// GameInstance 종료 이후 보류 데이터가 다음 World에 적용되지 않도록 전환 상태 초기화
	ResetTemporaryGameData();

	Super::Deinitialize();
}

bool UMD_SaveSlotManager::SaveGameData(const FString& DataName, bool bIsAuto)
{
	// 클라이언트가 서버 저장 슬롯과 분리된 로컬 파일을 생성하지 못하도록 하는 권한 제한
	if (HasServerAuthority() == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 게임 데이터 저장은 Host/Server에서만 실행할 수 있습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// 다른 비동기 I/O가 진행 중이면 활성 슬롯과 완료 순서가 뒤섞이지 않도록 새 요청을 거부한다.
	if (bAsyncOperationInProgress == true)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 다른 비동기 작업이 진행 중입니다."), *UE_CALL_LOCATIONINFO);

		return false;
	}

	// 저장 가능한 데이터의 최대 개수가 유효하지 않은 경우 함수 조기 종료
	int32 MaxSaveGameDataCount = bIsAuto == true ? MaxAutoSaveSlotCount : MaxUserSaveSlotCount;
	if (MaxSaveGameDataCount <= 0)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 저장기능이 비활성화 되어있습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// 저장되는 게임 데이터 수집
	bool bDataCollected = CollectGameData();
	if (bDataCollected == false)
	{
		return false;
	}

	if (bIsAuto == false && DataName.Contains("AutoSave") == true)
	{
		return false;
	}

	// 데이터 이름 설정
	TemporaryGameData->UniqueName = bIsAuto == true ? FString::Printf(TEXT("AutoSave_%d"), AutoSaveIndex + 1) : DataName;

	// 데이터가 저장되는 세이브 파일 불러오기
	USaveGame* LoadedGame = UGameplayStatics::LoadGameFromSlot(GameSaveFileName, 0);
	UMD_SaveGame* MD_Game = Cast<UMD_SaveGame>(LoadedGame);

	// 불러온 세이브 파일 유효성 확인
	if (IsValid(MD_Game) == false)
	{
		MD_Game = NewObject<UMD_SaveGame>();
	}

	// 수집한 게임 데이터 추가
	bool bListUpdated = InsertGameDataToSaveFile(MD_Game, *TemporaryGameData, bIsAuto);

	if (bListUpdated == false)
	{
		return false;
	}

	// 세이브 파일 저장
	bool bSaveSuccessful = UGameplayStatics::SaveGameToSlot(MD_Game, GameSaveFileName, 0);

	if (bSaveSuccessful == true)
	{
		LatestSavedGameData = MakeShared<FMD_GameData>(*TemporaryGameData);
		if (bIsAuto == true)
		{
			AutoSaveIndex = (AutoSaveIndex + 1) % MaxAutoSaveSlotCount;
		}
	}

	return bSaveSuccessful;
}

bool UMD_SaveSlotManager::RemoveGameData(const FMD_GameData& GameData)
{
	// 클라이언트가 자신의 로컬 저장 슬롯을 제거하지 못하도록 하는 권한 제한
	if (HasServerAuthority() == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 게임 데이터 제거는 Host/Server에서만 실행할 수 있습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// 다른 비동기 I/O가 진행 중이면 활성 슬롯과 완료 순서가 뒤섞이지 않도록 새 요청을 거부한다.
	if (bAsyncOperationInProgress == true)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 다른 비동기 작업이 진행 중입니다."), *UE_CALL_LOCATIONINFO);

		return false;
	}

	// 데이터가 저장되는 세이브 파일 불러오기
	USaveGame* LoadedGame = UGameplayStatics::LoadGameFromSlot(GameSaveFileName, 0);
	UMD_SaveGame* MD_Game = Cast<UMD_SaveGame>(LoadedGame);

	// 불러온 세이브 파일 유효성 확인
	if (IsValid(MD_Game) == false)
	{
		return false;
	}

	// 자동 저장 데이터 목록, 사용자 저장 데이터 목록에서 이름이 중복되는 게임 데이터 삭제
	MD_Game->AutoSaveList.RemoveAll([&GameData](const FMD_GameData& DataInList)
		{
			return DataInList.UniqueName == GameData.UniqueName;
		});

	MD_Game->UserSaveList.RemoveAll([&GameData](const FMD_GameData& DataInList)
		{
			return DataInList.UniqueName == GameData.UniqueName;
		});

	// 세이브 파일 저장
	bool bSaveSuccessful = UGameplayStatics::SaveGameToSlot(MD_Game, GameSaveFileName, 0);

	return bSaveSuccessful;
}

TArray<FMD_GameData> UMD_SaveSlotManager::LoadAllGameDataList()
{
	TArray<FMD_GameData> OutGameDataList;

	// 클라이언트가 자신의 로컬 저장 슬롯을 게임 목록으로 사용하지 못하도록 하는 권한 제한
	if (HasServerAuthority() == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 게임 데이터 목록 로드는 Host/Server에서만 실행할 수 있습니다."), *UE_CALL_LOCATIONINFO);
		return OutGameDataList;
	}

	// 데이터가 저장되는 세이브 파일 불러오기
	USaveGame* LoadedGame = UGameplayStatics::LoadGameFromSlot(GameSaveFileName, 0);
	UMD_SaveGame* MD_Game = Cast<UMD_SaveGame>(LoadedGame);

	// 불러온 세이브 파일 유효성 확인
	if (IsValid(MD_Game) == false)
	{
		return OutGameDataList;
	}

	OutGameDataList.Append(MD_Game->AutoSaveList);
	OutGameDataList.Append(MD_Game->UserSaveList);

	return OutGameDataList;
}

void UMD_SaveSlotManager::AsyncSaveGameData(const FString& DataName, bool bIsAuto, const FOnSaveMDGameDataComplete& SaveCompletionDelegate)
{
	// 클라이언트가 비동기 저장 파일 접근을 시작하지 못하도록 하는 권한 제한
	if (HasServerAuthority() == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 게임 데이터 저장은 Host/Server에서만 실행할 수 있습니다."), *UE_CALL_LOCATIONINFO);
		SaveCompletionDelegate.ExecuteIfBound(false);
		return;
	}

	// 다른 비동기 I/O가 진행 중이면 활성 슬롯과 완료 순서가 뒤섞이지 않도록 새 요청을 거부한다.
	if (bAsyncOperationInProgress == true)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 다른 비동기 작업이 진행 중입니다."), *UE_CALL_LOCATIONINFO);

		SaveCompletionDelegate.ExecuteIfBound(false);
		return;
	}

	// 저장 가능한 데이터의 최대 개수가 유효하지 않은 경우 함수 조기 종료
	int32 MaxSaveGameDataCount = bIsAuto == true ? MaxAutoSaveSlotCount : MaxUserSaveSlotCount;
	if (MaxSaveGameDataCount <= 0)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 저장기능이 비활성화 되어있습니다."), *UE_CALL_LOCATIONINFO);

		SaveCompletionDelegate.ExecuteIfBound(false);
		return;
	}

	if (bIsAuto == false && DataName.Contains("AutoSave") == true)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 데이터 저장에 허용되지 않은 이름입니다."), *UE_CALL_LOCATIONINFO);

		SaveCompletionDelegate.ExecuteIfBound(false);
		return;
	}

	// 저장해야 하는 데이터 수집 및 결과 확인
	bool bIsDataCollected = CollectGameData();
	if (bIsDataCollected == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 저장되는 GameSession 데이터를 수집하지 못하였습니다."), *UE_CALL_LOCATIONINFO);

		SaveCompletionDelegate.ExecuteIfBound(false);
		return;
	}

	// 비동기 작업 중 현재 게임 데이터가 변경되어도 저장 요청 내용이 유지되도록 하는 값 복사
	FMD_GameData CollectedGameData = *TemporaryGameData;

	// 요청된 슬롯을 식별하기 위한 저장 데이터 이름 설정
	CollectedGameData.UniqueName = bIsAuto == true ? FString::Printf(TEXT("AutoSave_%d"), AutoSaveIndex + 1) : DataName;

	// 비동기 작업 진행 상태 플래그 설정
	bAsyncOperationInProgress = true;

	// 비동기 콜백 함수 바인딩
	FAsyncLoadGameFromSlotDelegate AsyncLoadDelegate = FAsyncLoadGameFromSlotDelegate::CreateUObject(this, &ThisClass::HandleLoadGameToSaveDataComplete, CollectedGameData, bIsAuto, SaveCompletionDelegate);

	// Load Game 비동기 함수 실행
	UGameplayStatics::AsyncLoadGameFromSlot(GameSaveFileName, 0, AsyncLoadDelegate);
	return;
}

void UMD_SaveSlotManager::AsyncRemoveGameData(const FMD_GameData& GameData, const FOnRemoveMDGameDataComplete& RemoveCompletionDelegate)
{
	// 클라이언트가 비동기 제거를 위한 저장 파일 접근을 시작하지 못하도록 하는 권한 제한
	if (HasServerAuthority() == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 게임 데이터 제거는 Host/Server에서만 실행할 수 있습니다."), *UE_CALL_LOCATIONINFO);
		RemoveCompletionDelegate.ExecuteIfBound(false);
		return;
	}

	// 다른 비동기 I/O가 진행 중이면 활성 슬롯과 완료 순서가 뒤섞이지 않도록 새 요청을 거부한다.
	if (bAsyncOperationInProgress == true)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 다른 비동기 작업이 진행 중입니다."), *UE_CALL_LOCATIONINFO);

		RemoveCompletionDelegate.ExecuteIfBound(false);
		return;
	}

	// 비동기 작업 진행 상태 플래그 설정
	bAsyncOperationInProgress = true;

	// 비동기 콜백 함수 바인딩
	FAsyncLoadGameFromSlotDelegate AsyncLoadDelegate = FAsyncLoadGameFromSlotDelegate::CreateUObject(this, &ThisClass::HandleLoadGameToRemoveDataComplete, GameData, RemoveCompletionDelegate);

	// Load Game 비동기 함수 실행
	UGameplayStatics::AsyncLoadGameFromSlot(GameSaveFileName, 0, AsyncLoadDelegate);
	return;
}

void UMD_SaveSlotManager::AsyncLoadAllGameDataList(const FOnLoadMDGameDataListComplete& LoadCompletionDelegate)
{
	TArray<FMD_GameData> EmptyArray;

	// 클라이언트가 비동기 목록 로드를 위한 저장 파일 접근을 시작하지 못하도록 하는 권한 제한
	if (HasServerAuthority() == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 게임 데이터 목록 로드는 Host/Server에서만 실행할 수 있습니다."), *UE_CALL_LOCATIONINFO);
		LoadCompletionDelegate.ExecuteIfBound(EmptyArray, false);
		return;
	}

	// 다른 비동기 I/O가 진행 중이면 활성 슬롯과 완료 순서가 뒤섞이지 않도록 새 요청을 거부한다.
	if (bAsyncOperationInProgress == true)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 다른 비동기 작업이 진행 중입니다."), *UE_CALL_LOCATIONINFO);

		LoadCompletionDelegate.ExecuteIfBound(EmptyArray, false);
		return;
	}

	// 비동기 작업 진행 상태 플래그 설정
	bAsyncOperationInProgress = true;

	// 비동기 콜백 함수 바인딩
	FAsyncLoadGameFromSlotDelegate AsyncLoadDelegate = FAsyncLoadGameFromSlotDelegate::CreateUObject(this, &ThisClass::HandleLoadGameToLoadDataListComplete, LoadCompletionDelegate);

	// Load Game 비동기 함수 실행
	UGameplayStatics::AsyncLoadGameFromSlot(GameSaveFileName, 0, AsyncLoadDelegate);
}

bool UMD_SaveSlotManager::LoadGameDataToWorld(const FMD_GameData& GameData)
{
	// 클라이언트가 저장 데이터를 자신의 World에 적용하지 못하도록 하는 권한 제한
	if (HasServerAuthority() == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 게임 데이터 적용은 Host/Server에서만 실행할 수 있습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}
	// 현재 맵이 게임 데이터가 저장된 맵과 일치하는지 확인
	UWorld* World = GetWorld();
	UPackage* WorldPackage = IsValid(World) == true ? World->GetPackage() : nullptr;
	FString WorldPackagePath = IsValid(WorldPackage) == true ? UWorld::RemovePIEPrefix(WorldPackage->GetName()) : TEXT("");
	if (WorldPackagePath.IsEmpty() == true || WorldPackagePath != GameData.MapPackagePath)
	{
		return false;
	}

	UGameInstance* GameInstance = GetGameInstance();
	if (IsValid(GameInstance) == false)
	{
		return false;
	}

	// 필수 서버 상태가 준비되지 않은 시점의 부분 적용을 방지하기 위한 객체 검사
	AMD_GameStateBase* GameState = World->GetGameState<AMD_GameStateBase>();
	UMD_MerchantSubsystem* MerchantManager = GameInstance->GetSubsystem<UMD_MerchantSubsystem>();
	if (IsValid(GameState) == false || GameState->HasAuthority() == false || IsValid(MerchantManager) == false)
	{
		return false;
	}

	// 필수 데이터 필드의 누락이나 타입 불일치로 인한 불완전한 복원을 방지하기 위한 검사
	FMD_GameStateData GameStateData;
	FMD_MerchantData MerchantData;
	if (GameData.GetDataField(FMD_GameStateData::DataFieldKey, GameStateData) == false || GameData.GetDataField(FMD_MerchantData::DataFieldKey, MerchantData) == false)
	{
		return false;
	}

	// 월드 데이터가 존재하는 최신 세이브와 필드가 없는 이전 세이브를 구분하는 조회
	UMD_DayPersistenceSubsystem* DayPersistenceManager = GameInstance->GetSubsystem<UMD_DayPersistenceSubsystem>();
	FMD_WorldData WorldData;
	bool bContainsWorldData = GameData.DataFields.Contains(FMD_WorldData::DataFieldKey);
	bool bHasWorldData = GameData.GetDataField(FMD_WorldData::DataFieldKey, WorldData);
	if (bContainsWorldData == true && bHasWorldData == false)
	{
		return false;
	}
	if (bHasWorldData == true && IsValid(DayPersistenceManager) == false)
	{
		return false;
	}

	// GameState를 저장 스냅샷으로 교체하는 처리
	if (GameState->ApplyGameStateSaveData(GameStateData) == false)
	{
		return false;
	}

	// 상인 상태를 저장 스냅샷으로 교체하는 처리
	if (MerchantManager->ApplyMerchantSaveData(MerchantData) == false)
	{
		return false;
	}

	// 저장 데이터에 월드 필드가 존재하는 경우 파괴와 오염 상태까지 복원하는 처리
	if (bHasWorldData == true && DayPersistenceManager->ApplyWorldSaveData(World, WorldData) == false)
	{
		return false;
	}

	// 전체 플레이어 데이터 덮어씌우기
	const TArray<APlayerState*> PlayerArray = GameState->PlayerArray;
	for (const APlayerState* PlayerState : PlayerArray)
	{
		OverwriteDataToPlayerInternal(PlayerState, GameData);
	}

	// 불러온 게임 데이터를 멤버 변수에 저장
	LatestSavedGameData = MakeShared<FMD_GameData>(GameData);

	return true;
}

bool UMD_SaveSlotManager::ParseDataFromPlayer(const APlayerState* PlayerState)
{
	// 클라이언트 환경 실행 제한
	if (IsValid(PlayerState) == false || PlayerState->HasAuthority() == false)
	{
		return false;
	}

	// 데이터를 기록할 객체가 존재하지 않는 경우 객체를 새로 생성
	if (TemporaryGameData.IsValid() == false)
	{
		TemporaryGameData = MakeShared<FMD_GameData>();
	}

	// 플레이어 고유 식별자 얻기
	FUniqueNetIdRepl UniqueNetID = IsValid(PlayerState) == true ? PlayerState->GetUniqueId() : nullptr;
	FString PlayerKey = UniqueNetID.IsValid() == true ? UniqueNetID.ToString() : TEXT("");
	// Unique Net ID가 없는 경우 플레이어 이름을 고유 식별자로 사용
	PlayerKey = PlayerKey.IsEmpty() == true ? PlayerState->GetPlayerName() : PlayerKey;

	// 플레이어 고유 식별자의 유효성 검사
	if (PlayerKey.IsEmpty() == true)
	{
		return false;
	}

	// 플레이어가 조종하는 Pawn이 존재하지 않는 경우 함수 조기 종료
	APawn* PlayerPawn = PlayerState->GetPawn();
	if (IsValid(PlayerPawn) == false)
	{
		return false;
	}

	// 플레이어 캐릭터 참조
	AMD_Character* PlayerCharacter = Cast<AMD_Character>(PlayerPawn);

	// 빈 플레이어 데이터가 기록되지 않도록 장비 컴포넌트 유효성 검사
	UMD_EquipHotbarComponent* EquipmentComponent = IsValid(PlayerCharacter) == true ? PlayerCharacter->GetHotbarComponent() : nullptr;
	if (IsValid(EquipmentComponent) == false)
	{
		return false;
	}

	// 식별자에 대응하는 플레이어 장비 스냅샷 갱신
	FMD_PlayerData& PlayerData = TemporaryGameData->PlayerFields.FindOrAdd(PlayerKey);
	PlayerData.HotbarToolList = EquipmentComponent->BuildEquipmentSaveData();

	return true;
}

bool UMD_SaveSlotManager::LoadPlayerData(const APlayerState* PlayerState) const
{
	// 데이터를 덮어씌워질 플레이어가 유효하지 않거나 덮어씌울 데이터가 존재하지 않는 경우 함수 조기 종료
	if (IsValid(PlayerState) == false || TemporaryGameData.IsValid() == false)
	{
		return false;
	}

	bool bOverwriteSuccess = OverwriteDataToPlayerInternal(PlayerState, *TemporaryGameData);
	return bOverwriteSuccess;
}

void UMD_SaveSlotManager::OnTryStartGameWithGameData(const FMD_GameData& GameData)
{
	// 클라이언트가 로드할 데이터를 독립적으로 예약하지 못하도록 하는 권한 제한
	if (HasServerAuthority() == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 게임 데이터 기반 시작 요청은 Host/Server에서만 실행할 수 있습니다."), *UE_CALL_LOCATIONINFO);
		return;
	}

	// 게임 데이터의 이름이 비어있는 경우 새로 시작된 게임으로 간주한다.
	// bool bIsNewGame = GameData.IsEmpty();
	PendingGameDataOperation = GameData.UniqueName.IsEmpty() ? EMD_PendingGameDataOperation::NewGame : EMD_PendingGameDataOperation::LoadGame;
	TemporaryGameData = MakeShared<FMD_GameData>(GameData);
	return;
}

void UMD_SaveSlotManager::ResetTemporaryGameData()
{
	PendingGameDataOperation = EMD_PendingGameDataOperation::None;
	TemporaryGameData.Reset();
	return;
}

bool UMD_SaveSlotManager::ApplyPendingGameDataOperation()
{
	// 클라이언트가 보류된 새 게임 또는 불러오기 데이터를 자신의 World에 적용하지 못하도록 하는 권한 제한
	if (HasServerAuthority() == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 게임 데이터 초기화 작업은 Host/Server에서만 실행할 수 있습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// 보류 작업이 없는 일반 맵 시작을 변경하지 않기 위한 실행 제한
	if (PendingGameDataOperation == EMD_PendingGameDataOperation::None || TemporaryGameData.IsValid() == false)
	{
		return false;
	}

	// 서버의 목적지 맵이 완전히 시작된 경우에만 게임 데이터 초기화를 수행하기 위한 World 검사
	UWorld* World = GetWorld();
	UPackage* WorldPackage = IsValid(World) == true ? World->GetPackage() : nullptr;
	FString CurrentMapPackagePath = IsValid(WorldPackage) == true ? UWorld::RemovePIEPrefix(WorldPackage->GetName()) : TEXT("");
	if (IsValid(World) == false || CurrentMapPackagePath != TemporaryGameData->MapPackagePath)
	{
		return false;
	}

	// 이전 게임의 일일 스냅샷이 새 세션 또는 불러온 데이터 위에 적용되지 않도록 하는 초기화
	UGameInstance* GameInstance = GetGameInstance();
	UMD_DayPersistenceSubsystem* DayPersistence = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_DayPersistenceSubsystem>() : nullptr;
	if (IsValid(DayPersistence) == true)
	{
		DayPersistence->ClearSnapshot();
	}

	if (PendingGameDataOperation == EMD_PendingGameDataOperation::NewGame && TemporaryGameData.IsValid() == true)
	{
		bool bCollectingSuccess = CollectGameData();
		if (bCollectingSuccess == false)
		{
			return false;
		}

		LatestSavedGameData.Reset();
		PendingGameDataOperation = EMD_PendingGameDataOperation::None;

		return true;
	}

	// 불러오기 목적지의 서버 상태와 접속 플레이어에게 선택한 저장 데이터를 적용하는 처리
	if (PendingGameDataOperation == EMD_PendingGameDataOperation::LoadGame && TemporaryGameData.IsValid() == true)
	{
		bool bWasApplied = LoadGameDataToWorld(*TemporaryGameData);
		if (bWasApplied == true)
		{
			PendingGameDataOperation = EMD_PendingGameDataOperation::None;
			return true;
		}
	}

	// 손상된 보류 상태가 다음 맵까지 유지되지 않도록 하는 실패 정리
	ResetTemporaryGameData();

	return false;
}

bool UMD_SaveSlotManager::HasServerAuthority() const
{
	// 현재 맵의 NetMode가 Client의 경우 false 반환
	UWorld* World = GetWorld();
	return IsValid(World) == true && World->GetNetMode() != NM_Client;
}

bool UMD_SaveSlotManager::CollectGameData()
{
	if (TemporaryGameData.IsValid() == false)
	{
		TemporaryGameData = MakeShared<FMD_GameData>();
	}

	// 게임 데이터를 수집하기 위하여 필요한 객체들 참조
	UGameInstance* GameInstance = GetGameInstance();
	UMD_MerchantSubsystem* MerchantManager = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_MerchantSubsystem>() : nullptr;
	UMD_DayPersistenceSubsystem* DayPersistenceManager = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_DayPersistenceSubsystem>() : nullptr;
	if (IsValid(MerchantManager) == false || IsValid(DayPersistenceManager) == false)
	{
		return false;
	}

	// GameState 참조 및 클라이언트 환경 실행 제한
	UWorld* World = GetWorld();
	UPackage* WorldPackage = IsValid(World) == true ? World->GetPackage() : nullptr;
	AMD_GameStateBase* GameState = IsValid(World) == true ? World->GetGameState<AMD_GameStateBase>() : nullptr;
	if (IsValid(WorldPackage) == false || IsValid(GameState) == false || GameState->HasAuthority() == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 게임 데이터 저장은 Host/Server에서만 실행할 수 있습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// 현재 맵의 패키지 경로 저장(PIE 환경 접두사 제거)
	TemporaryGameData->MapPackagePath = UWorld::RemovePIEPrefix(WorldPackage->GetName());

	// 게임 데이터 수집 시각 저장
	TemporaryGameData->SaveTime = FDateTime::Now();

	// 상인 관련 정보 저장
	FMD_MerchantData MerchantData = MerchantManager->GetMerchantSaveData();
	if (TemporaryGameData->AddDataField(FMD_MerchantData::DataFieldKey, MerchantData) == false)
	{
		return false;
	}

	// 파괴된 벽과 동적 액터를 포함한 현재 월드 상태 저장
	FMD_WorldData WorldData;
	if (DayPersistenceManager->BuildWorldSaveData(World, WorldData) == false || TemporaryGameData->AddDataField(FMD_WorldData::DataFieldKey, WorldData) == false)
	{
		return false;
	}

	// GameState관련 정보 저장
	FMD_GameStateData GameStateData = GameState->GetGameStateSaveData();
	if (TemporaryGameData->AddDataField(FMD_GameStateData::DataFieldKey, GameStateData) == false)
	{
		return false;
	}

	// 플레이어 관련 정보 저장
	const TArray<APlayerState*> PlayerArray = GameState->PlayerArray;
	for (const APlayerState* PlayerState : PlayerArray)
	{
		ParseDataFromPlayer(PlayerState);
	}

	return true;
}

bool UMD_SaveSlotManager::OverwriteDataToPlayerInternal(const APlayerState* PlayerState, const FMD_GameData& GameData) const
{
	// 클라이언트 환경 실행 제한
	if (IsValid(PlayerState) == false || PlayerState->HasAuthority() == false)
	{
		return false;
	}

	// 새로하기 트래블 중 이전 세션의 플레이어 데이터가 새 Pawn에 적용되지 않도록 하는 복원 제한
	if (PendingGameDataOperation == EMD_PendingGameDataOperation::NewGame)
	{
		return false;
	}

	FUniqueNetIdRepl UniqueNetID = IsValid(PlayerState) == true ? PlayerState->GetUniqueId() : nullptr;
	FString PlayerKey = UniqueNetID.IsValid() == true ? UniqueNetID.ToString() : TEXT("");
	PlayerKey = PlayerKey.IsEmpty() == true ? PlayerState->GetPlayerName() : PlayerKey;
	if (PlayerKey.IsEmpty() == true)
	{
		return false;
	}

	// 플레이어 데이터의 존재 여부 검사
	const FMD_PlayerData* PlayerData = GameData.PlayerFields.Find(PlayerKey);
	if (PlayerData == nullptr)
	{
		return false;
	}

	// 플레이어가 조종하는 캐릭터(폰)이 존재하지 않으면 함수 조기 종료
	if (IsValid(PlayerState->GetPawn()) == false)
	{
		return false;
	}

	// 플레이어 캐릭터 참조
	AMD_Character* PlayerCharacter = Cast<AMD_Character>(PlayerState->GetPawn());

	// 플레이어 복원 성공 여부를 정확히 전달하기 위한 장비 컴포넌트 검사
	UMD_EquipHotbarComponent* EquipmentComponent = IsValid(PlayerCharacter) == true ? PlayerCharacter->GetHotbarComponent() : nullptr;
	if (IsValid(EquipmentComponent) == false)
	{
		return false;
	}

	// 저장된 장비 스냅샷을 서버 권위 플레이어에 적용하는 처리
	EquipmentComponent->RestoreEquipmentSaveData(PlayerData->HotbarToolList);

	return true;
}

bool UMD_SaveSlotManager::InsertGameDataToSaveFile(UMD_SaveGame* SaveFile, const FMD_GameData& GameData, bool bIsAuto)
{
	if (IsValid(SaveFile) == false || GameData.IsEmpty() == true)
	{
		return false;
	}

	TArray<FMD_GameData>& DataList = bIsAuto == true ? SaveFile->AutoSaveList : SaveFile->UserSaveList;

	DataList.RemoveAll([&GameData](const FMD_GameData& InData)
		{
			return InData.UniqueName == GameData.UniqueName;
		});

	DataList.Insert(GameData, 0);

	SaveFile->ApplyMaxSaveCountLimit(MaxAutoSaveSlotCount, MaxUserSaveSlotCount);

	return true;
}

void UMD_SaveSlotManager::HandleLoadGameToSaveDataComplete(const FString& LoadedSlotName, const int32 UserIndex, USaveGame* LoadedSaveGame, const FMD_GameData CollectedGameData, const bool bIsAuto, const FOnSaveMDGameDataComplete SaveCompletionDelegate)
{
	// 맵 전환 중 완료된 비동기 로드가 클라이언트 World에서 저장으로 이어지지 않도록 하는 권한 재검사
	if (HasServerAuthority() == false)
	{
		SaveCompletionDelegate.ExecuteIfBound(false);
		bAsyncOperationInProgress = false;
		return;
	}

	UMD_SaveGame* MD_Game = Cast<UMD_SaveGame>(LoadedSaveGame);
	if (IsValid(MD_Game) == false)
	{
		MD_Game = NewObject<UMD_SaveGame>();
	}

	// 수집한 게임 데이터 추가
	bool bListUpdated = InsertGameDataToSaveFile(MD_Game, CollectedGameData, bIsAuto);

	if (bListUpdated == false)
	{
		SaveCompletionDelegate.ExecuteIfBound(false);

		// 비동기 작업 진행 상태 플래그 설정
		bAsyncOperationInProgress = false;
		return;
	}

	// 비동기 콜백 바인딩
	FAsyncSaveGameToSlotDelegate AsyncSaveDelegate = FAsyncSaveGameToSlotDelegate::CreateWeakLambda(this, [this, CollectedGameData, bIsAuto, SaveCompletionDelegate](const FString& SavedSlotName, const int32 UserIndex, bool bSaveSuccess)
		{
			// 완료 시점의 서버 권한과 세이브 파일 저장 성공 여부 확인
			bool bWasSuccessful = HasServerAuthority() == true && bSaveSuccess == true && SavedSlotName == GameSaveFileName && UserIndex == 0;

			SaveCompletionDelegate.ExecuteIfBound(bWasSuccessful);

			if (bWasSuccessful == true)
			{
				// 저장 완료 시점까지 유지된 요청 스냅샷을 최근 저장 데이터로 확정하는 처리
				LatestSavedGameData = MakeShared<FMD_GameData>(CollectedGameData);

				if (bIsAuto == true)
				{
					AutoSaveIndex = (AutoSaveIndex + 1) % MaxAutoSaveSlotCount;
				}
			}

			// 비동기 작업 진행 상태 플래그 설정
			bAsyncOperationInProgress = false;
		});

	// 세이브 파일 저장
	UGameplayStatics::AsyncSaveGameToSlot(MD_Game, GameSaveFileName, 0, AsyncSaveDelegate);
}

void UMD_SaveSlotManager::HandleLoadGameToRemoveDataComplete(const FString& LoadedSlotName, const int32 UserIndex, USaveGame* LoadedSaveGame, const FMD_GameData GameDataToRemove, const FOnRemoveMDGameDataComplete RemoveCompletionDelegate)
{
	// 맵 전환 중 완료된 비동기 로드가 클라이언트 World에서 제거로 이어지지 않도록 하는 권한 재검사
	if (HasServerAuthority() == false)
	{
		RemoveCompletionDelegate.ExecuteIfBound(false);
		bAsyncOperationInProgress = false;
		return;
	}

	// 불러온 세이브 파일을 MD_SaveGame 타입으로 변환
	UMD_SaveGame* MD_Game = Cast<UMD_SaveGame>(LoadedSaveGame);

	// 세이브 파일 불러오기 성공 여부 확인
	bool bLoadSuccessful = LoadedSlotName == GameSaveFileName && UserIndex == 0 && IsValid(MD_Game);
	if (bLoadSuccessful == false)
	{
		RemoveCompletionDelegate.ExecuteIfBound(false);

		bAsyncOperationInProgress = false;
		return;
	}

	// 자동 저장 데이터 목록, 사용자 저장 데이터 목록에서 이름이 중복되는 게임 데이터 삭제
	MD_Game->AutoSaveList.RemoveAll([&GameDataToRemove](const FMD_GameData& DataInList)
		{
			return DataInList.UniqueName == GameDataToRemove.UniqueName;
		});

	MD_Game->UserSaveList.RemoveAll([&GameDataToRemove](const FMD_GameData& DataInList)
		{
			return DataInList.UniqueName == GameDataToRemove.UniqueName;
		});

	// 비동기 함수 콜백 등록
	FAsyncSaveGameToSlotDelegate AsyncSaveDelegate = FAsyncSaveGameToSlotDelegate::CreateWeakLambda(this, [this, RemoveCompletionDelegate](const FString& SavedSlotName, const int32 UserIndex, bool bSaveSuccess)
		{
			// 완료 시점의 서버 권한과 세이브 파일 저장 성공 여부 확인
			bool bWasSuccessful = HasServerAuthority() == true && bSaveSuccess == true && SavedSlotName == GameSaveFileName && UserIndex == 0;

			RemoveCompletionDelegate.ExecuteIfBound(bWasSuccessful);

			// 비동기 작업 진행 상태 플래그 설정
			bAsyncOperationInProgress = false;
			return;
		});

	// 세이브 파일 저장
	UGameplayStatics::AsyncSaveGameToSlot(MD_Game, GameSaveFileName, 0, AsyncSaveDelegate);
	return;
}

void UMD_SaveSlotManager::HandleLoadGameToLoadDataListComplete(const FString& LoadedSlotName, const int32 UserIndex, USaveGame* LoadedSaveGame, const FOnLoadMDGameDataListComplete LoadCompletionDelegate)
{
	TArray<FMD_GameData> OutGameDataList;

	// 맵 전환 중 완료된 비동기 로드가 클라이언트 World에 목록을 전달하지 않도록 하는 권한 재검사
	if (HasServerAuthority() == false)
	{
		LoadCompletionDelegate.ExecuteIfBound(OutGameDataList, false);
		bAsyncOperationInProgress = false;
		return;
	}

	// 불러온 세이브 파일을 MD_SaveGame 타입으로 변환
	UMD_SaveGame* MD_Game = Cast<UMD_SaveGame>(LoadedSaveGame);

	// 세이브 파일 불러오기 성공 여부 확인
	bool bLoadSuccessful = LoadedSlotName == GameSaveFileName && UserIndex == 0 && IsValid(MD_Game);

	// 세이브 파일이 유효한 경우 내부에 있는 데이터 목록 통합
	if (IsValid(MD_Game) == true)
	{
		OutGameDataList.Append(MD_Game->AutoSaveList);
		OutGameDataList.Append(MD_Game->UserSaveList);
	}

	LoadCompletionDelegate.ExecuteIfBound(OutGameDataList, bLoadSuccessful);

	bAsyncOperationInProgress = false;
	return;
}
