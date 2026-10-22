#include "Gameplay/Settings/MD_GraphicsSettingCategory.h"

#include "ComponentRecreateRenderStateContext.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "Gameplay/Contamination/MD_StainRayTracingResidency.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/Interface_PostProcessVolume.h"
#include "Misc/App.h"
#include "RenderUtils.h"
#include "Scalability.h"
#include "SceneUtils.h"
#include "Util/MD_LogUtility.h"

namespace MDGraphicsSettingCategory
{
	// DefaultScalability.ini가 이 GI 품질 단계 이하에서 Lumen 하드웨어 레이 트레이싱을 끄므로 레이 트레이싱 전체도 끈다.
	static int32 MaxGlobalIlluminationQualityWithoutRayTracing = 2;

	// 업스케일러·렌더 해상도·모니터 주사율 필드가 추가된 현재 저장 데이터 형식 버전
	static int32 CurrentSaveVersion = 1;

	// 버전 필드 이전 저장 데이터의 프레임 제한 기본값
	static float LegacyDefaultFrameRateLimit = 60.0f;
}

void UMD_GraphicsSettingCategory::ApplySettings()
{
	// 그래픽 품질 단계를 엔진이 지원하는 범위로 제한한다.
	Settings.OverallQuality = FMath::Clamp(Settings.OverallQuality, 0, 4);
	// 그림자 품질 단계를 엔진이 지원하는 범위로 제한한다.
	Settings.ShadowQuality = FMath::Clamp(Settings.ShadowQuality, 0, 4);
	// 효과 품질 단계를 엔진이 지원하는 범위로 제한한다.
	Settings.EffectsQuality = FMath::Clamp(Settings.EffectsQuality, 0, 4);
	// 후처리 품질 단계를 엔진이 지원하는 범위로 제한한다.
	Settings.PostProcessQuality = FMath::Clamp(Settings.PostProcessQuality, 0, 4);
	// 프레임 제한을 무제한 또는 지원 범위 안의 값으로 정리한다.
	Settings.FrameRateLimit = FMath::Clamp(Settings.FrameRateLimit, 0.0f, 360.0f);
	// 현재 플랫폼의 엔진 사용자 설정 객체를 찾는다.
	UGameUserSettings* GameUserSettings = UGameUserSettings::GetGameUserSettings();
	// 엔진 사용자 설정을 사용할 수 없으면 적용을 중단한다.
	if (IsValid(GameUserSettings) == false)
	{
		return;
	}
	// 선택한 창 모드를 엔진 사용자 설정에 적용한다.
	GameUserSettings->SetFullscreenMode(Settings.WindowMode);
	// 설정된 해상도의 가로 값이 유효한지 확인한다.
	bool bIsResolutionWidthValid = Settings.ScreenResolution.X > 0;
	// 설정된 해상도의 세로 값이 유효한지 확인한다.
	bool bIsResolutionHeightValid = Settings.ScreenResolution.Y > 0;
	// 가로와 세로 해상도가 모두 유효한 경우에만 출력 해상도를 적용한다.
	if (bIsResolutionWidthValid == true)
	{
		if (bIsResolutionHeightValid == true)
		{
			GameUserSettings->SetScreenResolution(Settings.ScreenResolution);
		}
	}
	// 선택한 전체 품질 단계를 모든 확장성 항목에 적용한다.
	GameUserSettings->SetOverallScalabilityLevel(Settings.OverallQuality);
	// 전체 품질과 별도로 선택한 그림자 품질 단계를 적용한다.
	GameUserSettings->SetShadowQuality(Settings.ShadowQuality);
	// 전체 품질과 별도로 선택한 효과 품질 단계를 적용한다.
	GameUserSettings->SetVisualEffectQuality(Settings.EffectsQuality);
	// 전체 품질과 별도로 선택한 후처리 품질 단계를 적용한다.
	GameUserSettings->SetPostProcessingQuality(Settings.PostProcessQuality);
	// 품질 단계 적용이 해상도 품질을 50~100으로 덮어쓰므로 그 뒤에 적용할 화면 비율을 구한다.
	float ScreenPercentage = GetScreenPercentage();
	// 선택한 렌더 해상도 비율을 엔진 사용자 설정의 해상도 품질에 반영한다.
	GameUserSettings->SetResolutionScaleValueEx(ScreenPercentage);
	// 선택한 수직 동기화 사용 여부를 적용한다.
	GameUserSettings->SetVSyncEnabled(Settings.bUseVSync);
	// 모니터 주사율 옵션을 반영한 최대 프레임 속도를 적용한다.
	GameUserSettings->SetFrameRateLimit(GetEffectiveFrameRateLimit());
	// 적용할 GI 품질에 맞춰 레이 트레이싱 전체 사용 여부를 바꿔야 하는지 미리 확인한다.
	bool bEnableRayTracing = true;
	// 바꿔야 할 때만 레이 트레이싱 사용 여부 콘솔 변수를 받는다.
	IConsoleVariable* RayTracingSwitch = FindRayTracingSwitch(GameUserSettings->GetGlobalIlluminationQuality(), bEnableRayTracing);
	// 게임 시작 시점처럼 엔진 초기화 전인지 확인한다.
	bool bIsEngineInitialized = GEngine != nullptr && GEngine->IsInitialized() == true;
	// 엔진 초기화 전에는 엔진 사용자 설정이 품질 단계를 적용하지 않으므로 직접 반영한다.
	if (bIsEngineInitialized == false)
	{
		Scalability::SetQualityLevels(GameUserSettings->ScalabilityQuality);
	}
	// 얼룩 RT 상주 관리는 레이 트레이싱 전환 전에 모든 단계를 복원해야 한다.
	if (RayTracingSwitch != nullptr)
	{
		MD_StainRayTracingResidency::RestoreAll();
	}
	{
		// 품질 단계와 레이 트레이싱 전환이 각자 렌더 상태를 다시 만들지 않고 한 번만 다시 만들도록 묶는다.
		FGlobalComponentRecreateRenderStateContext RecreateRenderStateContext;
		// 레이 트레이싱 전체 사용 여부가 바뀌는 경우에만 콘솔과 커맨드라인이 덮어쓸 수 있는 게임 설정 우선순위로 전환한다.
		if (RayTracingSwitch != nullptr)
		{
			RayTracingSwitch->Set(bEnableRayTracing == true ? 1 : 0, ECVF_SetByGameSetting);
		}
		// 변경한 엔진 설정을 실행하고 GameUserSettings 설정 파일에도 저장한다.
		GameUserSettings->ApplySettings(false);
	}
	// 엔진이 처리하지 않는 자동 비율 복귀를 위해 화면 비율을 직접 반영한다.
	ApplyScreenPercentage(ScreenPercentage);
	// 선택한 업스케일러를 적용한다.
	ApplyUpscaler();
}

void UMD_GraphicsSettingCategory::ResetToDefaults()
{
	// 그래픽 설정값을 하드웨어 감지값을 반영한 초기화 값으로 교체한다.
	Settings = MakeResetSettings();
}

FInstancedStruct UMD_GraphicsSettingCategory::MakeSaveData()
{
	// 저장할 그래픽 설정을 복사한다.
	FMD_GraphicsSettings SaveSettings = Settings;
	// 기본값 0과 달라 항상 기록되도록 현재 저장 형식 버전을 넣는다.
	SaveSettings.SaveVersion = MDGraphicsSettingCategory::CurrentSaveVersion;
	// 현재 그래픽 설정과 실제 구조체 타입을 포함하는 InstancedStruct를 생성한다.
	FInstancedStruct InstancedSaveData = FInstancedStruct::Make(SaveSettings);
	// 생성한 저장 데이터를 설정 매니저에 전달한다.
	return InstancedSaveData;
}

bool UMD_GraphicsSettingCategory::LoadSaveData(FInstancedStruct SaveData)
{
	// 저장 데이터가 그래픽 설정 형식인지 확인한다.
	bool bIsGraphicsData = SaveData.GetScriptStruct() == FMD_GraphicsSettings::StaticStruct();
	// 다른 카테고리의 저장 데이터이면 복원을 중단한다.
	if (bIsGraphicsData == false)
	{
		return false;
	}
	// 검증한 InstancedStruct에서 그래픽 설정 데이터를 읽는다.
	FMD_GraphicsSettings* GraphicsSaveData = SaveData.GetMutablePtr<FMD_GraphicsSettings>();
	// 저장 데이터 주소를 얻지 못하면 복원을 중단한다.
	if (GraphicsSaveData == nullptr)
	{
		return false;
	}
	// 저장된 그래픽 설정을 현재 값으로 복원한다.
	Settings = *GraphicsSaveData;
	// 버전 필드 이전 저장 데이터이면 새 필드를 보정한다.
	if (Settings.SaveVersion < MDGraphicsSettingCategory::CurrentSaveVersion)
	{
		MigrateLegacySaveData();
	}
	// 그래픽 설정 복원이 완료되었음을 반환한다.
	return true;
}

void UMD_GraphicsSettingCategory::MigrateLegacySaveData()
{
	// 기본값과 같은 필드는 저장되지 않아, 이전 기본값(VSync 켬·60 FPS)을 유지한 사용자는 새 기본값을 받는다.
	// 이전 기본값과 다른 프레임 제한을 직접 고른 사용자는 모니터 주사율 대신 그 값을 유지한다.
	if (Settings.FrameRateLimit != MDGraphicsSettingCategory::LegacyDefaultFrameRateLimit)
	{
		Settings.bLimitToDisplayRefreshRate = false;
	}
	// 이전에는 낮음 품질이 렌더 비율 50%를 함께 적용했으므로 같은 비용의 성능 단계를 유지한다.
	if (Settings.OverallQuality == 0)
	{
		Settings.RenderResolution = EMD_RenderResolutionMode::Performance;
	}
	// 보정이 끝난 데이터를 현재 저장 형식 버전으로 표시한다.
	Settings.SaveVersion = MDGraphicsSettingCategory::CurrentSaveVersion;
}

EMD_SettingsCategory UMD_GraphicsSettingCategory::GetCategory()
{
	// 이 인스턴스가 그래픽 설정을 담당함을 직접 반환한다.
	return EMD_SettingsCategory::Graphics;
}

float UMD_GraphicsSettingCategory::ToScreenPercentage(EMD_RenderResolutionMode RenderResolution)
{
	// 엔진 [ResolutionQuality] 프리셋 중 자동 비율을 뜻하는 이름을 기본값으로 둔다.
	FString PresetName = TEXT("Default");
	// 렌더 해상도 단계에 대응하는 엔진 프리셋 이름을 고른다.
	switch (RenderResolution)
	{
	case EMD_RenderResolutionMode::Native:
		PresetName = TEXT("Native");
		break;
	case EMD_RenderResolutionMode::Quality:
		PresetName = TEXT("Quality");
		break;
	case EMD_RenderResolutionMode::Balanced:
		PresetName = TEXT("Balanced");
		break;
	case EMD_RenderResolutionMode::Performance:
		PresetName = TEXT("Performance");
		break;
	default:
		break;
	}
	// 엔진 스케일러빌리티 ini에 정의된 해상도 프리셋을 순회한다.
	for (const Scalability::FResolutionPreset& Preset : Scalability::GetResolutionPresets())
	{
		// 이름이 같은 프리셋의 화면 비율을 반환한다.
		if (Preset.Name == PresetName)
		{
			return Preset.ResolutionQuality;
		}
	}
	// 프리셋을 찾지 못하면 엔진 자동 비율을 사용한다.
	return Scalability::MinResolutionScale;
}

float UMD_GraphicsSettingCategory::GetScreenPercentage() const
{
	// TAA는 낮은 해상도에서 업스케일 화질이 떨어지므로 화면 비율을 100으로 고정하며 1440p 상한 안전망은 그대로 적용된다.
	if (Settings.Upscaler == EMD_UpscalerMode::TAA)
	{
		return Scalability::MaxResolutionScale;
	}
	// TSR은 선택한 렌더 해상도 단계의 화면 비율을 사용한다.
	return ToScreenPercentage(Settings.RenderResolution);
}

float UMD_GraphicsSettingCategory::GetEffectiveFrameRateLimit() const
{
	// 모니터 주사율 옵션을 끈 경우 사용자가 고른 프레임 제한을 사용한다.
	if (Settings.bLimitToDisplayRefreshRate == false)
	{
		return Settings.FrameRateLimit;
	}
	// 엔진 프레임 페이서와 같은 기준인 주 모니터의 현재 주사율(최소 60)을 프레임 제한으로 사용한다.
	return static_cast<float>(FPlatformMisc::GetMaxRefreshRate());
}

void UMD_GraphicsSettingCategory::RefreshRayTracing()
{
	// 현재 적용된 GI 품질을 읽을 엔진 사용자 설정 객체를 찾는다.
	UGameUserSettings* GameUserSettings = UGameUserSettings::GetGameUserSettings();
	// 엔진 사용자 설정을 사용할 수 없으면 갱신을 중단한다.
	if (IsValid(GameUserSettings) == false)
	{
		return;
	}
	// 바뀔 레이 트레이싱 사용 여부를 받을 값을 준비한다.
	bool bEnableRayTracing = true;
	// 바꿔야 할 때만 레이 트레이싱 사용 여부 콘솔 변수를 받는다.
	IConsoleVariable* RayTracingSwitch = FindRayTracingSwitch(GameUserSettings->GetGlobalIlluminationQuality(), bEnableRayTracing);
	// 현재 상태가 이미 맞으면 렌더 상태를 다시 만들지 않는다.
	if (RayTracingSwitch == nullptr)
	{
		return;
	}
	// 얼룩 RT 상주 관리는 레이 트레이싱 전환 전에 모든 단계를 복원해야 한다.
	MD_StainRayTracingResidency::RestoreAll();
	// 콘솔과 커맨드라인이 덮어쓸 수 있는 게임 설정 우선순위로 레이 트레이싱 사용 여부를 바꾼다.
	RayTracingSwitch->Set(bEnableRayTracing == true ? 1 : 0, ECVF_SetByGameSetting);
}

void UMD_GraphicsSettingCategory::ReleaseUpscalerOverride()
{
	// 업스케일러를 결정하는 안티에일리어싱 방식 콘솔 변수를 찾는다.
	IConsoleVariable* AntiAliasingMethod = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod"));
	// 콘솔 변수를 찾지 못하면 해제를 중단한다.
	if (AntiAliasingMethod == nullptr)
	{
		return;
	}
	// GameOverride 값을 지워 기록에 남은 프로젝트 설정 값이 다시 적용되게 한다.
	AntiAliasingMethod->Unset(ECVF_SetByGameOverride);
}

bool UMD_GraphicsSettingCategory::CanRunHardwareBenchmark()
{
	// 커맨드릿 실행에서는 감지를 건너뛴다.
	if (IsRunningCommandlet() == true)
	{
		return false;
	}
	// 데디케이티드 서버는 그래픽 설정이 필요 없으므로 감지를 건너뛴다.
	if (IsRunningDedicatedServer() == true)
	{
		return false;
	}
	// 렌더링하지 않는 실행은 GPU 지수를 측정할 수 없어 잘못된 결과를 내므로 제외한다.
	if (FApp::CanEverRender() == false)
	{
		return false;
	}
	// 모든 조건을 통과하면 감지 가능 결과를 반환한다.
	bool bCanRun = true;
	// 감지 가능 여부를 호출부에 전달한다.
	return bCanRun;
}

bool UMD_GraphicsSettingCategory::DetectRecommendation()
{
	// 하드웨어 벤치마크를 실행할 수 없는 환경이면 감지를 중단한다.
	if (CanRunHardwareBenchmark() == false)
	{
		return false;
	}
	// 벤치마크를 실행할 엔진 사용자 설정 객체를 찾는다.
	UGameUserSettings* GameUserSettings = UGameUserSettings::GetGameUserSettings();
	// 엔진 사용자 설정을 사용할 수 없으면 감지를 중단한다.
	if (IsValid(GameUserSettings) == false)
	{
		return false;
	}
	// 벤치마크가 덮어쓰는 엔진 사용자 설정의 대기 품질값을 보관한다.
	Scalability::FQualityLevels PreviousQualityLevels = GameUserSettings->ScalabilityQuality;
	// CPU와 GPU 성능 지수로 품질 단계를 계산하며 엔진 CVar는 바꾸지 않는다.
	GameUserSettings->RunHardwareBenchmark();
	// 감지 결과를 기록할 권장값을 찾는다.
	FMD_GraphicsRecommendation& Recommendation = Settings.Recommendation;
	// 모든 품질 그룹이 같은 단계로 나왔으면 그 단계를 전체 품질로 사용한다.
	int32 OverallQuality = GameUserSettings->GetOverallScalabilityLevel();
	// 그룹별 단계가 섞여 전체 품질이 없으면 가장 낮은 그룹 단계를 사용한다.
	if (OverallQuality < 0)
	{
		OverallQuality = GameUserSettings->ScalabilityQuality.GetMinQualityLevel();
	}
	// 감지한 전체 품질 단계를 기록한다.
	Recommendation.OverallQuality = OverallQuality;
	// 감지한 그림자 품질 단계를 기록한다.
	Recommendation.ShadowQuality = GameUserSettings->GetShadowQuality();
	// 감지한 효과 품질 단계를 기록한다.
	Recommendation.EffectsQuality = GameUserSettings->GetVisualEffectQuality();
	// 감지한 후처리 품질 단계를 기록한다.
	Recommendation.PostProcessQuality = GameUserSettings->GetPostProcessingQuality();
	// GPU 지수로 정해진 해상도 품질이 성능 단계 비율 이하인지 확인한다.
	bool bNeedsPerformanceResolution = GameUserSettings->ScalabilityQuality.ResolutionQuality <= ToScreenPercentage(EMD_RenderResolutionMode::Performance);
	// 가장 낮은 GPU 단계는 렌더 비율 50%를 유지하고 그 외에는 엔진 자동 비율을 사용한다.
	Recommendation.RenderResolution = bNeedsPerformanceResolution == true ? EMD_RenderResolutionMode::Performance : EMD_RenderResolutionMode::Auto;
	// 감지 결과가 기록되었음을 표시한다.
	Recommendation.bIsDetected = true;
	// 초기화 미리보기처럼 적용하지 않는 감지가 이후 엔진 적용에 섞이지 않도록 대기 품질값을 되돌린다.
	GameUserSettings->ScalabilityQuality = PreviousQualityLevels;
	// 감지 결과를 로그로 남긴다.
	UE_LOG(MarineDemolition, Log, TEXT("%s 하드웨어 감지: CPU %.1f, GPU %.1f, 전체 %d, 그림자 %d, 효과 %d, 후처리 %d, 렌더 해상도 %s"), *UE_CALL_LOCATIONINFO,
		GameUserSettings->GetLastCPUBenchmarkResult(), GameUserSettings->GetLastGPUBenchmarkResult(), Recommendation.OverallQuality, Recommendation.ShadowQuality,
		Recommendation.EffectsQuality, Recommendation.PostProcessQuality, bNeedsPerformanceResolution == true ? TEXT("성능") : TEXT("자동"));
	// 감지 성공을 반환한다.
	return true;
}

void UMD_GraphicsSettingCategory::ApplyRecommendation(FMD_GraphicsSettings& TargetSettings)
{
	// 감지 기록이 없으면 대상 설정의 기존 값을 유지한다.
	if (TargetSettings.Recommendation.bIsDetected == false)
	{
		return;
	}
	// 감지한 전체 품질 단계를 적용한다.
	TargetSettings.OverallQuality = TargetSettings.Recommendation.OverallQuality;
	// 감지한 그림자 품질 단계를 적용한다.
	TargetSettings.ShadowQuality = TargetSettings.Recommendation.ShadowQuality;
	// 감지한 효과 품질 단계를 적용한다.
	TargetSettings.EffectsQuality = TargetSettings.Recommendation.EffectsQuality;
	// 감지한 후처리 품질 단계를 적용한다.
	TargetSettings.PostProcessQuality = TargetSettings.Recommendation.PostProcessQuality;
	// 감지한 GPU 단계에 맞춘 렌더 해상도 단계를 적용한다.
	TargetSettings.RenderResolution = TargetSettings.Recommendation.RenderResolution;
}

FMD_GraphicsSettings UMD_GraphicsSettingCategory::MakeResetSettings()
{
	// 감지 기록이 없는 기존 저장 데이터이면 지금 하드웨어를 감지해 기록한다.
	if (Settings.Recommendation.bIsDetected == false)
	{
		// 감지 결과의 명시적 소비이며 실패하면 프로젝트 기본값을 사용한다.
		bool bDetected = DetectRecommendation();
		// 감지 실패 시 프로젝트 기본값 유지
		(void)bDetected;
	}
	// 품질 외 항목에 사용할 프로젝트 기본값을 준비한다.
	FMD_GraphicsSettings ResetSettings;
	// 초기화 뒤에도 감지 기록이 사라지지 않도록 옮긴다.
	ResetSettings.Recommendation = Settings.Recommendation;
	// 감지 기록이 있으면 품질과 렌더 해상도를 감지값으로 덮어쓴다.
	ApplyRecommendation(ResetSettings);
	// 완성한 초기화 값을 반환한다.
	return ResetSettings;
}

void UMD_GraphicsSettingCategory::ApplyScreenPercentage(float ScreenPercentage)
{
	// 렌더 해상도 비율 콘솔 변수를 찾는다.
	IConsoleVariable* ScreenPercentageVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
	// 콘솔 변수를 찾지 못하면 적용을 중단한다.
	if (ScreenPercentageVariable == nullptr)
	{
		return;
	}
	// 엔진은 해상도 품질 0을 무시해 이전 수동 비율이 남으므로 스케일러빌리티와 같은 우선순위로 직접 덮어쓴다.
	ScreenPercentageVariable->Set(ScreenPercentage, ECVF_SetByScalability);
}

void UMD_GraphicsSettingCategory::ApplyUpscaler()
{
	// 업스케일러를 결정하는 안티에일리어싱 방식 콘솔 변수를 찾는다.
	IConsoleVariable* AntiAliasingMethod = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod"));
	// 콘솔 변수를 찾지 못하면 적용을 중단한다.
	if (AntiAliasingMethod == nullptr)
	{
		return;
	}
	// 선택한 업스케일러에 대응하는 엔진 안티에일리어싱 방식을 고른다.
	EAntiAliasingMethod Method = Settings.Upscaler == EMD_UpscalerMode::TAA ? AAM_TemporalAA : AAM_TSR;
	// 프로젝트 설정(ProjectSetting) 값을 이기는 GameOverride 우선순위로 적용한다.
	AntiAliasingMethod->Set(static_cast<int32>(Method), ECVF_SetByGameOverride);
}

bool UMD_GraphicsSettingCategory::IsMegaLightsRequested()
{
	// 품질 단계가 MegaLights를 허용하는지 나타내는 콘솔 변수를 찾는다.
	IConsoleVariable* MegaLightsAllowed = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MegaLights.Allowed"));
	// MegaLights가 허용되지 않으면 사용하지 않는 것으로 본다.
	if (MegaLightsAllowed == nullptr || MegaLightsAllowed->GetInt() == 0)
	{
		return false;
	}
	// 프로젝트 전체 MegaLights 사용 여부 콘솔 변수를 찾는다.
	IConsoleVariable* MegaLightsEnableForProject = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MegaLights.EnableForProject"));
	// 프로젝트 전체에서 켜져 있으면 사용 중으로 본다.
	if (MegaLightsEnableForProject != nullptr && MegaLightsEnableForProject->GetInt() != 0)
	{
		return true;
	}
	// 포스트 프로세스 볼륨별 사용 여부를 확인할 현재 월드를 찾는다.
	UWorld* World = GetWorld();
	// 게임 시작 직후처럼 월드가 없으면 볼륨 사용이 없는 것으로 본다.
	if (IsValid(World) == false)
	{
		return false;
	}
	// 현재 월드의 포스트 프로세스 볼륨을 순회한다.
	for (IInterface_PostProcessVolume& PostProcessVolume : World->GetPostProcessVolumeIterator())
	{
		// 볼륨의 활성 여부와 포스트 프로세스 설정을 읽는다.
		FPostProcessVolumeProperties VolumeProperties = PostProcessVolume.GetProperties();
		// 꺼져 있거나 설정이 없는 볼륨은 건너뛴다.
		if (VolumeProperties.bIsEnabled == false || VolumeProperties.Settings == nullptr)
		{
			continue;
		}
		// MegaLights를 직접 켠 볼륨이 있으면 사용 중으로 본다.
		if (VolumeProperties.Settings->bOverride_bMegaLights == 1 && VolumeProperties.Settings->bMegaLights == 1)
		{
			return true;
		}
	}
	// MegaLights를 요청하는 곳이 없으면 사용하지 않는 것으로 본다.
	return false;
}

IConsoleVariable* UMD_GraphicsSettingCategory::FindRayTracingSwitch(int32 GlobalIlluminationQuality, bool& bOutEnableRayTracing)
{
#if RHI_RAYTRACING
	// 에디터에서는 PIE 종료 뒤에도 값이 남아 에디터 뷰포트까지 바뀌므로 독립 실행에서만 전환한다.
	if (GIsEditor == true)
	{
		return nullptr;
	}
	// 렌더링하지 않는 서버이거나 RHI 초기화 전이면 레이 트레이싱 상태를 확인하지 않는다.
	if (FApp::CanEverRender() == false || GIsRHIInitialized == false)
	{
		return nullptr;
	}
	// 프로젝트가 레이 트레이싱을 허용하고 실행 중 전환을 지원하는 경우에만 처리한다.
	if (IsRayTracingAllowed() == false || IsRayTracingEnableOnDemandSupported() == false)
	{
		return nullptr;
	}
	// 레이 트레이싱 전체 사용 여부 콘솔 변수를 찾는다.
	IConsoleVariable* RayTracingEnable = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.Enable"));
	// 콘솔 변수를 찾지 못하면 전환하지 않는다.
	if (RayTracingEnable == nullptr)
	{
		return nullptr;
	}
	// 높은 GI 품질에서만 Lumen이 하드웨어 레이 트레이싱을 쓰는지 확인한다.
	bool bUsesLumenRayTracing = GlobalIlluminationQuality > MDGraphicsSettingCategory::MaxGlobalIlluminationQualityWithoutRayTracing;
	// Lumen이나 하드웨어 레이 트레이싱이 필요한 MegaLights 중 하나라도 쓰면 레이 트레이싱을 유지한다.
	bOutEnableRayTracing = bUsesLumenRayTracing == true || IsMegaLightsRequested() == true;
	// 현재 레이 트레이싱 사용 여부를 확인한다.
	bool bIsRayTracingEnabled = RayTracingEnable->GetInt() != 0;
	// 같은 값으로 설정해도 전체 렌더 상태를 다시 만들므로 상태가 바뀔 때만 콘솔 변수를 반환한다.
	if (bOutEnableRayTracing == bIsRayTracingEnabled)
	{
		return nullptr;
	}
	// 전환할 레이 트레이싱 사용 여부 콘솔 변수를 반환한다.
	return RayTracingEnable;
#else
	// 레이 트레이싱을 지원하지 않는 빌드에서는 전환하지 않는다.
	return nullptr;
#endif
}
