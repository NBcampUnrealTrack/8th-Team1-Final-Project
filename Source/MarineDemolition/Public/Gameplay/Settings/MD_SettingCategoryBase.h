#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Gameplay/Settings/MD_SettingsTypes.h"
#include "StructUtils/InstancedStruct.h"
#include "MD_SettingCategoryBase.generated.h"

class UMD_SettingsManager;

// 모든 설정 카테고리가 공통으로 제공해야 하는 적용과 저장 동작을 정의한다.
UCLASS(Abstract, BlueprintType)
class MARINEDEMOLITION_API UMD_SettingCategoryBase : public UObject
{
	GENERATED_BODY()

protected:
	// 카테고리를 생성하고 이벤트를 연결한 설정 매니저를 보관한다.
	UPROPERTY(Transient)
	TObjectPtr<UMD_SettingsManager> SettingsManager;

public:
	// 현재 인스턴스가 나타내는 설정 카테고리를 반환한다.
	UFUNCTION(BlueprintPure, Category = "MarineDemolition|Settings")
	virtual EMD_SettingsCategory GetCategory() PURE_VIRTUAL(UMD_SettingCategoryBase::GetCategory, return EMD_SettingsCategory::Graphics;);

	// 설정 매니저가 생성한 카테고리 인스턴스에 소유 매니저를 연결한다.
	void InitializeCategory(UMD_SettingsManager* InSettingsManager);

	// 현재 카테고리의 값을 실제 런타임 시스템에 적용한다.
	virtual void ApplySettings() PURE_VIRTUAL(UMD_SettingCategoryBase::ApplySettings, );

	// 현재 카테고리의 값을 프로젝트 기본값으로 복원한다.
	virtual void ResetToDefaults() PURE_VIRTUAL(UMD_SettingCategoryBase::ResetToDefaults, );

	// 카테고리의 현재 값을 InstancedStruct 저장 데이터로 만든다.
	virtual FInstancedStruct MakeSaveData() PURE_VIRTUAL(UMD_SettingCategoryBase::MakeSaveData, return FInstancedStruct(););

	// InstancedStruct에 저장된 값을 현재 카테고리에 복원한다.
	virtual bool LoadSaveData(FInstancedStruct SaveData) PURE_VIRTUAL(UMD_SettingCategoryBase::LoadSaveData, return false;);

	// 매니저의 저장 이벤트를 받아 요청 대상이면 현재 카테고리 데이터를 저장 Map에 등록한다.
	void HandleSaveRequested(EMD_SettingsCategory RequestedCategory);
};
