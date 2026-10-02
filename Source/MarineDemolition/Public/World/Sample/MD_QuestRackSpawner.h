#pragma once

#include "CoreMinimal.h"
#include "World/Sample/MD_QuestSampleSpawner.h"
#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "GameplayTagAssetInterface.h"
#include "MD_QuestRackSpawner.generated.h"

class UDecalComponent;
class UStaticMesh;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EMD_QuestRackMode : uint8
{
	Contamination UMETA(DisplayName = "Rack 1 - Clean 50 percent"),
	// Retain the serialized enum name/value for existing Blueprint assets and saves.
	BarnaclesAndPadlock UMETA(DisplayName = "Rack 2 - Cut padlock"),
	Open UMETA(DisplayName = "Rack 3 - Open access")
};

/** Same physical scraping/replication/visual implementation as cuttable walls. */
UCLASS()
class MARINEDEMOLITION_API UMD_QuestRackScrapeSurface : public UMD_ScrapeSurfaceComponent
{
	GENERATED_BODY()
public:
	UMD_QuestRackScrapeSurface();
	void ConfigureRackBeforePlay(UStaticMeshComponent* RackMesh, UDecalComponent* Front, UDecalComponent* Back, bool bEnabled);
	bool CompleteCleaningWithoutDrops();
};

/** A deterministic, one-cartridge rack. Three authored racks can serve one two-item quest. */
UCLASS(Blueprintable)
class MARINEDEMOLITION_API AMD_QuestRackSpawner : public AMD_QuestSampleSpawner,
	public IGameplayTagAssetInterface, public IMD_Interactable
{
	GENERATED_BODY()
public:
	AMD_QuestRackSpawner();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual bool IsRandomSpawnCandidate() const override { return false; }
	virtual bool ClaimsQuestPhase(const FPrimaryAssetId& DefinitionId, int32 PhaseIndex) const override;
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual FText GetInteractionDisplayName_Implementation() const override;
	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;
	virtual bool RequiresServerInteraction_Implementation() const override { return true; }
	virtual void Interact_Implementation(AActor* Interactor) override;

	UFUNCTION(BlueprintPure, Category="MD|Quest Rack")
	float GetContaminationRemovedRatio() const;
	UFUNCTION(BlueprintPure, Category="MD|Quest Rack")
	float GetPadlockCutRatio() const;
	UFUNCTION(BlueprintPure, Category="MD|Quest Rack")
	bool IsPadlockCut() const { return bPadlockCut; }
	UFUNCTION(BlueprintPure, Category="MD|Quest Rack")
	bool IsCartridgeReleased() const { return bCartridgeReleased; }
	UFUNCTION(BlueprintPure, Category="MD|Quest Rack")
	int32 GetRemainingBarnacles() const { return 0; }
	UFUNCTION(BlueprintPure, Category="MD|Quest Rack")
	bool IsRackDisabled() const { return bRackDisabled; }
	UFUNCTION(BlueprintPure, Category="MD|Quest Rack")
	bool IsCartridgeCollected() const { return bCartridgeCollected; }
	bool CanCollectCartridge(const AMD_QuestItem* Item) const;
	void NotifyCartridgeGrabbed(AMD_QuestItem* Item);

	/** Only the lock mesh during the active quest. Never cuts the rack. */
	bool CanCutPadlockFromHit(const FHitResult& Hit) const;
	/** Called only by the existing server cutter ability. Revalidates equipment and line of sight. */
	void AddPadlockCutProgress(float WorkSeconds, AActor* Cutter, const FHitResult& Hit);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Quest Rack|Components")
	TObjectPtr<UStaticMeshComponent> RackMesh;
	/** A front work panel; excludes unreachable rack interiors from cleaning progress. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Quest Rack|Components")
	TObjectPtr<UStaticMeshComponent> RackWorkSurface;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Quest Rack|Components")
	TObjectPtr<UStaticMeshComponent> PadlockMesh;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Quest Rack|Components")
	TObjectPtr<UMD_QuestRackScrapeSurface> RackScrapeSurface;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Quest Rack|Components")
	TObjectPtr<UDecalComponent> FrontContaminationDecal;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Quest Rack|Components")
	TObjectPtr<UDecalComponent> BackContaminationDecal;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_RackState, Category="MD|Quest Rack")
	EMD_QuestRackMode RackMode = EMD_QuestRackMode::Open;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MD|Quest Rack")
	FPrimaryAssetId RackQuestDefinitionId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MD|Quest Rack", meta=(ClampMin="0"))
	int32 RackPhaseIndex = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MD|Quest Rack", meta=(ClampMin="0.0", ClampMax="1.0"))
	float RequiredCleanRatio = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MD|Quest Rack", meta=(ClampMin="0.1", Units="s"))
	float RequiredPadlockCutSeconds = 3.6f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MD|Quest Rack", meta=(ClampMin="1.0", Units="cm"))
	float PadlockReachCm = 200.f;
	/** Optional visual variant, required to derive from the quest's required item class. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MD|Quest Rack")
	TSubclassOf<AMD_QuestItem> RackCartridgeClass;

protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TrySpawnForQuestState() override;

private:
	void ConfigureRackComponents();
	bool IsAccessRequirementMet() const;
	// 랙에 저장된 물품 구성으로 회수용 카트리지 생성
	AMD_QuestItem* SpawnCartridge();
	UFUNCTION() void OnRep_RackState();

	UPROPERTY(ReplicatedUsing=OnRep_RackState) bool bQuestActive = false;
	UPROPERTY(ReplicatedUsing=OnRep_RackState) bool bPadlockCut = false;
	UPROPERTY(Replicated) float PadlockCutSeconds = 0.f;
	/** Access is ready; the item is created only by a subsequent E interaction. */
	UPROPERTY(ReplicatedUsing=OnRep_RackState) bool bCartridgeReleased = false;
	UPROPERTY(ReplicatedUsing=OnRep_RackState) bool bCartridgeCollected = false;
	UPROPERTY(ReplicatedUsing=OnRep_RackState) bool bRackDisabled = false;
	UPROPERTY(ReplicatedUsing=OnRep_RackState) TObjectPtr<AMD_QuestItem> ReleasedCartridge;
	FTimerHandle GateEvaluationTimer;
	bool bCollectionInProgress = false;
	bool bRackEndingPlay = false;
	bool bSpawnFailureLogged = false;
};
