#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Types/Facility/MD_FacilityTypes.h"
#include "MD_FacilityFaultComponent.generated.h"

class UMD_FacilityFaultManagerComponent;
class UMD_FacilityFaultComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMD_OnFacilityStateChanged,
	UMD_FacilityFaultComponent*, Facility,
	const FMD_FacilitySnapshot&, Previous, const FMD_FacilitySnapshot&, Current);

/** Shared incident state. Tool, distance and puzzle validation belong to the caller. */
UCLASS(ClassGroup = (MD), meta = (BlueprintSpawnableComponent))
class MARINEDEMOLITION_API UMD_FacilityFaultComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMD_FacilityFaultComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** Actor-instance exclusion, including random, initial and direct fault requests. */
	static const FName FaultExemptActorTag;
	bool IsFaultExempt() const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Facility")
	EMD_FacilityKind Kind = EMD_FacilityKind::Door;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Facility")
	FName Group;
	/** Opt in explicitly; adding the component alone does not enable random faults. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Facility")
	bool bRandomFaultEligible = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Facility")
	bool bStartBroken = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Facility", meta = (ClampMin = "0"))
	float RefaultGraceSeconds = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Facility")
	bool bResetProgressOnCancel = false;
	/** Optional linked device for a remote panel; never changes unrelated devices automatically. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "MD|Facility")
	TObjectPtr<AActor> LinkedFacility = nullptr;

	UPROPERTY(BlueprintAssignable, Category = "MD|Facility")
	FMD_OnFacilityStateChanged OnStateChanged;

	UFUNCTION(BlueprintPure, Category = "MD|Facility")
	FMD_FacilitySnapshot GetSnapshot() const { return Snapshot; }
	UFUNCTION(BlueprintPure, Category = "MD|Facility")
	bool IsFaultActive() const { return Snapshot.IsFaultActive(); }
	UFUNCTION(BlueprintPure, Category = "MD|Facility")
	virtual bool CanReceiveRandomFault() const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Facility")
	bool TryBreak();
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Facility")
	bool BeginRepair(AActor* Repairer, int32 ExpectedFaultId);
	/** Positive normalized work; reaching 1 completes this incident once. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Facility")
	bool AddRepairProgress(AActor* Repairer, int32 ExpectedFaultId, float ProgressDelta);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Facility")
	bool CancelRepair(AActor* Repairer, int32 ExpectedFaultId);
	/** Caller has already validated and paid for an external tool hit. Server only. */
	bool CompleteRepairFromValidatedSource(int32 ExpectedFaultId);

protected:
	/** Common server incident transition; a development door cheat may skip occupancy only. */
	bool TryBreakInternal(bool bSkipDeviceCondition);
	/** Device-specific safety condition, checked again immediately before an incident starts. */
	virtual bool CanStartFault() const { return true; }
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	UPROPERTY(ReplicatedUsing = OnRep_Snapshot)
	FMD_FacilitySnapshot Snapshot;
	UFUNCTION()
	void OnRep_Snapshot(const FMD_FacilitySnapshot& Previous);
	void PublishChange(const FMD_FacilitySnapshot& Previous);
	void RegisterWithManager();
	bool HasServerAuthority() const;
	double GetServerTime() const;
	TWeakObjectPtr<UMD_FacilityFaultManagerComponent> Manager;
	FTimerHandle RegistrationTimer;
};
