#include "Gameplay/Lore/MD_LoreSubsystem.h"
#include "Data/SaveGame/MD_SaveSlotManager.h"
#include "Engine/GameInstance.h"

#include "Data/Lore/MD_LoreDefinition.h"
#include "Util/MD_LogUtility.h"

bool UMD_LoreSubsystem::TryDiscoverLore(FPrimaryAssetId LoreAssetId)
{
	// 서버 권한과 유효한 Asset ID가 모두 있어야 발견 원장을 변경한다.
	if (HasServerAuthority() == false)
	{
		return false;
	}

	if (LoreAssetId.IsValid() == false)
	{
		return false;
	}

	// 액터에 할당된 Asset ID가 실제 로어 정의로 해석되는지 확인한다.
	UMD_LoreDefinition* LoreDefinition = GetLoreDefinition(LoreAssetId);
	if (IsValid(LoreDefinition) == false)
	{
		return false;
	}

	if (LoreDefinition->IsValidDefinition() == false)
	{
		return false;
	}
	// 이미 발견한 로어는 전역 원장에 중복 기록하지 않는다.
	if (DiscoveredLoreAssetIds.Contains(LoreAssetId) == true)
	{
		return false;
	}

	// 카탈로그와 관계없이 전역 발견 사실을 기록한다.
	DiscoveredLoreAssetIds.Add(LoreAssetId);

	// 액터 파괴 이벤트보다 먼저 서버의 확정 결과를 남기기 위한 수집 로그
	UE_LOG(MarineDemolition, Log, TEXT("%s [로어] 기록물 수집 완료: 에셋=%s, 전체 진행=%d"), *UE_CALL_LOCATIONINFO, *LoreAssetId.ToString(), DiscoveredLoreAssetIds.Num());
	OnLoreProgressChanged.Broadcast();

	return true;
}

UMD_LoreDefinition* UMD_LoreSubsystem::GetLoreDefinition(FPrimaryAssetId LoreAssetId) const
{
	// 정의 클래스의 공통 Asset Manager 조회 경로를 사용한다.
	UMD_LoreDefinition* LoreDefinition = UMD_LoreDefinition::LoadDefinition(LoreAssetId);
	return LoreDefinition;
}

EMD_LoreEntryState UMD_LoreSubsystem::GetLoreState(const UMD_LoreCatalog* LoreCatalog, FPrimaryAssetId LoreAssetId) const
{
	if (IsValid(LoreCatalog) == false || LoreAssetId.IsValid() == false)
	{
		return EMD_LoreEntryState::Undiscovered;
	}

	EMD_LoreEntryState LoreState = LoreCatalog->ResolveLoreState(LoreAssetId, DiscoveredLoreAssetIds);
	return LoreState;
}

bool UMD_LoreSubsystem::HasDiscoveredLore(FPrimaryAssetId LoreAssetId) const
{
	if (LoreAssetId.IsValid() == false)
	{
		return false;
	}

	bool bIsDiscovered = DiscoveredLoreAssetIds.Contains(LoreAssetId);
	return bIsDiscovered;
}

bool UMD_LoreSubsystem::IsCatalogComplete(const UMD_LoreCatalog* LoreCatalog) const
{
	if (IsValid(LoreCatalog) == false || LoreCatalog->LoreEntries.Num() <= 0)
	{
		return false;
	}

	// 정적 카탈로그의 모든 식별자가 발견 원장에 존재하는지 확인하는 완성 판정
	for (int32 LoreIndex = 0; LoreIndex < LoreCatalog->GetLoreCount(); ++LoreIndex)
	{
		// 카탈로그 Asset ID를 완성 판정에 사용할 정의 에셋으로 해석한다.
		UMD_LoreDefinition* LoreDefinition = LoreCatalog->GetLoreDefinition(LoreIndex);
		if (IsValid(LoreDefinition) == false)
		{
			return false;
		}

		FPrimaryAssetId LoreAssetId = LoreCatalog->LoreEntries[LoreIndex];
		if (DiscoveredLoreAssetIds.Contains(LoreAssetId) == false)
		{
			return false;
		}
	}

	return true;
}

FMD_LoreData UMD_LoreSubsystem::GetLoreSaveData() const
{
	FMD_LoreData SaveData;
	SaveData.DiscoveredLoreAssetIds = DiscoveredLoreAssetIds;
	return SaveData;
}

bool UMD_LoreSubsystem::CanApplyLoreSaveData(const FMD_LoreData& SaveData) const
{
	return HasServerAuthority() == true && SaveData.IsStructurallyValid() == true;
}

bool UMD_LoreSubsystem::ApplyLoreSaveData(const FMD_LoreData& SaveData)
{
	if (CanApplyLoreSaveData(SaveData) == false)
	{
		return false;
	}

	DiscoveredLoreAssetIds = SaveData.DiscoveredLoreAssetIds;
	OnLoreProgressChanged.Broadcast();
	return true;
}

bool UMD_LoreSubsystem::HasServerAuthority() const
{
	const UWorld* World = GetWorld();
	return IsValid(World) == true && World->GetNetMode() != ENetMode::NM_Client;
}

void UMD_LoreSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    // 엔진 서브시스템 초기화 수행
    Super::Initialize(Collection);
    // 저장 관리자보다 먼저 구독을 시도하지 않도록 초기화 의존성 지정
    Collection.InitializeDependency<UMD_SaveSlotManager>();
    // 기능별 데이터 소유자를 등록할 저장 관리자 조회
    UMD_SaveSlotManager* SaveSlotManager = GetGameInstance()->GetSubsystem<UMD_SaveSlotManager>();
    // 저장 관리자가 유효한 경우에만 현재 서브시스템 연결
    if (IsValid(SaveSlotManager) == true)
    {
        // 소유 데이터 수집 이벤트 구독
        SaveSlotManager->OnCollectGameData.AddUniqueDynamic(this, &ThisClass::CollectGameData);
        // 소유 데이터 적용 이벤트 구독
        SaveSlotManager->OnOverwriteGameData.AddUniqueDynamic(this, &ThisClass::OverwriteGameData);
    }
}

void UMD_LoreSubsystem::Deinitialize()
{
    // 종료 중인 게임 인스턴스의 저장 관리자 조회 준비
    UGameInstance* GameInstance = GetGameInstance();
    // 게임 인스턴스가 남아 있는 경우에만 관리자 조회
    UMD_SaveSlotManager* SaveSlotManager = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_SaveSlotManager>() : nullptr;
    // 종료한 서브시스템이 다음 저장 요청에 참여하지 않도록 구독 해제
    if (IsValid(SaveSlotManager) == true)
    {
        // 소유 데이터 수집 연결 해제
        SaveSlotManager->OnCollectGameData.RemoveDynamic(this, &ThisClass::CollectGameData);
        // 소유 데이터 적용 연결 해제
        SaveSlotManager->OnOverwriteGameData.RemoveDynamic(this, &ThisClass::OverwriteGameData);
    }
    // 엔진 서브시스템 종료 수행
    Super::Deinitialize();
}

void UMD_LoreSubsystem::CollectGameData(FMD_GameData& GameData, bool& bWasSuccessful)
{
    // 현재 기능 원장을 자신의 저장 태그 필드에 기록
    if (GameData.AddDataField(FMD_LoreData::DataFieldKey, GetLoreSaveData()) == false)
    {
        // 다른 구독자의 성공으로 덮어쓰지 않는 현재 요청의 실패 반영
        bWasSuccessful = false;
        // 실패한 기능의 필드 키 기록
        UE_LOG(LogTemp, Warning, TEXT("저장 데이터 처리 실패: %s"), *FMD_LoreData::DataFieldKey.ToString());
    }
}

void UMD_LoreSubsystem::OverwriteGameData(const FMD_GameData& GameData, bool& bWasSuccessful)
{
    // 전달된 읽기 전용 데이터에서 조회할 담당 필드
    FMD_LoreData SaveData;
    // 전달된 데이터에서 자신의 태그에 해당하는 필드 조회
    if (GameData.GetDataField(FMD_LoreData::DataFieldKey, SaveData) == false)
    {
        // 다른 구독자의 성공으로 덮어쓰지 않는 현재 요청의 실패 반영
        bWasSuccessful = false;
        // 실패한 기능의 필드 키 기록
        UE_LOG(LogTemp, Warning, TEXT("저장 데이터 처리 실패: %s"), *FMD_LoreData::DataFieldKey.ToString());
        // 필드 조회 실패 시 복원 API 호출 차단
        return;
    }
    // 조회를 마친 필드를 기존 복원 API에 적용
    if (ApplyLoreSaveData(SaveData) == false)
    {
        // 다른 구독자의 성공으로 덮어쓰지 않는 현재 요청의 실패 반영
        bWasSuccessful = false;
        // 실패한 기능의 필드 키 기록
        UE_LOG(LogTemp, Warning, TEXT("저장 데이터 처리 실패: %s"), *FMD_LoreData::DataFieldKey.ToString());
    }
}
