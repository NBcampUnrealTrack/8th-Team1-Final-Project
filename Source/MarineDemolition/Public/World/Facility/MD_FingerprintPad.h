#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "MD_FingerprintPad.generated.h"

class AMD_FingerprintInterlock;
class UBoxComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UTextRenderComponent;
class APawn;
class UMaterialInstanceDynamic;

UCLASS(Blueprintable)
class MARINEDEMOLITION_API AMD_FingerprintPad : public AActor, public IMD_Interactable
{
	GENERATED_BODY()
public:
	AMD_FingerprintPad();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Fingerprint")
	TObjectPtr<UBoxComponent> InteractionBounds;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Fingerprint")
	TObjectPtr<UStaticMeshComponent> PadMesh;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Fingerprint")
	TObjectPtr<UStaticMeshComponent> FingerprintDisplay;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Fingerprint")
	TObjectPtr<UPointLightComponent> StatusLight;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Fingerprint")
	TObjectPtr<UTextRenderComponent> StatusText;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Replicated, Category="MD|Fingerprint")
	TObjectPtr<AMD_FingerprintInterlock> Interlock;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Replicated, Category="MD|Fingerprint", meta=(ClampMin="0",ClampMax="1"))
	int32 PadIndex = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MD|Fingerprint", meta=(ClampMin="1"))
	float ScanReach = 250.f;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="MD|Fingerprint")
	TObjectPtr<APawn> ActiveScanner;
	bool CanMaintainScan(const APawn* Pawn) const;
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
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DisplayMaterial;
};
