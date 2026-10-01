#pragma once

#include "CoreMinimal.h"
#include "World/Sample/MD_QuestSampleSpawner.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "MD_QuestSampleCabinet.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

/** Card-gated cabinet. The existing card pickup records access as quest phase progress. */
UCLASS(Blueprintable)
class MARINEDEMOLITION_API AMD_QuestSampleCabinet : public AMD_QuestSampleSpawner, public IMD_Interactable
{
    GENERATED_BODY()
public:
    AMD_QuestSampleCabinet();
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual bool IsRandomSpawnCandidate() const override { return false; }
    virtual bool ClaimsQuestPhase(const FPrimaryAssetId& Id, int32 Phase) const override;
    virtual FText GetInteractionDisplayName_Implementation() const override;
    virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;
    virtual bool CanInteract_Implementation(AActor* Interactor) const override;
    virtual void Interact_Implementation(AActor* Interactor) override;
    virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;

    UFUNCTION(BlueprintPure, Category="Cabinet")
    bool HasAccessCard() const;
    UFUNCTION(BlueprintPure, Category="Cabinet")
    bool IsOpen() const { return bOpened; }

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cabinet|Quest")
    FPrimaryAssetId CabinetQuestId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cabinet|Quest")
    int32 SamplePhaseIndex = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cabinet|Quest")
    int32 RequiredDay = 3;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cabinet", meta=(ClampMin="0.1"))
    float OpenDuration = 0.8f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cabinet")
    float OpenYaw = 110.f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cabinet")
    TObjectPtr<UStaticMeshComponent> BodyMesh;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cabinet")
    TObjectPtr<USceneComponent> DoorPivot;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cabinet")
    TObjectPtr<UStaticMeshComponent> DoorMesh;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cabinet")
    TObjectPtr<UStaticMeshComponent> GlassMesh;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cabinet")
    TObjectPtr<UStaticMeshComponent> SamplePreview;

protected:
    virtual void BeginPlay() override;
    virtual void TrySpawnForQuestState() override;
    UFUNCTION()
    void OnRep_Opened();
    UFUNCTION()
    void OnRep_Released();
    bool ReleaseSample();

    UPROPERTY(VisibleAnywhere, Category="Cabinet")
    TObjectPtr<UBoxComponent> ClosedDoorBlocker;
    UPROPERTY(ReplicatedUsing=OnRep_Opened)
    bool bOpened = false;
    UPROPERTY(ReplicatedUsing=OnRep_Released)
    bool bReleased = false;
    float OpenAlpha = 0.f;
};
