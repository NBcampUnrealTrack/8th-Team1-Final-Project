#pragma once

#include "CoreMinimal.h"
#include "Components/SphereComponent.h"
#include "MD_HazardVolumeComponent.generated.h"

class UDamageType;

// 활성 상태에서 겹친 폰에 주기적으로 피해를 주는 위험 볼륨
// 수도관 파열·미래 오염수 등에서 재사용한다. 실제 HP 반영은 MD_Character의 데미지 브리지가 담당.
UCLASS(ClassGroup = (MD), meta = (BlueprintSpawnableComponent))
class MARINEDEMOLITION_API UMD_HazardVolumeComponent : public USphereComponent
{
	GENERATED_BODY()

public:
	UMD_HazardVolumeComponent();

	// 위험 볼륨 on/off. 서버에서 호출. off 시 틱 피해 중단
	UFUNCTION(BlueprintCallable, Category = "MD|Hazard")
	void SetHazardActive(bool bNewActive);

	UFUNCTION(BlueprintPure, Category = "MD|Hazard")
	bool IsHazardActive() const { return bHazardActive; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 초당 피해량
	UPROPERTY(EditAnywhere, Category = "MD|Hazard")
	float DamagePerSecond = 8.f;

	// 피해 적용 주기(초)
	UPROPERTY(EditAnywhere, Category = "MD|Hazard", meta = (ClampMin = "0.1"))
	float DamageInterval = 1.f;

	// 피해 타입 (수중/오염 구분용, 미지정 시 기본)
	UPROPERTY(EditAnywhere, Category = "MD|Hazard")
	TSubclassOf<UDamageType> HazardDamageType;

private:
	// 겹친 폰 전부에 한 주기분 피해 적용 (서버 전용)
	void ApplyHazardTick();

	FTimerHandle HazardTimerHandle;

	bool bHazardActive = false;
};
