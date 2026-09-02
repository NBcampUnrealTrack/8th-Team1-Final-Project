#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Settings/MD_SettingCategoryBase.h"
#include "Gameplay/Settings/MD_SettingsTypes.h"
#include "MD_GraphicsSettingCategory.generated.h"

// 그래픽 설정 데이터와 엔진 적용 동작을 담당한다.
UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_GraphicsSettingCategory : public UMD_SettingCategoryBase
{
	GENERATED_BODY()

public:
	// 그래픽 설정을 엔진 사용자 설정에 적용한다.
	virtual void ApplySettings() override;

	// 그래픽 설정을 구조체 기본값으로 복원한다.
	virtual void ResetToDefaults() override;

	// 현재 그래픽 값을 InstancedStruct 저장 데이터로 만든다.
	virtual FInstancedStruct MakeSaveData() override;

	// InstancedStruct에서 그래픽 값을 복원한다.
	virtual bool LoadSaveData(FInstancedStruct SaveData) override;

	// 그래픽 카테고리 식별자를 반환한다.
	virtual EMD_SettingsCategory GetCategory() override;

public:
	// 현재 편집 중인 그래픽 설정값을 보관한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MarineDemolition|Settings|Graphics")
	FMD_GraphicsSettings Settings;
};
