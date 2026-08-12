#pragma once

#include "CoreMinimal.h"
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

protected:
	virtual void OnBroken(AActor* Cutter) override;
	virtual void OnRepaired(AActor* Repairer) override;

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
};
