#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/Interaction/MD_Grabbable.h"
#include "MD_InteractionComponent.generated.h"

class AActor;
class APawn;
class UCameraComponent;
class UPhysicsHandleComponent;
class UPrimitiveComponent;
class USceneComponent;

/**
 * 플레이어의 조준 상호작용과 물리 오브젝트 운반을 담당
 */
UCLASS(ClassGroup = (MD), meta = (BlueprintSpawnableComponent))
class MARINEDEMOLITION_API UMD_InteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMD_InteractionComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/** Character가 만든 시점·운반용 서브오브젝트를 연결한다. Character 생성자에서 한 번 호출한다. */
	void SetInteractionDependencies(
		UCameraComponent* InCamera,
		USceneComponent* InHoldPoint,
		UPhysicsHandleComponent* InPhysicsHandle);

	/** 현재 조준 중인 대상과 상호작용하거나, 들고 있다면 놓는다. */
	UFUNCTION(BlueprintCallable, Category = "MD|Interaction")
	void Interact();

	UFUNCTION(BlueprintPure, Category = "MD|Interaction")
	AActor* GetHeldActor() const { return HeldActor; }

	UFUNCTION(BlueprintPure, Category = "MD|Interaction")
	bool IsHolding() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, Category = "MD|Interaction", meta = (ClampMin = "0.0", Units = "cm"))
	float InteractTraceDistance = 200.f;

	/** 잡은 물체가 HoldPoint에 이만큼 가까워지면 운반이 안정된 것으로 본다. */
	UPROPERTY(EditAnywhere, Category = "MD|Interaction",
		meta = (ClampMin = "1.0", UIMin = "5.0", Units = "cm"))
	float CarrySettleTolerance = 20.f;

	/** 벽에 걸려 목표에 영영 못 닿는 경우를 대비한 상한. 지나면 안정된 것으로 친다. */
	UPROPERTY(EditAnywhere, Category = "MD|Interaction",
		meta = (ClampMin = "0.1", UIMin = "0.5", Units = "s"))
	float CarrySettleTimeout = 1.5f;

private:
	UFUNCTION(Server, Reliable)
	void ServerInteract(AActor* RequestedActor);

	bool TryHoldTarget(AActor* Target);
	bool TryGrabPhysicsComponent(UPrimitiveComponent* Component, const FVector& PreferredGrabLocation,
		const FMD_HoldProfile& HoldProfile);
	bool ValidateInteractionTarget(AActor* RequestedActor) const;
	bool IsHeldByOtherPawn(const AActor* Target) const;
	void ReleaseTarget();
	void UpdateHeldTarget();
	void CaptureDefaultHoldProfile();
	void ApplyHoldProfile(const FMD_HoldProfile& Profile);
	void UpdateInteractionFocus();
	AActor* TraceInteractionTarget() const;
	void SetCurrentTarget(AActor* NewTarget);
	FTransform GetHoldPointTransform() const;
	void UpdateCarrySettle(float DeltaSeconds);
	void StartFollowingHeldActor();
	void StopFollowingHeldActor();
	void UpdateFollowedHeldActor(float DeltaSeconds);
	APawn* GetOwnerPawn() const;

	UFUNCTION()
	void OnRep_HeldActor();

	UPROPERTY(Transient)
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> HoldPoint;

	UPROPERTY(Transient)
	TObjectPtr<UPhysicsHandleComponent> PhysicsHandle;

	TWeakObjectPtr<AActor> CurrentTarget;

	/** 서버가 잡은 대상. 모든 머신이 같은 대상을 HoldPoint로 따라가도록 복제한다. */
	UPROPERTY(ReplicatedUsing = OnRep_HeldActor)
	TObjectPtr<AActor> HeldActor = nullptr;

	TWeakObjectPtr<UPrimitiveComponent> FollowedComponent;
	FVector HeldTargetOffset = FVector::ZeroVector;
	FVector HeldGrabPointOffset = FVector::ZeroVector;
	bool bHeldActorSettled = false;
	float HeldCarrySeconds = 0.f;
	FMD_HoldProfile DefaultHoldProfile;
	FMD_HoldProfile ActiveHoldProfile;
};
