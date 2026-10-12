#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Settings/MD_SettingCategoryBase.h"
#include "Gameplay/Settings/MD_SettingsTypes.h"
#include "MD_ControlSettingCategory.generated.h"

class UInputMappingContext;

// 조작 설정 데이터와 유효 범위 검증을 담당한다.
UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_ControlSettingCategory : public UMD_SettingCategoryBase
{
	GENERATED_BODY()

public:
	// 조작 설정값을 지원 범위로 제한한다.
	virtual void ApplySettings() override;

	// 조작 설정을 구조체 기본값으로 복원한다.
	virtual void ResetToDefaults() override;

	// 현재 조작 값을 InstancedStruct 저장 데이터로 만든다.
	virtual FInstancedStruct MakeSaveData() override;

	// InstancedStruct에서 조작 값을 복원한다.
	virtual bool LoadSaveData(FInstancedStruct SaveData) override;

	// 조작 카테고리 식별자를 반환한다.
	virtual EMD_SettingsCategory GetCategory() override;

public:
	// 현재 편집 중인 조작 설정값을 보관한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MarineDemolition|Settings|Controls")
	FMD_ControlSettings Settings;

protected:
	// 적용 버튼을 통해 실제 입력에 반영된 조작 설정값을 보관한다.
	UPROPERTY(Transient)
	FMD_ControlSettings AppliedSettings;

	// 런타임 복제본의 원본으로 사용한 플레이어 입력 매핑 컨텍스트
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> SourceMappingContext;

	// 사용자 선택 키를 적용하고 원본 에셋과 분리한 런타임 입력 매핑 컨텍스트
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> RuntimeMappingContext;

public:
	// 현재 실제 입력에서 사용하는 조작 설정값을 반환한다.
	UFUNCTION(BlueprintPure, Category = "MarineDemolition|Settings|Controls")
	FORCEINLINE FMD_ControlSettings GetAppliedSettings()
	{
		return AppliedSettings;
	}

	// 현재 실제 입력에서 사용하는 상호작용 키를 반환한다.
	UFUNCTION(BlueprintPure, Category = "MarineDemolition|Settings|Controls")
	FORCEINLINE FKey GetAppliedInteractionKey() const
	{
		return AppliedSettings.InteractKey;
	}

	// 현재 적용값을 반영한 플레이어 입력 매핑 컨텍스트 준비
	UInputMappingContext* PrepareInputMappingContext(UInputMappingContext* InputMappingContext);

protected:
	// 저장 데이터나 직접 편집으로 들어온 무효 키와 핫바 개수 보정
	void NormalizeSettings();

	// 런타임 입력 매핑 컨텍스트의 비이동 액션 키 갱신
	void RefreshInputMappingContext();

	// 입력 액션 이름에 대응하는 현재 적용 키 조회
	FKey GetConfiguredKey(FName ActionName, FKey PreviousKey);
};
