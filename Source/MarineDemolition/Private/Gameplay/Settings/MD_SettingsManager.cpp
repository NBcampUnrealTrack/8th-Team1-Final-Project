#include "Gameplay/Settings/MD_SettingsManager.h"

#include "Engine/GameInstance.h"
#include "Gameplay/Settings/MD_AudioSettingCategory.h"
#include "Gameplay/Settings/MD_ControlSettingCategory.h"
#include "Gameplay/Settings/MD_GraphicsSettingCategory.h"
#include "Gameplay/Settings/MD_SettingCategoryBase.h"
#include "Gameplay/Settings/MD_SettingsSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "UI/Screen/Settings/MD_SettingsCategoryWidgetBase.h"

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
	// 소유 카테고리 참조 해제 전에 비동기 오디오 작업의 종료 요청
	UMD_AudioSettingCategory* AudioCategory = GetAudioSettings();
	// 생성된 오디오 카테고리의 월드 이벤트와 장치 요청 정리
	if (IsValid(AudioCategory) == true)
	{
		// GameInstance 종료 이후의 플랫폼 장치 변경 차단
		AudioCategory->ShutdownAudioDevices();
	}

	// 매니저가 소유한 저장 이벤트의 모든 카테고리 구독을 해제한다.
	OnSaveRequested.Clear();
	// 관리 중인 설정 인스턴스 참조를 비운다.
	SettingCategories.Empty();
	// 종료된 게임 인스턴스의 카테고리 화면 연결을 비운다.
	SettingsWidgets.Empty();
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

bool UMD_SettingsManager::SaveSettings(EMD_SettingsCategory Category)
{
	// 선택한 카테고리에 등록된 현재 편집 화면 조회
	UMD_SettingsCategoryWidgetBase* SettingsWidget = GetSettingsWidget(Category);
	// 화면이 등록된 카테고리에 한정한 미적용 선택값 수집
	if (IsValid(SettingsWidget) == true)
	{
		// 현재 화면 선택값의 카테고리 인스턴스 전달 결과 확인
		bool bCommitted = SettingsWidget->CommitPendingSettings();
		// 화면 편집값을 전달하지 못한 경우 기존 카테고리의 오적용 방지
		if (bCommitted == false)
		{
			// 현재 카테고리의 저장 실패 결과 준비
			bool bSaved = false;
			// 적용과 저장 없는 실패 반환
			return bSaved;
		}
	}
	// 저장 전에 현재 탭의 카테고리 값만 검증하고 실제 시스템에 적용한다.
	ApplyCategory(Category);
	// 요청한 카테고리 인스턴스만 자신의 데이터를 Map에 갱신하도록 저장 이벤트를 호출한다.
	OnSaveRequested.Broadcast(Category);
	// 변경한 카테고리와 기존 카테고리 데이터가 포함된 Map을 로컬 슬롯에 저장한다.
	bool bSaveSucceeded = SaveSettingDataMap();
	// 적용 과정의 범위 보정값을 현재 카테고리 화면에 다시 표시
	if (IsValid(SettingsWidget) == true)
	{
		// 실제 카테고리 기준값으로 미적용 화면 상태 동기화
		bool bReloaded = SettingsWidget->ReloadPendingSettings();
		// 화면 동기화 결과의 명시적 소비
		(void)bReloaded;
	}
	// 저장 성공 여부를 호출부에 전달한다.
	return bSaveSucceeded;
}

bool UMD_SettingsManager::RevertSettings(EMD_SettingsCategory Category)
{
	// 선택한 카테고리에 등록된 현재 편집 화면 조회
	UMD_SettingsCategoryWidgetBase* SettingsWidget = GetSettingsWidget(Category);
	// 표시 화면이 없는 카테고리의 변경 취소 실패
	if (IsValid(SettingsWidget) == false)
	{
		// 변경 취소 실패 결과 준비
		bool bReverted = false;
		// 기존 카테고리와 저장 데이터 변경 없는 실패 반환
		return bReverted;
	}
	// 현재 카테고리 값으로 화면의 미적용 선택을 교체한 결과 반환
	bool bReverted = SettingsWidget->ReloadPendingSettings();
	// 변경 취소 성공 여부 반환
	return bReverted;
}

bool UMD_SettingsManager::ResetSettings(EMD_SettingsCategory Category)
{
	// 선택한 카테고리에 등록된 현재 편집 화면 조회
	UMD_SettingsCategoryWidgetBase* SettingsWidget = GetSettingsWidget(Category);
	// 표시 화면이 없는 카테고리의 화면 초기화 실패
	if (IsValid(SettingsWidget) == false)
	{
		// 초기화 실패 결과 준비
		bool bReset = false;
		// 실제 카테고리와 저장 데이터 변경 없는 실패 반환
		return bReset;
	}
	// Save 호출 전까지 화면의 미적용 편집값만 기본값으로 교체
	SettingsWidget->ResetPendingSettings();
	// 화면 기본값 복원 성공 결과 준비
	bool bReset = true;
	// 초기화 성공 반환
	return bReset;
}

void UMD_SettingsManager::RegisterSettingsWidget(EMD_SettingsCategory Category, UMD_SettingsCategoryWidgetBase* SettingsWidget)
{
	// 파괴 중이거나 유효하지 않은 카테고리 화면의 등록 방지
	if (IsValid(SettingsWidget) == false)
	{
		return;
	}
	// 동일 카테고리의 최신 화면을 약한 참조로 등록
	SettingsWidgets.Add(Category, SettingsWidget);
}

void UMD_SettingsManager::UnregisterSettingsWidget(EMD_SettingsCategory Category, UMD_SettingsCategoryWidgetBase* SettingsWidget)
{
	// 카테고리에 등록된 화면 약한 참조 조회
	TWeakObjectPtr<UMD_SettingsCategoryWidgetBase>* FoundWidget = SettingsWidgets.Find(Category);
	// 등록되지 않은 카테고리의 해제 생략
	if (FoundWidget == nullptr)
	{
		return;
	}
	// 현재 카테고리에 등록된 실제 화면 조회
	UMD_SettingsCategoryWidgetBase* RegisteredWidget = FoundWidget->Get();
	// 유효한 다른 화면이 교체 등록된 경우 해당 연결 보존
	if (IsValid(RegisteredWidget) == true)
	{
		// 종료 대상과 현재 등록 화면의 동일성 확인
		if (RegisteredWidget != SettingsWidget)
		{
			return;
		}
	}
	// 종료 화면 또는 만료된 약한 참조의 카테고리 연결 제거
	SettingsWidgets.Remove(Category);
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

UMD_SettingsCategoryWidgetBase* UMD_SettingsManager::GetSettingsWidget(EMD_SettingsCategory Category)
{
	// 카테고리에 등록된 화면 약한 참조 조회
	TWeakObjectPtr<UMD_SettingsCategoryWidgetBase>* FoundWidget = SettingsWidgets.Find(Category);
	// 등록되지 않은 카테고리의 빈 결과 반환
	if (FoundWidget == nullptr)
	{
		return nullptr;
	}
	// 약한 참조가 현재 유효한지 확인
	bool bWidgetValid = FoundWidget->IsValid();
	// 파괴된 화면의 만료된 연결 정리
	if (bWidgetValid == false)
	{
		// 다음 조회에서 반복되지 않도록 카테고리 연결 제거
		SettingsWidgets.Remove(Category);
		// 유효한 화면이 없는 결과 반환
		return nullptr;
	}
	// 등록된 현재 카테고리 화면 조회
	UMD_SettingsCategoryWidgetBase* SettingsWidget = FoundWidget->Get();
	// 유효한 편집 화면 반환
	return SettingsWidget;
}
