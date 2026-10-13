#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Component/Facility/MD_FacilityFaultComponent.h"
#include "MD_FingerprintInterlock.generated.h"

class AMD_FingerprintPad;
class AMD_TrashSorter;
class APawn;
class UPostProcessComponent;

UCLASS()
class MARINEDEMOLITION_API UMD_FingerprintFaultComponent : public UMD_FacilityFaultComponent
{
	GENERATED_BODY()
protected:
	virtual bool CanStartFault() const override;
};

/** One processing-room safety incident, two authentication endpoints, one team charge. */
UCLASS(Blueprintable)
class MARINEDEMOLITION_API AMD_FingerprintInterlock : public AActor
{
	GENERATED_BODY()
public:
	AMD_FingerprintInterlock();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Fingerprint")
	TObjectPtr<UMD_FingerprintFaultComponent> Fault;
	/** Unbound locator visible to every local view while this incident is active. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Fingerprint|Presentation")
	TObjectPtr<UPostProcessComponent> FaultHighlight;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Replicated, Category="MD|Fingerprint")
	TArray<TObjectPtr<AMD_FingerprintPad>> Pads;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="MD|Fingerprint")
	TArray<TObjectPtr<AMD_TrashSorter>> LinkedSorters;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MD|Fingerprint", meta=(ClampMin="1"))
	float SoloWindowSeconds = 45.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MD|Fingerprint", meta=(ClampMin="0"))
	int32 CostUnit = 5;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MD|Fingerprint")
	bool bAutomaticFaults = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MD|Fingerprint", meta=(ClampMin="1"))
	float MinFaultInterval = 300.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MD|Fingerprint", meta=(ClampMin="1"))
	float MaxFaultInterval = 420.f;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="MD|Fingerprint")
	int32 LossPerSecond = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="MD|Fingerprint")
	int64 TotalFaultLoss = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="MD|Fingerprint")
	int32 SoloFirstPad = INDEX_NONE;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="MD|Fingerprint")
	double SoloDeadline = 0.;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="MD|Fingerprint")
	bool StartFault();
	UFUNCTION(BlueprintPure, Category="MD|Fingerprint")
	bool IsActive() const;
	UFUNCTION(BlueprintPure, Category="MD|Fingerprint")
	bool HasValidConfiguration() const;
	UFUNCTION(BlueprintPure, Category="MD|Fingerprint")
	int32 GetParticipantCount() const;
	static int32 CalculateLossPerSecond(int32 Count, int32 Unit = 5);
	// 요청 시점의 고장 번호와 현재 사건 일치 확인 후 패드 인증 시작
	bool BeginScan(AMD_FingerprintPad* Pad, APawn* Pawn, int32 ExpectedFaultId);
	void ReleaseScan(AMD_FingerprintPad* Pad, APawn* Pawn);
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	void RefreshFaultHighlight();
	UFUNCTION()
	void HandleFault(UMD_FacilityFaultComponent* Component, const FMD_FacilitySnapshot& Previous,
		const FMD_FacilitySnapshot& Current);
	void ArmNextFault();
	void AutomaticFault();
	void ResetScans();
	// 비용 반영 뒤에도 인증을 시작한 사건에만 복구 적용
	void CompleteAuthentication(int32 ExpectedFaultId);
	void RefreshParticipantMode();
	void SetSorterLocks(bool bLocked);
	void ApplyElapsedCharges();
	FTimerHandle FaultTimer;
	TWeakObjectPtr<APawn> SoloPawn;
	TArray<TWeakObjectPtr<AMD_TrashSorter>> LockedSorters;
	double LastChargeTime = 0.;
	bool bSoloMode = true;
};
