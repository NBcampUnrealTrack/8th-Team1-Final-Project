#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagAssetInterface.h"
#include "Gameplay/Interaction/MD_Grabbable.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "MD_Cart.generated.h"

class UMD_InteractionPromptComponent;

class APawn;
class UBoxComponent;
class UStaticMeshComponent;

/** Standalone Blueprint cart with an open physical tray, steered exclusively through its handle. */
UCLASS(Blueprintable)
class MARINEDEMOLITION_API AMD_Cart : public AActor,
	public IMD_Interactable, public IMD_Grabbable, public IGameplayTagAssetInterface
{
	GENERATED_BODY()

public:
	AMD_Cart();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	bool PrepareGrip(APawn* Pawn);
	bool ResolveSteeringTarget(APawn* Pawn, FTransform& OutTarget) const;
	bool IsSteering() const { return bSteering; }

	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;
	virtual FText GetInteractionDisplayName_Implementation() const override;
	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual void Interact_Implementation(AActor* Interactor) override {}
	virtual UPrimitiveComponent* GetGrabComponent_Implementation() const override;
	virtual USceneComponent* GetGrabPoint_Implementation() const override;
	virtual FVector GetHoldOffset_Implementation() const override { return FVector::ZeroVector; }
	virtual FMD_HoldProfile GetHoldProfile_Implementation() const override;
	virtual bool PreparePhysicsGrip_Implementation(APawn* Pawn) override;
	virtual EMD_PhysicsHoldTargetResult ResolvePhysicsHoldTarget_Implementation(APawn* Pawn, FTransform& OutTarget) const override;
	virtual void OnCarryStarted_Implementation() override;
	virtual void OnCarryEnded_Implementation() override;

	// 액터별 행동 유형을 공통 프롬프트 설정에 연결하는 구성
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Interaction")
	TObjectPtr<UMD_InteractionPromptComponent> InteractionPrompt;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Cart|Components")
	TObjectPtr<UBoxComponent> CartBody;
	/** Assign the cart mesh and its local transform in the Blueprint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Cart|Components")
	TObjectPtr<UStaticMeshComponent> Visual;
	/** Optional wheel mesh; assign its local transform in the Blueprint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Cart|Components")
	TObjectPtr<UStaticMeshComponent> WheelVisual;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Cart|Components")
	TArray<TObjectPtr<UBoxComponent>> Walls;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Cart|Components")
	TObjectPtr<UBoxComponent> Handle;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Cart|Components")
	TObjectPtr<UBoxComponent> CargoSensor;

	/** Half width, half length and half interior height around the floor-center root. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_Geometry, Category = "MD|Cart|Geometry", meta = (ClampMin = "1", Units = "cm"))
	FVector TrayHalfExtent = FVector(70.f, 95.f, 40.f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_Geometry, Category = "MD|Cart|Geometry", meta = (ClampMin = "1", Units = "cm"))
	float FloorHalfThickness = 6.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_Geometry, Category = "MD|Cart|Geometry", meta = (ClampMin = "1", Units = "cm"))
	float WallHalfThickness = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Cart|Grip", meta = (ClampMin = "30", ClampMax = "200", Units = "cm"))
	float HandleDistance = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Cart|Grip", meta = (ClampMin = "200", ClampMax = "600", Units = "cm"))
	float TetherDistance = 350.f;
	/** Both the pawn and its view must approach from the outward-facing side of the handle. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MD|Cart|Grip", meta = (ClampMin = "1", ClampMax = "89", Units = "deg"))
	float GripApproachHalfAngle = 60.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Cart|Grip", meta = (ClampMin = "0", ClampMax = "50", Units = "cm"))
	float MaximumStepHeight = 35.f;
	/** Local horizontal direction pointing outward from the rim that carries the handle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_Geometry, Category = "MD|Cart|Grip")
	FVector HandleDirection = FVector(0.f, 1.f, 0.f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Cart|Grip")
	FMD_HoldProfile SteeringProfile;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Cart|Physics", meta = (ClampMin = "10", ClampMax = "200", Units = "kg"))
	float CartMass = 65.f;
	/** Smooth limit on planar handle stretch; contact forces still push independent objects. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MD|Cart|Physics", meta = (ClampMin = "5", ClampMax = "100", Units = "cm"))
	float MaximumSteeringStretch = 30.f;
	/** Keep an imperfect support estimate from pulling the handle sharply upward. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MD|Cart|Physics", meta = (ClampMin = "0.1", ClampMax = "10", Units = "cm"))
	float MaximumVerticalStretch = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Cart|Cargo", meta = (ClampMin = "0", ClampMax = "10"))
	float CargoDamping = 4.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Cart|Cargo", meta = (ClampMin = "0", ClampMax = "600"))
	float MaxCargoAcceleration = 250.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Cart|Cargo", meta = (ClampMin = "10", ClampMax = "200", Units = "cm/s"))
	float SettledRelativeSpeed = 90.f;

private:
	UFUNCTION()
	void OnRep_Geometry();
	void ApplyGeometry();
	bool IsPawnInsideTray(const APawn* Pawn) const;
	bool IsAimingAtHandle(const APawn* Pawn) const;
	void LimitSteeringTarget(FTransform& Target) const;
	void RefreshCargo();
	UPROPERTY(Replicated)
	bool bSteering = false;

	TWeakObjectPtr<APawn> GripPawn;
	TMap<TWeakObjectPtr<UPrimitiveComponent>, float> Cargo;
	float CargoRefreshSeconds = 0.f;
};
