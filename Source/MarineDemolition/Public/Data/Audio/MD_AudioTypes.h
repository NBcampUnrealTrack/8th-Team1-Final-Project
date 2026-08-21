#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "MD_AudioTypes.generated.h"

class USoundBase;
class USoundAttenuation;
class USoundConcurrency;

/**
 * 효과음 카테고리. `Docs/공통 기획서/10 효과음 리스트.md` 의 A~L 코드와 1:1 대응하며,
 * 에셋 폴더(`Content/SFX/<코드>_*`)와 데이터 에셋 분할 단위를 함께 규정한다.
 */
UENUM(BlueprintType)
enum class EMD_AudioCategory : uint8
{
	/** A. 청소 · 헤라 */
	Clean UMETA(DisplayName = "A 청소·헤라"),

	/** B. 절단 · 철거 */
	Cut UMETA(DisplayName = "B 절단·철거"),

	/** C. 사고 · 안전 (붕괴 · 전선 · 수도관) */
	Hazard UMETA(DisplayName = "C 사고·안전"),

	/** D. 운반 · 쓰레기통 */
	Carry UMETA(DisplayName = "D 운반·쓰레기통"),

	/** E. 캐릭터 · 이동 · 생존 */
	Character UMETA(DisplayName = "E 캐릭터·생존"),

	/** F. 도구 · 핫바 · 스캐너 */
	Tool UMETA(DisplayName = "F 도구·핫바"),

	/** G. 샘플 */
	Sample UMETA(DisplayName = "G 샘플"),

	/** H. 모선 · 분류 · 정산 */
	Mothership UMETA(DisplayName = "H 모선·정산"),

	/** I. 상점 · 상인 */
	Merchant UMETA(DisplayName = "I 상점·상인"),

	/** J. UI · 시간 · 판정 */
	UI UMETA(DisplayName = "J UI·시간·판정"),

	/** K. 월드 · 문 · 섹터 */
	World UMETA(DisplayName = "K 월드·섹터"),

	/** L. 앰비언스 */
	Ambience UMETA(DisplayName = "L 앰비언스")
};

/**
 * 재생 공간. 수중 필터를 에셋에 굽지 않고 서브믹스로 처리하기 위한 라우팅 구분이다.
 * 소재 자체는 항상 드라이하게 만들고, 물속 느낌은 Underwater 서브믹스가 만든다.
 */
UENUM(BlueprintType)
enum class EMD_AudioSpace : uint8
{
	/** 수중 서브믹스(SM_MD_Underwater). 수심에 따라 LPF 컷오프가 움직인다 */
	Underwater UMETA(DisplayName = "수중"),

	/** 드라이 서브믹스(SM_MD_Dry). 모선 실내·정산·상점 */
	Dry UMETA(DisplayName = "드라이(모선)"),

	/** UI 피드백. 공간감을 주지 않고 항상 2D 로 재생한다 */
	UI UMETA(DisplayName = "UI(2D)")
};

/**
 * 이벤트 태그 하나에 대응하는 재생 정의.
 *
 * 같은 EventTag 를 가진 항목을 여러 개 둘 수 있고, 그때는 현재 상태·환경 태그와
 * 가장 잘 맞는 항목이 선택된다. "부상 중에는 다른 호흡", "암전 구역에서는 다른 앰비"
 * 처럼 상황별 변주를 코드 분기 없이 데이터로만 표현하기 위한 구조다.
 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_AudioEntry
{
	GENERATED_BODY()

public:
	/** 게임플레이가 호출하는 행동 식별자. 예: `SFX.Clean.Hera.ScrapeSludge` */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Audio")
	FGameplayTag EventTag;

	/** 실제로 재생할 SoundCue 또는 SoundWave */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Audio")
	TSoftObjectPtr<USoundBase> Sound;

	/** 서브믹스 라우팅. 월드 사운드는 Underwater, 조작 피드백은 UI */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Audio")
	EMD_AudioSpace Space = EMD_AudioSpace::Underwater;

	/** 루프 여부. true 면 재생 핸들을 보관하고 StopEvent 로만 멈춘다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Audio")
	bool bLooping = false;

	/** 위치를 무시하고 2D 로 재생한다. Space == UI 면 강제로 참으로 취급한다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Audio")
	bool b2D = false;

	/**
	 * 이 항목이 선택되기 위해 **모두** 켜져 있어야 하는 상태 태그.
	 * 예: `State.Character.Injured` → 부상 호흡. 비어 있으면 기본 항목이 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Audio|조건")
	FGameplayTagContainer RequiredStateTags;

	/** 하나라도 켜져 있으면 이 항목을 쓰지 않는 상태 태그 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Audio|조건")
	FGameplayTagContainer BlockedStateTags;

	/**
	 * 이 항목이 유효한 환경 태그. 예: `Env.Sector.Blackout`, `Env.Zone.Mothership`.
	 * 비어 있으면 모든 환경에서 유효하다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Audio|조건")
	FGameplayTagContainer RequiredEnvironmentTags;

	/**
	 * 하나라도 켜져 있으면 이 항목을 쓰지 않는 환경 태그.
	 * 수중 앰비언스에 `Env.Surface.Above` 를 넣어 수면 위에서 새지 않게 한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Audio|조건")
	FGameplayTagContainer BlockedEnvironmentTags;

	/**
	 * 재생 중 조건이 깨지면 스스로 멈춘다. 루프 항목에만 의미가 있다.
	 * 수면 위로 올라가면 기지 앰비가 끊기는 동작이 이 플래그로 만들어진다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Audio|조건")
	bool bStopWhenConditionsFail = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Audio|믹스", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float VolumeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Audio|믹스", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float PitchMultiplier = 1.0f;

	/**
	 * 수심에 따라 피치를 내린다. UE 의 피치는 재생 속도라 소리가 함께 느려지므로,
	 * 앰비언스·룸톤·노이즈성 소재에만 켠다. 켜면 LPF는 적용하지 않는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Audio|수중")
	bool bDepthPitch = false;

	/** 어테뉴에이션 프리셋. 비우면 3D 재생 시 엔진 기본값을 쓴다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Audio|믹스")
	TSoftObjectPtr<USoundAttenuation> Attenuation;

	/** 동시 재생 제한. 발소리·조각 낙하·따개비처럼 폭주하는 항목에 반드시 지정한다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Audio|믹스")
	TSoftObjectPtr<USoundConcurrency> Concurrency;

	/** 조건이 얼마나 구체적인지. 값이 큰 항목이 선택에서 이긴다 */
	int32 GetSpecificity() const
	{
		return RequiredStateTags.Num() * 2 + RequiredEnvironmentTags.Num()
			+ BlockedStateTags.Num() + BlockedEnvironmentTags.Num();
	}

	bool IsValidEntry() const
	{
		return EventTag.IsValid() && !Sound.IsNull();
	}
};
