#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagAssetInterface.h"
#include "MD_SampleItem.generated.h"

// 샘플 스텁. 붕괴 피해를 받으면 손상 상태가 되어 붉게 변한다.
UCLASS()
class MARINEDEMOLITION_API AMD_SampleItem
	: public AActor
	, public IGameplayTagAssetInterface
{
	GENERATED_BODY()

public:
	AMD_SampleItem();

	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sample")
	bool bDamaged = false;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnAnyDamage(AActor* DamagedActor, float Damage, const class UDamageType* DamageType,
		class AController* InstigatedBy, AActor* DamageCauser);

	UPROPERTY(VisibleAnywhere, Category = "Sample")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** 이 액터의 고정 분류와 픽업 방식을 설명한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Interaction")
	FGameplayTagContainer StaticGameplayTags;
};
