#pragma once

#include "CoreMinimal.h"
#include "Audio/MD_AudioSubsystem.h"
#include "World/Wall/MD_WallUtility.h"
#include "MD_WaterPipe.generated.h"

class UMD_HazardVolumeComponent;

// 수도관: 겹친 벽 철거 시 파열 → 오염수 분출(BP VFX) + 근접 틱 피해 + 점수. 폼건으로 봉합
UCLASS()
class MARINEDEMOLITION_API AMD_WaterPipe : public AMD_WallUtility
{
	GENERATED_BODY()

public:
	AMD_WaterPipe();

	virtual FMD_UtilityRecord BuildPersistenceRecord() override;
	virtual bool RestorePersistenceRecord(FMD_UtilityRecord Record) override;

protected:
	virtual void OnBroken(AActor* Cutter) override;
	virtual void OnRepaired(AActor* Repairer) override;

	/** 상태 변화를 모든 머신에서 받는다. 분출음은 여기서 켜고 끈다 */
	virtual void OnUtilityStateChangedLocal(EMD_UtilityState NewState) override;

	/**
	 * 파열 지속 분출음. 사운드 에셋이 아니라 태그다 —
	 * 어떤 소재가 나갈지는 DA_MD_Audio_Hazard 가 정한다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Utility|사운드")
	FGameplayTag GushLoopSoundTag;

	/** 봉합 시 분출음을 끊는 데 쓰는 페이드 길이 */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Utility|사운드",
		meta = (ClampMin = "0.0", Units = "s"))
	float GushStopFadeSeconds = 0.25f;

	// 파열 지점 근접 피해 볼륨 (-8/s)
	UPROPERTY(VisibleAnywhere, Category = "MD|Utility")
	TObjectPtr<UMD_HazardVolumeComponent> HazardVolume;

	// 미밀봉 누수 감점 주기(초)
	UPROPERTY(EditDefaultsOnly, Category = "MD|Utility")
	float LeakTickInterval = 30.f;

	// 누수 감점 최대 횟수 (상한 -80 / 틱당 -8 = 10회)
	UPROPERTY(EditDefaultsOnly, Category = "MD|Utility")
	int32 MaxLeakTicks = 10;

private:
	// 미밀봉 지속 감점 (서버). 상한 도달 시 타이머 중단
	void ApplyLeakTick();

	FTimerHandle LeakTimerHandle;

	int32 LeakTicksApplied = 0;

	/** 재생 중인 분출 루프. 각 머신이 자기 것을 들고 있다 (복제하지 않는다) */
	FMD_AudioHandle GushLoopHandle;
};
