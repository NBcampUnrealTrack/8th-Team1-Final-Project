#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/GenericWindow.h"
#include "InputCoreTypes.h"
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

// 렌더링 해상도를 출력 해상도로 키우는 업스케일러를 구분한다.
UENUM(BlueprintType)
enum class EMD_UpscalerMode : uint8
{
	// 낮은 렌더 해상도에서도 화질을 유지하는 Temporal Super Resolution을 사용한다.
	TSR UMETA(DisplayName = "TSR"),

	// 화면 비율을 100으로 고정하는 Temporal AA를 사용한다.
	TAA UMETA(DisplayName = "TAA")
};

// 출력 해상도 대비 실제 렌더링 해상도 단계를 구분하며 비율은 엔진 [ResolutionQuality] 프리셋과 같다.
// 모든 단계에 r.ScreenPercentage.MaxResolution 상한(16:9 환산 1440p)이 적용되어 4K에서는 네이티브도 66.7%로 렌더링된다.
UENUM(BlueprintType)
enum class EMD_RenderResolutionMode : uint8
{
	// 엔진이 디스플레이 해상도에 맞춰 렌더링 비율을 고른다.
	Auto UMETA(DisplayName = "자동"),

	// 출력 해상도 그대로 렌더링한다.
	Native UMETA(DisplayName = "네이티브"),

	// 출력 해상도의 66.7%로 렌더링한다.
	Quality UMETA(DisplayName = "품질"),

	// 출력 해상도의 58.3%로 렌더링한다.
	Balanced UMETA(DisplayName = "균형"),

	// 출력 해상도의 50%로 렌더링한다.
	Performance UMETA(DisplayName = "성능")
};

// 하드웨어 벤치마크로 감지한 그래픽 권장값을 묶으며 그래픽 초기화의 기준값으로 사용한다.
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_GraphicsRecommendation
{
	GENERATED_BODY()

	// 하드웨어 감지 결과가 기록되어 있는지 저장한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "그래픽")
	bool bIsDetected = false;

	// 감지한 전체 그래픽 품질 단계를 저장한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "그래픽", meta = (ClampMin = "0", ClampMax = "4"))
	int32 OverallQuality = 3;

	// 감지한 그림자 품질 단계를 저장한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "그래픽", meta = (ClampMin = "0", ClampMax = "4"))
	int32 ShadowQuality = 3;

	// 감지한 효과 품질 단계를 저장한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "그래픽", meta = (ClampMin = "0", ClampMax = "4"))
	int32 EffectsQuality = 3;

	// 감지한 후처리 품질 단계를 저장한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "그래픽", meta = (ClampMin = "0", ClampMax = "4"))
	int32 PostProcessQuality = 3;

	// 감지한 GPU 단계에 맞춘 렌더 해상도 단계를 저장한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "그래픽")
	EMD_RenderResolutionMode RenderResolution = EMD_RenderResolutionMode::Auto;
};

// 엔진 사용자 설정과 연동할 그래픽 설정값을 묶는다.
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_GraphicsSettings
{
	GENERATED_BODY()

	// 게임 화면을 표시할 창 모드를 저장한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "그래픽")
	TEnumAsByte<EWindowMode::Type> WindowMode = EWindowMode::WindowedFullscreen;

	// 게임 화면에 적용할 출력 해상도를 저장한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "그래픽")
	FIntPoint ScreenResolution = FIntPoint(1920, 1080);

	// 전체 그래픽 품질 단계를 저장한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "그래픽", meta = (ClampMin = "0", ClampMax = "4"))
	int32 OverallQuality = 3;

	// 그림자 렌더링 품질 단계를 저장한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "그래픽", meta = (ClampMin = "0", ClampMax = "4"))
	int32 ShadowQuality = 3;

	// 파티클과 시각 효과의 품질 단계를 저장한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "그래픽", meta = (ClampMin = "0", ClampMax = "4"))
	int32 EffectsQuality = 3;

	// 화면 후처리 효과의 품질 단계를 저장한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "그래픽", meta = (ClampMin = "0", ClampMax = "4"))
	int32 PostProcessQuality = 3;

	// 렌더링 해상도를 출력 해상도로 키우는 업스케일러를 저장한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "그래픽")
	EMD_UpscalerMode Upscaler = EMD_UpscalerMode::TSR;

	// 출력 해상도 대비 렌더링 해상도 단계를 저장하며 TAA에서는 화면 비율 100으로 고정된다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "그래픽")
	EMD_RenderResolutionMode RenderResolution = EMD_RenderResolutionMode::Auto;

	// 화면 찢어짐을 줄이는 수직 동기화 사용 여부를 저장한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "그래픽")
	bool bUseVSync = false;

	// 프레임 제한을 모니터 주사율에 맞출지 저장하며 끄면 FrameRateLimit 값을 사용한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "그래픽")
	bool bLimitToDisplayRefreshRate = true;

	// 게임이 사용할 최대 프레임 속도를 저장하며 0은 제한 없음을 의미한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "그래픽", meta = (ClampMin = "0.0", ClampMax = "360.0"))
	float FrameRateLimit = 60.0f;

	// 첫 실행이나 초기화 때 하드웨어 벤치마크로 감지한 권장값을 저장한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "그래픽")
	FMD_GraphicsRecommendation Recommendation;

	// 저장 데이터 형식 버전을 보관하며 기본값과 같은 필드는 저장되지 않으므로 0은 버전 필드 이전 저장을 의미한다.
	UPROPERTY(SaveGame)
	int32 SaveVersion = 0;
};

// 게임에서 사용하는 소리 종류별 음량을 묶는다.
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_AudioUserSettings
{
	GENERATED_BODY()

	// 게임 소리를 재생할 출력 장치 ID를 저장하며 빈 값은 시스템 기본 장치를 의미한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "소리")
	FString OutputDeviceId;

	// 음성 입력에 사용할 장치 ID를 저장하며 빈 값은 시스템 기본 장치를 의미한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "소리")
	FString InputDeviceId;

	// 모든 소리에 공통으로 적용할 50% 기본 전체 음량
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "소리", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MasterVolume = 0.5f;

	// 배경 음악에 적용할 50% 기본 음량
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "소리", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MusicVolume = 0.5f;

	// 게임 효과음에 적용할 50% 기본 음량
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "소리", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EffectsVolume = 0.5f;

	// 음성 입력 처리에 사용할 정규화 감도값 보관
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "소리", meta = (DisplayName = "음성 입력 감도", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float VoiceInputSensitivity = 0.5f;

	// 지정된 키를 누르는 동안만 음성을 전송할지 선택하는 설정값
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "소리", meta = (DisplayName = "Push to Talk"))
	bool bUsePushToTalk = true;
};

// 플레이어의 시점 조작에 사용되는 설정값을 묶는다.
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_ControlSettings
{
	GENERATED_BODY()

	// 점프와 수영 상승에 사용할 입력 키
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "조작")
	FKey JumpKey = EKeys::SpaceBar;

	// 대상 상호작용에 사용할 입력 키
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "조작")
	FKey InteractKey = EKeys::E;

	// 현재 장비의 능력 사용에 사용할 입력 키
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "조작")
	FKey AbilityKey = EKeys::LeftMouseButton;

	// 다음 장비 교체에 사용할 입력 키
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "조작")
	FKey SwapEquipmentKey = EKeys::MouseWheelAxis;

	// 음성 채팅 송신에 사용할 입력 키
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "조작")
	FKey VoiceChatKey = EKeys::V;

	// 여섯 개 핫바 슬롯의 직접 선택에 사용할 입력 키 목록
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "조작")
	TArray<FKey> HotbarSlotKeys = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six};

	// 마우스 입력에 곱할 감도 배율을 저장한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "조작", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float MouseSensitivity = 1.0f;

	// 마우스의 세로 시점 입력을 반대로 적용할지 저장한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "조작")
	bool bInvertVerticalLook = false;
};
