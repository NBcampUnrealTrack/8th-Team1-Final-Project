// Fill out your copyright notice in the Description page of Project Settings.

#include "Data/SaveGame/MD_SaveSlotManager.h"
#include "Data/SaveGame/MD_SaveSlotStore.h"
#include "Async/Async.h"
#include "GameFramework/PlayerState.h"

#include "Game/MD_GameStateBase.h"
#include "Character/MD_Character.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Gameplay/Lore/MD_LoreSubsystem.h"
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "Gameplay/Persistence/MD_DayPersistenceSubsystem.h"
#include "Util/MD_LogUtility.h"

const FString UMD_SaveSlotManager::GameSaveFileName = TEXT("MarineDemolitionGame");

namespace
{
	// 온라인 계정 식별자를 우선 사용하고 오프라인 환경에서는 플레이어 이름으로 대체하는 키 생성 함수
	FString BuildPlayerPersistenceKey(const APlayerState* PlayerState)
	{
		if (IsValid(PlayerState) == false)
		{
			return FString();
		}

		const FUniqueNetIdRepl UniqueNetID = PlayerState->GetUniqueId();
		const FString NetworkKey = UniqueNetID.IsValid() == true ? UniqueNetID.ToString() : FString();
		return NetworkKey.IsEmpty() == false ? NetworkKey : PlayerState->GetPlayerName();
	}


}

void UMD_SaveSlotManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 게임을 재실행할 때 첫 자동 저장 슬롯을 AutoSave_1로 고정하기 위한 인덱스 초기화
	AutoSaveIndex = 0;
}

void UMD_SaveSlotManager::Deinitialize()
{
	// GameInstance 종료 이후 보류 데이터가 다음 World에 적용되지 않도록 전환 상태 초기화
	ResetTemporaryGameData();
    SelectedLoadGameData.Reset();

	Super::Deinitialize();
}

bool UMD_SaveSlotManager::SaveGameData(const FString& DataName, bool bIsAuto)
{
    if (!HasServerAuthority() || bAsyncOperationInProgress
        || (bIsAuto ? MaxAutoSaveSlotCount : MaxUserSaveSlotCount) <= 0
        || (!bIsAuto && (DataName.IsEmpty() || DataName.Contains(TEXT("AutoSave"))))
        || !CollectGameData()) return false;
    TemporaryGameData->UniqueName = bIsAuto
        ? FString::Printf(TEXT("AutoSave_%d"), AutoSaveIndex + 1) : DataName;
    const bool bSuccess = FMD_SaveSlotStore::ForProject().Save(
        *TemporaryGameData, bIsAuto, MaxAutoSaveSlotCount, MaxUserSaveSlotCount);
    // Current-session player/economy continuity does not use an old world image.
    TemporaryGameData->DataFields.Remove(FMD_WorldData::DataFieldKey);
    if (bSuccess && bIsAuto) AutoSaveIndex = (AutoSaveIndex + 1) % MaxAutoSaveSlotCount;
    return bSuccess;
}

bool UMD_SaveSlotManager::RemoveGameData(const FMD_GameData& GameData)
{
    return HasServerAuthority() && !bAsyncOperationInProgress
        && FMD_SaveSlotStore::ForProject().Remove(GameData.UniqueName);
}

TArray<FMD_GameData> UMD_SaveSlotManager::LoadAllGameDataList()
{
    FMD_SaveSlotIndex Index;
    TArray<FMD_GameData> Rows;
    if (!HasServerAuthority() || bAsyncOperationInProgress
        || !FMD_SaveSlotStore::ForProject().ReadIndex(Index)) return Rows;
    Rows = MoveTemp(Index.Auto);
    Rows.Append(MoveTemp(Index.User));
    return Rows;
}

void UMD_SaveSlotManager::AsyncSaveGameData(const FString& DataName, bool bIsAuto,
    const FOnSaveMDGameDataComplete& Completion)
{
    if (!HasServerAuthority() || bAsyncOperationInProgress
        || (bIsAuto ? MaxAutoSaveSlotCount : MaxUserSaveSlotCount) <= 0
        || (!bIsAuto && (DataName.IsEmpty() || DataName.Contains(TEXT("AutoSave"))))
        || !CollectGameData())
    {
        Completion.ExecuteIfBound(false);
        return;
    }
    TemporaryGameData->UniqueName = bIsAuto
        ? FString::Printf(TEXT("AutoSave_%d"), AutoSaveIndex + 1) : DataName;
    bAsyncOperationInProgress = true;
    TWeakObjectPtr<UMD_SaveSlotManager> WeakThis(this);
    FMD_SaveSlotStore::ForProject().SaveAsync(*TemporaryGameData, bIsAuto,
        MaxAutoSaveSlotCount, MaxUserSaveSlotCount,
        [WeakThis, Completion, bIsAuto](bool bSuccess)
        {
            UMD_SaveSlotManager* Self = WeakThis.Get();
            if (!Self) return;
            const bool bAccepted = bSuccess && Self->HasServerAuthority();
            if (bAccepted && bIsAuto && Self->MaxAutoSaveSlotCount > 0)
                Self->AutoSaveIndex = (Self->AutoSaveIndex + 1) % Self->MaxAutoSaveSlotCount;
            Self->bAsyncOperationInProgress = false;
            Completion.ExecuteIfBound(bAccepted);
        });
    // Prepared write owns immutable blobs until worker completion, so no live-world snapshot is pinned here.
    TemporaryGameData->DataFields.Remove(FMD_WorldData::DataFieldKey);
}

void UMD_SaveSlotManager::AsyncRemoveGameData(const FMD_GameData& GameData,
    const FOnRemoveMDGameDataComplete& Completion)
{
    if (!HasServerAuthority() || bAsyncOperationInProgress)
    {
        Completion.ExecuteIfBound(false);
        return;
    }
    bAsyncOperationInProgress = true;
    TWeakObjectPtr<UMD_SaveSlotManager> WeakThis(this);
    const FString Name = GameData.UniqueName;
    AsyncTask(ENamedThreads::GameThread, [WeakThis, Name, Completion]()
    {
        UMD_SaveSlotManager* Self = WeakThis.Get();
        if (!Self) return;
        const bool bSuccess = Self->HasServerAuthority() && FMD_SaveSlotStore::ForProject().Remove(Name);
        Self->bAsyncOperationInProgress = false;
        Completion.ExecuteIfBound(bSuccess);
    });
}

void UMD_SaveSlotManager::AsyncLoadAllGameDataList(const FOnLoadMDGameDataListComplete& Completion)
{
    if (!HasServerAuthority() || bAsyncOperationInProgress)
    {
        Completion.ExecuteIfBound({}, false);
        return;
    }
    bAsyncOperationInProgress = true;
    TWeakObjectPtr<UMD_SaveSlotManager> WeakThis(this);
    AsyncTask(ENamedThreads::GameThread, [WeakThis, Completion]()
    {
        UMD_SaveSlotManager* Self = WeakThis.Get();
        if (!Self) return;
        FMD_SaveSlotIndex Index;
        const bool bSuccess = Self->HasServerAuthority() && FMD_SaveSlotStore::ForProject().ReadIndex(Index);
        TArray<FMD_GameData> Rows = MoveTemp(Index.Auto);
        Rows.Append(MoveTemp(Index.User));
        Self->bAsyncOperationInProgress = false;
        Completion.ExecuteIfBound(Rows, bSuccess);
    });
}

bool UMD_SaveSlotManager::LoadGameDataToWorld(const FMD_GameData& GameData)
{
    if (!HasServerAuthority() || bAsyncOperationInProgress) return false;
    if (!GameData.bIsSlotSummary) return ApplyGameDataToWorldInternal(GameData, true);
    FMD_GameData Loaded;
    return FMD_SaveSlotStore::ForProject().ReadSlot(GameData, Loaded)
        && ApplyGameDataToWorldInternal(Loaded, true);
}

bool UMD_SaveSlotManager::ApplyGameDataToWorldInternal(const FMD_GameData& GameData, bool bRequireSavedSlotIdentity)
{
	// 클라이언트가 저장 데이터를 자신의 World에 적용하지 못하도록 하는 권한 제한
	if (HasServerAuthority() == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 게임 데이터 적용은 Host/Server에서만 실행할 수 있습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// 구버전 슬롯을 현재 필드 스키마로 승격하고 모든 필수 데이터가 유효할 때만 적용하기 위한 복사
	FMD_GameData MigratedGameData = GameData;
	if (bRequireSavedSlotIdentity == false && MigratedGameData.UniqueName.IsEmpty() == true)
	{
		// 저장되지 않은 현재 회차도 저장 슬롯과 동일한 구조 검증을 재사용하기 위한 임시 식별자
		MigratedGameData.UniqueName = TEXT("TemporaryGameData");
	}
	if (MigratedGameData.MigrateToCurrentVersion() == false || MigratedGameData.IsValidForLoad() == false)
	{
		UE_LOG(MarineDemolition, Error, TEXT("%s 손상되었거나 지원하지 않는 게임 데이터의 적용을 거부합니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// 현재 맵이 게임 데이터가 저장된 맵과 일치하는지 확인
	UWorld* World = GetWorld();
	UPackage* WorldPackage = IsValid(World) == true ? World->GetPackage() : nullptr;
	FString WorldPackagePath = IsValid(WorldPackage) == true ? UWorld::RemovePIEPrefix(WorldPackage->GetName()) : TEXT("");
	if (WorldPackagePath.IsEmpty() == true || WorldPackagePath != MigratedGameData.MapPackagePath)
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
	UMD_LoreSubsystem* LoreManager = GameInstance->GetSubsystem<UMD_LoreSubsystem>();
	UMD_MerchantSubsystem* MerchantManager = GameInstance->GetSubsystem<UMD_MerchantSubsystem>();
	if (IsValid(GameState) == false || GameState->HasAuthority() == false || IsValid(LoreManager) == false || IsValid(MerchantManager) == false)
	{
		return false;
	}

	// 필수 데이터 필드의 누락이나 타입 불일치로 인한 불완전한 복원을 방지하기 위한 검사
	FMD_GameStateData GameStateData;
	if (MigratedGameData.GetDataField(FMD_GameStateData::DataFieldKey, GameStateData) == false)
	{
		return false;
	}
	FMD_MerchantData MerchantData;
	if (MigratedGameData.GetDataField(FMD_MerchantData::DataFieldKey, MerchantData) == false)
	{
		return false;
	}

	// 로어 필드가 없는 초기 슬롯은 빈 진행으로 복원하고 타입이 손상된 최신 슬롯만 거부하는 조회
	FMD_LoreData LoreData;
	const bool bContainsLoreData = MigratedGameData.DataFields.Contains(FMD_LoreData::DataFieldKey);
	const bool bHasLoreData = MigratedGameData.GetDataField(FMD_LoreData::DataFieldKey, LoreData);
	if (bContainsLoreData == true && bHasLoreData == false)
	{
		return false;
	}

	// 월드 데이터가 존재하는 최신 세이브와 필드가 없는 이전 세이브를 구분하는 조회
	UMD_DayPersistenceSubsystem* DayPersistenceManager = GameInstance->GetSubsystem<UMD_DayPersistenceSubsystem>();
	FMD_WorldData WorldData;
	bool bContainsWorldData = MigratedGameData.DataFields.Contains(FMD_WorldData::DataFieldKey);
	bool bHasWorldData = MigratedGameData.GetDataField(FMD_WorldData::DataFieldKey, WorldData);
	if (bContainsWorldData == true && bHasWorldData == false)
	{
		return false;
	}
	if (bHasWorldData == true && IsValid(DayPersistenceManager) == false)
	{
		return false;
	}

	// 상인 원장을 변경하기 전에 GameState 설정 범위까지 검증하여 부분 적용을 방지하는 처리
	if (GameState->CanApplyGameStateSaveData(GameStateData) == false)
	{
		return false;
	}
	if (LoreManager->CanApplyLoreSaveData(LoreData) == false)
	{
		return false;
	}

	// 상인 상태를 먼저 복원하여 GameState 일차 알림이 퀘스트를 다시 초기화하지 않도록 하는 처리
	if (MerchantManager->ApplyMerchantSaveData(MerchantData) == false)
	{
		return false;
	}

	// 기존 슬롯의 빈 상태를 포함한 로어 원장을 상인 상태와 같은 복원 시점에 적용하는 처리
	if (LoreManager->ApplyLoreSaveData(LoreData) == false)
	{
		return false;
	}

	// GameState를 저장 스냅샷으로 교체하고 복제 알림을 발생시키는 처리
	if (GameState->ApplyGameStateSaveData(GameStateData) == false)
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
		OverwriteDataToPlayerInternal(PlayerState, MigratedGameData);
	}

	// 이후 접속하거나 Pawn이 교체된 플레이어도 같은 개인 데이터를 사용하도록 현재 회차 데이터 유지
	TemporaryGameData = MakeShared<FMD_GameData>(MoveTemp(MigratedGameData));
    TemporaryGameData->DataFields.Remove(FMD_WorldData::DataFieldKey);

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

	// 로그아웃 수집과 재접속 복원이 같은 개인 데이터 항목을 사용하도록 공통 키를 생성하는 처리
	const FString PlayerKey = BuildPlayerPersistenceKey(PlayerState);

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

bool UMD_SaveSlotManager::OnTryStartGameWithGameData(const FMD_GameData& GameData, bool bLoadGame, FString& OutMapPackagePath)
{
	OutMapPackagePath.Reset();

	// 클라이언트가 로드할 데이터를 독립적으로 예약하지 못하도록 하는 권한 제한
	if (HasServerAuthority() == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 게임 데이터 기반 시작 요청은 Host/Server에서만 실행할 수 있습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	// 불러오기 버튼에서 전달한 저장 데이터만 선택 데이터로 보관하는 처리
	if (bLoadGame == true)
	{
		SelectedLoadGameData.Reset();

        FMD_GameData MigratedGameData;
        if (GameData.bIsSlotSummary)
        {
            if (!FMD_SaveSlotStore::ForProject().ReadSlot(GameData, MigratedGameData))
            {
                PendingGameDataOperation = EMD_PendingGameDataOperation::None;
                return false;
            }
        }
        else MigratedGameData = GameData;
		if (MigratedGameData.MigrateToCurrentVersion() == false || MigratedGameData.IsValidForLoad() == false)
		{
			UE_LOG(MarineDemolition, Error, TEXT("%s 시작할 저장 데이터가 손상되었거나 지원하지 않는 버전입니다."), *UE_CALL_LOCATIONINFO);
			PendingGameDataOperation = EMD_PendingGameDataOperation::None;
			return false;
		}

		SelectedLoadGameData = MakeShared<FMD_GameData>(MoveTemp(MigratedGameData));
		PendingGameDataOperation = EMD_PendingGameDataOperation::LoadGame;
		OutMapPackagePath = SelectedLoadGameData->MapPackagePath;
		return true;
	}

	// MultiplayManager가 Session 시작과 맵 이동에 사용할 요청 경로를 직접 반환하는 처리
	OutMapPackagePath = GameData.MapPackagePath;

	// 저장 전 현재 회차 데이터가 남아 있으면 새 게임을 만들지 않고 이어하기로 예약하는 처리
	if (TemporaryGameData.IsValid() == true)
	{
		PendingGameDataOperation = EMD_PendingGameDataOperation::ContinueGame;
		return true;
	}

	// 현재 회차가 없는 일반 시작 요청만 기존 데이터 변경 없이 새 게임으로 예약하는 처리
	PendingGameDataOperation = EMD_PendingGameDataOperation::NewGame;
	return true;
}

bool UMD_SaveSlotManager::HasTemporaryGameData() const
{
	bool bHasTemporaryGameData = TemporaryGameData.IsValid() == true;
	return bHasTemporaryGameData;
}

void UMD_SaveSlotManager::CancelPendingGameDataOperation()
{
	// 실패한 불러오기 선택 데이터가 다음 시작 요청에 적용되지 않도록 하는 정리
	if (PendingGameDataOperation == EMD_PendingGameDataOperation::LoadGame)
	{
		SelectedLoadGameData.Reset();
	}

	PendingGameDataOperation = EMD_PendingGameDataOperation::None;
	return;
}

void UMD_SaveSlotManager::ResetTemporaryGameData()
{
	PendingGameDataOperation = EMD_PendingGameDataOperation::None;
	TemporaryGameData.Reset();
	return;
}


void UMD_SaveSlotManager::PrepareContaminationForWorld(UWorld* World)
{
    if (!World || World->GetNetMode()==NM_Client) return;
    auto* GI=World->GetGameInstance();
    auto* Persistence=GI?GI->GetSubsystem<UMD_DayPersistenceSubsystem>():nullptr;
    if (!Persistence) return;
    if (PendingGameDataOperation==EMD_PendingGameDataOperation::LoadGame
        && SelectedLoadGameData.IsValid()
        && UWorld::RemovePIEPrefix(World->GetPackage()->GetName())==SelectedLoadGameData->MapPackagePath)
    {
        FMD_WorldData WorldData;
        const bool bHasWorldData=SelectedLoadGameData->GetDataField(FMD_WorldData::DataFieldKey,WorldData);
        Persistence->PrepareContaminationContext(World,&WorldData,false,bHasWorldData);
        return;
    }
    const bool bNewContract=PendingGameDataOperation==EMD_PendingGameDataOperation::NewGame
        || PendingGameDataOperation==EMD_PendingGameDataOperation::ContinueGame;
    Persistence->PrepareContaminationContext(World,nullptr,bNewContract,true);
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
	if (PendingGameDataOperation == EMD_PendingGameDataOperation::None)
	{
		return false;
	}

	// 서버의 목적지 맵이 완전히 시작된 경우에만 게임 데이터 초기화를 수행하기 위한 World 검사
	UWorld* World = GetWorld();
	UPackage* WorldPackage = IsValid(World) == true ? World->GetPackage() : nullptr;
	FString CurrentMapPackagePath = IsValid(WorldPackage) == true ? UWorld::RemovePIEPrefix(WorldPackage->GetName()) : TEXT("");
	if (IsValid(World) == false)
	{
		return false;
	}

	// 불러오기는 선택한 저장 데이터가 수집된 맵에 도착한 경우에만 적용하기 위한 검사
	if (PendingGameDataOperation == EMD_PendingGameDataOperation::LoadGame && (SelectedLoadGameData.IsValid() == false || CurrentMapPackagePath != SelectedLoadGameData->MapPackagePath))
	{
		CancelPendingGameDataOperation();
		return false;
	}

	// 이어하기는 이전 맵과 관계없이 현재 회차 데이터가 존재할 때만 적용하기 위한 검사
	if (PendingGameDataOperation == EMD_PendingGameDataOperation::ContinueGame && TemporaryGameData.IsValid() == false)
	{
		CancelPendingGameDataOperation();
		return false;
	}

	// 이전 게임의 일일 스냅샷이 새 세션 또는 불러온 데이터 위에 적용되지 않도록 하는 초기화
	UGameInstance* GameInstance = GetGameInstance();
	UMD_DayPersistenceSubsystem* DayPersistence = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_DayPersistenceSubsystem>() : nullptr;
	if (IsValid(DayPersistence) == true)
	{
		DayPersistence->ClearSnapshot();
	}

	if (PendingGameDataOperation == EMD_PendingGameDataOperation::NewGame)
	{
		// 저장 시점 전에는 World 데이터를 수집하지 않고 현재 회차 식별 정보만 생성하는 처리
		FMD_GameData NewGameData;
		NewGameData.SchemaVersion = FMD_GameData::CurrentSchemaVersion;
		NewGameData.MapPackagePath = CurrentMapPackagePath;
		TemporaryGameData = MakeShared<FMD_GameData>(MoveTemp(NewGameData));

		PendingGameDataOperation = EMD_PendingGameDataOperation::None;

		return true;
	}

	// 로비에서 보존한 현재 회차 데이터를 목적지 게임 맵에 적용하는 처리
	if (PendingGameDataOperation == EMD_PendingGameDataOperation::ContinueGame && TemporaryGameData.IsValid() == true)
	{
		// 이전 출항의 파괴와 오염 상태를 목적지 맵에 적용하지 않도록 현재 회차 데이터의 복사본을 구성하는 처리
		FMD_GameData ContinueGameData = *TemporaryGameData;
		ContinueGameData.DataFields.Remove(FMD_WorldData::DataFieldKey);
		ContinueGameData.MapPackagePath = CurrentMapPackagePath;

		bool bWasApplied = ApplyGameDataToWorldInternal(ContinueGameData, false);
		if (bWasApplied == true)
		{
			PendingGameDataOperation = EMD_PendingGameDataOperation::None;
			return true;
		}
	}

	// 불러오기 목적지의 서버 상태와 접속 플레이어에게 선택한 저장 데이터를 적용하는 처리
	if (PendingGameDataOperation == EMD_PendingGameDataOperation::LoadGame && SelectedLoadGameData.IsValid() == true)
	{
		bool bWasApplied = LoadGameDataToWorld(*SelectedLoadGameData);
		if (bWasApplied == true)
		{
            SelectedLoadGameData.Reset();
			PendingGameDataOperation = EMD_PendingGameDataOperation::None;
			return true;
		}
	}

	// 손상된 보류 상태가 다음 맵까지 유지되지 않도록 하는 실패 정리
	CancelPendingGameDataOperation();

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

	// 수집 결과가 항상 현재 실행 파일의 필드 스키마로 기록되도록 하는 버전 확정
	TemporaryGameData->SchemaVersion = FMD_GameData::CurrentSchemaVersion;

	// 게임 데이터를 수집하기 위하여 필요한 객체들 참조
	UGameInstance* GameInstance = GetGameInstance();
	UMD_LoreSubsystem* LoreManager = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_LoreSubsystem>() : nullptr;
	UMD_MerchantSubsystem* MerchantManager = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_MerchantSubsystem>() : nullptr;
	UMD_DayPersistenceSubsystem* DayPersistenceManager = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_DayPersistenceSubsystem>() : nullptr;
	if (IsValid(LoreManager) == false || IsValid(MerchantManager) == false || IsValid(DayPersistenceManager) == false)
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

    if (!GameState->IsWorldInitializationReady())
    {
        UE_LOG(MarineDemolition, Warning, TEXT("Save deferred: world initialization has not completed."));
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

	// 계약을 넘어 누적되는 현장별 발견 목록과 완료 보상 상태 저장
	FMD_LoreData LoreData = LoreManager->GetLoreSaveData();
	if (TemporaryGameData->AddDataField(FMD_LoreData::DataFieldKey, LoreData) == false)
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

	// 로그아웃 수집과 재접속 복원이 같은 개인 데이터 항목을 사용하도록 공통 키를 생성하는 처리
	const FString PlayerKey = BuildPlayerPersistenceKey(PlayerState);
	if (PlayerKey.IsEmpty() == true)
	{
		return false;
	}

	// 플레이어 데이터의 존재 여부 검사
	const FMD_PlayerData* PlayerData = GameData.PlayerFields.Find(PlayerKey);
	if (PlayerData == nullptr && PlayerState->GetPlayerName() != PlayerKey)
	{
		// 오프라인 이름 키로 저장된 초기 슬롯을 온라인 계정 키 환경에서도 재바인딩하기 위한 호환 조회
		PlayerData = GameData.PlayerFields.Find(PlayerState->GetPlayerName());
	}

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
	return EquipmentComponent->RestoreEquipmentSaveData(PlayerData->HotbarToolList);
}
