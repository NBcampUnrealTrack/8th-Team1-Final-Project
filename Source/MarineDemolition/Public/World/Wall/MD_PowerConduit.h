#pragma once

#include "CoreMinimal.h"
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

	// BP에서 조명 그룹 on/off·깜빡임 연출 처리. C++은 점수·상태만 담당
	// (조명 그룹 레퍼런스는 연출 특성상 BP/레벨에서 관리)
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|Utility")
	void SetPowerGroupEnabled(bool bEnabled);
};
