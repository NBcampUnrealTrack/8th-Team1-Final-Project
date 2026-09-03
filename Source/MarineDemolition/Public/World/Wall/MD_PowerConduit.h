#pragma once

#include "CoreMinimal.h"
#include "Audio/MD_AudioSubsystem.h"
#include "World/Wall/MD_WallUtility.h"
#include "MD_PowerConduit.generated.h"

// 배선: 겹친 벽 철거 시 담당 조명 그룹 OFF + 점수. 절연테이프로 재점등
UCLASS()
class MARINEDEMOLITION_API AMD_PowerConduit : public AMD_WallUtility
{
	GENERATED_BODY()

public:
	AMD_PowerConduit();

protected:
	virtual void OnBroken(AActor* Cutter) override;
	virtual void OnRepaired(AActor* Repairer) override;

	/** 파열음·스파크 루프를 각 머신에서 태그로 재생·정지한다 */
	virtual void OnUtilityStateChangedLocal(EMD_UtilityState NewState) override;

	/**
	 * 절단 순간 1회. 사운드 에셋이 아니라 태그다 — 어떤 소재가 나갈지는 DA_MD_Audio_Hazard 가 정한다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Utility|사운드")
	FGameplayTag CutSoundTag;

	/** 파열 지속 스파크. 루프 항목이라 수리 시 핸들로 끊는다 */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Utility|사운드")
	FGameplayTag SparkLoopSoundTag;

	/** 스파크 정지 페이드(초) */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Utility|사운드", meta = (ClampMin = "0.0"))
	float SparkStopFadeSeconds = 0.2f;

	// BP에서 조명 그룹 on/off·깜빡임 연출 처리. C++은 점수·상태만 담당
	// (조명 그룹 레퍼런스는 연출 특성상 BP/레벨에서 관리)
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|Utility")
	void SetPowerGroupEnabled(bool bEnabled);

private:
	/** 재생 중인 스파크 루프. 각 머신이 자기 것을 들고 있다 (복제하지 않는다) */
	FMD_AudioHandle SparkLoopHandle;
};
