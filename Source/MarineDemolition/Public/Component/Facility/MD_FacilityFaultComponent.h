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

	// 명시적 등록과 자동 등록이 동일한 수명 해제 연결을 공유하도록 관리자 접근 허용
	friend class UMD_FacilityFaultManagerComponent;

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
	// 파손 부수 효과와 상태 알림까지 동일 전이 안에서 확정
	bool TryBreakWithEffects(TFunctionRef<void()> Effects);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Facility")
	bool BeginRepair(AActor* Repairer, int32 ExpectedFaultId);
	/** Positive normalized work; reaching 1 completes this incident once. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Facility")
	bool AddRepairProgress(AActor* Repairer, int32 ExpectedFaultId, float ProgressDelta);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Facility")
	bool CancelRepair(AActor* Repairer, int32 ExpectedFaultId);
	/** Caller has already validated and paid for an external tool hit. Server only. */
	bool CompleteRepairFromValidatedSource(int32 ExpectedFaultId);
	// 수리 부수 효과와 상태 알림까지 동일 사건 전이 안에서 확정
	bool CompleteRepairWithEffects(int32 ExpectedFaultId, TFunctionRef<void()> Effects);

protected:
	/** Common server incident transition; a development door cheat may skip occupancy only. */
	bool TryBreakInternal(bool bSkipDeviceCondition);
	// 장치 조건과 효과를 포함한 공통 파손 확정
	bool TryBreakInternalWithEffects(bool bSkipDeviceCondition, TFunctionRef<void()> Effects);
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

protected:
	// 상태 효과와 방송 도중 같은 시설의 중첩 사건 변경 차단
	bool bStateTransitionInProgress = false;
};
