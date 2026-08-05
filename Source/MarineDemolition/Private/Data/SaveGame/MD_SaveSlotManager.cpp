// Fill out your copyright notice in the Description page of Project Settings.


#include "Data/SaveGame/MD_SaveSlotManager.h"
#include "Kismet/GameplayStatics.h"
#include "Data/SaveGame/MD_SaveSlotInfoList.h"
#include "Data/SaveGame/MD_SaveGameSession.h"
#include "Character/MD_Character.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Game/MD_GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Util/MD_LogUtility.h"

void UMD_SaveSlotManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 초기 Auto Save 인덱스는 0으로 설정
	NextAutoSaveIndex = 0;

	// 사용자가 설정한 세이브 슬롯 이름 목록 불러오기
	LoadSlotInfoList();
}

TArray<FMD_SaveSlotInfo> UMD_SaveSlotManager::GetSaveSlotInfoList() const
{
	// 전체 세이브 슬롯의 이름을 저장하는 배열
	TArray<FMD_SaveSlotInfo> OutSlotInfoList;

	// 자동 저장으로 생성된 세이브 슬롯 정보 추가
	OutSlotInfoList.Append(AutoSlotInfoList);

	// 사용자가 저장한 세이브 슬롯 정보 추가
	OutSlotInfoList.Append(SlotInfoList);

	return OutSlotInfoList;
}

bool UMD_SaveSlotManager::SaveSlotInfoList()
{
	// Save Slot 정보 목록이 저장되는 Slot 이름
	const FString SlotName = TEXT("SlotInfoList");

	// Save Slot 정보 목록을 저장하는 객체 생성 및 유효성 검사
	UMD_SaveSlotInfoList* SlotInfoListData = NewObject<UMD_SaveSlotInfoList>(this);
	if (IsValid(SlotInfoListData) == false)
	{
		// 로그 출력
		UE_LOG(MarineDemolition, Warning, TEXT("%s UMD_SaveSlotInfoList 타입의 객체가 생성되지 않았습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// 저장 데이터 설정
	SlotInfoListData->SlotInformations = SlotInfoList;
	SlotInfoListData->AutoSlotInformations = AutoSlotInfoList;

	// 데이터 저장
	bool bSaveSuccess = UGameplayStatics::SaveGameToSlot(SlotInfoListData, SlotName, 0);

	// 저장된 슬롯의 개수가 기준치를 초과하는 경우
	if (SlotInfoList.Num() > MaxSaveSlotCount)
	{
		// 가장 오래된 Save Slot 제거
		FMD_SaveSlotInfo OldestSlotInfo = SlotInfoList.Pop();
		UGameplayStatics::DeleteGameInSlot(OldestSlotInfo.SlotName, 0);
	}

	// 변경된 슬롯 정보 목록 저장
	if (SaveSlotInfoList() == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s Game Session은 저장되었지만 Save Slot 정보 목록 저장에 실패하였습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	return true;
}

bool UMD_SaveSlotManager::SaveCurrentGamesessionByAuto()
{
	// 자동 저장 슬롯의 이름 설정
	FString SlotName = FString::Printf(TEXT("%s_%d"), *AutoSaveSlotPrefix, NextAutoSaveIndex + 1);

	// GameSession 데이터 저장
	TSharedPtr<FMD_SaveSlotInfo> SaveSlotInfo = SaveCurrentGameSessionInternal(SlotName, true);

	// 데이터 저장 성공 여부 검사
	if (SaveSlotInfo.IsValid() == false)
	{
		// 로그 출력
		UE_LOG(MarineDemolition, Warning, TEXT("%s UMD_SaveSlotInfoList 데이터 저장에 실패하였습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// 자동 저장 인덱스가 유효한 경우 해당 인덱스 위치의 세이브 슬롯 정보 업데이트
	if (AutoSlotInfoList.IsValidIndex(NextAutoSaveIndex) == true)
	{
		AutoSlotInfoList[NextAutoSaveIndex] = *SaveSlotInfo;
	}
	else
	{
		// 목록의 뒤쪽에 세이브 슬롯 정보 추가
		AutoSlotInfoList.Add(*SaveSlotInfo);
	}

	// 자동저장 인덱스 증가
	NextAutoSaveIndex = (NextAutoSaveIndex + 1) % MaxAutoSaveSlotCount;

	// 다음 실행에서도 자동 저장 슬롯을 찾을 수 있도록 변경된 슬롯 정보 목록 저장
	if (SaveSlotInfoList() == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 자동 저장 데이터는 저장되었지만 Save Slot 정보 목록 저장에 실패하였습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	return true;
}

bool UMD_SaveSlotManager::SaveCurrentGameSessionWithName(const FString& SlotName, bool bIsOverWrite)
{
	// GameSession 데이터 저장
	TSharedPtr<FMD_SaveSlotInfo> SaveSlotInfo = SaveCurrentGameSessionInternal(SlotName, bIsOverWrite);

	// 데이터 저장 성공 여부 검사
	if (SaveSlotInfo.IsValid() == false)
	{
		// 로그 출력
		UE_LOG(MarineDemolition, Warning, TEXT("%s Game Session 데이터 저장에 실패하였습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// 덮어쓰기의 경우 기존 중복되는 세이브 슬롯 정보 삭제
	if (bIsOverWrite == true)
	{
		SlotInfoList.Remove(*SaveSlotInfo);
	}

	// Game Session의 데이터가 저장된 슬롯의 정보를 목록에 추가
	SlotInfoList.Insert(*SaveSlotInfo, 0);

	// 저장된 슬롯의 개수가 기준치를 초과하는 경우
	if (SlotInfoList.Num() > MaxSaveSlotCount)
	{
		// 가장 오래된 Save Slot 제거
		FMD_SaveSlotInfo OldestSlotInfo = SlotInfoList.Pop();
		UGameplayStatics::DeleteGameInSlot(OldestSlotInfo.SlotName, 0);
	}

	// 변경된 슬롯 정보 목록 저장
	if (SaveSlotInfoList() == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s Game Session은 저장되었지만 Save Slot 정보 목록 저장에 실패하였습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	return true;
}

bool UMD_SaveSlotManager::SaveCurrentGamesessionByAuto()
{
	// 자동 저장 슬롯의 이름 설정
	FString SlotName = FString::Printf(TEXT("%s_%d"), *AutoSaveSlotPrefix, NextAutoSaveIndex + 1);

	// GameSession 데이터 저장
	TSharedPtr<FMD_SaveSlotInfo> SaveSlotInfo = SaveCurrentGameSessionInternal(SlotName, true);

	// 데이터 저장 성공 여부 검사
	if (SaveSlotInfo.IsValid() == false)
	{
		// 로그 출력
		UE_LOG(MarineDemolition, Warning, TEXT("%s 자동 저장에 실패하였습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// 자동 저장 인덱스가 유효한 경우 해당 인덱스 위치의 세이브 슬롯 정보 업데이트
	if (AutoSlotInfoList.IsValidIndex(NextAutoSaveIndex) == true)
	{
		AutoSlotInfoList[NextAutoSaveIndex] = *SaveSlotInfo;
	}
	else
	{
		// 목록의 뒤쪽에 세이브 슬롯 정보 추가
		AutoSlotInfoList.Add(*SaveSlotInfo);
	}

	// 자동저장 인덱스 증가
	NextAutoSaveIndex = (NextAutoSaveIndex + 1) % MaxAutoSaveSlotCount;

	// 다음 실행에서도 자동 저장 슬롯을 찾을 수 있도록 변경된 슬롯 정보 목록 저장
	if (SaveSlotInfoList() == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 자동 저장 데이터는 저장되었지만 Save Slot 정보 목록 저장에 실패하였습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	return true;
}

bool UMD_SaveSlotManager::PrepareGameSessionForHosting(const FString& SlotName)
{
	// 이전 호스팅 시도의 데이터가 새 Session에 섞이지 않도록 대기 상태 초기화
	PendingGameSessionData = nullptr;
	PendingSaveSlotInfo.Reset();

	// 세이브 슬롯에서 저장된 데이터 불러오기
	UMD_SaveGameSession* const LoadedGameSession = LoadGameSessionDataInternal(SlotName);

	if (IsValid(LoadedGameSession) == false)
	{
		return false;
	}

	// GameInstanceSubsystem이 객체를 참조해 비동기 세션 생성과 맵 이동 사이의 수명을 보장
	PendingGameSessionData = LoadedGameSession;

	// 새로 로드한 Session을 이전 활성 Session과 분리해 다른 플레이어 데이터가 섞이지 않게 처리
	ActiveGameSessionData = nullptr;

	// 선택한 슬롯 정보를 찾아 호스팅 대상 슬롯의 메타데이터도 함께 보관
	const FMD_SaveSlotInfo* const ManualSlotInfo = SlotInfoList.FindByPredicate([&SlotName](const FMD_SaveSlotInfo& SlotInfo) { return SlotInfo.SlotName == SlotName; });
	const FMD_SaveSlotInfo* const AutoSlotInfo = AutoSlotInfoList.FindByPredicate([&SlotName](const FMD_SaveSlotInfo& SlotInfo) { return SlotInfo.SlotName == SlotName; });
	const FMD_SaveSlotInfo* const SelectedSlotInfo = ManualSlotInfo != nullptr ? ManualSlotInfo : AutoSlotInfo;
	if (SelectedSlotInfo != nullptr)
	{
		PendingSaveSlotInfo = MakeShared<FMD_SaveSlotInfo>(*SelectedSlotInfo);
	}

	return true;
}

bool UMD_SaveSlotManager::ActivatePendingGameSession()
{
	// 저장 데이터를 선택하지 않은 신규 Session은 기존 기본 상태를 유지
	if (IsValid(PendingGameSessionData) == false)
	{
		return false;
	}

	// 맵 이동을 마친 데이터를 현재 Session의 재접속 및 자동 저장 기준 데이터로 전환
	ActiveGameSessionData = PendingGameSessionData;
	PendingGameSessionData = nullptr;
	return true;
}

bool UMD_SaveSlotManager::SavePlayerEquipment(AController* PlayerController)
{
	// 클라이언트가 로컬 Equipment 상태를 서버 Session 데이터에 기록하지 못하도록 권위 검사
	if (IsValid(PlayerController) == false || PlayerController->HasAuthority() == false)
	{
		return false;
	}

	// 재접속 이후에도 같은 Player를 찾을 수 있도록 Online ID 기반 키 생성
	FString PlayerSaveKey;
	if (BuildPlayerSaveKey(PlayerController, PlayerSaveKey) == false)
	{
		return false;
	}

	// Equipment Component를 소유한 현재 Player Character 확인
	AMD_Character* const PlayerCharacter = Cast<AMD_Character>(PlayerController->GetPawn());
	if (IsValid(PlayerCharacter) == false)
	{
		return false;
	}

	// Character에서 서버 권위 Equipment Hotbar Component 확인
	UMD_EquipHotbarComponent* const EquipmentComponent = PlayerCharacter->GetHotbarComponent();
	if (IsValid(EquipmentComponent) == false)
	{
		return false;
	}

	// 신규 Session도 재접속 데이터를 보관할 수 있도록 현재 Session 저장 객체 확보
	UMD_SaveGameSession* const SessionData = GetMutableEquipmentSessionData();
	if (IsValid(SessionData) == false)
	{
		return false;
	}

	// 동일 Player의 이전 기록을 최신 서버 권위 Equipment 상태로 교체
	SessionData->InventoryData.FindOrAdd(PlayerSaveKey) = EquipmentComponent->BuildEquipmentSaveData();
	return true;
}

bool UMD_SaveSlotManager::LoadPlayerEquipment(AController* PlayerController)
{
	// Equipment 복원은 서버 권위 Pawn에만 적용해 클라이언트 로컬 세이브 개입 차단
	if (IsValid(PlayerController) == false || PlayerController->HasAuthority() == false)
	{
		return false;
	}

	// 현재 활성 또는 호스팅 대기 중인 GameSession 데이터 확인
	const UMD_SaveGameSession* const SessionData = GetEquipmentSessionData();
	if (IsValid(SessionData) == false)
	{
		return false;
	}

	// 접속한 Player의 Online ID를 저장 데이터 조회 키로 변환
	FString PlayerSaveKey;
	if (BuildPlayerSaveKey(PlayerController, PlayerSaveKey) == false)
	{
		return false;
	}

	// 처음 접속한 Player는 저장 기록이 없으므로 Character의 기본 Equipment 유지
	const FMD_EquipHotbarDataWrapper* const SavedEquipmentData = SessionData->InventoryData.Find(PlayerSaveKey);
	if (SavedEquipmentData == nullptr)
	{
		return false;
	}

	// 부모 RestartPlayer가 생성하고 Possess한 Player Character 확인
	AMD_Character* const PlayerCharacter = Cast<AMD_Character>(PlayerController->GetPawn());
	if (IsValid(PlayerCharacter) == false)
	{
		return false;
	}

	// 저장된 Equipment를 적용할 Hotbar Component 확인
	UMD_EquipHotbarComponent* const EquipmentComponent = PlayerCharacter->GetHotbarComponent();
	if (IsValid(EquipmentComponent) == false)
	{
		return false;
	}

	// Component가 제공하는 서버 권위 역직렬화 함수로 Equipment 복원
	return EquipmentComponent->RestoreEquipmentSaveData(*SavedEquipmentData);
}

bool UMD_SaveSlotManager::LoadSlotInfoList()
{
	// Save Slot 정보 목록이 저장된 Slot 이름
	const FString SlotName = TEXT("SlotInfoList");

	// Save Slot 정보가 저장된 데이터 불러오기
	USaveGame* SaveData = UGameplayStatics::LoadGameFromSlot(SlotName, 0);
	UMD_SaveSlotInfoList* SlotInfoListData = Cast<UMD_SaveSlotInfoList>(SaveData);

	// 불러온 데이터 유효성 검사
	if (IsValid(SlotInfoListData) == false)
	{
		// 로그 출력
		UE_LOG(MarineDemolition, Warning, TEXT("%s (%s)이름으로 저장된 Save 데이터가 존재하지 않습니다."), *UE_CALL_LOCATIONINFO, *SlotName);
		return false;
	}
	
	// 세이브 슬롯 정보 목록 저장
	SlotInfoList = SlotInfoListData->SlotInformations;
	AutoSlotInfoList = SlotInfoListData->AutoSlotInformations;

	return true;
}

TSharedPtr<FMD_SaveSlotInfo> UMD_SaveSlotManager::SaveCurrentGameSessionInternal(const FString& SlotName, bool bIsOverWrite)
{
	// 동일한 이름의 세이브 슬롯 존재 검사
	bool bIsExist = UGameplayStatics::DoesSaveGameExist(SlotName, 0);

	// 덮어쓰기 설정 검사
	if (bIsOverWrite == false && bIsExist == true)
	{
		// 로그 출력
		UE_LOG(MarineDemolition, Warning, TEXT("%s 동일한 이름(%s)으로 저장된 세이브 데이터가 존재합니다."), *UE_CALL_LOCATIONINFO, *SlotName);
		return nullptr;
	}

	// 현재 Session의 GameState 얻기 및 유효성 확인
	UWorld* CurrentWorld = GetWorld();
	AMD_GameStateBase* SessionGameState = IsValid(CurrentWorld) == true ? CurrentWorld->GetGameState<AMD_GameStateBase>() : nullptr;
	if (IsValid(SessionGameState) == false)
	{
		// 로그 출력
		UE_LOG(MarineDemolition, Error, TEXT("%s 현재 World의 GameState[AMD_GameStateBase]가 존재하지 않습니다."), *UE_CALL_LOCATIONINFO);
		return nullptr;
	}

	// 클라이언트 환경 실행 제한
	if (SessionGameState->HasAuthority() == false)
	{
		// 로그 출력
		UE_LOG(MarineDemolition, Warning, TEXT("%s Server에서만 Session 데이터를 저장할 수 있습니다."), *UE_CALL_LOCATIONINFO);
		return nullptr;
	}

	// 접속 중인 모든 Player의 최신 Equipment를 세션 저장 객체에 포함하도록 먼저 캡처
	CaptureAllConnectedPlayerEquipment();

	// Game Session의 데이터를 저장하는 객체 생성
	UMD_SaveGameSession* const GameSessionData = NewObject<UMD_SaveGameSession>(this);
	if (IsValid(GameSessionData) == false)
	{
		UE_LOG(MarineDemolition, Error, TEXT("%s UMD_SaveGameSession 객체 생성에 실패하였습니다."), *UE_CALL_LOCATIONINFO);
		return nullptr;
	}

	// 저장되는 데이터 설정
	GameSessionData->Score = SessionGameState->GetScore();
	GameSessionData->Credit = 0;
	GameSessionData->MerchantLikeability = 0;
	GameSessionData->ElapsedDays = SessionGameState->GetCurrentDay();

	// 접속 종료된 Player를 포함한 현재 Session의 Equipment 캐시를 저장 객체에 복사
	const UMD_SaveGameSession* const EquipmentSessionData = GetEquipmentSessionData();
	if (IsValid(EquipmentSessionData) == true)
	{
		GameSessionData->InventoryData = EquipmentSessionData->InventoryData;
	}

	// 데이터 저장
	bool bSaveSuccess = UGameplayStatics::SaveGameToSlot(GameSessionData, SlotName, 0);

	// 데이터 저장 성공 여부 검사
	if (bSaveSuccess == false)
	{
		// 로그 출력
		UE_LOG(MarineDemolition, Warning, TEXT("%s Session 데이터 저장에 실패하였습니다."), *UE_CALL_LOCATIONINFO);
		return nullptr;
	}

	// 저장에 성공한 객체를 현재 Session 데이터로 유지해 이후 재접속과 자동 저장에 사용
	ActiveGameSessionData = GameSessionData;

	// Game Session 데이터를 저장한 세이브 슬롯의 정보를 담은 MD_SaveSlotInfo 객체 생성
	TSharedPtr<FMD_SaveSlotInfo> SaveSlotInfo = MakeShared<FMD_SaveSlotInfo>();
	SaveSlotInfo->SlotName = SlotName;
	SaveSlotInfo->QuestID = FPrimaryAssetId();

	return SaveSlotInfo;
}

UMD_SaveGameSession* UMD_SaveSlotManager::LoadGameSessionDataInternal(const FString& SlotName)
{
	bool bIsSaveSlotExist = UGameplayStatics::DoesSaveGameExist(SlotName, 0);
	if (bIsSaveSlotExist == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s (%s)이름으로 저장된 Save 데이터가 존재하지 않습니다."), *UE_CALL_LOCATIONINFO, *SlotName);
		return nullptr;
	}

	// SlotName 이름의 Slot으로 부터 저장된 데이터 불러오기
	USaveGame* SaveData = UGameplayStatics::LoadGameFromSlot(SlotName, 0);

	// 불러온 데이터를 MD_SaveGameSession으로 변환 및 유효성 검사
	UMD_SaveGameSession* GameSessionData = Cast<UMD_SaveGameSession>(SaveData);
	if (IsValid(GameSessionData) == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 불러온 데이터를 MD_SaveGameSession 타입으로 변환할 수 없습니다."), *UE_CALL_LOCATIONINFO);
		return nullptr;
	}

	return GameSessionData;
}

bool UMD_SaveSlotManager::BuildPlayerSaveKey(const AController* PlayerController, FString& OutPlayerSaveKey) const
{
	// 실패한 조회가 이전 호출의 키를 재사용하지 않도록 출력 문자열 초기화
	OutPlayerSaveKey.Reset();

	// Player ID를 소유한 PlayerState가 존재하는지 확인
	if (IsValid(PlayerController) == false)
	{
		return false;
	}

	const APlayerState* PlayerState = PlayerController->GetPlayerState<APlayerState>();
	if (IsValid(PlayerState) == false)
	{
		return false;
	}

	// Online Subsystem에서 발급한 ID가 없으면 재접속 Player를 안정적으로 식별할 수 없으므로 중단
	const FUniqueNetIdRepl& UniqueId = PlayerState->GetUniqueId();
	if (UniqueId.IsValid() == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s Player의 UniqueNetId가 유효하지 않아 Equipment 데이터를 처리할 수 없습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// 세이브 파일에서 직렬화하기 쉬운 문자열 형식으로 Online ID 변환
	OutPlayerSaveKey = UniqueId.ToString();
	return OutPlayerSaveKey.IsEmpty() == false;
}

UMD_SaveGameSession* UMD_SaveSlotManager::GetMutableEquipmentSessionData()
{
	// 호스팅이 완료된 Session 데이터가 있으면 모든 변경을 해당 객체에 기록
	if (IsValid(ActiveGameSessionData) == true)
	{
		return ActiveGameSessionData;
	}

	// 맵 이동 중에는 아직 활성화되지 않은 로드 객체에 Equipment 변경 기록
	if (IsValid(PendingGameSessionData) == true)
	{
		return PendingGameSessionData;
	}

	// 신규 Session에서도 재접속 Equipment를 보관할 수 있도록 런타임 저장 객체 생성
	ActiveGameSessionData = NewObject<UMD_SaveGameSession>(this);
	return ActiveGameSessionData;
}

const UMD_SaveGameSession* UMD_SaveSlotManager::GetEquipmentSessionData() const
{
	// 현재 Session 데이터가 있으면 재접속과 저장의 우선 기준으로 사용
	if (IsValid(ActiveGameSessionData) == true)
	{
		return ActiveGameSessionData;
	}

	// 첫 맵 진입 중에는 아직 활성화되지 않은 대기 데이터를 복원 기준으로 사용
	return IsValid(PendingGameSessionData) == true ? PendingGameSessionData.Get() : nullptr;
}

void UMD_SaveSlotManager::CaptureAllConnectedPlayerEquipment()
{
	// 현재 서버 월드가 없으면 접속자 목록을 조회할 수 없으므로 중단
	UWorld* const CurrentWorld = GetWorld();
	if (IsValid(CurrentWorld) == false || CurrentWorld->GetNetMode() == NM_Client)
	{
		return;
	}

	// 저장 시점에 접속 중인 모든 PlayerController의 서버 권위 Equipment 상태 수집
	for (FConstPlayerControllerIterator PlayerControllerIterator = CurrentWorld->GetPlayerControllerIterator(); PlayerControllerIterator; ++PlayerControllerIterator)
	{
		APlayerController* const PlayerController = PlayerControllerIterator->Get();
		if (IsValid(PlayerController) == true)
		{
			SavePlayerEquipment(PlayerController);
		}
	}
}
