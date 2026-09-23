#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MD_DoorStatusLightComponent.generated.h"

class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/**
 * BP_Door의 잠금 상태를 디스플레이 점멸로 표시한다.
 * bLocked가 true인 동안 1초 간격으로 빨강/검정을 반복하고,
 * 잠금이 풀리면 원래 EmissiveColor로 복원한다.
 */
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

	/** 소유 BP_Door의 잠금/고장 상태 변수 이름 */
	UPROPERTY(EditAnywhere, Category="MD|Door|Status Light")
	FName LockedPropertyName = TEXT("bLocked");

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

	bool InitializeDisplayMaterial();
	bool IsDoorLocked() const;
	void RefreshBlinkState();
	void ApplyEmissiveColor(const FLinearColor& Color);
	void RestoreNormalColor();
};
