#pragma once

#include "CoreMinimal.h"
#include "MD_FacilityTypes.generated.h"

class UMaterialInterface;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EMD_FacilityKind : uint8
{
	Door,
	WiringPanel,
	CoolingPump,
	TrashSorter,
	Fingerprint,
	Breaker,
	/** Existing cuttable infrastructure; never accidentally counted as a door. */
	LegacyUtility
};

UENUM(BlueprintType)
enum class EMD_FacilityState : uint8
{
	Normal,
	Broken,
	Repairing,
	Repaired
};

/** One authoritative incident; repair callbacks must carry its FaultId. */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_FacilitySnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	EMD_FacilityState State = EMD_FacilityState::Normal;
	UPROPERTY(BlueprintReadOnly)
	int32 FaultId = 0;
	UPROPERTY(BlueprintReadOnly)
	float RepairProgress = 0.f;
	UPROPERTY(BlueprintReadOnly)
	double FaultStartedAt = 0.0;
	UPROPERTY(BlueprintReadOnly)
	double RepairedAt = 0.0;
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<AActor> Repairer = nullptr;

	bool IsFaultActive() const
	{
		return State == EMD_FacilityState::Broken || State == EMD_FacilityState::Repairing;
	}
};

/** Each enabled schedule selects one healthy facility per occurrence. */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_FacilityFaultSchedule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName ScheduleId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EMD_FacilityKind Kind = EMD_FacilityKind::Door;
	/** None selects all groups of this kind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName Group;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bEnabled = false;
	/** Zero means unconfigured; disabled schedules never create a timer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"))
	float MinIntervalSeconds = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"))
	float MaxIntervalSeconds = 0.f;

	bool HasValidInterval() const
	{
		return FMath::IsFinite(MinIntervalSeconds) && FMath::IsFinite(MaxIntervalSeconds)
			&& MinIntervalSeconds > 0.f && MaxIntervalSeconds >= MinIntervalSeconds;
	}
};

// 실제 메시 슬롯에 영향을 주는 활성 전선 고장과 머티리얼의 지속 복제 상태
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_PowerLightState
{
	GENERATED_BODY()

	// 여러 전선이 공유할 수 있는 실제 스태틱 메시 컴포넌트
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UStaticMeshComponent> MeshComponent = nullptr;
	// 해당 컴포넌트에서 교체할 머티리얼 슬롯
	UPROPERTY(BlueprintReadOnly)
	int32 MaterialIndex = 0;
	// 서버가 선택한 정상 상태 머티리얼
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UMaterialInterface> NormalMaterial = nullptr;
	// 서버가 선택한 정전 상태 머티리얼
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UMaterialInterface> BlackoutMaterial = nullptr;
	// 복구 전까지 해당 슬롯에 영향을 주는 전선 고장 수
	UPROPERTY(BlueprintReadOnly)
	int32 ActiveFaultCount = 0;
	// 전선 참조 해제 시 최초 머티리얼 복원 여부 전달
	UPROPERTY(BlueprintReadOnly)
	bool bHasPowerSource = true;
};

// 머티리얼 교체와 GC 이후에도 복원할 최초 슬롯 상태 보관
USTRUCT()
struct MARINEDEMOLITION_API FMD_PowerMaterialBaseline
{
	GENERATED_BODY()

	// 최초 머티리얼을 보관한 실제 컴포넌트
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> MeshComponent = nullptr;
	// 최초 상태를 복원할 머티리얼 슬롯
	UPROPERTY()
	int32 MaterialIndex = 0;
	// 동적 머티리얼도 GC에서 보존할 최초 슬롯 머티리얼
	UPROPERTY()
	TObjectPtr<UMaterialInterface> OriginalMaterial = nullptr;
};
