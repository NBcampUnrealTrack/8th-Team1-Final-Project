#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagAssetInterface.h"
#include "Interaction/MD_Interactable.h"
#include "MD_SampleItem.generated.h"

// 샘플 스텁. 붕괴 피해를 받으면 손상 상태가 되어 붉게 변한다.
UCLASS()
class MARINEDEMOLITION_API AMD_SampleItem
	: public AActor
	, public IGameplayTagAssetInterface
	, public IMD_Interactable
{
	GENERATED_BODY()

public:
	AMD_SampleItem();

	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sample")
	bool bDamaged = false;

	// 손상 상태 적용. 붕괴 피해와 Day 복원에서 공용
	void SetDamaged();
	
protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnAnyDamage(AActor* DamagedActor, float Damage, const class UDamageType* DamageType,
		class AController* InstigatedBy, AActor* DamageCauser);

	UPROPERTY(VisibleAnywhere, Category = "Sample")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** 이 액터를 집을 때 사용할 처리 방식. Interaction.Pickup 아래 태그만 선택한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Interaction",
		meta = (Categories = "Interaction.Pickup"))
	FGameplayTag PickupMethodTag;
};
