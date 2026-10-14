#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Component/Facility/MD_FacilityFaultComponent.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "MD_RepairInstruction.generated.h"

class AMD_PowerConduit;
class AMD_RepairInstructionControl;
class ATriggerBox;
class UBoxComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class USpotLightComponent;
class UPrimitiveComponent;
class APawn;
class UMaterialInterface;
class UMaterialInstanceDynamic;

UENUM(BlueprintType)
enum class EMD_InstructionControlKind : uint8 { Guide, Restart, Knob };

USTRUCT(BlueprintType)
struct FMD_RepairInstructionState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) bool bActive = false;
	UPROPERTY(BlueprintReadOnly) bool bGuideRead = false;
	UPROPERTY(BlueprintReadOnly) bool bDoorLocked = false;
	UPROPERTY(BlueprintReadOnly) bool bDoorPreviouslyLocked = false;
	UPROPERTY(BlueprintReadOnly) int32 Revision = 0;
	/** Server world time; a short visual response shared by all peers. */
	UPROPERTY(BlueprintReadOnly) double RestartFlashUntil = 0;
	/** Zero-based internally; the board numbers slots from the top, starting at one. */
	UPROPERTY(BlueprintReadOnly) TArray<int32> TargetRows;
	UPROPERTY(BlueprintReadOnly) TArray<int32> CurrentRows;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMD_OnInstructionRepairCompleted, int32, FaultId);

/** Periodic breaker incident, independent of tape-repaired cut wires. */
UCLASS(Blueprintable)
class MARINEDEMOLITION_API AMD_RepairInstructionController : public AActor, public IMD_Interactable
{
	GENERATED_BODY()
public:
	AMD_RepairInstructionController();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Instruction") TObjectPtr<UMD_FacilityFaultComponent> Fault;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Instruction") TObjectPtr<UStaticMeshComponent> BreakerBody;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Instruction") TObjectPtr<UBoxComponent> BreakerInteractionBounds;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="MD|Instruction") TObjectPtr<APawn> OperatingPawn;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="MD|Instruction") int32 OperationId = 0;
	/** One placed breaker owns all four controls; false supports older serialized fixtures. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MD|Instruction") bool bUseIntegratedKnobs = true;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Instruction") TArray<TObjectPtr<UBoxComponent>> KnobBounds;
	/** Location follows the five holes. Component scale and rotation remain artist-editable. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Instruction") TArray<TObjectPtr<UStaticMeshComponent>> KnobMeshes;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Replicated, Category="MD|Instruction") TObjectPtr<AMD_RepairInstructionControl> Guide;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Replicated, Category="MD|Instruction") TObjectPtr<AMD_RepairInstructionControl> Restart;
	/** Legacy map references only; integrated breakers do not need separate knob actors. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Replicated, Category="MD|Instruction|Legacy", meta=(AdvancedDisplay,EditCondition="!bUseIntegratedKnobs")) TArray<TObjectPtr<AMD_RepairInstructionControl>> Knobs;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Replicated, Category="MD|Instruction") TObjectPtr<AActor> InstructionDoor;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="MD|Instruction") TObjectPtr<ATriggerBox> InstructionRoom;
	/** Legacy circuit references; breaker completion never repairs these wires. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="MD|Instruction") TArray<TObjectPtr<AMD_PowerConduit>> PowerConduits;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MD|Instruction|Schedule") bool bTimedBlackoutsEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MD|Instruction|Schedule", meta=(ClampMin="0.1", Units="s")) float BlackoutIntervalSeconds = 600.f;
	/** Fixed cadence from level start; an active incident is not restarted or stacked. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="MD|Instruction|Schedule") void ConfigureBlackoutSchedule(bool bEnabled, float IntervalSeconds);
	UFUNCTION(BlueprintPure, Category="MD|Instruction|Schedule") float GetSecondsUntilTimedBlackout() const;
	UPROPERTY(ReplicatedUsing=OnRep_State, BlueprintReadOnly, Category="MD|Instruction") FMD_RepairInstructionState State;
	/** Called once after validated completion; team blackout code may also bind in Blueprint. */
	UPROPERTY(BlueprintAssignable, Category="MD|Instruction") FMD_OnInstructionRepairCompleted OnRepairCompleted;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="MD|Instruction") bool StartRepairIncident();
	UFUNCTION(BlueprintPure, Category="MD|Instruction") bool HasValidConfiguration() const;
	UFUNCTION(BlueprintPure, Category="MD|Instruction") int32 GetParticipantCount() const;
	bool ApplyControl(AMD_RepairInstructionControl* Control, APawn* Pawn, int32 ExpectedRevision);
	bool ApplyKnob(APawn* Pawn, int32 Column, int32 ExpectedRevision);
	UFUNCTION(BlueprintPure, Category="MD|Instruction") int32 GetAimedKnobColumn(const APawn* Pawn) const;
	/** Root-local hole center measured from SM_BreakerBase's front-face UVs; row zero is the top hole. */
	UFUNCTION(BlueprintPure, Category="MD|Instruction") FVector GetKnobSlotCenter(int32 Column, int32 Row) const;
	UFUNCTION(BlueprintCallable, CallInEditor, Category="MD|Instruction") void RefreshKnobs();
	int32 BeginOperation(APawn* Pawn, int32 ExpectedRevision);
	void EndOperation(APawn* Pawn, int32 ExpectedOperationId);
	bool ApplyKnobSlot(APawn* Pawn, int32 Column, int32 Row, int32 ExpectedRevision, int32 ExpectedOperationId);
	bool CanContinueOperation(const APawn* Pawn, int32 ExpectedRevision) const;
	int32 FindNearestKnobSlot(int32 Column, const FVector& RootLocalPoint) const;
	void PreviewKnobAt(int32 Column, const FVector& RootLocalSurface);
	FVector GetPanelNormal() const;
	virtual bool RequiresServerInteraction_Implementation() const override { return false; }
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;
	virtual FText GetInteractionDisplayName_Implementation() const override;
	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;
	static TArray<int32> GenerateDifferentAnswer(const TArray<int32>& Previous, FRandomStream& Random);
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	UFUNCTION() void OnRep_State();
	UFUNCTION() void HandleOwnFault(UMD_FacilityFaultComponent* Source, const FMD_FacilitySnapshot& Previous, const FMD_FacilitySnapshot& Current);
	void PublishState();
	void ApplyDoorState();
	void ReleaseDoor();
	void FinishRepair();
	bool IsInInstructionRoom(const APawn* Pawn) const;
	bool CanApplyInput(const APawn* Pawn, int32 ExpectedRevision) const;
	bool AdvanceKnob(int32 Column);
	int32 NextOperationId = 0;
	FTimerHandle BlackoutTimer;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> SuppressedSensors;
	TWeakObjectPtr<AActor> ManagedDoor;
	bool bPreviousLocked = false;
	bool bDoorOwned = false;
	bool bCompleting = false;
	FMD_RepairInstructionState LastAudioState;
	bool bAudioInitialized = false;
	FRandomStream Random;
};

/** Guide, explicit restart button, or one physical knob rail. E is routed through the player's owned component. */
UCLASS(Blueprintable)
class MARINEDEMOLITION_API AMD_RepairInstructionControl : public AActor, public IMD_Interactable
{
	GENERATED_BODY()
public:
	AMD_RepairInstructionControl();
	virtual void PostLoad() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Instruction") TObjectPtr<UBoxComponent> InteractionBounds;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Instruction") TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Instruction") TObjectPtr<UStaticMeshComponent> AuxiliaryLightBody;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Instruction") TObjectPtr<UTextRenderComponent> Heading;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Instruction") TObjectPtr<USpotLightComponent> AuxiliaryLight;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Replicated, Category="MD|Instruction") TObjectPtr<AMD_RepairInstructionController> Controller;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category="MD|Instruction") EMD_InstructionControlKind ControlKind = EMD_InstructionControlKind::Guide;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category="MD|Instruction", meta=(ClampMin="0",ClampMax="3")) int32 Column = 0;
	/** Original atlas circles supply the answer without any extra screen geometry or text. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MD|Instruction") TObjectPtr<UMaterialInterface> GuideLightMaterial;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MD|Instruction", meta=(ClampMin="0",ClampMax="20")) float GuideGlowStrength=3.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MD|Instruction", meta=(ClampMin="0.1",ClampMax="3")) float RestartFlashSeconds=.6f;
	/** Center of the original mesh's circular restart button, measured from its atlas. */
	UFUNCTION(BlueprintPure, Category="MD|Instruction") FVector GetRestartButtonWorldLocation() const;
	static FVector2D GetGuideCircleUV(int32 Column, int32 Row);
	UFUNCTION(BlueprintCallable, CallInEditor, Category="MD|Instruction") void RefreshPresentation();
	virtual bool RequiresServerInteraction_Implementation() const override { return false; }
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;
	virtual FText GetInteractionDisplayName_Implementation() const override;
	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;
private:
	UPROPERTY(Transient, DuplicateTransient) TObjectPtr<UMaterialInstanceDynamic> GuideMID;
	void RemoveLegacyOverlayComponents();
	void UpdateRestartFlash();
	void UpdateAuxiliaryLighting();
	int32 PresentedRevision = INDEX_NONE;
	int32 PresentedRow = INDEX_NONE;
	bool bPresentedActive = false;
};
