#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Settings/MD_SettingCategoryBase.h"
#include "Gameplay/Settings/MD_SettingsTypes.h"
#include "MD_AudioSettingCategory.generated.h"

// 소리 설정 데이터와 유효 범위 검증을 담당한다.
UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_AudioSettingCategory : public UMD_SettingCategoryBase
{
	GENERATED_BODY()

public:
	// 소리 설정값을 지원 범위로 제한한다.
	virtual void ApplySettings() override;

	// 소리 설정을 구조체 기본값으로 복원한다.
	virtual void ResetToDefaults() override;

	// 현재 소리 값을 InstancedStruct 저장 데이터로 만든다.
	virtual FInstancedStruct MakeSaveData() override;

	// InstancedStruct에서 소리 값을 복원한다.
	virtual bool LoadSaveData(FInstancedStruct SaveData) override;

	// 소리 카테고리 식별자를 반환한다.
	virtual EMD_SettingsCategory GetCategory() override;

public:
	// 현재 편집 중인 소리 설정값을 보관한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MarineDemolition|Settings|Audio")
	FMD_AudioUserSettings Settings;
};
