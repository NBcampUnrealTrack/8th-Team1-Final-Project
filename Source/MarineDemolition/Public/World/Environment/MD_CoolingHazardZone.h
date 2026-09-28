#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MD_CoolingHazardZone.generated.h"

class AMD_CoolingPump;
class UBoxComponent;
class UDamageType;
class UPrimitiveComponent;

/** Place across isolation B's entrance AND interior; link the pump with the eyedropper. */
UCLASS(Blueprintable)
class MARINEDEMOLITION_API AMD_CoolingHazardZone : public AActor
{
	GENERATED_BODY()

public:
	AMD_CoolingHazardZone();

	/** Soft level reference avoids forcing the distant pump's level to load. Missing pump fails closed. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "MD|Cooling")
	TSoftObjectPtr<AMD_CoolingPump> CoolingPump;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Cooling")
	TObjectPtr<UBoxComponent> DamageZone;

	/** On the server, reads the pump directly so repair immediately prevents further damage. */
	UFUNCTION(BlueprintPure, Category = "MD|Cooling")
	bool IsHazardActive() const;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, Category = "MD|Cooling", meta = (ClampMin = "0.0"))
	float DamagePerSecond = 8.f;

	UPROPERTY(EditAnywhere, Category = "MD|Cooling", meta = (ClampMin = "0.1", Units = "s"))
	float DamageInterval = 1.f;

	UPROPERTY(EditAnywhere, Category = "MD|Cooling")
	TSubclassOf<UDamageType> HazardDamageType;

	/** Optional room steam/warning FX hook, including initial state and late-joining clients. */
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|Cooling")
	void OnHazardStateChanged(bool bActive);

private:
	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnRep_HazardActive();

	void UpdateHazard();
	void DamagePawn(AActor* Actor);

	UPROPERTY(ReplicatedUsing = OnRep_HazardActive)
	bool bHazardActive = true;

	FTimerHandle UpdateTimer;
	TMap<TWeakObjectPtr<AActor>, double> NextDamageTime;
};
