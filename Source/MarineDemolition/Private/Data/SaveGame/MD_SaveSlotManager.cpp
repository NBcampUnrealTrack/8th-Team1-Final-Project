// Fill out your copyright notice in the Description page of Project Settings.

#include "Data/SaveGame/MD_SaveSlotManager.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerState.h"

#include "Game/MD_GameStateBase.h"
#include "Character/MD_Character.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Util/MD_LogUtility.h"

const FString UMD_SaveSlotManager::SlotListSaveFileName = TEXT("MD_SaveSlotList");
const FString UMD_SaveSlotManager::SQL_DatabaseFileName = TEXT("MD_GameSession.sqlite");

void UMD_SaveSlotManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	LoadSlotList();
}

void UMD_SaveSlotManager::Deinitialize()
{
	Super::Deinitialize();
}

FMD_SaveSlot UMD_SaveSlotManager::SaveGameSessionData(const FString& SaveSlotName, EMD_SaveSlotType SaveSlotType) const
{
	// 비동기 파일 I/O 중 같은 Manager에서 동기 요청이 실행되어 슬롯 파일이 경합하지 않게 한다.
	if (bAsyncOperationInProgress == true)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 비동기 세이브 작업 중에는 동기 저장을 실행할 수 없습니다."), *UE_CALL_LOCATIONINFO);
		return FMD_SaveSlot::Empty;
	}

	// 선택된 세이브 슬롯 종류 또는 최대 저장 가능 개수가 유효하지 않은 경우 함수 조기 종료
	int32 MaxSaveSlotCount = SaveSlotType == EMD_SaveSlotType::Auto ? MaxAutoSaveSlotCount : MaxUserSaveSlotCount;
	if ((SaveSlotType != EMD_SaveSlotType::User && SaveSlotType != EMD_SaveSlotType::Auto) || MaxSaveSlotCount <= 0)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 선택한 세이브 슬롯 종류를 사용할 수 없습니다."), *UE_CALL_LOCATIONINFO);
		return FMD_SaveSlot::Empty;
	}

	if (SaveSlotName.IsEmpty() == true || SaveSlotName.TrimStartAndEnd() != SaveSlotName || FPaths::MakeValidFileName(SaveSlotName) != SaveSlotName)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 사용할 수 없는 세이브 슬롯 이름입니다: %s"), *UE_CALL_LOCATIONINFO, *SaveSlotName);
		return FMD_SaveSlot::Empty;
	}

	// 데이터 세이브를 위한 객체 생성
	FMD_SaveSlot SaveSlot = FMD_SaveSlot(SaveSlotName);
	UMD_SaveGameSession* GameSessionData = NewObject<UMD_SaveGameSession>();

	// 저장해야 하는 데이터 수집 및 결과 확인
	bool bIsDataCollected = CollectGameSessionData(SaveSlot, GameSessionData);
	if (bIsDataCollected == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 저장되는 GameSession 데이터를 수집하지 못하였습니다."), *UE_CALL_LOCATIONINFO);
		return FMD_SaveSlot::Empty;
	}

	// MD_SaveGameSession 객체를 슬롯에 저장
	bool bSaveSuccess = UGameplayStatics::SaveGameToSlot(GameSessionData, SaveSlotName, 0);

	// 데이터 슬롯 저장 성공 여부 검사
	if (bSaveSuccess == false)
	{
		UE_LOG(MarineDemolition, Error, TEXT("%s GameSession 저장에 실패했습니다: %s"), *UE_CALL_LOCATIONINFO, *SaveSlotName);
		return FMD_SaveSlot::Empty;
	}

	// 세이브 슬롯을 정보 저장 및 가장 오래된 세이브 파일&정보 제거
	bool bSlotListUpdated = AddSlotToList(SaveSlot, SaveSlotType);
	if (bSlotListUpdated == false)
	{
		UE_LOG(MarineDemolition, Error, TEXT("%s GameSession 슬롯 목록 갱신 또는 오래된 파일 삭제에 실패했습니다: %s"), *UE_CALL_LOCATIONINFO, *SaveSlotName);
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

void UMD_SaveSlotManager::SaveGameSessionDataAsync(const FString& SaveSlotName, EMD_SaveSlotType SaveSlotType, const FOnSaveMDGameSessionAsyncComplete& CompletionDelegate)
{
	// 다른 비동기 I/O가 진행 중이면 활성 슬롯과 완료 순서가 뒤섞이지 않도록 새 요청을 거부한다.
	if (bAsyncOperationInProgress == true)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 다른 비동기 세이브 작업이 진행 중입니다."), *UE_CALL_LOCATIONINFO);
		CompletionDelegate.ExecuteIfBound(FMD_SaveSlot::Empty, false);
		return;
	}

	// 선택된 세이브 슬롯 종류 또는 최대 저장 가능 개수가 유효하지 않은 경우 함수 조기 종료
	int32 MaxSaveSlotCount = SaveSlotType == EMD_SaveSlotType::Auto ? MaxAutoSaveSlotCount : MaxUserSaveSlotCount;
	if ((SaveSlotType != EMD_SaveSlotType::User && SaveSlotType != EMD_SaveSlotType::Auto) || MaxSaveSlotCount <= 0)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 선택한 세이브 슬롯 종류를 사용할 수 없습니다."), *UE_CALL_LOCATIONINFO);
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

	// 데이터 세이브를 위한 객체 생성
	FMD_SaveSlot SaveSlot = FMD_SaveSlot(SaveSlotName);
	UMD_SaveGameSession* GameSessionData = NewObject<UMD_SaveGameSession>();

	// 저장해야 하는 데이터 수집 및 결과 확인
	bool bIsDataCollected = CollectGameSessionData(SaveSlot, GameSessionData);
	if (bIsDataCollected == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 저장되는 GameSession 데이터를 수집하지 못하였습니다."), *UE_CALL_LOCATIONINFO);
		CompletionDelegate.ExecuteIfBound(FMD_SaveSlot::Empty, false);
		return;
	}

	// 비동기 작업 진행 상태 플래그 설정
	bAsyncOperationInProgress = true;

	// 비동기 콜백 함수 바인딩
	FAsyncSaveGameToSlotDelegate AsyncSaveDelegate = FAsyncSaveGameToSlotDelegate::CreateUObject(this, &ThisClass::HandleSaveGameSessionDataComplete, SaveSlot, SaveSlotType, CompletionDelegate);

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

bool UMD_SaveSlotManager::OverwriteDataToPlayer(const APlayerState* PlayerState) const
{
	// 클라이언트 환경 실행 제한
	if (IsValid(PlayerState) == false || PlayerState->HasAuthority() == false)
	{
		return false;
	}

	if (ActiveSlot.IsValid() == false || IsValid(ActiveGameSessionData) == false)
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

	const TMap<FString, FMD_PlayerData>& PlayerDataMap = ActiveGameSessionData->PlayerDataMap;
	const FMD_PlayerData* PlayerData = PlayerDataMap.Find(PlayerKey);
	if (PlayerData == nullptr)
	{
		return false;
	}

	// 플레이어 캐릭터 참조
	AMD_Character* PlayerCharacter = Cast<AMD_Character>(PlayerState->GetPawn());

	// MD_EquipHotbar Component가 유효하면 저장된 데이터 덮어씌우기
	UMD_EquipHotbarComponent* EquipmentComponent = IsValid(PlayerCharacter) == true ? PlayerCharacter->GetHotbarComponent() : nullptr;
	if (IsValid(EquipmentComponent) == true)
	{
		EquipmentComponent->RestoreEquipmentSaveData(PlayerData->ToolSlots);
	}

	return true;
}

bool UMD_SaveSlotManager::OverwriteDataToAllPlayer() const
{
	// 클라이언트 환경 실행 제한
	UWorld* World = GetWorld();
	AGameStateBase* GameState = IsValid(World) == true ? World->GetGameState() : nullptr;
	if (IsValid(GameState) == false || GameState->HasAuthority() == false)
	{
		return false;
	}

	TArray<APlayerState*> PlayerArray = GameState->PlayerArray;
	for (const APlayerState* PlayerState : PlayerArray)
	{
		OverwriteDataToPlayer(PlayerState);
	}

	return true;
}

bool UMD_SaveSlotManager::AddSlotToList(const FMD_SaveSlot& SaveSlot, EMD_SaveSlotType SaveSlotType) const
{
	// 저장하려는 세이브 슬롯이 비어있는지 확인
	if (SaveSlot.IsEmpty() == true)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 비어있는 세이브 슬롯을 추가 또는 저장할 수 없습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// 선택된 유형의 세이브 슬롯 최대 개수가 0 이하인 경우 함수 조기 종료
	int32 MaxSaveSlotCount = SaveSlotType == EMD_SaveSlotType::Auto ? MaxAutoSaveSlotCount : MaxUserSaveSlotCount;
	if (MaxSaveSlotCount <= 0)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s %s유형의 세이브 슬롯을 저장할 수 없습니다."), *UE_CALL_LOCATIONINFO, *UEnum::GetValueAsString(SaveSlotType));
		return false;
	}

	// 선택된 유형의 세이브 슬롯을 담는 목록 얻기
	TArray<FMD_SaveSlot>& Slots = SaveSlotType == EMD_SaveSlotType::Auto ? SlotList->AutoSaveSlotList : SlotList->UserSaveSlotList;

	// 중복 슬롯 제거
	Slots.Remove(SaveSlot);

	// 목록에 세이브 슬롯 추가
	Slots.Insert(SaveSlot, 0);

	// 세이브 슬롯 목록이 최대 개수보다 많은 경우 가장 오래된 세이브 슬롯 정보 삭제
	if (Slots.Num() > MaxSaveSlotCount)
	{
		// 가장 오래된 세이브 슬롯을 목록에서 제거
		FMD_SaveSlot OldestSlot = Slots.Pop();

		// 슬롯에 저장된 GameSession 데이터 삭제
		UGameplayStatics::DeleteGameInSlot(OldestSlot.SlotName, 0);
	}

	// 변경된 SaveData 저장
	bool bSaveSuccess = UGameplayStatics::SaveGameToSlot(SlotList, SlotListSaveFileName, 0);

	return bSaveSuccess;
}

void UMD_SaveSlotManager::LoadSlotList()
{
	// 고정된 이름을 사용하여 세이브 슬롯 목록이 저장된 데이터 불러오기
	USaveGame* SaveData = UGameplayStatics::LoadGameFromSlot(SlotListSaveFileName, 0);
	UMD_SaveSlotList* LoadedSlotList = Cast<UMD_SaveSlotList>(SaveData);
	if (IsValid(LoadedSlotList) == false)
	{
		LoadedSlotList = NewObject<UMD_SaveSlotList>();
	}

	SlotList = LoadedSlotList;

	return;
}

bool UMD_SaveSlotManager::CollectGameSessionData(FMD_SaveSlot& OutSaveSlot, UMD_SaveGameSession* OutGameSessionData) const
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

		// 플레이어가 조종하는 Pawn이 존재하지 않는 경우 건너뛰기
		APawn* PlayerPawn = PlayerState->GetPawn();
		if (IsValid(PlayerPawn) == false)
		{
			continue;
		}

		// 플레이어 데이터가 저장되는 객체 찾기 또는 추가
		FMD_PlayerData& PlayerData = OutGameSessionData->PlayerDataMap.FindOrAdd(PlayerKey);

		// 플레이어 캐릭터 참조
		AMD_Character* PlayerCharacter = Cast<AMD_Character>(PlayerPawn);

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
void UMD_SaveSlotManager::HandleSaveGameSessionDataComplete(const FString& SavedSlotName, const int32 UserIndex, bool bSaveSuccess, const FMD_SaveSlot SaveSlot, EMD_SaveSlotType SaveSlotType, const FOnSaveMDGameSessionAsyncComplete CompletionDelegate)
{
	// 비동기 작업 상태 플래그 해제
	bAsyncOperationInProgress = false;

	// 데이터 저장 성공 여부 확인
	bool bWasSuccessful = bSaveSuccess == true && SavedSlotName == SaveSlot.SlotName && UserIndex == 0;
	if (bWasSuccessful == false)
	{
		UE_LOG(MarineDemolition, Error, TEXT("%s GameSession 데이터 비동기 저장에 실패했습니다: %s"), *UE_CALL_LOCATIONINFO, *SaveSlot.SlotName);

		CompletionDelegate.ExecuteIfBound(FMD_SaveSlot::Empty, false);
		return;
	}

	bWasSuccessful = AddSlotToList(SaveSlot, SaveSlotType);
	if (bWasSuccessful == false)
	{
		UE_LOG(MarineDemolition, Error, TEXT("%s GameSession 슬롯 목록 갱신 또는 오래된 파일 삭제에 실패했습니다: %s"), *UE_CALL_LOCATIONINFO, *SaveSlot.SlotName);
		CompletionDelegate.ExecuteIfBound(FMD_SaveSlot::Empty, false);
		return;
	}

	UE_LOG(MarineDemolition, Log, TEXT("%s GameSession 비동기 저장을 완료했습니다: %s"), *UE_CALL_LOCATIONINFO, *SavedSlotName);

	CompletionDelegate.ExecuteIfBound(SaveSlot, true);
}

void UMD_SaveSlotManager::HandleLoadGameSessionDataComplete(const FString& LoadedSlotName, const int32 UserIndex, USaveGame* LoadedSaveGame, const FMD_SaveSlot SaveSlot, const FOnLoadMDGameSessionAsyncComplete CompletionDelegate)
{
	// 비동기 작업 상태 플래그 해제
	bAsyncOperationInProgress = false;

	// 불러온 데이터 타입 변환
	UMD_SaveGameSession* GameSessionData = Cast<UMD_SaveGameSession>(LoadedSaveGame);

	// 데이터 불러오기 성공 여부 확인
	bool bWasSuccessful = IsValid(GameSessionData) == true && LoadedSlotName == SaveSlot.SlotName && UserIndex == 0;
	if (bWasSuccessful == false)
	{
		UE_LOG(MarineDemolition, Error, TEXT("%s GameSession 비동기 로드에 실패했습니다: %s"), *UE_CALL_LOCATIONINFO, *SaveSlot.SlotName);

		CompletionDelegate.ExecuteIfBound(SaveSlot, nullptr, false);
		return;
	}

	// 불러온 게임 데이터 적용할 GameState 객체 얻기
	UWorld* LoadedWorld = GetWorld();
	AMD_GameStateBase* GameState = IsValid(LoadedWorld) == true ? LoadedWorld->GetGameState<AMD_GameStateBase>() : nullptr;
	if (IsValid(GameState) == false || GameState->HasAuthority() == false)
	{
		UE_LOG(MarineDemolition, Error, TEXT("%s 불러온 데이터를 적용할 GameState를 찾지 못했습니다."), *UE_CALL_LOCATIONINFO);

		CompletionDelegate.ExecuteIfBound(SaveSlot, GameSessionData, false);
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
}
