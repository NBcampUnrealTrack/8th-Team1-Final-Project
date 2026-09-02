#pragma once

#include "CoreMinimal.h"
#include "MD_SettingsTypes.generated.h"

// 통합 설정 화면에서 관리하는 카테고리를 구분한다.
UENUM(BlueprintType)
enum class EMD_SettingsCategory : uint8
{
	// 화면과 성능에 관련된 설정을 나타낸다.
	Graphics UMETA(DisplayName = "그래픽"),

	// 전체 음량과 소리 종류별 음량 설정을 나타낸다.
	Audio UMETA(DisplayName = "소리"),

	// 마우스 감도와 시점 반전 같은 조작 설정을 나타낸다.
	Controls UMETA(DisplayName = "조작")
};

// 엔진 사용자 설정과 연동할 그래픽 설정값을 묶는다.
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_GraphicsSettings
{
	GENERATED_BODY()

	// 전체 그래픽 품질 단계를 저장한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "그래픽", meta = (ClampMin = "0", ClampMax = "4"))
	int32 OverallQuality = 3;

	// 화면 찢어짐을 줄이는 수직 동기화 사용 여부를 저장한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "그래픽")
	bool bUseVSync = true;

	// 게임이 사용할 최대 프레임 속도를 저장하며 0은 제한 없음을 의미한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "그래픽", meta = (ClampMin = "0.0", ClampMax = "360.0"))
	float FrameRateLimit = 60.0f;
};

// 게임에서 사용하는 소리 종류별 음량을 묶는다.
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_AudioUserSettings
{
	GENERATED_BODY()

	// 모든 소리에 공통으로 적용할 전체 음량을 저장한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "소리", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MasterVolume = 1.0f;

	// 배경 음악에 적용할 음량을 저장한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "소리", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MusicVolume = 1.0f;

	// 게임 효과음에 적용할 음량을 저장한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "소리", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EffectsVolume = 1.0f;

	// 음성 재생에 적용할 음량을 저장한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "소리", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float VoiceVolume = 1.0f;
};

// 플레이어의 시점 조작에 사용되는 설정값을 묶는다.
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_ControlSettings
{
	GENERATED_BODY()

	// 마우스 입력에 곱할 감도 배율을 저장한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "조작", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float MouseSensitivity = 1.0f;

	// 마우스의 세로 시점 입력을 반대로 적용할지 저장한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "조작")
	bool bInvertVerticalLook = false;
};
