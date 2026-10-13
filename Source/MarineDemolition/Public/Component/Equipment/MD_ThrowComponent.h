#pragma once

#include "CoreMinimal.h"
#include "Audio/MD_GameplayAudioFeedback.h"
#include "Components/ActorComponent.h"
#include "MD_ThrowComponent.generated.h"

class AMD_Character;
class UMD_ThrowSettings;
class UMD_ToolDefinition;

/** Charge locally for feedback; only the server releases physics or transfers ownership. */
UCLASS(ClassGroup = (MarineDemolition), meta = (BlueprintSpawnableComponent))
class MARINEDEMOLITION_API UMD_ThrowComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMD_ThrowComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "Throw")
	void Configure(UMD_ThrowSettings* InSettings);

	UFUNCTION(BlueprintCallable, Category = "Throw")
	void BeginThrow();

	UFUNCTION(BlueprintCallable, Category = "Throw")
	void ReleaseThrow();

	UFUNCTION(BlueprintCallable, Category = "Throw")
	void CancelThrow();

	UFUNCTION(BlueprintPure, Category = "Throw")
	bool IsCharging() const { return bLocalCharging; }

	UFUNCTION(BlueprintPure, Category = "Throw")
	float GetChargeFraction() const;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMD_ThrowSettings> Settings;

	TWeakObjectPtr<AActor> LocalHeldActor;
	TWeakObjectPtr<UMD_ToolDefinition> LocalTool;
	int32 LocalSlot = INDEX_NONE;
	double LocalStartedAt = 0.0;
	uint32 LocalRequestId = 0;
	bool bLocalCharging = false;
	FMD_AudioHandle ChargeAudio;

	TWeakObjectPtr<AActor> ServerHeldActor;
	TWeakObjectPtr<UMD_ToolDefinition> ServerTool;
	int32 ServerSlot = INDEX_NONE;
	double ServerStartedAt = 0.0;
	double NextServerThrowTime = 0.0;
	uint32 ServerRequestId = 0;
	bool bServerCharging = false;

	AMD_Character* GetCharacter() const;
	bool CanThrow() const;
	bool ResolveTarget(AActor*& OutHeldActor, UMD_ToolDefinition*& OutTool, int32& OutSlot) const;
	bool MatchesTarget(AActor* HeldActor, UMD_ToolDefinition* Tool, int32 Slot) const;
	void StopToolUse();
	void ResetLocalCharge();
	void EndServerCharge();
	void RefreshTickEnabled();
	bool ThrowHeldActor(AActor* HeldActor, const FVector& Velocity);
	bool ThrowEquipment(UMD_ToolDefinition* Tool, int32 Slot, const FVector& Direction, const FVector& Velocity);

	UFUNCTION(Server, Reliable)
	void ServerBeginThrow(uint32 RequestId, AActor* ExpectedHeldActor, UMD_ToolDefinition* ExpectedTool, int32 ExpectedSlot);

	UFUNCTION(Server, Reliable)
	void ServerReleaseThrow(uint32 RequestId);

	UFUNCTION(Server, Reliable)
	void ServerCancelThrow(uint32 RequestId);

	UFUNCTION(Client, Reliable)
	void ClientEndThrow(uint32 RequestId);
};
