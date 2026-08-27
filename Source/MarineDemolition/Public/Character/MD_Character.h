// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "AbilitySystemInterface.h"
#include "Types/State/MD_HealthTypes.h"
#include "MD_Character.generated.h"

class UMD_InputConfig;
class UInputMappingContext;
class UInputAction;
class UEnhancedInputLocalPlayerSubsystem;
class UCameraComponent;
class UStaticMesh;
class UStaticMeshComponent;
class USceneComponent;
class USkeletalMeshComponent;
class UPhysicsHandleComponent;
class UMD_CharacterMovementComponent;
class UMD_InteractionComponent;
class UMD_ScrapeToolComponent;
class UGameplayAbility;
class UMD_EquipHotbarComponent;
class UMD_AudioRelayComponent;
class UMD_ToolDefinition;
class UMD_SupplyComponent;
class UMD_OxygenComponent;
class UWidgetComponent;
class UNiagaraSystem;
class UNiagaraComponent;
struct FMinimalViewInfo;
struct FGameplayTag;
// 캡슐 오버랩 콜백 매개변수 형식 전방 선언
class UPrimitiveComponent;

UENUM(BlueprintType)
enum class EMDMovementState : uint8
{
	Walking,
	Swimming,
	// 사다리 전용 이동 상태
	Climbing
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMDOnMovementStateChanged, EMDMovementState, NewMovementState);

UCLASS()
class MARINEDEMOLITION_API AMD_Character : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	AMD_Character(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(BlueprintAssignable, Category = "Movement|Water")
	FMDOnMovementStateChanged OnMovementStateChanged;

	UFUNCTION(BlueprintPure, Category = "Movement|Water")
	EMDMovementState GetCurrentMovementState() const { return CurrentMovementState; }

	UFUNCTION(BlueprintCallable, Category = "Movement|Water")
	void ForceEnterWalkState();

	UFUNCTION(BlueprintPure, Category = "MD|Scrape")
	UMD_ScrapeToolComponent* GetScrapeToolComponent() const	{ return ScrapeToolComponent; }

	/**
	 * 서버/로컬이 확정한 접촉 자세를 기존 ABP 입력 형식으로 변환한다.
	 * 카메라 중앙을 다시 Trace하지 않으며 원격 Pawn도 복제 pose만 소비한다.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "MD|Scrape|Animation")
	void GetScrapeAnimationAim(FRotator& AimDelta, bool& Active);

	/**
	 * 명시적 blade 진행축을 도구의 소켓 기준 twist로 적용한다.
	 * ControlRotation 변화량으로 가로/세로를 재추정하지 않는다.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "MD|Scrape|Animation")
	void UpdateScrapeAnimationBlade(
		bool Active,
		double DeltaTime,
		FRotator& Swing);

	UFUNCTION(BlueprintPure, Category = "Equipment")
	UMD_EquipHotbarComponent* GetHotbarComponent() const { return EquipHotbarComponent; }

	UFUNCTION(BlueprintPure, Category = "MD|Audio")
	UMD_AudioRelayComponent* GetAudioRelayComponent() const { return AudioRelayComponent; }

	UFUNCTION(BlueprintPure, Category = "MD|Interaction")
	UMD_InteractionComponent* GetInteractionComponent() const { return InteractionComponent; }

	UFUNCTION(BlueprintPure, Category = "MD|Oxygen")
	UMD_OxygenComponent* GetOxygenComponent() const { return OxygenComponent; }

	// 절단 루프 VFX 토글. 어빌리티가 서버·소유 클라 양쪽에서 부르고, 사본 프록시는 복제로 따라온다
	void SetCutterVFXActive(bool bActive);

	/** UI·앱 비활성화·외부 시스템이 입력을 가져가기 전에 헤라 Ability와 Focus를 정리한다. */
	UFUNCTION(BlueprintCallable, Category = "MD|Scrape")
	void CancelScrapeInteraction();

	/**
	 * Called from the actual GA activation callback on both client and server.
	 * The session id prevents a late EndAbility from clearing a newer activation
	 * that reuses the same GameplayAbilitySpecHandle.
	 */
	bool RegisterActiveScrapeAbility(
		FGameplayAbilitySpecHandle AbilityHandle,
		uint64 FocusSessionId);

	/** Clears the tracked activation only when both handle and session still match. */
	void UnregisterActiveScrapeAbility(
		FGameplayAbilitySpecHandle AbilityHandle,
		uint64 FocusSessionId);

	/** Server Focus cleanup cancels exactly the GA registered for this session. */
	void CancelScrapeAbilityForSession(uint64 FocusSessionId);

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	virtual void OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode) override;

	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
	virtual void PawnClientRestart() override;

	virtual void OnRep_PlayerState() override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;
	virtual void OnRep_Controller() override;
public:
	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
protected:

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCameraComponent> Camera;

	/**
	 * 1인칭 팔 전용 피벗. Camera가 아니라 캡슐에 붙어 있고 매 프레임 조준 회전을 따라간다.
	 * 덕분에 Camera 위치·회전을 팔과 독립적으로 조정할 수 있다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> FirstPersonArmsPivot;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USkeletalMeshComponent> FirstPersonMesh;

	/** 조준 기준점에서 팔을 얼마나 띄울지. 조준 방향 기준 (X=앞, Y=오른쪽, Z=위). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MD|FirstPerson")
	FVector FirstPersonArmsOffset = FVector::ZeroVector;

	/** 팔에 추가로 줄 회전. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MD|FirstPerson")
	FRotator FirstPersonArmsRotationOffset = FRotator::ZeroRotator;

	/** 팔이 조준을 따라가는 속도. 0이면 지연 없이 즉시 붙고, 값이 작을수록 스웨이가 커진다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MD|FirstPerson",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FirstPersonArmsFollowSpeed = 0.f;

	/** 월드 메시 관통을 줄이도록 팔·도구를 카메라 쪽으로 당겨 그리는 비율. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MD|FirstPerson",
		meta = (ClampMin = "0.1", ClampMax = "1.0", UIMin = "0.1", UIMax = "1.0"))
	float FirstPersonRenderScale = 0.8f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMD_ScrapeToolComponent> ScrapeToolComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMD_EquipHotbarComponent> EquipHotbarComponent;

	/** 도구 작업음을 다른 클라이언트에게 중계한다 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMD_AudioRelayComponent> AudioRelayComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMD_InteractionComponent> InteractionComponent;

	/** 다른 플레이어가 보는 전체 도구 메시. 적재 단계가 바뀌면 메시 자체를 교체한다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ThirdPersonToolMesh;

	/** 로컬 플레이어가 보는 전체 도구 메시. 적재 단계가 바뀌면 메시 자체를 교체한다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> FirstPersonToolMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	FName ToolAttachSocketName = TEXT("ToolSocket");

	/** 절단 중 톱날에 붙여 재생할 루프 이펙트 (NS_Cutter_Cut) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|VFX")
	TObjectPtr<UNiagaraSystem> CutterCutEffect;

	/** 도구 스태틱 메시에서 절단 이펙트를 붙일 소켓. 없으면 메시 원점에 붙는다 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|VFX")
	FName CutterVFXSocketName = TEXT("VFX_Blade");

	/**
	 * 도구 스태틱 메시에서 손에 닿을 지점을 표시하는 소켓 이름.
	 * 이 소켓이 있으면 그 지점이 캐릭터 손 소켓에 오도록 도구를 배치하므로,
	 * 메시 에디터에서 소켓을 옮기는 것만으로 잡는 위치가 바뀐다.
	 * 소켓이 없는 도구는 예전처럼 메시 원점이 손에 온다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	FName ToolGripSocketName = TEXT("Grip_R");

	/** 모든 물리 픽업이 공유하는 운반 기준점. Camera에 부착되며 대상별 배치는 GetHoldOffset()으로 조정한다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> HoldPoint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPhysicsHandleComponent> PhysicsHandle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMD_SupplyComponent> SupplyComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMD_OxygenComponent> OxygenComponent;

	/** 원격 플레이어의 플랫폼 닉네임을 머리 위에 표시하는 이름표 컴포넌트 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UWidgetComponent> NameTagWidgetComponent;

	/** 모선 리스폰 지점을 찾을 때 사용할 PlayerStart 태그 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Health")
	FName MothershipRespawnTag = TEXT("MothershipRespawn");

protected:

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> PlayerMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UMD_InputConfig> InputConfig;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Water")
	EMDMovementState CurrentMovementState = EMDMovementState::Walking;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Abilities")
	TArray<TSubclassOf<UGameplayAbility>> DefaultAbilities;

	//UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Abilities")
	//TSubclassOf<UGameplayAbility> WallCutAbilityClass;
	
	//UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Abilities")
	//TSubclassOf<UGameplayAbility> WallScanAbilityClass;

private:
	// PlayerState의 이름 복제 알림이 이름표를 직접 갱신할 수 있게 접근을 허용한다.
	friend class AMD_PlayerState;

	void HandleMoveInput(const FInputActionValue& Value);
	void HandleLookInput(const FInputActionValue& Value);
	void HandleInteractInput(const FInputActionValue& Value);
	void HandleAbilityInput(const FInputActionValue& Value);
	void HandleAbilityReleasedInput(const FInputActionValue& Value);
	void HandleScrapeFocusInputStarted();
	void HandleScrapeFocusInputEnded();
	void HandleSwapEquipmentInput(const FInputActionValue& Value);
	void HandleSelectHotbarSlotInput(const FInputActionValue& Value, int32 SlotIndex);
	void HandleJumpOrSwimInput(const FInputActionValue& Value);
	void HandleJumpOrSwimReleasedInput(const FInputActionValue& Value);

	// 사다리 진입을 처리하는 캡슐 오버랩 시작 콜백
	UFUNCTION()
	void HandleCollisionBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	// 사다리 이탈을 처리하는 캡슐 오버랩 종료 콜백
	UFUNCTION()
	void HandleCollisionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex);

	// 클라이언트의 사다리 점프 이탈을 서버에 반영하는 요청
	UFUNCTION(Server, Reliable)
	void ServerJumpOffLadder();
	//void HandleWallCutInput(const FInputActionValue& Value);
	//void HandleWallScanInput(const FInputActionValue& Value);

	void InitializeAbilitySystem();
	void GrantDefaultAbilities();
	void RefreshNameTag();

	UFUNCTION()
	void HandleHotbarChanged();

	UFUNCTION()
	void HandleScrapeLoadChanged(float NewScrapeLoad);

	void RefreshToolMesh();
	void RefreshScrapeLoadVisual();

	UFUNCTION()
	void OnRep_CutterVFXActive();

	// 로컬은 1인칭, 타인은 3인칭 도구 메시에 붙여 시점별 표시를 기존 OwnerSee 구조에 맡긴다
	void RefreshCutterVFX();

	/** 절단 어빌리티가 활성인지. 서버가 굳히고 사본 프록시가 복제로 받는다 */
	UPROPERTY(ReplicatedUsing = OnRep_CutterVFXActive)
	bool bCutterVFXActive = false;

	/** 현재 도구 메시에 붙어 있는 절단 루프 이펙트 인스턴스 */
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> CutterVFXComponent;

	/** 우클릭 헤라 포커스 입력용 런타임 액션/컨텍스트를 필요할 때 생성한다. */
	void EnsureScrapeFocusInputObjects();

	/** 현재 장착 도구에 맞춰 로컬 플레이어의 헤라 포커스 입력 컨텍스트를 갱신한다. */
	void RefreshScrapeFocusInputContext();

	/** 컨텍스트 제거 전에 진행 중인 헤라 포커스를 항상 종료한다. */
	void RemoveScrapeFocusInputContext();

	/** 이번 LMB 입력으로 정확히 활성화한 헤라 Ability만 취소한다. */
	void CancelActiveScrapeAbility();

	/** Ability 취소와 Focus 종료를 한 입력 경계에서 순서대로 처리한다. */
	void CancelActiveScrapeAbilityAndEndFocus();

	bool TryActivateScrapeAbility(
		UAbilitySystemComponent* AbilitySystem,
		const FGameplayTag& AbilityTag,
		UMD_ToolDefinition* SourceTool);

	bool IsScrapeToolEquipped() const;
	UEnhancedInputLocalPlayerSubsystem* GetEnhancedInputSubsystem() const;

	/**
	 * 도구 메시의 그립 소켓이 손 소켓 원점에 오도록 하는 상대 트랜스폼.
	 * 그립 소켓이 없으면 Tool의 AttachOffset을 그대로 돌려준다.
	 */
	FTransform GetToolAttachTransform(
		const UStaticMesh* ToolMesh,
		const UMD_ToolDefinition* Tool) const;

	/** 로컬 1인칭 팔 또는 클라이언트 이름표 갱신이 필요한 경우 캐릭터 틱을 활성화한다. */
	void RefreshFirstPersonArmsTick();

	/** 월드 공간 이름표가 현재 로컬 카메라를 향하도록 회전시킨다. */
	void UpdateNameTagRotation();

	/** 팔·도구의 데칼 수신을 끄고 UE 전용 1인칭 렌더링을 적용한다. */
	void ConfigureFirstPersonRendering();

	/** 1인칭 팔이 3인칭 메시의 포즈를 따라가도록 Leader Pose를 건다. */
	void BindFirstPersonArmsToBodyPose();

	void UpdateFirstPersonArms(float DeltaSeconds, bool bSnapToTarget = false);

	/**
	 * 1인칭 도구 메시를 본체 포즈에서 직접 배치한다.
	 *
	 * FirstPersonMesh 는 SetLeaderPoseComponent 팔로워라 자기 본 갱신 경로를
	 * 건너뛴다. 그래서 그 본 소켓에 붙은 자식 컴포넌트는 본 단위 회전(날 비틀기)을
	 * 받지 못하고, 1인칭에서만 도구가 안 도는 현상이 생긴다.
	 * 본체의 컴포넌트 공간 소켓 트랜스폼을 1인칭 팔의 컴포넌트 트랜스폼에 얹는다.
	 */
	void UpdateFirstPersonToolTransform();


private:
	UFUNCTION()
	void HandleHealthStateChanged(EMD_HealthState NewState);

	// 데미지 브리지: ApplyDamage(오염수 DoT·붕괴 등)를 Health 감소로 연결한다 (서버 전용).
	// 클램프·부상/사망 전환은 AttributeSet·PlayerState가 처리한다.
	UFUNCTION()
	void HandleTakeAnyDamage(AActor* DamagedActor, float Damage, const class UDamageType* DamageType,
		class AController* InstigatedBy, AActor* DamageCauser);

	/** 사망 시 이동을 멈추고, 부활 시 되돌린다. */
	void ApplyDeathMovementLock(bool bLocked);

	/** 모선 리스폰 지점으로 옮긴다. 서버에서만 호출한다. */
	void TeleportToMothership();

	/** 직전에 사망 상태였는지. 부활 전이 판별용 */
	bool bWasDeadState = false;

	/** 에셋 의존 없이 우클릭 포커스를 바인딩하는 런타임 전용 입력 액션. */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ScrapeFocusInputAction;

	/** 헤라를 장착한 동안에만 로컬 플레이어에게 추가하는 입력 오버레이. */
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> ScrapeFocusInputMappingContext;

	/** 컨트롤러 교체 뒤에도 이전 로컬 플레이어에서 컨텍스트를 제거하기 위한 약한 참조. */
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> ScrapeFocusInputSubsystem;

	/** 수량 변경과 실제 장비 교체를 구분하기 위해 컨텍스트 적용 당시의 도구를 기억한다. */
	TWeakObjectPtr<UMD_ToolDefinition> ScrapeFocusContextTool;

	/** 서버가 현재 ScrapeTool runtime snapshot에 적용한 기본 Profile의 도구 identity. */
	TWeakObjectPtr<UMD_ToolDefinition> AppliedScrapeProfileTool;

	/** LMB로 실제 활성화된 Scrape Ability Spec. 장비가 바뀌어도 이 핸들로 정확히 취소한다. */
	FGameplayAbilitySpecHandle ActiveScrapeAbilityHandle;

	/** 같은 SpecHandle을 재사용하는 빠른 Focus 재진입을 구분하는 권위 session identity. */
	uint64 ActiveScrapeAbilitySessionId = 0;

	/** 수량 변경과 장비 교체를 구분하는 활성 Ability의 도구 identity. */
	TWeakObjectPtr<UMD_ToolDefinition> ActiveScrapeAbilityTool;

	/** 장비 변경이 LMB release보다 빨라도 release가 새 도구를 취소하지 않도록 입력 소유권을 유지한다. */
	bool bScrapeAbilityInputCaptured = false;

	bool bScrapeFocusInputContextAdded = false;

	/** 0값 Triggered가 Focus를 해제하지 않도록 하는 이동 입력 deadzone. */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Scrape|Focus",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ScrapeFocusMoveInputDeadzone = 0.1f;

	/** 180도 동치 blade 축이 프레임마다 뒤집히지 않도록 마지막 부호를 보존한다. */
	FVector LastScrapeAnimationBladeWorldDirection = FVector::ZeroVector;

	/** 현재 메시에서 날 축에 대응하는 component-space 기준 방향. */
	FVector ScrapeAnimationBladeReferenceDirection = FVector::ZeroVector;
	FVector ScrapeAnimationToolTwistAxis = FVector::ZeroVector;

	TWeakObjectPtr<UStaticMesh> ScrapeAnimationBladeReferenceMesh;

	/** 접촉이 끊길 때 도구 회전을 AttachOffset으로 부드럽게 복귀시키는 현재 값. */
	FRotator ScrapeAnimationToolRelativeRotation = FRotator::ZeroRotator;

	/**
	 * 날 축 twist 를 정의된 값으로 인정하는 최소 투영 길이. 투영 길이는 스트로크
	 * 방향과 날 긴 축이 이루는 각의 sin 이므로, 이 값은 "축에서 몇 도 이상
	 * 떨어져야 회전을 신뢰하는가"를 뜻한다. 0.25 ≈ 14.5도.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Scrape|Animation",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ScrapeBladeTwistMinProjection = 0.25f;

	/**
	 * 마지막으로 정의된 날 축 twist(도). 스트로크 방향이 날 축과 평행해지는
	 * 특이점 구간에서는 새로 계산하지 않고 이 값을 유지한다 — AttachOffset 으로
	 * 되돌리면 프레임마다 70~80도를 왕복한다.
	 */
	float LastScrapeAnimationTwistDegrees = 0.0f;

	bool bHasScrapeAnimationBladeDirection = false;
	bool bHasScrapeAnimationTwist = false;
};
