#pragma once

#include "CoreMinimal.h"
#include "MD_FacilityTypes.generated.h"

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
