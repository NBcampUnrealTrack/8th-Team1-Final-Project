#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Component/Facility/MD_FacilityFaultComponent.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "Types/Interaction/MD_InteractionTypes.h"
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
	/** Zero-based internally; the board numbers slots from the top, starting at one. */
	UPROPERTY(BlueprintReadOnly) TArray<int32> TargetRows;
	UPROPERTY(BlueprintReadOnly) TArray<int32> CurrentRows;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMD_OnInstructionRepairCompleted, int32, FaultId);

/** Subscribes to the existing wire faults. Does not schedule blackouts or replace light management. */
UCLASS(Blueprintable)
class MARINEDEMOLITION_API AMD_RepairInstructionController : public AActor
{
	GENERATED_BODY()
public:
	AMD_RepairInstructionController();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Instruction") TObjectPtr<UMD_FacilityFaultComponent> Fault;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Instruction") TObjectPtr<UStaticMeshComponent> BreakerBody;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Replicated, Category="MD|Instruction") TObjectPtr<AMD_RepairInstructionControl> Guide;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Replicated, Category="MD|Instruction") TObjectPtr<AMD_RepairInstructionControl> Restart;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Replicated, Category="MD|Instruction") TArray<TObjectPtr<AMD_RepairInstructionControl>> Knobs;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Replicated, Category="MD|Instruction") TObjectPtr<AActor> InstructionDoor;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="MD|Instruction") TObjectPtr<ATriggerBox> InstructionRoom;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="MD|Instruction") TArray<TObjectPtr<AMD_PowerConduit>> PowerConduits;
	UPROPERTY(ReplicatedUsing=OnRep_State, BlueprintReadOnly, Category="MD|Instruction") FMD_RepairInstructionState State;
	/** Called once after validated completion; team blackout code may also bind in Blueprint. */
	UPROPERTY(BlueprintAssignable, Category="MD|Instruction") FMD_OnInstructionRepairCompleted OnRepairCompleted;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="MD|Instruction") bool StartRepairIncident();
	UFUNCTION(BlueprintPure, Category="MD|Instruction") bool HasValidConfiguration() const;
	UFUNCTION(BlueprintPure, Category="MD|Instruction") int32 GetParticipantCount() const;
	bool ApplyControl(AMD_RepairInstructionControl* Control, APawn* Pawn, int32 ExpectedRevision);
	static TArray<int32> GenerateDifferentAnswer(const TArray<int32>& Previous, FRandomStream& Random);
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	UFUNCTION() void OnRep_State();
	UFUNCTION() void HandlePowerFault(UMD_FacilityFaultComponent* Source, const FMD_FacilitySnapshot& Previous, const FMD_FacilitySnapshot& Current);
	UFUNCTION() void HandleOwnFault(UMD_FacilityFaultComponent* Source, const FMD_FacilitySnapshot& Previous, const FMD_FacilitySnapshot& Current);
	void PublishState();
	void ApplyDoorState();
	void ReleaseDoor();
	void FinishRepair();
	bool IsInInstructionRoom(const APawn* Pawn) const;
	void TrackBrokenPowerSources();
	TMap<TWeakObjectPtr<UMD_FacilityFaultComponent>, int32> SourceIncidents;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> SuppressedSensors;
	TWeakObjectPtr<AActor> ManagedDoor;
	bool bPreviousLocked = false;
	bool bDoorOwned = false;
	bool bCompleting = false;
	FRandomStream Random;
};

/** Guide, explicit restart button, or one physical knob rail. E is routed through the player's owned component. */
UCLASS(Blueprintable)
class MARINEDEMOLITION_API AMD_RepairInstructionControl : public AActor, public IMD_Interactable
{
	GENERATED_BODY()
public:
	AMD_RepairInstructionControl();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Instruction") TObjectPtr<UBoxComponent> InteractionBounds;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Instruction") TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Instruction") TObjectPtr<UStaticMeshComponent> ScreenSurface;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Instruction") TObjectPtr<UStaticMeshComponent> AuxiliaryLightBody;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Instruction") TObjectPtr<UTextRenderComponent> Heading;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Instruction") TObjectPtr<USpotLightComponent> AuxiliaryLight;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Replicated, Category="MD|Instruction") TObjectPtr<AMD_RepairInstructionController> Controller;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category="MD|Instruction") EMD_InstructionControlKind ControlKind = EMD_InstructionControlKind::Guide;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category="MD|Instruction", meta=(ClampMin="0",ClampMax="3")) int32 Column = 0;
	// 지침 판독 장치가 사용할 공통 행동 문구 유형
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Instruction|Interaction")
	EMD_InteractionPromptType GuidePromptType = EMD_InteractionPromptType::UseInstruction;
	// 재시작 장치가 사용할 공통 행동 문구 유형
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Instruction|Interaction")
	EMD_InteractionPromptType RestartPromptType = EMD_InteractionPromptType::RestartInstruction;
	// 노브 조작에 현재 행 정보를 덧붙일 공통 행동 문구 유형
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Instruction|Interaction")
	EMD_InteractionPromptType KnobPromptType = EMD_InteractionPromptType::AdvanceBreakerRow;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Instruction") TArray<TObjectPtr<UStaticMeshComponent>> Markers;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Instruction") TArray<TObjectPtr<UTextRenderComponent>> Labels;
	void RefreshPresentation();
	virtual bool RequiresServerInteraction_Implementation() const override { return false; }
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;
	virtual FText GetInteractionDisplayName_Implementation() const override;
	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;
private:
	int32 PresentedRevision = INDEX_NONE;
	int32 PresentedRow = INDEX_NONE;
	bool bPresentedActive = false;
};
