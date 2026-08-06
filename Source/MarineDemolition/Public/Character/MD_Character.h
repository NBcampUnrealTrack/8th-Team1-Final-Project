// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "AbilitySystemInterface.h"
#include "Gameplay/Interaction/MD_Grabbable.h"
#include "Types/State/MD_HealthTypes.h"
#include "MD_Character.generated.h"

class UMD_InputConfig;
class UInputMappingContext;
class UInputAction;
class UCameraComponent;
class UStaticMeshComponent;
class USceneComponent;
class USkeletalMeshComponent;
class UPhysicsHandleComponent;
class UMD_CharacterMovementComponent;
class UMD_ScrapeToolComponent;
class UGameplayAbility;
class UMD_EquipHotbarComponent;
class UMD_ToolDefinition;
class UMD_SupplyComponent;

UENUM(BlueprintType)
enum class EMDMovementState : uint8
{
	Walking,
	Swimming
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMDOnMovementStateChanged, EMDMovementState, NewMovementState);

UCLASS()
class MARINEDEMOLITION_API AMD_Character : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	AMD_Character(const FObjectInitializer& ObjectInitializer);

	virtual void Tick(float DeltaSeconds) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(BlueprintAssignable, Category = "Movement|Water")
	FMDOnMovementStateChanged OnMovementStateChanged;

	UFUNCTION(BlueprintPure, Category = "Movement|Water")
	EMDMovementState GetCurrentMovementState() const { return CurrentMovementState; }

	UFUNCTION(BlueprintCallable, Category = "Movement|Water")
	void ForceEnterWalkState();

	UFUNCTION(BlueprintPure, Category = "MD|Scrape")
	UMD_ScrapeToolComponent* GetScrapeToolComponent() const	{ return ScrapeToolComponent; }

	UFUNCTION(BlueprintPure, Category = "Equipment")
	UMD_EquipHotbarComponent* GetHotbarComponent() const { return EquipHotbarComponent; }

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode) override;

	virtual void PossessedBy(AController* NewController) override;

	virtual void OnRep_PlayerState() override;
public:
	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
protected:

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USkeletalMeshComponent> FirstPersonMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMD_ScrapeToolComponent> ScrapeToolComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMD_EquipHotbarComponent> EquipHotbarComponent;

	// 3인칭 도구 메시 (타 플레이어에게만 보임)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ThirdPersonToolMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	FName ToolAttachSocketName = TEXT("hand_r");

	/** 모든 물리 픽업이 공유하는 운반 기준점. Camera에 부착되며 대상별 배치는 GetHoldOffset()으로 조정한다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> HoldPoint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPhysicsHandleComponent> PhysicsHandle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMD_SupplyComponent> SupplyComponent;

	/** 모선 리스폰 지점을 찾을 때 사용할 PlayerStart 태그 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Health")
	FName MothershipRespawnTag = TEXT("MothershipRespawn");

protected:

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> PlayerMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UMD_InputConfig> InputConfig;

	UPROPERTY(EditAnywhere, Category = "Interact")
	float InteractTraceDistance = 200.f;

	/** 잡은 물체가 HoldPoint에 이만큼 가까워지면 운반이 안정된 것으로 본다 */
	UPROPERTY(EditAnywhere, Category = "Interact",
		meta = (ClampMin = "1.0", UIMin = "5.0", Units = "cm"))
	float CarrySettleTolerance = 20.f;

	/** 벽에 걸려 목표에 영영 못 닿는 경우를 대비한 상한. 지나면 안정된 것으로 친다 */
	UPROPERTY(EditAnywhere, Category = "Interact",
		meta = (ClampMin = "0.1", UIMin = "0.5", Units = "s"))
	float CarrySettleTimeout = 1.5f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Water")
	EMDMovementState CurrentMovementState = EMDMovementState::Walking;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Abilities")
	TArray<TSubclassOf<UGameplayAbility>> DefaultAbilities;

	//UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Abilities")
	//TSubclassOf<UGameplayAbility> WallCutAbilityClass;
	
	//UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Abilities")
	//TSubclassOf<UGameplayAbility> WallScanAbilityClass;

private:

	void HandleMoveInput(const FInputActionValue& Value);
	void HandleLookInput(const FInputActionValue& Value);
	void HandleInteractInput(const FInputActionValue& Value);
	void HandleAbilityInput(const FInputActionValue& Value);
	void HandleAbilityReleasedInput(const FInputActionValue& Value);
	void HandleSwapEquipmentInput(const FInputActionValue& Value);
	void HandleJumpOrSwimInput(const FInputActionValue& Value);
	void HandleJumpOrSwimReleasedInput(const FInputActionValue& Value);
	//void HandleWallCutInput(const FInputActionValue& Value);
	//void HandleWallScanInput(const FInputActionValue& Value);

	UFUNCTION(Server, Reliable)
	void ServerInteract(AActor* RequestedActor);

	bool TryHoldTarget(AActor* Target);
	bool IsHolding() const;

	/** 다른 캐릭터가 이미 운반 중인 대상인지. 서버에서만 의미가 있다 */
	bool IsHeldByOtherCharacter(const AActor* Target) const;

	void ReleaseTarget();
	void UpdateHeldTarget();
	void CaptureDefaultHoldProfile();
	void ApplyHoldProfile(const FMD_HoldProfile& Profile);
	void UpdateInteractionFocus();
	AActor* TraceInteractionTarget() const;
	void SetCurrentTarget(AActor* NewTarget);

	/** HoldPoint의 월드 Transform. Camera 회전은 원격 머신에서 낡으므로 시점 회전으로 다시 만든다 */
	FTransform GetHoldPointTransform() const;

	/** HoldPoint 도달 여부를 보고 대상에 한 번 알린다 */
	void UpdateCarrySettle(float DeltaSeconds);

	/** 서버가 운반 중인 대상을 이 머신에서도 직접 따라가게 만든다. 비권한 머신 전용 */
	void StartFollowingHeldActor();
	void StopFollowingHeldActor();
	void UpdateFollowedHeldActor(float DeltaSeconds);

	UFUNCTION()
	void OnRep_HeldActor();

	void InitializeAbilitySystem();
	void GrantDefaultAbilities();

	UFUNCTION()
	void HandleHotbarChanged();

	void RefreshToolMesh();
	TWeakObjectPtr<AActor> CurrentTarget;

	/**
	 * PhysicsHandle로 운반 중인 대상. Grab은 서버에서만 일어난다.
	 * 클라이언트가 물리 상태 복제를 그대로 시뮬레이션하면 스프링 속도까지 적용돼 물체가 튀어오른다.
	 * 대신 이 값을 받아 각 머신이 HoldPoint를 직접 따라간다.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_HeldActor)
	TObjectPtr<AActor> HeldActor = nullptr;

	/** 비권한 머신에서 물리를 끄고 따라가는 중인 컴포넌트 */
	TWeakObjectPtr<UPrimitiveComponent> FollowedComponent;

	/** 잡는 순간 대상에서 한 번 읽어 캐시한 HoldOffset */
	FVector HeldTargetOffset = FVector::ZeroVector;

	/** 잡은 컴포넌트의 로컬 공간에서 본 GrabPoint 위치. 물리 핸들이 목표에 맞추는 기준점이다. */
	FVector HeldGrabPointOffset = FVector::ZeroVector;

	/** 잡은 물체가 HoldPoint에 도달했는지. 도달 알림을 한 번만 보내기 위해 들고 있다 */
	bool bHeldActorSettled = false;

	/** 잡은 뒤 흐른 시간. 목표에 못 닿는 경우의 상한 판정용 */
	float HeldCarrySeconds = 0.f;

	/** BeginPlay에서 캡처한 PhysicsHandle 기본 설정. 놓을 때 여기로 되돌린다. */
	FMD_HoldProfile DefaultHoldProfile;

	/** 현재 잡고 있는 대상에 적용 중인 프로필 */
	FMD_HoldProfile ActiveHoldProfile;

private:
	UFUNCTION()
	void HandleHealthStateChanged(EMD_HealthState NewState);

	/** 사망 시 이동을 멈추고, 부활 시 되돌린다. */
	void ApplyDeathMovementLock(bool bLocked);

	/** 모선 리스폰 지점으로 옮긴다. 서버에서만 호출한다. */
	void TeleportToMothership();

	/** 직전에 사망 상태였는지. 부활 전이 판별용 */
	bool bWasDeadState = false;
};
