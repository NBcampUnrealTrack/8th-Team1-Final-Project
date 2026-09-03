#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MD_ComfortSettings.generated.h"

/** 사용자 설정을 건드릴지 결정한다. Unchanged면 현재 값을 그대로 둔다. */
UENUM(BlueprintType)
enum class EMD_ComfortOverride : uint8
{
	Unchanged UMETA(DisplayName = "건드리지 않음"),
	ForceOff UMETA(DisplayName = "강제 끔"),
	ForceOn UMETA(DisplayName = "강제 켬")
};

/**
 * Project Settings -> Game -> MD Comfort에서 3D 멀미(시뮬레이터 멀미) 완화 값을 조정한다.
 * 값은 로컬 플레이어에게만 적용되며, UMD_ComfortComponent가 BeginPlay에서 읽어 반영한다.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "MD Comfort"))
class MARINEDEMOLITION_API UMD_ComfortSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const UMD_ComfortSettings* Get();

	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }
	virtual FName GetSectionName() const override { return FName(TEXT("MD Comfort")); }

	// ---------------------------------------------------------------- 모션 블러

	/**
	 * 켜면 로컬 플레이어 카메라에서 모션 블러를 MotionBlurAmount로 덮어쓴다.
	 * 레벨의 PostProcessVolume을 수정하지 않고 카메라 후처리로만 눌러서, 맵 에셋 충돌을 피한다.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Motion Blur")
	bool bOverrideMotionBlur = true;

	/** 모션 블러 강도. 0이면 완전히 끈다. 멀미 완화 효과가 가장 큰 항목이다. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Motion Blur",
		meta = (EditCondition = "bOverrideMotionBlur", ClampMin = "0.0", ClampMax = "1.0",
			UIMin = "0.0", UIMax = "1.0"))
	float MotionBlurAmount = 0.f;

	// ------------------------------------------------------------------- 시야각

	/** 켜면 로컬 플레이어 카메라의 시야각을 FieldOfView로 덮어쓴다. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Field Of View")
	bool bOverrideFieldOfView = false;

	/**
	 * 수평 시야각. 좁으면 멀미가 늘고 넓으면 원근 왜곡이 늘어난다.
	 * 기본값은 현재 카메라와 같은 90이며, 95~105 사이에서 비교해 보기를 권한다.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Field Of View",
		meta = (EditCondition = "bOverrideFieldOfView", ClampMin = "70.0", ClampMax = "120.0",
			UIMin = "80.0", UIMax = "110.0"))
	float FieldOfView = 90.f;

	// ------------------------------------------------------------- 시야 회전 입력

	/**
	 * 마우스 입력 응답 커브 지수. 1이면 선형이라 엔진 기본 동작과 완전히 같다.
	 * 1보다 크면 작게 움직일 때 더 천천히(정밀하게), 크게 움직일 때는 그대로 돌아간다.
	 * 프레임 지연을 만들지 않으므로 멀미를 늘리지 않는다.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Look",
		meta = (ClampMin = "1.0", ClampMax = "2.5", UIMin = "1.0", UIMax = "2.0"))
	float LookResponseExponent = 1.2f;

	/**
	 * 응답 커브에서 '전속' 취급할 프레임당 입력 크기. 이 값 이상은 커브가 감쇠하지 않는다.
	 * 마우스 감도와 DPI에 따라 적정값이 달라지므로, 정밀 구간이 너무 무겁게 느껴지면 낮춘다.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Look",
		meta = (ClampMin = "0.1", UIMin = "0.5", UIMax = "20.0"))
	float LookResponseReferenceDelta = 4.f;

	/**
	 * 시간 기반 시야 스무딩의 반감기(초). 0이면 끈다.
	 * 주의: 이 값을 올리면 카메라는 부드러워지지만 입력 지연이 생겨 오히려 멀미가 늘 수 있다.
	 * 총 회전량은 보존되므로 조준 위치가 밀리지는 않는다. 0.02~0.06부터 시도할 것.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Look",
		meta = (ClampMin = "0.0", ClampMax = "0.25", UIMin = "0.0", UIMax = "0.1"))
	float LookSmoothingHalfLife = 0.f;

	// -------------------------------------------------------------------- 프레임

	/**
	 * 0보다 크면 최대 프레임을 이 값으로 제한한다.
	 * 프레임이 들쭉날쭉한 것이 절대 프레임 수보다 멀미에 더 나쁘므로, 안정적으로 유지 가능한 값을 잡는다.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Frame Pacing",
		meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "240.0"))
	float FrameRateLimit = 0.f;

	/**
	 * VSync를 강제할지 여부. Unchanged면 사용자 설정을 그대로 둔다.
	 * 주의: 에디터는 r.VSync를 더 높은 우선순위로 잡고 있어서 PIE에서는 반영되지 않는다.
	 * 패키징된 빌드나 스탠드얼론에서만 실제로 적용된다.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Frame Pacing")
	EMD_ComfortOverride VSyncOverride = EMD_ComfortOverride::Unchanged;
};
