#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Component/Facility/MD_FacilityRepairComponent.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "Audio/MD_AudioSubsystem.h"
#include "MD_DoorWiringPanel.generated.h"

class UStaticMeshComponent;
class UBoxComponent;
class UNiagaraComponent;

/** Interaction proxy only. The linked door owns the one authoritative incident. */
UCLASS(ClassGroup = (MD))
class MARINEDEMOLITION_API UMD_DoorPanelRepairComponent : public UMD_FacilityRepairComponent
{
	GENERATED_BODY()
public:
	UMD_DoorPanelRepairComponent();
protected:
	virtual UMD_FacilityFaultComponent* ResolveRepairTarget() const override;
};

/** Blueprint base for a wall-mounted door repair panel. +X faces into the room. */
UCLASS(Blueprintable)
class MARINEDEMOLITION_API AMD_DoorWiringPanel : public AActor, public IMD_Interactable
{
	GENERATED_BODY()
public:
	AMD_DoorWiringPanel();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UPROPERTY(EditInstanceOnly, ReplicatedUsing = OnRep_LinkedDoor, BlueprintReadOnly, Category = "MD|Wiring Panel",
		meta = (AllowedClasses = "/Script/Engine.Actor", ToolTip = "문 BP 인스턴스만 연결합니다. 분리수거기는 직접 수리합니다."))
	TObjectPtr<AActor> LinkedDoor;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Wiring Panel")
	TObjectPtr<UStaticMeshComponent> PanelMesh;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Wiring Panel")
	TObjectPtr<UBoxComponent> InteractionBounds;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Wiring Panel")
	TObjectPtr<UMD_DoorPanelRepairComponent> FacilityRepair;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Wiring Panel")
	TObjectPtr<UNiagaraComponent> FaultSparks;
	/** NS_Wire_Cut is a burst; repeat it until this door is repaired. */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Wiring Panel", meta = (ClampMin = "0.2", Units = "s"))
	float SparkRepeatSeconds = 1.2f;
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;
	virtual FText GetInteractionDisplayName_Implementation() const override;
	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual bool IsHoldInteraction_Implementation() const override { return true; }
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual void InteractReleased_Implementation(AActor* Interactor) override;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	UFUNCTION()
	void OnRep_LinkedDoor();
	UFUNCTION()
	void HandleDoorState(UMD_FacilityFaultComponent* Facility, const FMD_FacilitySnapshot& Previous, const FMD_FacilitySnapshot& Current);
	void RefreshLinkedDoor();
	void EmitSparks();
	void StopSparkAudio();
	FMD_AudioHandle SparkAudioHandle;
	TWeakObjectPtr<UMD_FacilityFaultComponent> BoundDoorFault;
	FTimerHandle LinkTimer;
	FTimerHandle SparkTimer;
	bool bShowingFault = false;
};
