#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Gameplay/Settings/MD_SettingsTypes.h"
#include "StructUtils/InstancedStruct.h"
#include "MD_SettingsManager.generated.h"

class UMD_AudioSettingCategory;
class UMD_ControlSettingCategory;
class UMD_GraphicsSettingCategory;
class UMD_SettingCategoryBase;

// 특정 카테고리의 설정이 적용된 시점을 UI와 런타임 시스템에 알린다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMD_OnSettingsApplied, EMD_SettingsCategory, Category);

// 저장 직전에 요청한 카테고리 인스턴스가 자신의 데이터를 Map에 등록하도록 알린다.
DECLARE_MULTICAST_DELEGATE_OneParam(FMD_OnSettingsSaveRequested, EMD_SettingsCategory);

// UObject 설정 인스턴스의 생성, 적용 이벤트 및 InstancedStruct 저장을 통합 관리한다.
UCLASS()
class MARINEDEMOLITION_API UMD_SettingsManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// 설정 인스턴스를 만들고 저장 데이터를 복원한 뒤 전체 설정을 적용한다.
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// 설정 인스턴스의 저장 이벤트 연결을 안전하게 해제한다.
	virtual void Deinitialize() override;

public:
	// 카테고리 설정이 적용된 이후 구독 중인 시스템에 알린다.
	UPROPERTY(BlueprintAssignable, Category = "MarineDemolition|Settings")
	FMD_OnSettingsApplied OnSettingsApplied;

	// 저장 요청 시 지정한 카테고리 인스턴스가 데이터를 등록하도록 호출한다.
	FMD_OnSettingsSaveRequested OnSaveRequested;

protected:
	// 카테고리 식별자와 런타임 UObject 설정 인스턴스를 연결한다.
	UPROPERTY(Transient)
	TMap<EMD_SettingsCategory, TObjectPtr<UMD_SettingCategoryBase>> SettingCategories;

	// 저장 슬롯에서 불러왔거나 저장 이벤트로 갱신한 카테고리 데이터를 보관한다.
	UPROPERTY(Transient)
	TMap<EMD_SettingsCategory, FInstancedStruct> SettingDataMap;

public:
	// 월드에서 현재 게임 인스턴스의 설정 매니저를 찾는다.
	UFUNCTION(BlueprintPure, Category = "MarineDemolition|Settings", meta = (WorldContext = "WorldContextObject"))
	static UMD_SettingsManager* Get(UObject* WorldContextObject);

	// 지정한 카테고리의 공통 설정 인스턴스를 반환한다.
	UFUNCTION(BlueprintPure, Category = "MarineDemolition|Settings")
	UMD_SettingCategoryBase* GetSettingCategory(EMD_SettingsCategory Category);

	// 그래픽 설정 인스턴스를 반환한다.
	UFUNCTION(BlueprintPure, Category = "MarineDemolition|Settings|Graphics")
	UMD_GraphicsSettingCategory* GetGraphicsSettings();

	// 소리 설정 인스턴스를 반환한다.
	UFUNCTION(BlueprintPure, Category = "MarineDemolition|Settings|Audio")
	UMD_AudioSettingCategory* GetAudioSettings();

	// 조작 설정 인스턴스를 반환한다.
	UFUNCTION(BlueprintPure, Category = "MarineDemolition|Settings|Controls")
	UMD_ControlSettingCategory* GetControlSettings();

	// 지정한 카테고리의 현재 값을 실제 시스템에 반영한다.
	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Settings")
	void ApplyCategory(EMD_SettingsCategory Category);

	// 모든 카테고리의 현재 값을 실제 시스템에 반영한다.
	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Settings")
	void ApplyAllSettings();

	// 지정한 카테고리만 적용하고 저장 이벤트로 해당 데이터를 수집한 뒤 슬롯에 저장한다.
	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Settings")
	bool SaveSettings(EMD_SettingsCategory Category);

	// 지정한 카테고리를 프로젝트 기본값으로 되돌리고 적용한다.
	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Settings")
	void ResetCategoryToDefaults(EMD_SettingsCategory Category);

	// 설정 인스턴스가 생성한 카테고리 데이터를 저장 Map에 등록한다.
	void RegisterSettingData(EMD_SettingsCategory Category, FInstancedStruct SettingData);

protected:
	// 기본 카테고리 UObject 인스턴스를 생성하고 저장 이벤트를 연결한다.
	void CreateSettingCategories();

	// 로컬 설정 슬롯의 InstancedStruct 맵을 불러와 각 인스턴스에 전달한다.
	void LoadSettings();

	// 현재 InstancedStruct 맵을 설정 전용 슬롯에 저장한다.
	bool SaveSettingDataMap();
};
