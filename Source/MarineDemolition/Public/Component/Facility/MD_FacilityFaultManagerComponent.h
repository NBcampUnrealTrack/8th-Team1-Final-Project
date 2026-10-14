#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Component/Facility/MD_FacilityFaultComponent.h"
#include "MD_FacilityFaultManagerComponent.generated.h"

class AMD_PowerConduit;
class UStaticMeshComponent;
class ULightComponent;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMD_OnBlackoutChanged, bool, bBlackout);

/** GameState-owned server registry and timer-based random incident scheduler. */
UCLASS(ClassGroup = (MD), meta = (BlueprintSpawnableComponent))
class MARINEDEMOLITION_API UMD_FacilityFaultManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMD_FacilityFaultManagerComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Facility")
	TArray<FMD_FacilityFaultSchedule> Schedules;
	/** Zero uses a fresh session seed. Set a nonzero seed for reproducible validation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Facility")
	int32 RandomSeed = 0;
	UPROPERTY(BlueprintAssignable, Category = "MD|Facility")
	FMD_OnFacilityStateChanged OnFacilityStateChanged;
	UFUNCTION(BlueprintPure, Category="MD|Facility|Power") bool IsBlackoutActive() const { return bBlackoutActive; }
	UPROPERTY(BlueprintAssignable, Category="MD|Facility|Power") FMD_OnBlackoutChanged OnBlackoutChanged;
	/** Only tagged interior lamps participate; scanner, status and outdoor lights stay independent. */
	static const FName BaseLightActorTag;
	UFUNCTION(BlueprintCallable, Category="MD|Facility|Power") void RegisterBlackoutLights(AActor* Actor);

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
	UPROPERTY(ReplicatedUsing=OnRep_BlackoutActive) bool bBlackoutActive=false;
	UFUNCTION() void OnRep_BlackoutActive();
	void RefreshBlackoutState();
	void ApplyBaseLighting();
	struct FBaseLamp { TWeakObjectPtr<ULightComponent> Light; bool bOriginalVisible=true; };
	TArray<FBaseLamp> BaseLamps;
	FDelegateHandle LightSpawnHandle;
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

protected:
	// 공유 메시 슬롯의 재질 선택 순서를 유지하는 전선 등록 정보
	struct FPowerSource
	{
		// 현재 고장 상태를 제공하는 전선의 약한 참조
		TWeakObjectPtr<AMD_PowerConduit> Source;
		// 해당 전선이 담당하는 메시 슬롯과 BP 재질 설정
		TArray<FMD_PowerLightState> Targets;
		// 동일 설정 재등록에서 고장 활성 상태의 변경 여부 비교
		bool bFaultActive = false;
	};
	// 중도 합류에도 남아 있는 메시 슬롯별 활성 정전 사건
	UPROPERTY(ReplicatedUsing = OnRep_PowerLightStates)
	TArray<FMD_PowerLightState> PowerLightStates;
	// 서버에서 최초 등록된 전선부터 유지하는 메시 슬롯 연결
	TArray<FPowerSource> PowerSources;
	// 동적 재질의 GC 제거를 막는 각 머신의 최초 재질 보관
	UPROPERTY(Transient)
	TArray<FMD_PowerMaterialBaseline> LocalPowerLights;
	// 재질 콜백에서 발생한 중첩 적용의 다음 틱 예약
	FTimerHandle PowerLightRefreshTimer;
	// 재질 적용 중 동일 스택의 재진입 차단
	bool bApplyingPowerLights = false;
	// 종료 중 원래 재질 복원 콜백의 재등록 차단
	bool bPowerLightsShuttingDown = false;

public:
	// 서버 전선과 메시 슬롯별 BP 재질 설정의 갱신
	void UpdatePowerSource(AMD_PowerConduit* Source, TArray<FMD_PowerLightState> Lights);
	// 제거된 전선의 정전 기여 해제
	void RemovePowerSource(AMD_PowerConduit* Source);
	// 복제 상태보다 먼저 도착한 메시 슬롯의 최초 재질 보관
	void RegisterLocalPowerLights(TArray<FMD_PowerLightState> Lights);
	// 특정 메시 슬롯에 남아 있는 서버 확정 정전 사건 수 조회
	UFUNCTION(BlueprintPure, Category = "MD|Facility|Power")
	int32 GetPowerOutageCount(UStaticMeshComponent* MeshComponent, int32 MaterialIndex);

protected:
	UFUNCTION()
	void OnRep_PowerLightStates();
	// 현재 전선의 고장 사건을 메시 슬롯별로 재집계
	void RebuildPowerLightStates();
	// 최초 등록한 메시 슬롯의 원래 재질 보관과 캐시 반환
	FMD_PowerMaterialBaseline* CacheLocalPowerLight(UStaticMeshComponent* MeshComponent, int32 MaterialIndex);
	// 서버 또는 복제 상태를 각 머신의 메시 슬롯 재질에 적용
	void ApplyPowerLightStates();
	// 다른 월드 또는 범위를 벗어난 메시 슬롯의 적용 차단
	bool IsPowerMeshValid(UStaticMeshComponent* MeshComponent, int32 MaterialIndex);
};
