#pragma once

#include "CoreMinimal.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "MD_EquipmentPickup.generated.h"

class UBoxComponent;
class UPhysicalMaterial;
class UMD_InteractionPromptComponent;
class UPrimitiveComponent;
class UStaticMeshComponent;

/** One existing inventory item in the world, retaining its owner's stored tool state. */
UCLASS(Blueprintable)
class MARINEDEMOLITION_API AMD_EquipmentPickup : public AActor, public IMD_Interactable
{
	GENERATED_BODY()

public:
	AMD_EquipmentPickup();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;
	virtual FText GetInteractionDisplayName_Implementation() const override;
	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual void Interact_Implementation(AActor* Interactor) override;

	/** Prepare before inventory removal; preparation alone does not allow pickup. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Equipment|Throw")
	bool InitializeItem(const FMD_HotbarToolEntry& Entry);

	UFUNCTION(BlueprintPure, Category = "MD|Equipment|Throw")
	UPrimitiveComponent* GetThrowPrimitive() const;

	/** Make the prepared item available only after inventory ownership was removed. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Equipment|Throw")
	void ActivatePickup();

	UFUNCTION(BlueprintPure, Category = "MD|Equipment|Throw")
	FMD_HotbarToolEntry GetItemPayload() const { return ItemPayload; }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditDefaultsOnly, Category = "MD|Equipment|Physics")
	TObjectPtr<UPhysicalMaterial> ContactMaterial;

	// 액터에서 선택하고 공용 설정에서 문구를 관리하는 안내 유형
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Interaction")
	TObjectPtr<UMD_InteractionPromptComponent> InteractionPrompt;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Equipment")
	TObjectPtr<UBoxComponent> PhysicsRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Equipment")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** A simple body supports tools whose visual mesh has no authored collision. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Equipment|Physics", meta = (ClampMin = "0.1", Units = "cm"))
	float MinimumCollisionExtent = 2.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Equipment|Physics", meta = (ClampMin = "0.01", Units = "kg"))
	float ItemMassKg = 0.5f;

	UPROPERTY(ReplicatedUsing = OnRep_ItemPayload, BlueprintReadOnly, Category = "MD|Equipment")
	FMD_HotbarToolEntry ItemPayload;

	UPROPERTY(ReplicatedUsing = OnRep_PickupActive, BlueprintReadOnly, Category = "MD|Equipment")
	bool bPickupActive = false;

private:
	UFUNCTION()
	void OnRep_ItemPayload();

	UFUNCTION()
	void OnRep_PickupActive();

	bool ApplyItemVisual();
	void RefreshPickupState();
	bool bTransferInProgress = false;
};
