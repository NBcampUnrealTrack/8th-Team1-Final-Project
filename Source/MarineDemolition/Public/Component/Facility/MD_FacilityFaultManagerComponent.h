#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Component/Facility/MD_FacilityFaultComponent.h"
#include "MD_FacilityFaultManagerComponent.generated.h"

/** GameState-owned server registry and timer-based random incident scheduler. */
UCLASS(ClassGroup = (MD), meta = (BlueprintSpawnableComponent))
class MARINEDEMOLITION_API UMD_FacilityFaultManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMD_FacilityFaultManagerComponent();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Facility")
	TArray<FMD_FacilityFaultSchedule> Schedules;
	/** Zero uses a fresh session seed. Set a nonzero seed for reproducible validation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Facility")
	int32 RandomSeed = 0;
	UPROPERTY(BlueprintAssignable, Category = "MD|Facility")
	FMD_OnFacilityStateChanged OnFacilityStateChanged;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Facility")
	bool RegisterFacility(UMD_FacilityFaultComponent* Facility);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Facility")
	void UnregisterFacility(UMD_FacilityFaultComponent* Facility);
	/** Server registry: clients read each facility's replicated snapshot instead. */
	UFUNCTION(BlueprintPure, Category = "MD|Facility")
	TArray<UMD_FacilityFaultComponent*> GetRegisteredFacilities() const;
	UFUNCTION(BlueprintPure, Category = "MD|Facility")
	TArray<UMD_FacilityFaultComponent*> GetActiveFaults() const;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Facility")
	void StartFaultSchedules();
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Facility")
	void StopFaultSchedules();
	/** Manual occurrence uses the same candidate filtering as scheduled occurrences. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Facility")
	UMD_FacilityFaultComponent* TriggerRandomFault(EMD_FacilityKind Kind, FName Group);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	UFUNCTION()
	void HandleFacilityStateChanged(UMD_FacilityFaultComponent* Facility,
		const FMD_FacilitySnapshot& Previous, const FMD_FacilitySnapshot& Current);
	void ArmSchedule(int32 Index);
	void Occur(int32 Index, int32 Generation);
	bool HasServerAuthority() const;
	TArray<TWeakObjectPtr<UMD_FacilityFaultComponent>> Facilities;
	TArray<FMD_FacilityFaultSchedule> RunningSchedules;
	TArray<FTimerHandle> ScheduleTimers;
	FRandomStream Random;
	int32 ScheduleGeneration = 0;
};
