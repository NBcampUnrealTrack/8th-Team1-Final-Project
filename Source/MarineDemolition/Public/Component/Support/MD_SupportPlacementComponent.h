#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MD_SupportPlacementComponent.generated.h"

class APawn;
class AController;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMesh;
class UStaticMeshComponent;

/** Local presentation only; the placement ability repeats the query on the server. */
UCLASS(ClassGroup = (MD), meta = (BlueprintSpawnableComponent))
class MARINEDEMOLITION_API UMD_SupportPlacementComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UMD_SupportPlacementComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void RefreshEquippedState();
	UFUNCTION()
	void HandleControllerChanged(APawn* Pawn, AController* PreviousController, AController* NewController);
	void DestroyPreview();
	void UpdatePreview();

	/** 캐릭터 Blueprint의 SupportPlacementComponent에서 직접 지정한다. 자동 대체 에셋은 없다. */
	UPROPERTY(EditDefaultsOnly, Category = "Support|Preview")
	TObjectPtr<UMaterialInterface> HologramMaterial = nullptr;
	UPROPERTY(EditDefaultsOnly, Category = "Support|Preview")
	TObjectPtr<UMaterialInterface> RangeMaterial = nullptr;
	UPROPERTY(EditDefaultsOnly, Category = "Support|Preview")
	TObjectPtr<UStaticMesh> RangeMesh = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Ghost;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Range;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GhostMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RangeMID;
	bool bLastValid = false;
};
