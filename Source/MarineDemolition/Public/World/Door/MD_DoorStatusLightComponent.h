#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Types/Facility/MD_FacilityTypes.h"
#include "MD_DoorStatusLightComponent.generated.h"

class UMD_FacilityFaultComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/** Actual facility faults blink red/black; ordinary quest locks keep the normal color. */
UCLASS(ClassGroup=(MarineDemolition), meta=(BlueprintSpawnableComponent))
class MARINEDEMOLITION_API UMD_DoorStatusLightComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMD_DoorStatusLightComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** BP_Door에서 상태 표시용 StaticMeshComponent 이름 */
	UPROPERTY(EditAnywhere, Category="MD|Door|Status Light")
	FName DisplayComponentName = TEXT("Display");

	/** Display 컴포넌트에서 MI_Light_A가 들어가는 슬롯 */
	UPROPERTY(EditAnywhere, Category="MD|Door|Status Light")
	FName DisplayMaterialSlotName = TEXT("Light_A_001");

	/** MI_Light_A가 노출하는 발광 색 파라미터 */
	UPROPERTY(EditAnywhere, Category="MD|Door|Status Light")
	FName EmissiveColorParameterName = TEXT("EmissiveColor");

	/** 고장 상태에서 켜지는 색 */
	UPROPERTY(EditAnywhere, Category="MD|Door|Status Light")
	FLinearColor FaultColor = FLinearColor::Red;

	/** 빨강/검정 한 단계가 유지되는 시간 */
	UPROPERTY(EditAnywhere, Category="MD|Door|Status Light", meta=(ClampMin="0.1", Units="s"))
	float BlinkIntervalSeconds = 1.0f;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> DisplayMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DisplayMaterial;

	FLinearColor NormalEmissiveColor = FLinearColor::White;
	FTimerHandle BlinkTimerHandle;
	bool bFaultLightOn = false;
	bool bWasLocked = false;
	TWeakObjectPtr<UMD_FacilityFaultComponent> BoundFault;

	bool InitializeDisplayMaterial();
	bool IsDoorFaultActive() const;
	UFUNCTION()
	void HandleFaultChanged(UMD_FacilityFaultComponent* Facility, const FMD_FacilitySnapshot& Previous, const FMD_FacilitySnapshot& Current);
	void RefreshBlinkState();
	void ApplyEmissiveColor(const FLinearColor& Color);
	void RestoreNormalColor();
};
