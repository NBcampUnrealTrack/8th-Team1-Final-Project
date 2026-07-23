#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MDSampleItem.generated.h"

// 샘플 스텁. 붕괴 피해를 받으면 손상 상태가 되어 붉게 변한다.
UCLASS()
class MARINEDEMOLITION_API AMDSampleItem : public AActor
{
	GENERATED_BODY()

public:
	AMDSampleItem();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sample")
	bool bDamaged = false;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnAnyDamage(AActor* DamagedActor, float Damage, const class UDamageType* DamageType,
		class AController* InstigatedBy, AActor* DamageCauser);

	UPROPERTY(VisibleAnywhere, Category = "Sample")
	TObjectPtr<UStaticMeshComponent> Mesh;
};
