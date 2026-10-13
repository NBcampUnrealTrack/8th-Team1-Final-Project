#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "MD_FingerprintPad.generated.h"

class UMD_InteractionPromptComponent;

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
	/** Reserved for the fault locator, distinct from hover and handheld scanner stencils. */
	static constexpr int32 FaultHighlightStencilValue = 249;
	virtual void OnConstruction(const FTransform& Transform) override;
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
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MD|Fingerprint|Presentation")
	FVector2D FingerprintSize = FVector2D(14.f, 19.f);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MD|Fingerprint|Presentation")
	FVector FingerprintOffset = FVector(9.2f, 0.f, 0.f);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MD|Fingerprint|Presentation", meta=(ClampMin="0.05", ClampMax="2.0"))
	float ScanFadeInSeconds = 0.45f;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="MD|Fingerprint")
	TObjectPtr<APawn> ActiveScanner;
	/** Also survives scans completed in the same frame, unlike ActiveScanner. */
	UPROPERTY(Replicated)
	double ScanVisualStartTime = -1.;
	void NotifyScanAccepted();
	bool CanMaintainScan(const APawn* Pawn) const;
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;
	virtual FText GetInteractionDisplayName_Implementation() const override;
	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual bool IsHoldInteraction_Implementation() const override { return true; }
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual void InteractReleased_Implementation(AActor* Interactor) override;
	// 액터별 행동 유형을 공통 프롬프트 설정에 연결하는 구성
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Interaction")
	TObjectPtr<UMD_InteractionPromptComponent> InteractionPrompt;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	void RefreshHighlight();
	bool bInteractionHighlighted = false;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DisplayMaterial;
	void ApplyDisplayLayout();
	float DisplayOpacity = 1.f;
	int32 LastPresentationState = INDEX_NONE;
};
