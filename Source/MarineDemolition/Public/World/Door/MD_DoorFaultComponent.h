#pragma once
#include "CoreMinimal.h"
#include "Component/Facility/MD_FacilityRepairComponent.h"
#include "Audio/MD_AudioSubsystem.h"
#include "MD_DoorFaultComponent.generated.h"

class UPrimitiveComponent;

/** Runtime adapter for both Ocean door Blueprints; does not modify their assets. */
UCLASS()
class MARINEDEMOLITION_API UMD_DoorFaultComponent : public UMD_FacilityRepairComponent
{
	GENERATED_BODY()
public:
	UMD_DoorFaultComponent();
#if !UE_BUILD_SHIPPING
	/** Console testing: bypass room occupancy, preserve authority/exemptions/active incident. */
	bool DebugForceBreak() { return TryBreakInternal(true); }
#endif
	virtual bool CanReceiveRandomFault() const override;
	/** A possessed player must be inside a tagged repair-room box; not just near the doorway. */
	bool HasRepairRoomOccupant() const;
	/** Both sides; validates the equipped tool, aimed surface and incident on the server. */
	bool TryRepairWithCrowbar(APawn* Pawn, int32 ExpectedFaultId);
	static const FString RepairRoomTagPrefix;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
protected:
	virtual bool CanStartFault() const override { return HasRepairRoomOccupant(); }
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	UFUNCTION()
	void ApplyFault(UMD_FacilityFaultComponent* Facility, const FMD_FacilitySnapshot& Previous, const FMD_FacilitySnapshot& Current);
	void CloseDoor(bool bForceAnimation);
	void RestoreOverlapEvents();
	void StopFaultAudio();
	FMD_AudioHandle FaultAudioHandle;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> SuppressedOverlapComponents;
	bool bOwnsLock = false;
	bool bPreviousLocked = false;
};
