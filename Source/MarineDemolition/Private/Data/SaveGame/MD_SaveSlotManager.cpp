// Fill out your copyright notice in the Description page of Project Settings.

#include "Data/SaveGame/MD_SaveSlotManager.h"
#include "Data/SaveGame/MD_SaveSlotStore.h"
#include "GameFramework/PlayerState.h"

#include "Game/MD_GameModeBase.h"
#include "Game/MD_GameStateBase.h"
#include "Gameplay/Persistence/MD_DayPersistenceSubsystem.h"
#include "Util/MD_LogUtility.h"

void UMD_SaveSlotManager::AddReferencedObjects(UObject* Object, FReferenceCollector& Collector)
{
    // 맵 이동을 넘어 유지되는 저장 데이터를 보유한 관리자 조회
    auto* Self = CastChecked<UMD_SaveSlotManager>(Object);
    // 공유 저장 구조체 내부의 UObject 참조를 GC 추적 대상으로 등록
    for (auto& Data : {Self->SelectedLoadGameData, Self->TemporaryGameData})
    {
        // 실제 저장 데이터가 존재하는 경우에만 구조체 참조 수집
        if (Data.IsValid() == true)
        {
            // 확장 저장 필드에 포함된 객체 참조의 조기 수거 방지
            Collector.AddPropertyReferencesWithStructARO(FMD_GameData::StaticStruct(), Data.Get(), Self);
        }
    }
    Super::AddReferencedObjects(Object, Collector);
}

void UMD_SaveSlotManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 게임을 재실행할 때 첫 자동 저장 슬롯을 AutoSave_1로 고정하기 위한 인덱스 초기화
	AutoSaveIndex = 0;
}

void UMD_SaveSlotManager::Deinitialize()
{
    // 종료된 데이터 소유자를 다음 이벤트에서 참조하지 않도록 구독 정리
    OnCollectGameData.Clear();
    // 종료된 복원 구독자의 참조 해제
    OnOverwriteGameData.Clear();
	// GameInstance 종료 이후 보류 데이터가 다음 World에 적용되지 않도록 전환 상태 초기화
	ResetTemporaryGameData();
    SelectedLoadGameData.Reset();

	Super::Deinitialize();
}

bool UMD_SaveSlotManager::SaveGameData(const FString& DataName, bool bIsAuto)
{
    // 관리자의 권한과 진행 중인 작업을 함께 확인
    if (HasServerAuthority() == false || bAsyncOperationInProgress == true || bGameDataOperationInProgress == true)
    {
        // 실행할 수 없는 저장 요청 종료
        return false;
    }
    // 선택한 저장 종류의 슬롯 허용 개수 확인
    if ((bIsAuto ? MaxAutoSaveSlotCount : MaxUserSaveSlotCount) <= 0)
    {
        // 허용 슬롯이 없는 저장 요청 종료
        return false;
    }
    // 자동 저장은 현재 게임 모드의 맵 제외 정책을 수집 전에 확인
    if (bIsAuto == true)
    {
        // 서버 권한 검사로 확인한 월드의 게임 모드 조회
        AMD_GameModeBase* GameMode = GetWorld()->GetAuthGameMode<AMD_GameModeBase>();
        if (IsValid(GameMode) == true && GameMode->IsAutoSaveAllowed() == false)
        {
            // 제외 맵의 회차 데이터와 자동 저장 슬롯 보존
            return false;
        }
    }
    else
    {
        // 같은 이름 값의 누락과 예약 이름 사용을 함께 확인
        if (DataName.IsEmpty() == true || DataName.Contains(TEXT("AutoSave")) == true)
        {
            // 유효하지 않은 사용자 저장 이름 거부
            return false;
        }
    }
    // 입력 검증을 마친 요청의 실제 데이터 수집 결과 확인
    if (CollectGameData() == false)
    {
        // 수집에 실패한 저장 요청 종료
        return false;
    }
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
    // 관리자의 권한과 진행 중인 작업을 함께 확인
    if (HasServerAuthority() == false || bAsyncOperationInProgress == true || bGameDataOperationInProgress == true)
    {
        // 실행할 수 없는 삭제 요청 종료
        return false;
    }
    // 실행 상태 검증 후 실제 슬롯 삭제 결과 반환
    return FMD_SaveSlotStore::ForProject().Remove(GameData.UniqueName);
}

TArray<FMD_GameData> UMD_SaveSlotManager::LoadAllGameDataList()
{
    FMD_SaveSlotIndex Index;
    TArray<FMD_GameData> Rows;
    // 관리자의 권한과 진행 중인 작업을 함께 확인
    if (HasServerAuthority() == false || bAsyncOperationInProgress == true || bGameDataOperationInProgress == true)
    {
        // 조회를 실행할 수 없는 요청의 빈 목록 반환
        return Rows;
    }
    // 실행 상태 검증 후 저장 목록 읽기 결과 확인
    if (FMD_SaveSlotStore::ForProject().ReadIndex(Index) == false)
    {
        // 목록 읽기 실패 시 기존 빈 목록 반환
        return Rows;
    }
    Rows = MoveTemp(Index.Auto);
    Rows.Append(MoveTemp(Index.User));
    return Rows;
}

void UMD_SaveSlotManager::AsyncSaveGameData(const FString& DataName, bool bIsAuto,
    const FOnSaveMDGameDataComplete& Completion)
{
    // 관리자의 권한과 진행 중인 작업을 함께 확인
    if (HasServerAuthority() == false || bAsyncOperationInProgress == true || bGameDataOperationInProgress == true)
    {
        // 현재 요청의 실패 결과 전달
        Completion.ExecuteIfBound(false);
        // 실행할 수 없는 저장 요청 종료
        return;
    }
    // 선택한 저장 종류의 슬롯 허용 개수 확인
    if ((bIsAuto ? MaxAutoSaveSlotCount : MaxUserSaveSlotCount) <= 0)
    {
        // 현재 요청의 실패 결과 전달
        Completion.ExecuteIfBound(false);
        // 허용 슬롯이 없는 저장 요청 종료
        return;
    }
    // 비동기 자동 저장도 동일한 맵 제외 정책을 수집 전에 확인
    if (bIsAuto == true)
    {
        // 서버 권한 검사로 확인한 월드의 게임 모드 조회
        AMD_GameModeBase* GameMode = GetWorld()->GetAuthGameMode<AMD_GameModeBase>();
        if (IsValid(GameMode) == true && GameMode->IsAutoSaveAllowed() == false)
        {
            // 저장을 실행하지 않은 결과를 호출자에게 전달
            Completion.ExecuteIfBound(false);
            // 데이터 수집과 파일 작업 및 슬롯 순환 생략
            return;
        }
    }
    else
    {
        // 같은 이름 값의 누락과 예약 이름 사용을 함께 확인
        if (DataName.IsEmpty() == true || DataName.Contains(TEXT("AutoSave")) == true)
        {
            // 현재 요청의 실패 결과 전달
            Completion.ExecuteIfBound(false);
            // 유효하지 않은 사용자 저장 이름 거부
            return;
        }
    }
    // 입력 검증을 마친 요청의 실제 데이터 수집 결과 확인
    if (CollectGameData() == false)
    {
        // 현재 요청의 실패 결과 전달
        Completion.ExecuteIfBound(false);
        // 수집에 실패한 저장 요청 종료
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
    // 서버 권한과 파일 작업 및 이벤트 재진입 상태 확인
    if (HasServerAuthority() == false || bAsyncOperationInProgress == true || bGameDataOperationInProgress == true)
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
    // 서버 권한과 파일 작업 및 이벤트 재진입 상태 확인
    if (HasServerAuthority() == false || bAsyncOperationInProgress == true || bGameDataOperationInProgress == true)
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
    // 서버 권한과 파일 작업 및 이벤트 재진입 상태 확인
    if (HasServerAuthority() == false || bAsyncOperationInProgress == true || bGameDataOperationInProgress == true) return false;
    if (!GameData.bIsSlotSummary) return ApplyGameDataToWorldInternal(GameData, true);
    FMD_GameData Loaded;
    // 선택한 슬롯의 데이터 읽기 결과 확인
    if (FMD_SaveSlotStore::ForProject().ReadSlot(GameData, Loaded) == false)
    {
        // 슬롯 읽기 실패 시 월드 적용 차단
        return false;
    }
    // 읽기를 마친 데이터의 월드 적용 결과 반환
    return ApplyGameDataToWorldInternal(Loaded, true);
}

bool UMD_SaveSlotManager::ApplyGameDataToWorldInternal(const FMD_GameData& GameData, bool bRequireSavedSlotIdentity)
{
    // 관리자의 서버 권한과 재진입 상태 확인
    if (HasServerAuthority() == false || bGameDataOperationInProgress == true)
    {
        // 실행할 수 없는 적용 요청의 실패 반환
        return false;
    }
    // 복원 이벤트를 처리할 구독자 존재 여부 확인
    if (OnOverwriteGameData.IsBound() == false)
    {
        // 복원 구독자가 없는 요청 종료
        return false;
    }

    // 원본 슬롯을 변경하지 않고 현재 스키마를 검증할 작업 데이터 생성
    FMD_GameData ValidatedGameData = GameData;
    // 저장 전 회차의 임시 식별자 허용 여부 확인
    if (bRequireSavedSlotIdentity == false)
    {
        // 검증 데이터의 식별자가 없는 경우에만 임시 이름 보완
        if (ValidatedGameData.UniqueName.IsEmpty() == true)
        {
            // 저장 전 회차의 검증용 식별자 설정
            ValidatedGameData.UniqueName = TEXT("TemporaryGameData");
        }
    }
    // 지원하지 않는 버전과 손상 필드를 적용 이벤트 전에 차단
    if (ValidatedGameData.IsValidForLoad(bRequireSavedSlotIdentity) == false)
    {
        // 적용 거부 원인 기록
        UE_LOG(MarineDemolition, Error, TEXT("저장 데이터의 버전 또는 구조가 유효하지 않습니다."));
        // 손상된 저장 데이터의 적용 실패 반환
        return false;
    }

    // 저장 당시 맵과 적용 대상 맵을 비교하기 위한 서버 월드 조회
    UWorld* World = GetWorld();
    // PIE 접두사 제거에 사용할 현재 맵 패키지 조회
    UPackage* WorldPackage = IsValid(World) == true ? World->GetPackage() : nullptr;
    // 다른 맵의 액터 상태가 현재 맵에 적용되는 상황 차단
    if (IsValid(WorldPackage) == false || UWorld::RemovePIEPrefix(WorldPackage->GetName()) != ValidatedGameData.MapPackagePath)
    {
        // 적용 대상 맵 불일치의 실패 반환
        return false;
    }

    // 적용과 수집의 중첩 요청을 전체 복원 완료 시점까지 차단
    TGuardValue<bool> OperationGuard(bGameDataOperationInProgress, true);
    // 이번 복원의 결과만 구독자와 공유하는 성공 상태 생성
    bool bWasSuccessful = true;
    // 모든 구독자에게 읽기 전용 데이터를 전달하여 각자의 필드를 직접 복원
    OnOverwriteGameData.Broadcast(ValidatedGameData, bWasSuccessful);
    // 구독자가 보고한 적용 실패를 현재 요청의 결과에 반영
    if (bWasSuccessful == false)
    {
        // 실패한 복원 결과로 현재 회차를 교체하는 상황 방지
        return false;
    }

    // 재접속 플레이어가 적용 완료된 동일한 개인 데이터를 사용하도록 회차 교체
    TemporaryGameData = MakeShared<FMD_GameData>(MoveTemp(ValidatedGameData));
    // 현재 회차가 이전 맵의 무거운 월드 스냅샷을 계속 보유하는 상황 방지
    TemporaryGameData->DataFields.Remove(FMD_WorldData::DataFieldKey);
    // 모든 구독자의 복원 성공 반환
    return true;
}

bool UMD_SaveSlotManager::ParseDataFromPlayer(const APlayerState* PlayerState)
{
    // 진행 중인 데이터 처리와 개인 수집의 중첩 차단
    if (bGameDataOperationInProgress == true)
    {
        // 재진입한 개인 수집 요청 종료
        return false;
    }
    // 같은 플레이어 객체의 유효성과 서버 권한을 함께 확인
    if (IsValid(PlayerState) == false || PlayerState->HasAuthority() == false)
    {
        // 잘못된 플레이어 수집 요청의 실패 반환
        return false;
    }
    // 플레이어 저장 책임을 가진 현재 서버 게임 모드 조회
    UWorld* World = GetWorld();
    // 서버 월드의 게임 모드를 통한 기존 Blueprint API 연결
    AMD_GameModeBase* GameMode = IsValid(World) == true ? World->GetAuthGameMode<AMD_GameModeBase>() : nullptr;
    // 플레이어 데이터 소유자가 없는 경우 수집 차단
    if (IsValid(GameMode) == false)
    {
        // 게임 모드 부재의 실패 반환
        return false;
    }
    // 최초 로그아웃 전에 현재 회차 데이터가 생성되지 않은 경우 확인
    if (TemporaryGameData.IsValid() == false)
    {
        // 개인 기록을 보관할 회차 데이터 생성
        TemporaryGameData = MakeShared<FMD_GameData>();
    }
    // 단일 플레이어 수집도 전체 저장 이벤트와 동일한 구현으로 위임
    return GameMode->CollectPlayerGameData(PlayerState, *TemporaryGameData);
}

bool UMD_SaveSlotManager::LoadPlayerData(const APlayerState* PlayerState) const
{
    // 관리자의 새 게임 예약과 데이터 처리 상태를 함께 확인
    if (bGameDataOperationInProgress == true || PendingGameDataOperation == EMD_PendingGameDataOperation::NewGame)
    {
        // 이전 개인 데이터를 적용할 수 없는 관리자 상태 거부
        return false;
    }
    // 개인 기록을 적용할 플레이어의 유효성 확인
    if (IsValid(PlayerState) == false)
    {
        // 적용 대상이 없는 개인 데이터 요청 종료
        return false;
    }
    // 적용할 현재 회차 데이터의 존재 여부 확인
    if (TemporaryGameData.IsValid() == false)
    {
        // 현재 회차가 없는 개인 데이터 요청 종료
        return false;
    }
    // 재접속 플레이어의 실제 복원을 담당하는 서버 게임 모드 조회
    UWorld* World = GetWorld();
    // 기존 Blueprint 함수 시그니처를 유지하면서 데이터 소유자에 연결
    AMD_GameModeBase* GameMode = IsValid(World) == true ? World->GetAuthGameMode<AMD_GameModeBase>() : nullptr;
    // 개인 데이터 조회와 장비 적용을 게임 모드에 위임
    return IsValid(GameMode) == true && GameMode->OverwritePlayerGameData(PlayerState, *TemporaryGameData);
}

bool UMD_SaveSlotManager::OnTryStartGameWithGameData(const FMD_GameData& GameData, bool bLoadGame, FString& OutMapPackagePath)
{
    // 이벤트 도중 활성 회차와 보류 작업이 교체되는 상황 차단
    if (bGameDataOperationInProgress == true)
    {
        // 진행 중인 요청을 유지한 채 재진입 종료
        return false;
    }

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

        FMD_GameData ValidatedGameData;
        if (GameData.bIsSlotSummary)
        {
            if (!FMD_SaveSlotStore::ForProject().ReadSlot(GameData, ValidatedGameData))
            {
                PendingGameDataOperation = EMD_PendingGameDataOperation::None;
                return false;
            }
        }
        else ValidatedGameData = GameData;
		if (ValidatedGameData.IsValidForLoad() == false)
		{
			UE_LOG(MarineDemolition, Error, TEXT("%s 시작할 저장 데이터가 손상되었거나 지원하지 않는 버전입니다."), *UE_CALL_LOCATIONINFO);
			PendingGameDataOperation = EMD_PendingGameDataOperation::None;
			return false;
		}

		SelectedLoadGameData = MakeShared<FMD_GameData>(MoveTemp(ValidatedGameData));
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
    // 이벤트 도중 활성 회차와 보류 작업이 교체되는 상황 차단
    if (bGameDataOperationInProgress == true)
    {
        // 진행 중인 요청을 유지한 채 재진입 종료
        return;
    }

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
    // 이벤트 도중 활성 회차와 보류 작업이 교체되는 상황 차단
    if (bGameDataOperationInProgress == true)
    {
        // 진행 중인 요청을 유지한 채 재진입 종료
        return;
    }

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
    // 이벤트 도중 활성 회차와 보류 작업이 교체되는 상황 차단
    if (bGameDataOperationInProgress == true)
    {
        // 진행 중인 요청을 유지한 채 재진입 종료
        return false;
    }

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
    // 관리자의 재진입 상태와 서버 권한을 함께 확인
    if (bGameDataOperationInProgress == true || HasServerAuthority() == false)
    {
        // 처리할 수 없는 수집 요청의 실패 반환
        return false;
    }
    // 수집 이벤트를 처리할 구독자 존재 여부 확인
    if (OnCollectGameData.IsBound() == false)
    {
        // 수집 구독자가 없는 요청 종료
        return false;
    }

    // 현재 맵의 저장 식별자를 안전하게 구성하기 위한 패키지 조회
    UWorld* World = GetWorld();
    // 유효한 서버 월드의 맵 패키지 조회
    UPackage* WorldPackage = IsValid(World) == true ? World->GetPackage() : nullptr;
    // 맵 패키지가 없는 상태에서 저장 데이터 생성 차단
    if (IsValid(WorldPackage) == false)
    {
        // 맵 식별 정보 누락의 실패 반환
        return false;
    }

    // 초기화가 끝난 서버 월드에서만 저장 이벤트를 호출하기 위한 진행 상태 조회
    AMD_GameStateBase* GameState = World->GetGameState<AMD_GameStateBase>();
    // 같은 진행 상태 객체의 유효성 및 권한과 월드 초기화 완료 여부 확인
    if (IsValid(GameState) == false || GameState->HasAuthority() == false || GameState->IsWorldInitializationReady() == false)
    {
        // 초기화 중인 월드를 저장하지 않은 원인 기록
        UE_LOG(MarineDemolition, Warning, TEXT("Save deferred: world initialization has not completed."));
        // 미완성 월드의 데이터 수집 이벤트 실행 차단
        return false;
    }

    // 모든 조기 반환 경로에서 재진입 제한을 해제하는 범위 보호
    TGuardValue<bool> OperationGuard(bGameDataOperationInProgress, true);
    // 이번 수집의 결과만 구독자와 공유하는 성공 상태 생성
    bool bWasSuccessful = true;
    // 이전 요청의 기능별 필드가 남지 않는 이번 저장용 데이터 생성
    FMD_GameData CollectedGameData;
    // 현재 실행 파일이 기록하는 스키마 버전 반영
    CollectedGameData.SchemaVersion = FMD_GameData::CurrentSchemaVersion;
    // 저장 전 구조 검증에 사용할 임시 슬롯 식별자 설정
    CollectedGameData.UniqueName = TEXT("TemporaryGameData");
    // PIE 접두사를 제외한 실제 맵 경로 기록
    CollectedGameData.MapPackagePath = UWorld::RemovePIEPrefix(WorldPackage->GetName());
    // 이번 수집 시점의 저장 시각 기록
    CollectedGameData.SaveTime = FDateTime::Now();
    // 로그아웃한 플레이어 기록을 새 수집에서도 유지하기 위한 기존 데이터 확인
    if (TemporaryGameData.IsValid() == true)
    {
        // 현재 접속자의 갱신에 앞서 이전 개인 기록 승계
        CollectedGameData.PlayerFields = TemporaryGameData->PlayerFields;
    }

    // 구독 중인 데이터 소유자에게 수정 가능한 저장 구조체 전달
    OnCollectGameData.Broadcast(CollectedGameData, bWasSuccessful);
    // 구독자가 공유한 이번 수집의 성공 여부 확인
    if (bWasSuccessful == false)
    {
        // 구독자 실패 시 수집 결과 반영 차단
        return false;
    }
    // 수집을 마친 데이터의 저장 구조 유효성 확인
    if (CollectedGameData.IsValidForLoad() == false)
    {
        // 미완성 수집 결과를 기존 회차에 반영하지 않고 실패 반환
        return false;
    }

    // 전체 수집이 성공한 경우에만 현재 회차 데이터 교체
    TemporaryGameData = MakeShared<FMD_GameData>(MoveTemp(CollectedGameData));
    // 검증된 저장 스냅샷 수집 성공 반환
    return true;
}
