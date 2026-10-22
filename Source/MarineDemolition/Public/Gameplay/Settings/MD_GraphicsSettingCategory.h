#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Settings/MD_SettingCategoryBase.h"
#include "Gameplay/Settings/MD_SettingsTypes.h"
#include "MD_GraphicsSettingCategory.generated.h"

class IConsoleVariable;

// 그래픽 설정 데이터와 엔진 적용 동작을 담당한다.
UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_GraphicsSettingCategory : public UMD_SettingCategoryBase
{
	GENERATED_BODY()

public:
	// 그래픽 설정을 엔진 사용자 설정에 적용한다.
	virtual void ApplySettings() override;

	// 그래픽 설정을 하드웨어 감지값 기반의 초기화 값으로 복원한다.
	virtual void ResetToDefaults() override;

	// 현재 그래픽 값을 InstancedStruct 저장 데이터로 만든다.
	virtual FInstancedStruct MakeSaveData() override;

	// InstancedStruct에서 그래픽 값을 복원한다.
	virtual bool LoadSaveData(FInstancedStruct SaveData) override;

	// 그래픽 카테고리 식별자를 반환한다.
	virtual EMD_SettingsCategory GetCategory() override;

	// 렌더 해상도 단계에 대응하는 엔진 화면 비율을 반환하며 0은 엔진 자동 비율을 의미한다.
	static float ToScreenPercentage(EMD_RenderResolutionMode RenderResolution);

	// 업스케일러를 반영해 실제로 적용할 화면 비율을 반환한다.
	float GetScreenPercentage() const;

	// 모니터 주사율 옵션을 반영해 실제로 적용할 프레임 제한을 반환한다.
	float GetEffectiveFrameRateLimit() const;

	// 맵 로드로 MegaLights 사용 여부가 바뀌었을 때 레이 트레이싱 사용 여부를 다시 맞춘다.
	void RefreshRayTracing();

	// PIE 종료 시 에디터에 남지 않도록 업스케일러 덮어쓰기를 해제해 프로젝트 설정을 되살린다.
	void ReleaseUpscalerOverride();

	// 서버, 커맨드릿, 렌더링 없는 실행을 제외하고 하드웨어 벤치마크를 실행할 수 있는지 확인한다.
	static bool CanRunHardwareBenchmark();

	// 하드웨어 벤치마크를 실행해 권장 품질과 렌더 해상도를 Settings.Recommendation에 기록한다.
	bool DetectRecommendation();

	// 감지 기록이 있으면 권장 품질과 렌더 해상도를 대상 설정에 덮어쓴다.
	static void ApplyRecommendation(FMD_GraphicsSettings& TargetSettings);

	// 프로젝트 기본값에 하드웨어 감지값을 덮어쓴 초기화 값을 만들며 감지 기록이 없으면 먼저 감지한다.
	FMD_GraphicsSettings MakeResetSettings();

protected:
	// 버전 필드 이전 저장 데이터의 새 필드를 기존 선택이 유지되도록 보정한다.
	void MigrateLegacySaveData();

	// 품질 단계가 남긴 수동 화면 비율을 지우고 선택한 비율을 엔진에 반영한다.
	void ApplyScreenPercentage(float ScreenPercentage);

	// 선택한 업스케일러를 프로젝트 설정보다 높은 우선순위로 적용한다.
	void ApplyUpscaler();

	// 현재 월드나 프로젝트 설정이 MegaLights를 요청하는지 확인한다.
	bool IsMegaLightsRequested();

	// 레이 트레이싱 전체 사용 여부를 바꿔야 하면 해당 콘솔 변수를 반환하고 그대로 두면 nullptr를 반환한다.
	IConsoleVariable* FindRayTracingSwitch(int32 GlobalIlluminationQuality, bool& bOutEnableRayTracing);

public:
	// 현재 편집 중인 그래픽 설정값을 보관한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MarineDemolition|Settings|Graphics")
	FMD_GraphicsSettings Settings;
};
