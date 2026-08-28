#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MD_AudioSettings.generated.h"

class UMD_AudioLibrary;
class USoundSubmix;
class USubmixEffectFilterPreset;

/**
 * 오디오 시스템 프로젝트 설정. Project Settings → Game → MD Audio 에 나온다.
 *
 * 수심 → LPF 컷오프 매핑 수치는 귀로 맞추는 값이라 코드 상수로 박지 않고 여기에 둔다.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "MD Audio"))
class MARINEDEMOLITION_API UMD_AudioSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMD_AudioSettings();

	static const UMD_AudioSettings& Get();

	/** 이벤트 태그 인덱스를 만들 카탈로그 */
	UPROPERTY(EditAnywhere, Config, Category = "카탈로그", meta = (AllowedClasses = "/Script/MarineDemolition.MD_AudioLibrary"))
	TSoftObjectPtr<UMD_AudioLibrary> AudioLibrary;

	UPROPERTY(EditAnywhere, Config, Category = "서브믹스")
	TSoftObjectPtr<USoundSubmix> UnderwaterSubmix;

	UPROPERTY(EditAnywhere, Config, Category = "서브믹스")
	TSoftObjectPtr<USoundSubmix> DrySubmix;

	/** 수심에 따라 컷오프를 움직일 필터 프리셋 */
	UPROPERTY(EditAnywhere, Config, Category = "수중 필터")
	TSoftObjectPtr<USubmixEffectFilterPreset> UnderwaterFilter;

	/** 수면의 월드 Z. L_Ocean 의 WaterBodyCustom 이 0 이라 기본값도 0 이다 */
	UPROPERTY(EditAnywhere, Config, Category = "수중 필터")
	float WaterSurfaceZ = 0.0f;

	/** 수면 깊이 0 에서의 컷오프(Hz) */
	UPROPERTY(EditAnywhere, Config, Category = "수중 필터", meta = (ClampMin = "200.0", ClampMax = "20000.0"))
	float CutoffAtSurface = 2000.0f;

	/** MaxDepth 이상에서의 컷오프(Hz) */
	UPROPERTY(EditAnywhere, Config, Category = "수중 필터", meta = (ClampMin = "100.0", ClampMax = "20000.0"))
	float CutoffAtMaxDepth = 800.0f;

	/** 컷오프가 최저값에 도달하는 수심(cm) */
	UPROPERTY(EditAnywhere, Config, Category = "수중 필터", meta = (ClampMin = "100.0"))
	float MaxDepthCm = 3000.0f;

	/**
	 * 컷오프 보간 속도. 값이 낮으면 부드럽지만 반응이 늦고,
	 * 높으면 즉각적이지만 빠른 상하 이동에서 필터가 훑는 소리가 들린다.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "수중 필터", meta = (ClampMin = "0.1", ClampMax = "20.0"))
	float CutoffInterpSpeed = 3.0f;

	/**
	 * 수심 → 컷오프를 로그(옥타브) 공간에서 보간한다.
	 * 사람의 음높이 지각이 로그라서, 켜 두면 깊이 변화가 균일하게 느껴진다.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "수중 필터")
	bool bLogarithmicCutoffBlend = true;

	/** 수면에서의 피치 배율 */
	UPROPERTY(EditAnywhere, Config, Category = "수중 피치", meta = (ClampMin = "0.2", ClampMax = "2.0"))
	float PitchAtSurface = 1.0f;

	/** MaxDepthCm 이상에서의 피치 배율. 너무 낮추면 소리가 눈에 띄게 느려진다 */
	UPROPERTY(EditAnywhere, Config, Category = "수중 피치", meta = (ClampMin = "0.2", ClampMax = "2.0"))
	float PitchAtMaxDepth = 0.85f;

	/**
	 * 물속으로 볼 캡슐 잠김 비율. `UMD_OxygenComponent` 와 같은 기준을 쓴다.
	 * 이 값보다 낮으면 머리를 내민 것으로 보고 수중 앰비를 끈다.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "수중 필터", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SubmergedImmersionThreshold = 0.5f;

	/** 컷오프 갱신 주기(초). 0 이면 매 프레임. 0.1 정도로 충분하다 */
	UPROPERTY(EditAnywhere, Config, Category = "수중 필터", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float CutoffUpdateInterval = 0.05f;
};
