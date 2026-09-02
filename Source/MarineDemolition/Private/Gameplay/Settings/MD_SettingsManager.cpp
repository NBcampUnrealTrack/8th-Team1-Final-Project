#include "Gameplay/Settings/MD_SettingsManager.h"

#include "Engine/GameInstance.h"
#include "Gameplay/Settings/MD_AudioSettingCategory.h"
#include "Gameplay/Settings/MD_ControlSettingCategory.h"
#include "Gameplay/Settings/MD_GraphicsSettingCategory.h"
#include "Gameplay/Settings/MD_SettingCategoryBase.h"
#include "Gameplay/Settings/MD_SettingsSaveGame.h"
#include "Kismet/GameplayStatics.h"

namespace MDSettingsManager
{
	// 게임 진행 저장과 충돌하지 않는 독립적인 설정 슬롯 이름을 사용한다.
	static FString SlotName = TEXT("MD_PlayerSettings");
	// 현재 프로젝트는 로컬 사용자 인덱스 0의 설정을 관리한다.
	static int32 UserIndex = 0;
}

void UMD_SettingsManager::Initialize(FSubsystemCollectionBase& Collection)
{
	// 상위 서브시스템 초기화 흐름을 먼저 실행한다.
	Super::Initialize(Collection);
	// 카테고리별 UObject 인스턴스를 생성하고 이벤트를 연결한다.
	CreateSettingCategories();
	// 디스크에 저장된 InstancedStruct 맵을 각 인스턴스에 복원한다.
	LoadSettings();
	// 게임 시작 시 복원한 값을 각 런타임 시스템에 반영한다.
	ApplyAllSettings();
}

void UMD_SettingsManager::Deinitialize()
{
	// 매니저가 소유한 저장 이벤트의 모든 카테고리 구독을 해제한다.
	OnSaveRequested.Clear();
	// 관리 중인 설정 인스턴스 참조를 비운다.
	SettingCategories.Empty();
	// 상위 서브시스템 종료 흐름을 실행한다.
	Super::Deinitialize();
}

UMD_SettingsManager* UMD_SettingsManager::Get(UObject* WorldContextObject)
{
	// 월드 컨텍스트가 유효하지 않으면 매니저 조회를 중단한다.
	if (IsValid(WorldContextObject) == false)
	{
		return nullptr;
	}
	// 컨텍스트가 속한 월드를 찾는다.
	UWorld* World = WorldContextObject->GetWorld();
	// 유효한 월드를 찾지 못하면 매니저 조회를 중단한다.
	if (IsValid(World) == false)
	{
		return nullptr;
	}
	// 월드가 사용하는 게임 인스턴스를 찾는다.
	UGameInstance* GameInstance = World->GetGameInstance();
	// 유효한 게임 인스턴스를 찾지 못하면 매니저 조회를 중단한다.
	if (IsValid(GameInstance) == false)
	{
		return nullptr;
	}
	// 게임 인스턴스가 자동 생성한 설정 매니저를 직접 반환한다.
	return GameInstance->GetSubsystem<UMD_SettingsManager>();
}

UMD_SettingCategoryBase* UMD_SettingsManager::GetSettingCategory(EMD_SettingsCategory Category)
{
	// 카테고리 맵에서 요청한 설정 인스턴스 주소를 찾는다.
	TObjectPtr<UMD_SettingCategoryBase>* FoundSetting = SettingCategories.Find(Category);
	// 등록되지 않은 카테고리이면 빈 결과를 반환한다.
	if (FoundSetting == nullptr)
	{
		return nullptr;
	}
	// 찾은 설정 인스턴스를 직접 반환한다.
	return FoundSetting->Get();
}

UMD_GraphicsSettingCategory* UMD_SettingsManager::GetGraphicsSettings()
{
	// 공통 카테고리 맵의 그래픽 설정 인스턴스를 직접 반환한다.
	return Cast<UMD_GraphicsSettingCategory>(GetSettingCategory(EMD_SettingsCategory::Graphics));
}

UMD_AudioSettingCategory* UMD_SettingsManager::GetAudioSettings()
{
	// 공통 카테고리 맵의 소리 설정 인스턴스를 직접 반환한다.
	return Cast<UMD_AudioSettingCategory>(GetSettingCategory(EMD_SettingsCategory::Audio));
}

UMD_ControlSettingCategory* UMD_SettingsManager::GetControlSettings()
{
	// 공통 카테고리 맵의 조작 설정 인스턴스를 직접 반환한다.
	return Cast<UMD_ControlSettingCategory>(GetSettingCategory(EMD_SettingsCategory::Controls));
}

void UMD_SettingsManager::ApplyCategory(EMD_SettingsCategory Category)
{
	// 카테고리 맵에서 적용할 설정 인스턴스를 찾는다.
	UMD_SettingCategoryBase* SettingCategory = GetSettingCategory(Category);
	// 등록된 설정 인스턴스가 없으면 적용을 중단한다.
	if (IsValid(SettingCategory) == false)
	{
		return;
	}
	// 공통 가상 함수를 통해 카테고리 고유 설정을 적용한다.
	SettingCategory->ApplySettings();
	// 해당 카테고리의 적용이 끝났음을 UI와 런타임 시스템에 알린다.
	OnSettingsApplied.Broadcast(Category);
}

void UMD_SettingsManager::ApplyAllSettings()
{
	// 생성된 모든 설정 카테고리를 순회한다.
	for (TPair<EMD_SettingsCategory, TObjectPtr<UMD_SettingCategoryBase>>& SettingPair : SettingCategories)
	{
		// 현재 카테고리의 공통 적용 흐름을 실행한다.
		ApplyCategory(SettingPair.Key);
	}
}

bool UMD_SettingsManager::SaveSettings()
{
	// 저장 전에 모든 카테고리 값을 검증하고 실제 시스템에 적용한다.
	ApplyAllSettings();
	// 현재 인스턴스들에서 새 데이터를 수집하기 전에 기존 Map을 비운다.
	SettingDataMap.Empty();
	// 각 설정 인스턴스가 자신의 데이터를 Map에 등록하도록 저장 이벤트를 호출한다.
	OnSaveRequested.Broadcast();
	// 모든 인스턴스가 등록한 설정 데이터 Map을 로컬 슬롯에 저장한다.
	bool bSaveSucceeded = SaveSettingDataMap();
	// 저장 성공 여부를 호출부에 전달한다.
	return bSaveSucceeded;
}

void UMD_SettingsManager::ResetCategoryToDefaults(EMD_SettingsCategory Category)
{
	// 카테고리 맵에서 초기화할 설정 인스턴스를 찾는다.
	UMD_SettingCategoryBase* SettingCategory = GetSettingCategory(Category);
	// 등록된 설정 인스턴스가 없으면 초기화를 중단한다.
	if (IsValid(SettingCategory) == false)
	{
		return;
	}
	// 공통 가상 함수를 통해 카테고리 고유 기본값을 복원한다.
	SettingCategory->ResetToDefaults();
	// 매니저 저장 이벤트를 통해 기본값 적용과 모든 카테고리 저장을 함께 실행한다.
	bool bSaveSucceeded = SaveSettings();
	// 저장 결과는 기본값 복원 흐름에서 명시적으로 소비한다.
	(void)bSaveSucceeded;
}

void UMD_SettingsManager::RegisterSettingData(EMD_SettingsCategory Category, FInstancedStruct SettingData)
{
	// 카테고리 인스턴스가 생성한 저장 데이터가 유효한지 확인한다.
	bool bIsSettingDataValid = SettingData.IsValid();
	// 유효한 구조체 데이터가 아니면 Map에 등록하지 않는다.
	if (bIsSettingDataValid == false)
	{
		return;
	}
	// 카테고리 키에 해당하는 저장 데이터를 새 값으로 등록한다.
	SettingDataMap.Add(Category, MoveTemp(SettingData));
}

void UMD_SettingsManager::CreateSettingCategories()
{
	// 그래픽 설정 UObject 인스턴스를 생성한다.
	UMD_GraphicsSettingCategory* GraphicsSettings = NewObject<UMD_GraphicsSettingCategory>(this);
	// 소리 설정 UObject 인스턴스를 생성한다.
	UMD_AudioSettingCategory* AudioSettings = NewObject<UMD_AudioSettingCategory>(this);
	// 조작 설정 UObject 인스턴스를 생성한다.
	UMD_ControlSettingCategory* ControlSettings = NewObject<UMD_ControlSettingCategory>(this);
	// 생성한 그래픽 설정을 공통 카테고리 맵에 등록한다.
	SettingCategories.Add(EMD_SettingsCategory::Graphics, GraphicsSettings);
	// 생성한 소리 설정을 공통 카테고리 맵에 등록한다.
	SettingCategories.Add(EMD_SettingsCategory::Audio, AudioSettings);
	// 생성한 조작 설정을 공통 카테고리 맵에 등록한다.
	SettingCategories.Add(EMD_SettingsCategory::Controls, ControlSettings);
	// 등록한 모든 설정 인스턴스를 순회한다.
	for (TPair<EMD_SettingsCategory, TObjectPtr<UMD_SettingCategoryBase>>& SettingPair : SettingCategories)
	{
		// 현재 설정 인스턴스에 소유 매니저를 연결한다.
		SettingPair.Value->InitializeCategory(this);
	}
}

void UMD_SettingsManager::LoadSettings()
{
	// 설정 슬롯에 저장 데이터가 있는지 확인한다.
	bool bSettingsExist = UGameplayStatics::DoesSaveGameExist(MDSettingsManager::SlotName, MDSettingsManager::UserIndex);
	// 저장 데이터가 없으면 각 UObject의 기본값을 유지한다.
	if (bSettingsExist == false)
	{
		return;
	}
	// 설정 전용 슬롯에서 저장 객체를 불러온다.
	USaveGame* LoadedSaveGame = UGameplayStatics::LoadGameFromSlot(MDSettingsManager::SlotName, MDSettingsManager::UserIndex);
	// 불러온 객체를 프로젝트 설정 저장 형식으로 변환한다.
	UMD_SettingsSaveGame* SettingsSaveGame = Cast<UMD_SettingsSaveGame>(LoadedSaveGame);
	// 올바른 설정 저장 데이터가 아니면 기본값을 유지한다.
	if (IsValid(SettingsSaveGame) == false)
	{
		return;
	}
	// 불러온 카테고리별 InstancedStruct 맵을 현재 저장 데이터로 복사한다.
	SettingDataMap = SettingsSaveGame->SettingDataMap;
	// 생성된 모든 설정 인스턴스를 순회한다.
	for (TPair<EMD_SettingsCategory, TObjectPtr<UMD_SettingCategoryBase>>& SettingPair : SettingCategories)
	{
		// 현재 카테고리에 대응하는 InstancedStruct 저장 데이터를 찾는다.
		FInstancedStruct* FoundSaveData = SettingDataMap.Find(SettingPair.Key);
		// 저장 데이터가 없는 카테고리는 UObject 기본값을 유지한다.
		if (FoundSaveData == nullptr)
		{
			continue;
		}
		// 공통 가상 함수를 통해 카테고리 고유 데이터를 복원한다.
		bool bLoadSucceeded = SettingPair.Value->LoadSaveData(*FoundSaveData);
		// 형식이 일치하지 않은 데이터는 다음 저장 시 교체되도록 맵에서 제거한다.
		if (bLoadSucceeded == false)
		{
			SettingDataMap.Remove(SettingPair.Key);
		}
	}
}

bool UMD_SettingsManager::SaveSettingDataMap()
{
	// 현재 설정 맵을 기록할 저장 객체를 생성한다.
	UMD_SettingsSaveGame* SettingsSaveGame = Cast<UMD_SettingsSaveGame>(UGameplayStatics::CreateSaveGameObject(UMD_SettingsSaveGame::StaticClass()));
	// 저장 객체 생성에 실패하면 저장을 중단한다.
	if (IsValid(SettingsSaveGame) == false)
	{
		return false;
	}
	// 현재 카테고리별 InstancedStruct 맵을 저장 객체에 복사한다.
	SettingsSaveGame->SettingDataMap = SettingDataMap;
	// 설정 전용 슬롯에 저장한 결과를 보관한다.
	bool bSaveSucceeded = UGameplayStatics::SaveGameToSlot(SettingsSaveGame, MDSettingsManager::SlotName, MDSettingsManager::UserIndex);
	// 저장 성공 여부를 호출부에 전달한다.
	return bSaveSucceeded;
}
