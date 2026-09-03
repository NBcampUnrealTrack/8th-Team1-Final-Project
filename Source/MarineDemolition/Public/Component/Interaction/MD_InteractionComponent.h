#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "Gameplay/Interaction/MD_Grabbable.h"
#include "MD_InteractionComponent.generated.h"

class AActor;
class APawn;
class UCameraComponent;
class UPhysicsHandleComponent;
class UPrimitiveComponent;
class USceneComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMDOnInteractionTargetChanged, AActor*, NewTarget);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMDOnInteractionPromptChanged, FText, PromptText);

/**
 * 플레이어의 조준 상호작용과 물리 오브젝트 운반을 담당
 */
UCLASS(ClassGroup = (MD))
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

	/** Clears local focus and releases any authoritative carry state. Safe to call during death/teleport/teardown. */
	UFUNCTION(BlueprintCallable, Category = "MD|Interaction")
	void CancelInteraction();

	/** Releases the held actor on the server. Client calls are intentionally ignored. */
	UFUNCTION(BlueprintCallable, Category = "MD|Interaction")
	void ReleaseHeldActor();

	/** Blocks new interactions and immediately cancels the current interaction when blocked. */
	UFUNCTION(BlueprintCallable, Category = "MD|Interaction")
	void SetInteractionBlocked(bool bBlocked);

	UFUNCTION(BlueprintPure, Category = "MD|Interaction")
	AActor* GetHeldActor() const { return HeldActor; }

	/** 로컬 플레이어가 현재 조준 중인 상호작용 가능 대상. */
	UFUNCTION(BlueprintPure, Category = "MD|Interaction")
	AActor* GetCurrentTarget() const { return CurrentTarget.Get(); }

	/** 현재 대상의 데이터 기반 표시 이름. 대상이나 이름이 없으면 빈 텍스트. */
	UFUNCTION(BlueprintPure, Category = "MD|Interaction")
	FText GetCurrentTargetDisplayName() const;

	/** 일반 E 상호작용과 현재 장착 도구의 GA 조준을 합친 최종 HUD 문구. */
	UFUNCTION(BlueprintPure, Category = "MD|Interaction")
	FText GetCurrentInteractionPrompt() const { return CurrentInteractionPrompt; }

	/** 로컬 플레이어가 조준하는 상호작용 가능 대상이 바뀔 때 방송한다. */
	UPROPERTY(BlueprintAssignable, Category = "MD|Interaction")
	FMDOnInteractionTargetChanged OnInteractionTargetChanged;

	/** 대상·도구·중앙 셀 상태 중 하나가 바뀌어 최종 HUD 문구가 달라질 때 방송한다. */
	UPROPERTY(BlueprintAssignable, Category = "MD|Interaction")
	FMDOnInteractionPromptChanged OnInteractionPromptChanged;

	UFUNCTION(BlueprintPure, Category = "MD|Interaction")
	bool IsHolding() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, Category = "MD|Interaction", meta = (ClampMin = "0.0", Units = "cm"))
	float InteractTraceDistance = 200.f;

	/** 커팅·스캔 도구의 중앙 조준 프롬프트가 확인할 거리. GA 기본 TraceDistance와 같다. */
	UPROPERTY(EditAnywhere, Category = "MD|Interaction", meta = (ClampMin = "0.0", Units = "cm"))
	float AbilityPromptTraceDistance = 200.f;

	/** 서버에서 새 상호작용 요청을 받아들이는 최소 간격. 운반 해제 요청에는 적용하지 않는다. */
	UPROPERTY(EditAnywhere, Category = "MD|Interaction",
		meta = (ClampMin = "0.05", ClampMax = "0.1", Units = "s"))
	float ServerInteractionMinInterval = 0.075f;

	/** 잡은 물체가 HoldPoint에 이만큼 가까워지면 운반이 안정된 것으로 본다. */
	UPROPERTY(EditAnywhere, Category = "MD|Interaction",
		meta = (ClampMin = "1.0", UIMin = "5.0", Units = "cm"))
	float CarrySettleTolerance = 20.f;

	/** 벽에 걸려 목표에 영영 못 닿는 경우를 대비한 감시 상한. 지나면 정착 콜백 없이 감시만 끝낸다. */
	UPROPERTY(EditAnywhere, Category = "MD|Interaction",
		meta = (ClampMin = "0.1", UIMin = "0.5", Units = "s"))
	float CarrySettleTimeout = 1.5f;

	/**
	 * 홀드 목표점을 월드에 대고 훑을 때 쓰는 구체 반지름을 물체 바운드에서 뽑는 배율.
	 * 1이면 물체가 지오메트리에 절대 안 겹치고, 낮추면 시선을 더 충실히 따라오는 대신 살짝 파고든다.
	 */
	UPROPERTY(EditAnywhere, Category = "MD|Interaction",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HoldProbeRadiusScale = 1.f;

	/** 작은 물체도 최소한 이만큼은 여유를 두고 멈춘다. */
	UPROPERTY(EditAnywhere, Category = "MD|Interaction",
		meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float HoldProbeMinRadius = 8.f;

	/**
	 * 홀드 목표점이 카메라 쪽으로 붕괴할 수 있는 하한.
	 *
	 * 벽에 붙으면 스윕이 목표점을 머리 안쪽까지 당기고, 스프링은 물체를 거기로 끌어당긴다.
	 * 이 값보다 가까워지지 않게 잡아 물체가 시야 앞에 남도록 한다. 벽이 이보다 가까우면
	 * 목표가 벽 너머에 서지만, 캡슐이 물체를 막아주는 한 정지 접촉이라 발산하지 않는다.
	 */
	UPROPERTY(EditAnywhere, Category = "MD|Interaction",
		meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float HoldMinDistance = 60.f;

	/**
	 * 운반 중인 물체의 Pawn 채널 응답을 Ignore로 낮출지 여부.
	 *
	 * 기본값 false. 캐릭터 캡슐은 kinematic이라 dynamic 물체가 솔버로 밀지 못하고,
	 * 물체가 캐릭터를 날리는 실제 경로(무브먼트 베이스)는 IgnoreComponentWhenMoving만으로
	 * 막힌다. 반대로 Ignore로 낮추면 물체를 막아설 것이 없어져 머리 뒤로 통과한다.
	 *
	 * 그래도 발사 현상이 재발하면 여기를 켜서 원인을 좁힌다. 켜는 순간 통과 문제가 같이 돌아온다.
	 */
	UPROPERTY(EditAnywhere, Category = "MD|Interaction")
	bool bIsolateCarriedFromPawnChannel = false;

private:
	UFUNCTION(Server, Reliable)
	void ServerInteract(AActor* RequestedActor);

	bool TryHoldTarget(AActor* Target);
	bool TryGrabPhysicsComponent(UPrimitiveComponent* Component, const FVector& PreferredGrabLocation,
		const FMD_HoldProfile& HoldProfile);

	/**
	 * 운반 중인 물체와 운반자 캡슐의 충돌을 끊는다. 물리를 켜기 전에 호출해야 한다.
	 *
	 * HoldPoint는 Camera의 자식이라 시선을 내리면 캡슐 안쪽을 가리킨다. 그 상태에서
	 * PhysicsHandle 스프링이 물체를 캡슐로 끌어당기면 물체가 캡슐을 밀고, 밀린 캡슐을 따라
	 * 목표점이 도망가고, 스프링이 더 세게 당기는 되먹임이 걸려 둘 다 발사된다.
	 */
	void BeginCarryCollisionIsolation(UPrimitiveComponent* Component);

	/** BeginCarryCollisionIsolation을 되돌린다. 호출 횟수와 무관하게 안전하다. */
	void EndCarryCollisionIsolation();

	/** 운반자의 캡슐. 없으면 nullptr. */
	UPrimitiveComponent* GetOwnerCollisionComponent() const;

	/**
	 * HoldPoint가 가리키는 지점을 월드 지오메트리 앞까지 당겨서 돌려준다.
	 *
	 * HoldPoint는 카메라 기준 앞쪽 고정 거리라, 시선을 내리면 목표점이 바닥 밑으로 들어간다.
	 * 도달 불가능한 목표를 스프링이 계속 당기고 바닥이 계속 밀어내면 평형점이 없어 떨린다.
	 * 목표를 항상 빈 공간에 두면 그 싸움 자체가 없어진다.
	 */
	FVector ResolveHoldTargetLocation(const FTransform& HoldTransform) const;
	bool ValidateInteractionTarget(AActor* RequestedActor) const;
	bool IsHeldByOtherPawn(const AActor* Target) const;
	bool CanOwnerInteract() const;
	bool HasAuthorityPhysicsGrip() const;
	void RefreshTickEnabled();
	void UpdateHeldTarget();
	void CaptureDefaultHoldProfile();
	void ApplyHoldProfile(const FMD_HoldProfile& Profile);
	void UpdateInteractionFocus();
	void UpdateInteractionPrompt();
	AActor* TraceInteractionTarget() const;
	FText ResolveCurrentInteractionPrompt() const;
	FText ResolveAbilityInteractionPrompt() const;
	bool TryTraceAimedWall(class AMD_CuttableWall*& OutWall, FHitResult& OutHit) const;
	void SetCurrentInteractionPrompt(const FText& NewPrompt);
	void SetCurrentTarget(AActor* NewTarget);
	FTransform GetHoldPointTransform() const;
	void UpdateCarrySettle(float DeltaSeconds);
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

	UPROPERTY(Transient)
	FText CurrentInteractionPrompt;

	/** 서버가 잡은 대상. 운반 상태 UI를 위해 복제하며 위치는 대상 액터의 서버 이동 복제를 따른다. */
	UPROPERTY(ReplicatedUsing = OnRep_HeldActor)
	TObjectPtr<AActor> HeldActor = nullptr;

	FVector HeldTargetOffset = FVector::ZeroVector;
	FVector HeldGrabPointOffset = FVector::ZeroVector;

	/** 충돌을 끊어둔 운반 중인 컴포넌트. 해제 시 원복 대상이다. */
	TWeakObjectPtr<UPrimitiveComponent> CarryIsolatedComponent;

	/** 끊기 전 Pawn 채널 응답. 프리팹마다 다를 수 있어 값을 기억했다가 그대로 되돌린다. */
	ECollisionResponse CarryIsolatedPawnResponse = ECR_Block;

	bool bHasCarryCollisionIsolation = false;

	UPROPERTY(Transient)
	bool bInteractionBlocked = false;

	double LastServerInteractionTimeSeconds = 0.0;
	bool bHasServerInteractionTimestamp = false;
	bool bHeldActorSettled = false;
	float HeldCarrySeconds = 0.f;
	FMD_HoldProfile DefaultHoldProfile;
	FMD_HoldProfile ActiveHoldProfile;
};
