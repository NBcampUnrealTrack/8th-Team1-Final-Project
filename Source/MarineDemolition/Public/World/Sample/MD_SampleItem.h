#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagAssetInterface.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "MD_SampleItem.generated.h"

class UPrimitiveComponent;
class UStaticMeshComponent;

// 샘플 스텁. 붕괴 피해를 받으면 손상되고 큰 물리 충격을 받으면 파괴된다.
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

	/** 큰 물리 충격을 받으면 서버에서 샘플을 파괴한다. */
	UFUNCTION()
	void HandleMeshHit(
		UPrimitiveComponent* HitComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		FVector NormalImpulse,
		const FHitResult& Hit);

	UPROPERTY(VisibleAnywhere, Category = "Sample")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** 이 액터를 집을 때 사용할 물리 운반 방식. Blueprint 기본값에서 지정한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Interaction",
		meta = (Categories = "Interaction.Pickup.Physics"))
	FGameplayTag PickupMethodTag;

	/** 보관함이 이 샘플을 식별할 때 사용할 내용물 분류 태그 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Resource",
		meta = (Categories = "Resource.Storage.Content"))
	FGameplayTag StorageContentTag;

	/**
	 * 샘플을 파괴하는 최소 충격 속도(cm/s).
	 * 물리 충돌 impulse를 샘플 질량으로 나눈 값과 비교한다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sample",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float BreakImpactSpeed;
};
