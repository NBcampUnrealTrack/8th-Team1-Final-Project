#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Audio/MD_AudioSubsystem.h"
#include "MD_AmbienceEmitter.generated.h"

/**
 * 레벨에 놓아 앰비언스 루프를 재생하는 액터.
 *
 * 사운드 에셋을 직접 들지 않고 이벤트 태그만 가진다. 어떤 소재가 나갈지는
 * `DA_MD_Audio_Ambience` 와 현재 환경 태그가 결정한다.
 * (예: `SFX.Ambience.StationInterior` 는 `Env.Zone.Station` 이 켜져 있을 때만 유효)
 *
 * 위치가 필요 없는 룸톤은 b2D 를 켠 항목을 쓰고, 기포·조명처럼 위치가 있는
 * 앰비는 3D 항목을 쓰면서 이 액터를 해당 지점에 배치한다.
 */
UCLASS(Blueprintable)
class MARINEDEMOLITION_API AMD_AmbienceEmitter : public AActor
{
	GENERATED_BODY()

public:
	AMD_AmbienceEmitter();

	//~ AActor
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 재생을 시작한다. bAutoPlay 가 꺼져 있을 때 외부에서 호출 */
	UFUNCTION(BlueprintCallable, Category = "MD|Audio")
	void StartAmbience();

	UFUNCTION(BlueprintCallable, Category = "MD|Audio")
	void StopAmbience();

protected:
	/** 재생할 앰비언스 이벤트 태그 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Audio")
	FGameplayTag AmbienceTag;

	/**
	 * 재생 시 함께 켜 둘 환경 태그. 앰비언스 항목의 RequiredEnvironmentTags 와
	 * 짝이 맞아야 소재가 선택된다. 구역 시스템이 따로 켜 준다면 비워 둔다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Audio")
	FGameplayTag ApplyEnvironmentTagOnBeginPlay;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Audio")
	bool bAutoPlay = true;

	/** 정지 시 페이드 아웃 시간(초) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Audio", meta = (ClampMin = "0.0"))
	float FadeOutSeconds = 0.5f;

private:
	UMD_AudioSubsystem* GetAudioSubsystem() const;

	FMD_AudioHandle Handle;
};
