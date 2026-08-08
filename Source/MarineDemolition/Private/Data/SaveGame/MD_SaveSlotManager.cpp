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
	bool bDataCollected =CollectGameData();
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

bool UMD_SaveSlotManager::LoadGameDataToWorld(const FMD_GameData& GameData)
{
	// 현재 맵이 게임 데이터가 저장된 맵과 일치하는지 확인
	UWorld* World = GetWorld();
	UPackage* WorldPackage = IsValid(World) == true ? World->GetPackage() : nullptr;
	FString WorldPackagePath = IsValid(WorldPackage) == true ? UWorld::RemovePIEPrefix(WorldPackage->GetName()) : TEXT("");
	if (WorldPackagePath.IsEmpty() == true || WorldPackagePath != GameData.MapPackagePath)
	{
		return false;
	}

	// GameState 참조 및 클라이언트 환경 실행 제한
	AMD_GameStateBase* GameState = World->GetGameState<AMD_GameStateBase>();
	if (IsValid(GameState) == false || GameState->HasAuthority() == false)
	{
		return false;
	}

	// 상인 관리 서브시스템 참조
	UMD_MerchantSubsystem* MerchantManager = GetGameInstance()->GetSubsystem<UMD_MerchantSubsystem>();
	if (IsValid(MerchantManager) == false)
	{
		return false;
	}

	// 불러온 게임 데이터를 멤버 변수에 저장
	LatestSavedGameData = MakeShared<FMD_GameData>(GameData);

	// 퀘스트 덮어씌우기
	UE_LOG(MarineDemolition, Warning, TEXT("%s 게임 데이터 로드 시 퀘스트 정보 덮어씌우기 보강 필요"), *UE_CALL_LOCATIONINFO);

	// 점수 데이터 덮어씌우기
	int32 ScoreDelta = LatestSavedGameData->Score - GameState->GetScore();
	if (ScoreDelta != 0)
	{
		GameState->AddScoreDelta(ScoreDelta);
	}

	// 인게임 재화 데이터 덮어씌우기
	int32 CreditDelta = LatestSavedGameData->Credit - MerchantManager->GetCredits();
	if (CreditDelta != 0)
	{
		MerchantManager->AddCredits(CreditDelta);
	}

	// 상인 우호도 데이터 덮어씌우기
	UE_LOG(MarineDemolition, Warning, TEXT("%s 게임 데이터 로드 시 상인 우호도 정보 덮어씌우기 보강 필요"), *UE_CALL_LOCATIONINFO);

	// 인게임 시간 데이터 덮어씌우기
	int32 InGameDay = FMath::FloorToInt(FMath::Max(LatestSavedGameData->InGameTime, 1));
	if (GameState->GetCurrentDay() != InGameDay)
	{
		GameState->SetCurrentDay(InGameDay);
	}

	// 전체 플레이어 데이터 덮어씌우기
	const TArray<APlayerState*> PlayerArray = GameState->PlayerArray;
	for (const APlayerState* PlayerState : PlayerArray)
	{
		OverwriteDataToPlayer(PlayerState);
	}

	return true;
}

void UMD_SaveSlotManager::OnTryStartGameWithGameData(const FMD_GameData& GameData)
{
	// 게임 데이터의 이름이 비어있는 경우 새로 시작된 게임으로 간주한다.
	// bool bIsNewGame = GameData.UniqueName.IsEmpty();
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

bool UMD_SaveSlotManager::RemoveGameData(const FMD_GameData& GameData)
{
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
	MD_Game->AutoSaveList.RemoveAll([&GameData](const FMD_GameData& Element)
		{
			return Element.UniqueName == GameData.UniqueName;
		});

	MD_Game->UserSaveList.RemoveAll([&GameData](const FMD_GameData& Element)
		{
			return Element.UniqueName == GameData.UniqueName;
		});

	// 세이브 파일 저장
	bool bSaveSuccessful = UGameplayStatics::SaveGameToSlot(MD_Game, GameSaveFileName, 0);

	return bSaveSuccessful;
}

void UMD_SaveSlotManager::AsyncSaveGameData(const FString& DataName, bool bIsAuto, const FOnSaveMDGameDataComplete& SaveCompletionDelegate)
{
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

	// SaveGame 비동기 함수 실행
	UGameplayStatics::AsyncLoadGameFromSlot(GameSaveFileName, 0, AsyncLoadDelegate);
	return;
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

	// 플레이어 데이터가 저장되는 객체 찾기 또는 추가
	FMD_PlayerData& PlayerData = TemporaryGameData->PlayerData.FindOrAdd(PlayerKey);

	// 플레이어 캐릭터 참조
	AMD_Character* PlayerCharacter = Cast<AMD_Character>(PlayerPawn);

	// 플레이어 캐릭터의 EquipHotbar Component가 유효한 경우 데이터 저장
	UMD_EquipHotbarComponent* EquipmentComponent = IsValid(PlayerCharacter) == true ? PlayerCharacter->GetHotbarComponent() : nullptr;
	if (IsValid(EquipmentComponent) == true)
	{
		PlayerData.HotbarToolList = EquipmentComponent->BuildEquipmentSaveData();
	}

	return true;
}

bool UMD_SaveSlotManager::OverwriteDataToPlayer(const APlayerState* PlayerState) const
{
	// 클라이언트 환경 실행 제한
	if (IsValid(PlayerState) == false || PlayerState->HasAuthority() == false)
	{
		return false;
	}

	// 로드한 데이터가 존재하지 않은 경우 함수 조기 종료
	if (LatestSavedGameData.IsValid() == false)
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
	const FMD_PlayerData* PlayerData = LatestSavedGameData->PlayerData.Find(PlayerKey);
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

	// MD_EquipHotbar Component가 유효하면 저장된 데이터 덮어씌우기
	UMD_EquipHotbarComponent* EquipmentComponent = IsValid(PlayerCharacter) == true ? PlayerCharacter->GetHotbarComponent() : nullptr;
	if (IsValid(EquipmentComponent) == true)
	{
		EquipmentComponent->RestoreEquipmentSaveData(PlayerData->HotbarToolList);
	}

	return true;
}

TArray<FMD_GameData> UMD_SaveSlotManager::LoadAllGameDataList()
{
	TArray<FMD_GameData> OutGameDataList;

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

bool UMD_SaveSlotManager::ApplyPendingGameDataOperation()
{
	// 보류 작업이 없는 일반 맵 시작을 변경하지 않기 위한 실행 제한
	if (PendingGameDataOperation == EMD_PendingGameDataOperation::None || TemporaryGameData.IsValid() == false)
	{
		return false;
	}

	// 서버의 목적지 맵이 완전히 시작된 경우에만 게임 데이터 초기화를 수행하기 위한 World 검사
	UWorld* World = GetWorld();
	UPackage* WorldPackage = IsValid(World) == true ? World->GetPackage() : nullptr;
	FString CurrentMapPackagePath = IsValid(WorldPackage) == true ? UWorld::RemovePIEPrefix(WorldPackage->GetName()) : TEXT("");
	if (IsValid(World) == false || World->GetNetMode() == NM_Client || CurrentMapPackagePath != TemporaryGameData->MapPackagePath)
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

bool UMD_SaveSlotManager::CollectGameData()
{
	if (TemporaryGameData.IsValid() == false)
	{
		TemporaryGameData = MakeShared<FMD_GameData>();
	}

	// GameState 참조 및 클라이언트 환경 실행 제한
	UWorld* World = GetWorld();
	AMD_GameStateBase* GameState = IsValid(World) == true ? World->GetGameState<AMD_GameStateBase>() : nullptr;
	if (IsValid(GameState) == false || GameState->HasAuthority() == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 게임 데이터 저장은 Host/Server에서만 실행할 수 있습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// 맵이 저장된 패키지 얻기 및 유효성 확인
	UPackage* WorldPackage = World->GetPackage();
	if (IsValid(WorldPackage) == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 현재 맵의 패키지가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// 현재 맵의 패키지 경로 저장(PIE 환경 접두사 제거)
	TemporaryGameData->MapPackagePath = UWorld::RemovePIEPrefix(WorldPackage->GetName());
	
	// 현재 진행중인 퀘스트의 아이디 저장
	TemporaryGameData->QuestID = FPrimaryAssetId();

	// 누계 점수 저장
	TemporaryGameData->Score = GameState->GetScore();

	// 인게임 재화량 저장
	UMD_MerchantSubsystem* MerchantManager = GetGameInstance()->GetSubsystem<UMD_MerchantSubsystem>();
	TemporaryGameData->Credit = IsValid(MerchantManager) == true ? MerchantManager->GetCredits() : 0;

	// 상인 우호도 저장
	TemporaryGameData->MerchantLevel = 0;

	// 인게임 경과 시간 저장
	TemporaryGameData->InGameTime = GameState->GetCurrentDay() + GameState->GetElapsedGameSeconds();

	// 현재 접속중인 플레이어 별 데이터 저장
	for (const APlayerState* PlayerState : GameState->PlayerArray)
	{
		ParseDataFromPlayer(PlayerState);
	}

	// 게임 데이터 수집 시각 저장
	TemporaryGameData->SaveTime = FDateTime::Now();

	return true;
}

bool UMD_SaveSlotManager::InsertGameDataToSaveFile(UMD_SaveGame* SaveFile, const FMD_GameData& GameData, bool bIsAuto)
{
	if (IsValid(SaveFile) == false)
	{
		return false;
	}

	TArray<FMD_GameData>& DataList = bIsAuto == true ? SaveFile->AutoSaveList : SaveFile->UserSaveList;

	DataList.RemoveAll([&GameData](const FMD_GameData& Element)
		{
			return Element.UniqueName == GameData.UniqueName;
		});

	DataList.Insert(GameData, 0);

	SaveFile->ApplyMaxSaveCountLimit(MaxAutoSaveSlotCount, MaxUserSaveSlotCount);

	return true;
}

void UMD_SaveSlotManager::HandleLoadGameToSaveDataComplete(const FString& LoadedSlotName, const int32 UserIndex, USaveGame* LoadedSaveGame, const FMD_GameData CollectedGameData, const bool bIsAuto, const FOnSaveMDGameDataComplete SaveCompletionDelegate)
{
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
			// 세이브 파일 저장 성공 여부 확인
			bool bWasSuccessful = bSaveSuccess == true && SavedSlotName == GameSaveFileName && UserIndex == 0;

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
