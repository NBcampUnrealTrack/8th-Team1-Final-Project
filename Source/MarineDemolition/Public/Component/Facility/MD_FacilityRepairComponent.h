#pragma once

#include "CoreMinimal.h"
#include "Component/Facility/MD_FacilityFaultComponent.h"
#include "MD_FacilityRepairComponent.generated.h"

class APawn;

/** Validates repair inputs on the server; owner-pawn RPCs carry client requests. */
UCLASS(ClassGroup = (MD), meta = (BlueprintSpawnableComponent))
class MARINEDEMOLITION_API UMD_FacilityRepairComponent : public UMD_FacilityFaultComponent
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Facility|Repair")
	bool bAllowManualRepair = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Facility|Repair")
	bool bAllowInsulationTape = false;
	/** Seconds per participating player: 1/2/3/4 players take 10/20/30/40 seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Facility|Repair", meta = (DisplayName = "Manual Repair Seconds Per Player", ClampMin = "0.1", Units = "s"))
	float ManualRepairSeconds = 10.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Facility|Repair", meta = (ClampMin = "1", Units = "cm"))
	float RepairReach = 200.f;

	/** A panel may forward work to a linked door; ordinary devices always repair themselves. */
	UFUNCTION(BlueprintPure, Category = "MD|Facility|Repair")
	FMD_FacilitySnapshot GetRepairSnapshot() const;
	UFUNCTION(BlueprintPure, Category = "MD|Facility|Repair")
	bool HasRepairFault() const;
	/** Active work keeps the duration sampled at its start; otherwise returns the next duration. */
	UFUNCTION(BlueprintPure, Category = "MD|Facility|Repair")
	float GetManualRepairDuration() const;

	bool CanReachForRepair(const APawn* Pawn) const;
	bool BeginManualRepair(APawn* Pawn);
	void StopManualRepair(APawn* Pawn);
	bool TryRepairWithTape(APawn* Pawn, int32 ExpectedFaultId);
	FText GetRepairPrompt() const;
protected:
	virtual UMD_FacilityFaultComponent* ResolveRepairTarget() const;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	void AdvanceManualRepair();
	FTimerHandle ManualTimer;
	TWeakObjectPtr<APawn> ManualRepairer;
	TWeakObjectPtr<UMD_FacilityFaultComponent> ManualTarget;
	int32 ManualFaultId = 0;
	double LastWorkTime = 0.0;
	float ActiveManualRepairSeconds = 0.f;
};
