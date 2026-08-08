// Fill out your copyright notice in the Description page of Project Settings.

#include "Data/SaveGame/MD_SaveSlotManager.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerState.h"

#include "Game/MD_GameStateBase.h"
#include "Character/MD_Character.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "Util/MD_LogUtility.h"

const FString UMD_SaveSlotManager::GameSaveFileName = TEXT("MarineDemolitionGame");

void UMD_SaveSlotManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

}

void UMD_SaveSlotManager::Deinitialize()
{
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

	// 저장되는 게임 데이터 수집
	FMD_GameData CollectedGameData;
	bool bDataCollected =CollectGameSessionData(CollectedGameData);
	if (bDataCollected == false)
	{
		return false;
	}

	// 데이터 이름 설정
	CollectedGameData.UniqueName = bIsAuto == true ? FString::Printf(TEXT("AutoSave_%d"), AutoSaveIndex + 1) : DataName;

	// 데이터가 저장되는 세이브 파일 불러오기
	USaveGame* LoadedGame = UGameplayStatics::LoadGameFromSlot(GameSaveFileName, 0);
	UMD_SaveGame* MD_Game = Cast<UMD_SaveGame>(LoadedGame);

	// 불러온 세이브 파일 유효성 확인
	if (IsValid(MD_Game) == false)
	{
		MD_Game = NewObject<UMD_SaveGame>();
	}

	// 수집한 게임 데이터 추가
	if (bIsAuto == true)
	{
		// 저장되어야 인덱스가 자동 저장 목록 크기보다 작은 경우 해당 인덱스에 위치한 데이터 교체
		if (AutoSaveIndex < MD_Game->AutoSaveList.Num())
		{
			MD_Game->AutoSaveList[AutoSaveIndex] = CollectedGameData;
		}
		else
		{
			MD_Game->AutoSaveList.Add(CollectedGameData);
		}
	}
	else
	{
		// 동일한 이름으로 저장된 게임 데이터 삭제
		MD_Game->UserSaveList.RemoveAll([&CollectedGameData](const FMD_GameData& Element)
			{
				return Element.UniqueName == CollectedGameData.UniqueName;
			});

		// 가장 최근의 게임 데이터가 앞순서에 오도록 0번 인덱스에 삽입
		MD_Game->UserSaveList.Insert(CollectedGameData, 0);
	}

	// 최대 저장 개수 제한 적용
	MD_Game->ApplyMasSaveCountLimit(MaxAutoSaveSlotCount, MaxUserSaveSlotCount);

	// 세이브 파일 저장
	bool bSaveSuccessful = UGameplayStatics::SaveGameToSlot(MD_Game, GameSaveFileName, 0);

	if (bSaveSuccessful == true && bIsAuto == true)
	{
		AutoSaveIndex = (AutoSaveIndex + 1) % MaxAutoSaveSlotCount;
	}

	return bSaveSuccessful;
}

bool UMD_SaveSlotManager::LoadGameDataToWorld(const FMD_GameData& GameData)
{
	// 불러온 게임 데이터를 멤버 변수에 저장
	ActiveGameData = MakeShared<FMD_GameData>(GameData);

	// 현재 맵이 게임 데이터가 저장된 맵과 일치하는지 확인
	UWorld* World = GetWorld();
	UPackage* WorldPackage = IsValid(World) == true ? World->GetPackage() : nullptr;
	FString WorldPackagePath = IsValid(WorldPackage) == true ? UWorld::RemovePIEPrefix(WorldPackage->GetName()) : TEXT("");
	if (WorldPackagePath.IsEmpty() == true || WorldPackagePath != ActiveGameData->MapPackagePath)
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

	// 퀘스트 덮어씌우기
	UE_LOG(MarineDemolition, Warning, TEXT("%s 게임 데이터 로드 시 퀘스트 정보 덮어씌우기 보강 필요"), *UE_CALL_LOCATIONINFO);

	// 점수 데이터 덮어씌우기
	int32 ScoreDelta = ActiveGameData->Score - GameState->GetScore();
	if (ScoreDelta != 0)
	{
		GameState->AddScoreDelta(ScoreDelta);
	}

	// 인게임 재화 데이터 덮어씌우기
	int32 CreditDelta = ActiveGameData->Credit - MerchantManager->GetCredits();
	if (CreditDelta != 0)
	{
		MerchantManager->AddCredits(CreditDelta);
	}

	// 상인 우호도 데이터 덮어씌우기
	UE_LOG(MarineDemolition, Warning, TEXT("%s 게임 데이터 로드 시 상인 우호도 정보 덮어씌우기 보강 필요"), *UE_CALL_LOCATIONINFO);

	// 인게임 시간 데이터 덮어씌우기
	int32 InGameDay = FMath::FloorToInt(FMath::Max(ActiveGameData->InGameTime, 1));
	if (GameState->GetCurrentDay() != InGameDay)
	{
		GameState->SetCurrentDay(InGameDay);
	}

	PlayerDataLogMap = GameData.PlayerData;
	// 전체 플레이어 데이터 덮어씌우기
	const TArray<APlayerState*> PlayerArray = GameState->PlayerArray;
	for (const APlayerState* PlayerState : PlayerArray)
	{
		OverwriteDataToPlayer(PlayerState);
	}

	return true;
}

bool UMD_SaveSlotManager::LoadGameData(const FString& DataName)
{
	// 다른 비동기 I/O가 진행 중이면 활성 슬롯과 완료 순서가 뒤섞이지 않도록 새 요청을 거부한다.
	if (bAsyncOperationInProgress == true)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 다른 비동기 작업이 진행 중입니다."), *UE_CALL_LOCATIONINFO);

		return false;
	}

	// 세이브 파일로부터 전체 게임 데이터 목록 불러오기
	const TArray<FMD_GameData> GameDataList = LoadAllGameData();

	// 이름이 동일한 게임 데이터 찾기
	const FMD_GameData* LoadedGameData = GameDataList.FindByPredicate([&DataName](const FMD_GameData& GameData)
		{
			return GameData.UniqueName == DataName;
		});

	// 찾는 게임 데이터가 존재하지 않으면 함수 조기 종료
	if (LoadedGameData == nullptr)
	{
		return false;
	}

	bool LoadSuccess = LoadGameDataToWorld(*LoadedGameData);

	return LoadSuccess;
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
	FMD_GameData CollectedGameData;

	// 다른 비동기 I/O가 진행 중이면 활성 슬롯과 완료 순서가 뒤섞이지 않도록 새 요청을 거부한다.
	if (bAsyncOperationInProgress == true)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 다른 비동기 작업이 진행 중입니다."), *UE_CALL_LOCATIONINFO);

		SaveCompletionDelegate.ExecuteIfBound(CollectedGameData, false);
		return;
	}

	// 저장해야 하는 데이터 수집 및 결과 확인
	bool bIsDataCollected = CollectGameSessionData(CollectedGameData);
	if (bIsDataCollected == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 저장되는 GameSession 데이터를 수집하지 못하였습니다."), *UE_CALL_LOCATIONINFO);

		SaveCompletionDelegate.ExecuteIfBound(CollectedGameData, false);
		return;
	}
	CollectedGameData.UniqueName = DataName;

	// 비동기 작업 진행 상태 플래그 설정
	bAsyncOperationInProgress = true;

	// 비동기 콜백 함수 바인딩
	FAsyncLoadGameFromSlotDelegate AsyncLoadDelegate = FAsyncLoadGameFromSlotDelegate::CreateUObject(this, &ThisClass::HandleLoadGameToSaveDataComplete, CollectedGameData, bIsAuto, SaveCompletionDelegate);

	// SaveGame 비동기 함수 실행
	UGameplayStatics::AsyncLoadGameFromSlot(GameSaveFileName, 0, AsyncLoadDelegate);
	return;
}

void UMD_SaveSlotManager::AsyncLoadGameData(const FString& DataName, const FOnLoadMDGameDataComplete& LoadCompletionDelegate)
{
	FMD_GameData LoadedGameData;

	// 다른 비동기 I/O가 진행 중이면 활성 슬롯과 완료 순서가 뒤섞이지 않도록 새 요청을 거부한다.
	if (bAsyncOperationInProgress == true)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 다른 비동기 작업이 진행 중입니다."), *UE_CALL_LOCATIONINFO);

		LoadCompletionDelegate.ExecuteIfBound(LoadedGameData, false);
		return;
	}

	bAsyncOperationInProgress = true;

	FAsyncLoadGameFromSlotDelegate AsyncLoadDelegate = FAsyncLoadGameFromSlotDelegate::CreateUObject(this, &ThisClass::HandleLoadGameToLoadDataComplete, DataName, LoadCompletionDelegate);

	UGameplayStatics::AsyncLoadGameFromSlot(GameSaveFileName, 0, AsyncLoadDelegate);
}

bool UMD_SaveSlotManager::ParseDataFromPlayer(const APlayerState* PlayerState)
{
	// 클라이언트 환경 실행 제한
	if (IsValid(PlayerState) == false || PlayerState->HasAuthority() == false)
	{
		return false;
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
	FMD_PlayerData& PlayerData = PlayerDataLogMap.FindOrAdd(PlayerKey);

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

	FUniqueNetIdRepl UniqueNetID = IsValid(PlayerState) == true ? PlayerState->GetUniqueId() : nullptr;
	FString PlayerKey = UniqueNetID.IsValid() == true ? UniqueNetID.ToString() : TEXT("");
	PlayerKey = PlayerKey.IsEmpty() == true ? PlayerState->GetPlayerName() : PlayerKey;
	if (PlayerKey.IsEmpty() == true)
	{
		return false;
	}

	// 플레이어 데이터의 존재 여부 검사
	const FMD_PlayerData* PlayerData = PlayerDataLogMap.Find(PlayerKey);
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

TArray<FMD_GameData> UMD_SaveSlotManager::LoadAllGameData()
{
	TArray<FMD_GameData> OutGameDataList;

	// 데이터가 저장되는 세이브 파일 불러오기
	USaveGame* LoadedGame = UGameplayStatics::LoadGameFromSlot(GameSaveFileName, 0);
	UMD_SaveGame* MD_Game = Cast<UMD_SaveGame>(LoadedGame);

	// 불러온 세이브 파일 유효성 확인
	if (IsValid(MD_Game) == false)
	{
		SaveFile = NewObject<UMD_SaveGame>();
		return OutGameDataList;
	}

	SaveFile = MD_Game;

	OutGameDataList.Append(MD_Game->AutoSaveList);
	OutGameDataList.Append(MD_Game->UserSaveList);

	return OutGameDataList;
}

bool UMD_SaveSlotManager::CollectGameSessionData(FMD_GameData& OutGameData)
{
	// GameState 참조 및 클라이언트 환경 실행 제한
	UWorld* World = GetWorld();
	AMD_GameStateBase* GameState = IsValid(World) == true ? World->GetGameState<AMD_GameStateBase>() : nullptr;
	if (IsValid(GameState) == false || GameState->HasAuthority() == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 게임 데이터 저장은 Host/Server에서만 실행할 수 있습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// 게임 데이터를 수집하는 객체 생성
	FMD_GameData GameData;

	// 맵이 저장된 패키지 얻기 및 유효성 확인
	UPackage* WorldPackage = World->GetPackage();
	if (IsValid(WorldPackage) == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 현재 맵의 패키지가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// 현재 맵의 패키지 경로 저장(PIE 환경 접두사 제거) 및 저장 시각 기록
	GameData.MapPackagePath = UWorld::RemovePIEPrefix(WorldPackage->GetName());
	
	// 현재 진행중인 퀘스트의 아이디 저장
	GameData.QuestID = FPrimaryAssetId();

	// 누계 점수 저장
	GameData.Score = GameState->GetScore();

	// 인게임 재화량 저장
	GameData.Credit = 0;

	// 상인 우호도 저장
	GameData.MerchantLevel = 0;

	// 인게임 경과 시간 저장
	GameData.InGameTime = GameState->GetCurrentDay() + GameState->GetElapsedGameSeconds();

	// 현재 접속중인 플레이어 별 데이터 저장
	for (const APlayerState* PlayerState : GameState->PlayerArray)
	{
		ParseDataFromPlayer(PlayerState);
	}
	GameData.PlayerData = PlayerDataLogMap;

	GameData.SaveTime = FDateTime::Now();

	OutGameData = GameData;
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
	if (bIsAuto == true)
	{
		// 저장되어야 인덱스가 자동 저장 목록 크기보다 작은 경우 해당 인덱스에 위치한 데이터 교체
		if (AutoSaveIndex < MD_Game->AutoSaveList.Num())
		{
			MD_Game->AutoSaveList[AutoSaveIndex] = CollectedGameData;
		}
		else
		{
			MD_Game->AutoSaveList.Add(CollectedGameData);
		}
	}
	else
	{
		// 동일한 이름으로 저장된 게임 데이터 삭제
		MD_Game->UserSaveList.RemoveAll([&CollectedGameData](const FMD_GameData& Element)
			{
				return Element.UniqueName == CollectedGameData.UniqueName;
			});

		// 가장 최근의 게임 데이터가 앞순서에 오도록 0번 인덱스에 삽입
		MD_Game->UserSaveList.Insert(CollectedGameData, 0);
	}

	// 최대 저장 개수 제한 적용
	MD_Game->ApplyMasSaveCountLimit(MaxAutoSaveSlotCount, MaxUserSaveSlotCount);

	// 비동기 콜백 바인딩
	FAsyncSaveGameToSlotDelegate AsyncSaveDelegate = FAsyncSaveGameToSlotDelegate::CreateLambda([this, CollectedGameData, bIsAuto, SaveCompletionDelegate](const FString& SavedSlotName, const int32 UserIndex, bool bSaveSuccess)
		{
			// 세이브 파일 저장 성공 여부 확인
			bool bWasSuccessful = bSaveSuccess == true && SavedSlotName == GameSaveFileName && UserIndex == 0;

			if (bWasSuccessful == true && bIsAuto == true)
			{
				AutoSaveIndex = (AutoSaveIndex + 1) % MaxAutoSaveSlotCount;
			}

			SaveCompletionDelegate.ExecuteIfBound(CollectedGameData, bWasSuccessful);

			// 비동기 작업 진행 상태 플래그 설정
			bAsyncOperationInProgress = false;
		});

	// 세이브 파일 저장
	UGameplayStatics::AsyncSaveGameToSlot(MD_Game, GameSaveFileName, 0, AsyncSaveDelegate);
}

void UMD_SaveSlotManager::HandleLoadGameToLoadDataComplete(const FString& LoadedSlotName, const int32 UserIndex, USaveGame* LoadedSaveGame, const FString DataName, const FOnLoadMDGameDataComplete LoadCompletionDelegate)
{
	FMD_GameData* LoadedGameData = nullptr;

	// 세이브 파일 불러오기 성공 여부 확인
	UMD_SaveGame* MD_Game = Cast<UMD_SaveGame>(LoadedSaveGame);
	bool bWasSuccessful = IsValid(MD_Game) == true && LoadedSlotName == GameSaveFileName && UserIndex == 0;

	if (bWasSuccessful == false)
	{
		LoadCompletionDelegate.ExecuteIfBound(FMD_GameData(), false);

		bAsyncOperationInProgress = false;
		return;
	}

	if (LoadedGameData == nullptr)
	{
		LoadedGameData = MD_Game->AutoSaveList.FindByPredicate([&DataName](const FMD_GameData& GameData)
			{
				return GameData.UniqueName == DataName;
			});
	}
	if (LoadedGameData == nullptr)
	{
		LoadedGameData = MD_Game->UserSaveList.FindByPredicate([&DataName](const FMD_GameData& GameData)
			{
				return GameData.UniqueName == DataName;
			});
	}

	if (LoadedGameData == nullptr)
	{
		LoadCompletionDelegate.ExecuteIfBound(FMD_GameData(), false);

		bAsyncOperationInProgress = false;
		return;
	}

	bool LoadSuccess = LoadGameDataToWorld(*LoadedGameData);

	LoadCompletionDelegate.ExecuteIfBound(*LoadedGameData, LoadSuccess);

	bAsyncOperationInProgress = false;
	return;
}
