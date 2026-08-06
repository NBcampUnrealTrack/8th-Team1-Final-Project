// 프로젝트 설정의 저작권 안내 문구를 사용한다.

#include "Data/SaveGame/MD_SaveSlotManager.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerState.h"

#include "Game/MD_GameStateBase.h"
#include "Character/MD_Character.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Util/MD_LogUtility.h"

FMD_SaveSlot UMD_SaveSlotManager::SaveGameSessionData(const FString& SaveSlotName) const
{
	// 비동기 파일 I/O 중 같은 Manager에서 동기 요청이 실행되어 슬롯 파일이 경합하지 않게 한다.
	if (bAsyncOperationInProgress == true)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 비동기 세이브 작업 중에는 동기 저장을 실행할 수 없습니다."), *UE_CALL_LOCATIONINFO);
		return FMD_SaveSlot::Empty;
	}

	// 세이브 슬롯 이름의 유효성 확인
	if (SaveSlotName.IsEmpty() == true || SaveSlotName.TrimStartAndEnd() != SaveSlotName || FPaths::MakeValidFileName(SaveSlotName) != SaveSlotName)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 사용할 수 없는 세이브 슬롯 이름입니다: %s"), *UE_CALL_LOCATIONINFO, *SaveSlotName);
		return FMD_SaveSlot::Empty;
	}

	// 현재 World의 유효성 확인 및 클라이언트 환경 실행 제한
	UWorld* World = GetWorld();
	if (IsValid(World) == false || World->GetNetMode() == NM_Client)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s GameSession 저장은 Host/Server에서만 실행할 수 있습니다."), *UE_CALL_LOCATIONINFO);
		return FMD_SaveSlot::Empty;
	}

	// GameState 참조 (접속 플레이어 목록)
	AMD_GameStateBase* GameState = IsValid(World) == true ? World->GetGameState<AMD_GameStateBase>() : nullptr;
	if (IsValid(GameState) == false || GameState->HasAuthority() == false)
	{
		UE_LOG(MarineDemolition, Error, TEXT("%s 저장할 GameState를 찾지 못했습니다."), *UE_CALL_LOCATIONINFO);
		return FMD_SaveSlot::Empty;
	}

	// 저장되는 데이터 객체 생성 및 유효성 확인
	UMD_SaveGameSession* GameSessionData = NewObject<UMD_SaveGameSession>();
	if (IsValid(GameSessionData) == false)
	{
		UE_LOG(MarineDemolition, Error, TEXT("%s GameSession 세이브 데이터 객체를 생성하지 못했습니다."), *UE_CALL_LOCATIONINFO);
		return FMD_SaveSlot::Empty;
	}

	// 누계 점수 및 경과 일자 저장
	GameSessionData->Score = GameState->GetScore();
	GameSessionData->ElapsedDays = GameState->GetCurrentDay();

	// 현재 접속중인 플레이어 별 데이터 저장
	for (const APlayerState* PlayerState : GameState->PlayerArray)
	{
		// 유효하지 않은 플레이어의 경우 건너뛰기
		if (IsValid(PlayerState) == false)
		{
			continue;
		}

		// 플레이어 고유 식별자 얻기
		FUniqueNetIdRepl UniqueNetID = PlayerState->GetUniqueId();
		FString PlayerKey = (UniqueNetID.IsValid() == true && UniqueNetID->IsValid() == true) ? UniqueNetID.ToString() : TEXT("");
		// Unique Net ID가 없는 경우 플레이어 이름을 고유 식별자로 사용
		PlayerKey = PlayerKey.IsEmpty() == true ? PlayerState->GetPlayerName() : PlayerKey;

		// 플레이어 고유 식별자의 유효성 검사
		if (PlayerKey.IsEmpty() == true)
		{
			continue;
		}

		// 플레이어 데이터가 저장되는 객체 찾기 또는 추가
		FMD_PlayerData& PlayerData = GameSessionData->PlayerDataMap.FindOrAdd(PlayerKey);

		// 플레이어 캐릭터 참조
		AMD_Character* PlayerCharacter = Cast<AMD_Character>(PlayerState->GetPawn());

		// 플레이어 캐릭터의 EquipHotbar Component가 유효한 경우 데이터 저장
		UMD_EquipHotbarComponent* EquipmentComponent = IsValid(PlayerCharacter) == true ? PlayerCharacter->GetHotbarComponent() : nullptr;
		if (IsValid(EquipmentComponent) == true)
		{
			PlayerData.ToolSlots = EquipmentComponent->BuildEquipmentSaveData();
		}
	}

	// 세이브 슬롯 객체 생성
	FMD_SaveSlot SaveSlot = FMD_SaveSlot(SaveSlotName);

	// 맵이 저장된 패키지 얻기 및 유효성 확인
	UPackage* WorldPackage = World->GetPackage();
	if (IsValid(WorldPackage) == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 현재 맵의 패키지가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);
		return FMD_SaveSlot::Empty;
	}

	// 현재 맵의 패키지 경로 저장(PIE 환경 접두사 제거) 및 저장 시각 기록
	SaveSlot.MapPackagePath = UWorld::RemovePIEPrefix(WorldPackage->GetName());
	SaveSlot.SaveTime = FDateTime::Now();

	// MD_SaveGameSession 객체를 슬롯에 저장
	bool bSaveSuccess = UGameplayStatics::SaveGameToSlot(GameSessionData, SaveSlotName, 0);

	// 데이터 슬롯 저장 성공 여부 검사
	if (bSaveSuccess == false)
	{
		UE_LOG(MarineDemolition, Error, TEXT("%s GameSession 저장에 실패했습니다: %s"), *UE_CALL_LOCATIONINFO, *SaveSlotName);
		return FMD_SaveSlot::Empty;
	}

	UE_LOG(MarineDemolition, Log, TEXT("%s GameSession 저장을 완료했습니다: %s"), *UE_CALL_LOCATIONINFO, *SaveSlotName);
	return SaveSlot;
}

bool UMD_SaveSlotManager::LoadGameSessionData(const FMD_SaveSlot& SaveSlot) const
{
	// 비동기 파일 I/O 중 같은 Manager에서 동기 요청이 실행되어 활성 데이터가 경합하지 않게 한다.
	if (bAsyncOperationInProgress == true)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 비동기 세이브 작업 중에는 동기 로드를 실행할 수 없습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// 현재 World의 유효성 확인 및 클라이언트 환경 실행 제한
	UWorld* World = GetWorld();
	if (IsValid(World) == false || World->GetNetMode() == NM_Client)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s GameSession 로드는 Host/Server에서만 실행할 수 있습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// 세이브 슬롯이 비어있는지 확인
	if (SaveSlot.IsEmpty() == true)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 로드할 세이브 슬롯이 존재하지 않습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// 무상태 Manager는 트래블 뒤 복원 데이터를 보관하지 않으므로 호출부가 저장된 맵을 먼저 연 상태에서만 로드한다.
	FString CurrentMapPackagePath = UWorld::RemovePIEPrefix(World->GetPackage()->GetName());
	if (SaveSlot.MapPackagePath != CurrentMapPackagePath)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 저장된 맵을 먼저 연 뒤 GameSession을 로드해야 합니다. 현재: %s, 저장: %s"), *UE_CALL_LOCATIONINFO, *CurrentMapPackagePath, *SaveSlot.MapPackagePath);
		return false;
	}

	// 저장된 게임 데이터 로드 및 유효성 확인
	USaveGame* GameData = UGameplayStatics::LoadGameFromSlot(SaveSlot.SlotName, 0);
	UMD_SaveGameSession* GameSessionData = Cast<UMD_SaveGameSession>(GameData);
	if (IsValid(GameSessionData) == false)
	{
		UE_LOG(MarineDemolition, Error, TEXT("%s GameSession 세이브 파일이 손상되었거나 타입이 다릅니다: %s"), *UE_CALL_LOCATIONINFO, *SaveSlot.SlotName);
		return false;
	}

	// 불러온 게임 데이터 적용할 GameState 객체 얻기
	AMD_GameStateBase* GameState = IsValid(World) == true ? World->GetGameState<AMD_GameStateBase>() : nullptr;
	if (IsValid(GameState) == false || GameState->HasAuthority() == false)
	{
		UE_LOG(MarineDemolition, Error, TEXT("%s 불러온 데이터를 적용할 GameState를 찾지 못했습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// 정의된 함수를 호출하여 불러온 게임 데이터 적용 시 Replication 및 Event 실행 순서를 유지
	int32 ScoreDelta = GameSessionData->Score - GameState->GetScore();
	if (ScoreDelta != 0)
	{
		GameState->AddScoreDelta(ScoreDelta);
	}
	int32 LoadedDay = FMath::Max(GameSessionData->ElapsedDays, 1);
	if (GameState->GetCurrentDay() != LoadedDay)
	{
		GameState->SetCurrentDay(LoadedDay);
	}

	// 현재 접속 중인 플레이어에게 불러온 데이터 적용
	for (const APlayerState* PlayerState : GameState->PlayerArray)
	{
		// 유효하지 않은 플레이어의 경우 건너뛰기
		if (IsValid(PlayerState) == false)
		{
			continue;
		}

		// 플레이어 고유 식별자 얻기
		FUniqueNetIdRepl UniqueNetID = PlayerState->GetUniqueId();
		FString PlayerKey = (UniqueNetID.IsValid() == true && UniqueNetID->IsValid() == true) ? UniqueNetID.ToString() : TEXT("");
		// Unique Net ID가 없는 경우 플레이어 이름을 고유 식별자로 사용
		PlayerKey = PlayerKey.IsEmpty() == true ? PlayerState->GetPlayerName() : PlayerKey;

		// 플레이어 고유 식별자의 유효성 검사
		if (PlayerKey.IsEmpty() == true)
		{
			continue;
		}

		// 저장된 플레이어 데이터가 존재하지 않는 경우 건너뛰기
		FMD_PlayerData* PlayerData = GameSessionData->PlayerDataMap.Find(PlayerKey);
		if (PlayerData == nullptr)
		{
			continue;
		}

		// 플레이어 캐릭터 참조
		AMD_Character* PlayerCharacter = Cast<AMD_Character>(PlayerState->GetPawn());

		// 플레이어 캐릭터의 EquipHotbar Component가 유효한 경우 데이터 불러온 데이터 적용
		UMD_EquipHotbarComponent* EquipmentComponent = IsValid(PlayerCharacter) == true ? PlayerCharacter->GetHotbarComponent() : nullptr;
		if (IsValid(EquipmentComponent) == true)
		{
			EquipmentComponent->RestoreEquipmentSaveData(PlayerData->ToolSlots);
		}
	}

	// 서버 GameState 변경을 다음 네트워크 업데이트에서 접속 클라이언트에 즉시 전달한다.
	GameState->ForceNetUpdate();

	UE_LOG(MarineDemolition, Log, TEXT("%s GameSession 로드를 완료했습니다: %s"), *UE_CALL_LOCATIONINFO, *SaveSlot.SlotName);

	return true;
}

void UMD_SaveSlotManager::SaveGameSessionDataAsync(const FString& SaveSlotName, const FOnSaveMDGameSessionAsyncComplete& CompletionDelegate)
{
	// 다른 비동기 I/O가 진행 중이면 활성 슬롯과 완료 순서가 뒤섞이지 않도록 새 요청을 거부한다.
	if (bAsyncOperationInProgress == true)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 다른 비동기 세이브 작업이 진행 중입니다."), *UE_CALL_LOCATIONINFO);
		CompletionDelegate.ExecuteIfBound(FMD_SaveSlot::Empty, false);
		return;
	}

	// 세이브 슬롯 이름의 유효성 확인
	if (SaveSlotName.IsEmpty() == true || SaveSlotName.TrimStartAndEnd() != SaveSlotName || FPaths::MakeValidFileName(SaveSlotName) != SaveSlotName)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 사용할 수 없는 세이브 슬롯 이름입니다: %s"), *UE_CALL_LOCATIONINFO, *SaveSlotName);
		CompletionDelegate.ExecuteIfBound(FMD_SaveSlot::Empty, false);
		return;
	}

	// GameState 참조 및 클라이언트 환경 실행 제한 (게임 스레드)
	UWorld* World = GetWorld();
	AMD_GameStateBase* GameState = IsValid(World) == true ? World->GetGameState<AMD_GameStateBase>() : nullptr;
	if (IsValid(GameState) == false || GameState->HasAuthority() == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s GameSession 비동기 저장은 Host/Server에서만 실행할 수 있습니다."), *UE_CALL_LOCATIONINFO);
		CompletionDelegate.ExecuteIfBound(FMD_SaveSlot::Empty, false);
		return;
	}

	// 게임 데이터 저장 객체 생성 및 유효성 확인
	UMD_SaveGameSession* GameSessionData = NewObject<UMD_SaveGameSession>();
	if (IsValid(GameSessionData) == false)
	{
		UE_LOG(MarineDemolition, Error, TEXT("%s GameSession 세이브 데이터 객체를 생성하지 못했습니다."), *UE_CALL_LOCATIONINFO);
		CompletionDelegate.ExecuteIfBound(FMD_SaveSlot::Empty, false);
		return;
	}

	// 누계 점수 및 경과 일자 저장
	GameSessionData->Score = GameState->GetScore();
	GameSessionData->ElapsedDays = GameState->GetCurrentDay();

	// 현재 접속중인 플레이어 별 데이터 저장
	for (const APlayerState* PlayerState : GameState->PlayerArray)
	{
		// 유효하지 않은 플레이어의 경우 건너뛰기
		if (IsValid(PlayerState) == false)
		{
			continue;
		}

		// 플레이어 고유 식별자 얻기
		FUniqueNetIdRepl UniqueNetID = PlayerState->GetUniqueId();
		FString PlayerKey = (UniqueNetID.IsValid() == true && UniqueNetID->IsValid() == true) ? UniqueNetID.ToString() : TEXT("");
		// Unique Net ID가 없는 경우 플레이어 이름을 고유 식별자로 사용
		PlayerKey = PlayerKey.IsEmpty() == true ? PlayerState->GetPlayerName() : PlayerKey;

		// 플레이어 고유 식별자의 유효성 검사
		if (PlayerKey.IsEmpty() == true)
		{
			continue;
		}

		// 플레이어 데이터가 저장되는 객체 찾기 또는 추가
		FMD_PlayerData& PlayerData = GameSessionData->PlayerDataMap.FindOrAdd(PlayerKey);

		// 플레이어 캐릭터 참조
		AMD_Character* PlayerCharacter = Cast<AMD_Character>(PlayerState->GetPawn());

		// 플레이어 캐릭터의 EquipHotbar Component가 유효한 경우 데이터 저장
		UMD_EquipHotbarComponent* EquipmentComponent = IsValid(PlayerCharacter) == true ? PlayerCharacter->GetHotbarComponent() : nullptr;
		if (IsValid(EquipmentComponent) == true)
		{
			PlayerData.ToolSlots = EquipmentComponent->BuildEquipmentSaveData();
		}
	}

	// 세이브 슬롯 객체 생성
	FMD_SaveSlot SaveSlot = FMD_SaveSlot(SaveSlotName);

	// 맵이 저장된 패키지 얻기 및 유효성 확인
	UPackage* WorldPackage = World->GetPackage();
	if (IsValid(WorldPackage) == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 현재 맵의 패키지가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);
		CompletionDelegate.ExecuteIfBound(FMD_SaveSlot::Empty, false);
		return;
	}

	// 현재 맵의 패키지 경로 저장(PIE 환경 접두사 제거) 및 저장 시각 기록
	SaveSlot.MapPackagePath = UWorld::RemovePIEPrefix(WorldPackage->GetName());
	SaveSlot.SaveTime = FDateTime::Now();

	// 비동기 작업 진행 상태 플래그 설정
	bAsyncOperationInProgress = true;

	// 비동기 직렬화가 끝날 때까지 저장 객체를 UPROPERTY로 보관하고 중복 요청을 차단한다.
	PendingAsyncGameSessionData = GameSessionData;

	// 비동기 콜백 함수 바인딩
	FAsyncSaveGameToSlotDelegate AsyncSaveDelegate = FAsyncSaveGameToSlotDelegate::CreateUObject(this, &ThisClass::HandleSaveGameSessionDataComplete, SaveSlot, CompletionDelegate);

	// SaveGame 비동기 함수 실행
	UGameplayStatics::AsyncSaveGameToSlot(GameSessionData, SaveSlot.SlotName, 0, AsyncSaveDelegate);
	
	return;
}

void UMD_SaveSlotManager::LoadGameSessionDataAsync(const FMD_SaveSlot& SaveSlot, const FOnLoadMDGameSessionAsyncComplete& CompletionDelegate)
{
	// 다른 비동기 I/O가 진행 중이면 완료 순서에 따라 활성 데이터가 역전되지 않도록 새 요청을 거부한다.
	if (bAsyncOperationInProgress == true)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 다른 비동기 세이브 작업이 진행 중입니다."), *UE_CALL_LOCATIONINFO);
		CompletionDelegate.ExecuteIfBound(SaveSlot, nullptr, false);
		return;
	}

	// 현재 World의 유효성 확인 및 클라이언트 환경 실행 제한
	UWorld* World = GetWorld();
	if (IsValid(World) == false || World->GetNetMode() == NM_Client)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s GameSession 비동기 로드는 Host/Server에서만 실행할 수 있습니다."), *UE_CALL_LOCATIONINFO);
		CompletionDelegate.ExecuteIfBound(SaveSlot, nullptr, false);
		return;
	}

	// 세이브 슬롯이 비어있는지 확인
	if (SaveSlot.IsEmpty() == true)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 로드할 세이브 슬롯이 존재하지 않습니다: %s"), *UE_CALL_LOCATIONINFO, *SaveSlot.SlotName);
		CompletionDelegate.ExecuteIfBound(SaveSlot, nullptr, false);
		return;
	}

	// 비동기 작업 진행 상태 플래그 설정
	bAsyncOperationInProgress = true;

	// 비동기 콜백 함수 바인딩
	FAsyncLoadGameFromSlotDelegate AsyncLoadDelegate = FAsyncLoadGameFromSlotDelegate::CreateUObject(this, &ThisClass::HandleLoadGameSessionDataComplete, SaveSlot, CompletionDelegate);

	// 플랫폼 파일 읽기는 작업 스레드에서 수행되고 UObject 생성과 완료 콜백은 게임 스레드에서 실행된다.
	UGameplayStatics::AsyncLoadGameFromSlot(SaveSlot.SlotName, 0, AsyncLoadDelegate);
}

bool UMD_SaveSlotManager::CollectGameSessionData(FMD_SaveSlot& OutSaveSlot, UMD_SaveGameSession* OutGameSessionData)
{
	// GameState 참조 및 클라이언트 환경 실행 제한 (게임 스레드)
	UWorld* World = GetWorld();
	AMD_GameStateBase* GameState = IsValid(World) == true ? World->GetGameState<AMD_GameStateBase>() : nullptr;
	if (IsValid(GameState) == false || GameState->HasAuthority() == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s GameSession 비동기 저장은 Host/Server에서만 실행할 수 있습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	if (IsValid(OutGameSessionData) == false)
	{
		UE_LOG(MarineDemolition, Error, TEXT("%s GameSession 데이터를 저장할 객체가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// 누계 점수 및 경과 일자 저장
	OutGameSessionData->Score = GameState->GetScore();
	OutGameSessionData->ElapsedDays = GameState->GetCurrentDay();

	// 현재 접속중인 플레이어 별 데이터 저장
	for (const APlayerState* PlayerState : GameState->PlayerArray)
	{
		// 유효하지 않은 플레이어의 경우 건너뛰기
		if (IsValid(PlayerState) == false)
		{
			continue;
		}

		// 플레이어 고유 식별자 얻기
		FUniqueNetIdRepl UniqueNetID = PlayerState->GetUniqueId();
		FString PlayerKey = (UniqueNetID.IsValid() == true && UniqueNetID->IsValid() == true) ? UniqueNetID.ToString() : TEXT("");
		// Unique Net ID가 없는 경우 플레이어 이름을 고유 식별자로 사용
		PlayerKey = PlayerKey.IsEmpty() == true ? PlayerState->GetPlayerName() : PlayerKey;

		// 플레이어 고유 식별자의 유효성 검사
		if (PlayerKey.IsEmpty() == true)
		{
			continue;
		}

		// 플레이어 데이터가 저장되는 객체 찾기 또는 추가
		FMD_PlayerData& PlayerData = OutGameSessionData->PlayerDataMap.FindOrAdd(PlayerKey);

		// 플레이어 캐릭터 참조
		AMD_Character* PlayerCharacter = Cast<AMD_Character>(PlayerState->GetPawn());

		// 플레이어 캐릭터의 EquipHotbar Component가 유효한 경우 데이터 저장
		UMD_EquipHotbarComponent* EquipmentComponent = IsValid(PlayerCharacter) == true ? PlayerCharacter->GetHotbarComponent() : nullptr;
		if (IsValid(EquipmentComponent) == true)
		{
			PlayerData.ToolSlots = EquipmentComponent->BuildEquipmentSaveData();
		}
	}

	// 맵이 저장된 패키지 얻기 및 유효성 확인
	UPackage* WorldPackage = World->GetPackage();
	if (IsValid(WorldPackage) == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 현재 맵의 패키지가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// 현재 맵의 패키지 경로 저장(PIE 환경 접두사 제거) 및 저장 시각 기록
	OutSaveSlot.MapPackagePath = UWorld::RemovePIEPrefix(WorldPackage->GetName());
	OutSaveSlot.SaveTime = FDateTime::Now();

	return true;
}

void UMD_SaveSlotManager::HandleSaveGameSessionDataComplete(const FString& SavedSlotName, const int32 UserIndex, bool bSaveSuccess, const FMD_SaveSlot SaveSlot, const FOnSaveMDGameSessionAsyncComplete CompletionDelegate)
{
	// 데이터 저장 성공 여부 확인
	bool bWasSuccessful = bSaveSuccess == true && SavedSlotName == SaveSlot.SlotName && UserIndex == 0;
	if (bWasSuccessful == false)
	{
		UE_LOG(MarineDemolition, Error, TEXT("%s GameSession 데이터 비동기 저장에 실패했습니다: %s"), *UE_CALL_LOCATIONINFO, *SaveSlot.SlotName);

		CompletionDelegate.ExecuteIfBound(FMD_SaveSlot::Empty, false);

		// 비동기 작업 상태 플래그 해제
		bAsyncOperationInProgress = false;
		return;
	}

	UE_LOG(MarineDemolition, Log, TEXT("%s GameSession 비동기 저장을 완료했습니다: %s"), *UE_CALL_LOCATIONINFO, *SavedSlotName);

	CompletionDelegate.ExecuteIfBound(SaveSlot, true);

	// 비동기 작업 상태 플래그 해제
	bAsyncOperationInProgress = false;
}

void UMD_SaveSlotManager::HandleLoadGameSessionDataComplete(const FString& LoadedSlotName, const int32 UserIndex, USaveGame* LoadedSaveGame, const FMD_SaveSlot SaveSlot, const FOnLoadMDGameSessionAsyncComplete CompletionDelegate)
{
	// 불러온 데이터 타입 변환
	UMD_SaveGameSession* GameSessionData = Cast<UMD_SaveGameSession>(LoadedSaveGame);

	// 데이터 불러오기 성공 여부 확인
	bool bWasSuccessful = IsValid(GameSessionData) == true && LoadedSlotName == SaveSlot.SlotName && UserIndex == 0;
	if (bWasSuccessful == false)
	{
		UE_LOG(MarineDemolition, Error, TEXT("%s GameSession 비동기 로드에 실패했습니다: %s"), *UE_CALL_LOCATIONINFO, *SaveSlot.SlotName);

		CompletionDelegate.ExecuteIfBound(SaveSlot, nullptr, false);

		// 비동기 작업 상태 플래그 해제
		bAsyncOperationInProgress = false;
		return;
	}

	// 불러온 게임 데이터 적용할 GameState 객체 얻기
	UWorld* LoadedWorld = GetWorld();
	AMD_GameStateBase* GameState = IsValid(LoadedWorld) == true ? LoadedWorld->GetGameState<AMD_GameStateBase>() : nullptr;
	if (IsValid(GameState) == false || GameState->HasAuthority() == false)
	{
		UE_LOG(MarineDemolition, Error, TEXT("%s 불러온 데이터를 적용할 GameState를 찾지 못했습니다."), *UE_CALL_LOCATIONINFO);

		CompletionDelegate.ExecuteIfBound(SaveSlot, GameSessionData, false);

		// 비동기 작업 상태 플래그 해제
		bAsyncOperationInProgress = false;
		return;
	}

	// 정의된 함수를 호출하여 불러온 게임 데이터 적용 시 Replication 및 Event 실행 순서를 유지
	int32 ScoreDelta = GameSessionData->Score - GameState->GetScore();
	if (ScoreDelta != 0)
	{
		GameState->AddScoreDelta(ScoreDelta);
	}
	int32 LoadedDay = FMath::Max(GameSessionData->ElapsedDays, 1);
	if (GameState->GetCurrentDay() != LoadedDay)
	{
		GameState->SetCurrentDay(LoadedDay);
	}

	// 현재 접속 중인 플레이어에게 불러온 데이터 적용
	for (const APlayerState* PlayerState : GameState->PlayerArray)
	{
		// 유효하지 않은 플레이어의 경우 건너뛰기
		if (IsValid(PlayerState) == false)
		{
			continue;
		}

		// 플레이어 고유 식별자 얻기
		FUniqueNetIdRepl UniqueNetID = PlayerState->GetUniqueId();
		FString PlayerKey = (UniqueNetID.IsValid() == true && UniqueNetID->IsValid() == true) ? UniqueNetID.ToString() : TEXT("");
		// Unique Net ID가 없는 경우 플레이어 이름을 고유 식별자로 사용
		PlayerKey = PlayerKey.IsEmpty() == true ? PlayerState->GetPlayerName() : PlayerKey;

		// 플레이어 고유 식별자의 유효성 검사
		if (PlayerKey.IsEmpty() == true)
		{
			continue;
		}

		// 저장된 플레이어 데이터가 존재하지 않는 경우 건너뛰기
		FMD_PlayerData* PlayerData = GameSessionData->PlayerDataMap.Find(PlayerKey);
		if (PlayerData == nullptr)
		{
			continue;
		}

		// 플레이어 캐릭터 참조
		AMD_Character* PlayerCharacter = Cast<AMD_Character>(PlayerState->GetPawn());

		// 플레이어 캐릭터의 EquipHotbar Component가 유효한 경우 데이터 불러온 데이터 적용
		UMD_EquipHotbarComponent* EquipmentComponent = IsValid(PlayerCharacter) == true ? PlayerCharacter->GetHotbarComponent() : nullptr;
		if (IsValid(EquipmentComponent) == true)
		{
			EquipmentComponent->RestoreEquipmentSaveData(PlayerData->ToolSlots);
		}
	}

	// 서버 GameState 변경을 다음 네트워크 업데이트에서 접속 클라이언트에 즉시 전달한다.
	GameState->ForceNetUpdate();

	// 불러오기된 세이브 슬롯과 데이터를 멤버 변수에 저장
	ActiveSlot = MakeShared<FMD_SaveSlot>(SaveSlot);
	ActiveGameSessionData = GameSessionData;

	UE_LOG(MarineDemolition, Log, TEXT("%s GameSession 비동기 로드를 완료했습니다: %s"), *UE_CALL_LOCATIONINFO, *LoadedSlotName);

	CompletionDelegate.ExecuteIfBound(SaveSlot, GameSessionData, true);

	// 비동기 작업 상태 플래그 해제
	bAsyncOperationInProgress = false;
}
