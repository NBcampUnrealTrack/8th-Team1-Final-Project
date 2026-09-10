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
class UMD_FoamGunComponent;
class UMD_OxygenComponent;
class UMD_ComfortComponent;
class AMD_WallUtility;
class UWidgetComponent;
class UNiagaraSystem;
class UNiagaraComponent;
class UMD_CharacterCustomizationComponent;
class UAnimMontage;
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

	/** 현재 장착한 헤라에 털어낼 적재량이 있는지 외부 상호작용 대상에 알려준다. */
	UFUNCTION(BlueprintPure, Category = "MD|Scrape|Unload")
	bool CanUnloadScrapeLoad() const;

	/** 현재 장착한 헤라에서 요청량만큼 실제 표현 가능한 감소가 생기는지 반환한다. */
	bool CanUnloadScrapeLoadForRequest(float RequestedAmount) const;

	/** 서버에서 장착 상태를 재검증하고 요청량만큼 헤라 적재량 제거를 시도한다. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Scrape|Unload")
	float TryUnloadScrapeLoad(float RequestedAmount);

	/** 기존 AnimBP 호환용 출력. 3인칭 포인터 조준은 사용하지 않으므로 항상 비활성이다. */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "MD|Scrape|Animation")
	void GetScrapeAnimationAim(FRotator& AimDelta, bool& Active);

	/** 기존 AnimBP 호환용 갱신. 3인칭 도구를 기본 손 소켓 자세로 유지한다. */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "MD|Scrape|Animation")
	void UpdateScrapeAnimationBlade(
		bool Active,
		double DeltaTime,
		FRotator& Swing);

	/**
	 * 기존 AnimBP 호환용 출력. 3인칭은 접촉 IK를 사용하지 않으므로 항상 비활성이다.
	 * AnimBP에서 오른팔 IK 노드를 제거한 뒤 후속 정리에서 함께 삭제할 수 있다.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "MD|Scrape|Animation")
	void GetScrapeAnimationHandIK(
		double DeltaTime,
		FTransform& EffectorTransformComponentSpace,
		FVector& JointTargetLocationComponentSpace,
		float& Alpha,
		bool& Active,
		bool& Reachable,
		bool& TeleportOrSnap,
		int64& PoseGeneration);

	UFUNCTION(BlueprintPure, Category = "Equipment")
	UMD_EquipHotbarComponent* GetHotbarComponent() const { return EquipHotbarComponent; }

	UFUNCTION(BlueprintPure, Category = "MD|Audio")
	UMD_AudioRelayComponent* GetAudioRelayComponent() const { return AudioRelayComponent; }

	UFUNCTION(BlueprintPure, Category = "MD|Interaction")
	UMD_InteractionComponent* GetInteractionComponent() const { return InteractionComponent; }

	UFUNCTION(BlueprintPure, Category = "MD|Oxygen")
	UMD_OxygenComponent* GetOxygenComponent() const { return OxygenComponent; }

	UFUNCTION(BlueprintPure, Category = "MD|FoamGun")
	UMD_FoamGunComponent* GetFoamGunComponent() const { return FoamGunComponent; }

	// 절단 루프 VFX 토글. 어빌리티가 서버·소유 클라 양쪽에서 부르고, 사본 프록시는 복제로 따라온다
	void SetCutterVFXActive(bool bActive);

	/**
	 * 따개비 타격용 원샷 몽타주(AS_Scraper_Oneshot_Montage)를 3인칭 메시에서 재생한다.
	 * 클릭 제거/타격 연동·멀티캐스트는 이후 단계에서 붙인다.
	 * @return Montage_Play 가 돌려준 길이. 재생하지 못하면 0.
	 */
	UFUNCTION(BlueprintCallable, Category = "MD|Scrape|Animation")
	float PlayBarnacleScrapeMontage();

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

	/**
	 * 3D 멀미 완화 담당. 수치는 Project Settings -> Game -> MD Comfort에 있고,
	 * 로컬 플레이어에게만 동작한다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMD_ComfortComponent> ComfortComponent;

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
	 * 따개비 타격 원샷 몽타주. 헤라 연속 긁기 포즈(AS_Scraper_Pose)와 별개이며,
	 * DefaultSlot 에 한 번 재생된다. BP(또는 CDO)에서 할당한다. 비어 있으면
	 * PlayBarnacleScrapeMontage 는 no-op.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Animation")
	TObjectPtr<UAnimMontage> BarnacleScrapeMontage;

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

	/** 폼건 충전·탄도 정본·조준 궤적 프리뷰 (기획 08 §4.2) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMD_FoamGunComponent> FoamGunComponent;

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
	void HandleScrapeFocusToggleInput();
	void HandleScrapeFocusReleasedInput();
	void HandleScrapeFocusInputEnded();
	void HandleSwapEquipmentInput(const FInputActionValue& Value);
	void HandleSelectHotbarSlotInput(const FInputActionValue& Value, int32 SlotIndex);
	void HandleJumpOrSwimInput(const FInputActionValue& Value);
	void HandleJumpOrSwimReleasedInput(const FInputActionValue& Value);
	// 조작 설정 키가 반영된 런타임 플레이어 입력 컨텍스트 조회
	UInputMappingContext* ResolvePlayerMappingContext();

	// 클라이언트의 사다리 점프 이탈을 서버에 반영하는 요청
	UFUNCTION(Server, Reliable)
	void ServerJumpOffLadder();

	// 도구 입력(LMB) 전용 인프라 수리(절연테이프). 클라가 조준한 대상을 서버가 거리 재검증 후 수리한다
	void TryToolInputUtilityRepair();
	UFUNCTION(Server, Reliable)
	void ServerRepairUtilityWithTool(AMD_WallUtility* Utility);
	// 도구 입력 수리의 조준 트레이스 거리 (상호작용 트레이스와 동일)
	static constexpr float ToolInputRepairTraceDistance = 200.f;
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

	/** 장비 identity 변경 시 이전 헤라 입력 소유권과 Focus를 정리한다. */
	void RefreshScrapeFocusInputState();

	/** 빙의/종료 경계에서 진행 중인 헤라 입력과 장비 identity를 초기화한다. */
	void ResetScrapeFocusInputState();

	/** 이번 LMB 입력으로 정확히 활성화한 헤라 Ability만 취소한다. */
	void CancelActiveScrapeAbility();

	/** Ability 취소와 Focus 종료를 한 입력 경계에서 순서대로 처리한다. */
	void CancelActiveScrapeAbilityAndEndFocus();

	bool TryActivateScrapeAbility(
		UAbilitySystemComponent* AbilitySystem,
		const FGameplayTag& AbilityTag,
		UMD_ToolDefinition* SourceTool);

	bool IsScrapeToolEquipped() const;

	/** 현재 슬롯이 폼건인지. RMB 조준 컨텍스트 유지 판정에 쓴다 */
	bool IsFoamGunEquipped() const;
	UEnhancedInputLocalPlayerSubsystem* GetEnhancedInputSubsystem() const;

	/**
	 * 도구 메시의 그립 소켓이 손 소켓 원점에 오도록 하는 상대 트랜스폼.
	 * 그립 소켓이 없으면 Tool의 AttachOffset을 그대로 돌려준다.
	 */
	FTransform GetToolAttachTransform(
		const UStaticMesh* ToolMesh,
		const UMD_ToolDefinition* Tool) const;
	FTransform GetToolAttachTransformFromOffset(
		const UStaticMesh* ToolMesh,
		const FTransform& AttachOffset) const;

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
	 * 1인칭 도구를 Focus viewmodel 또는 기본 팔 소켓 경로로 배치한다.
	 *
	 * Focus 중에는 Blade_Tip을 가상 포인터에 맞추고, Focus 해제 후에는
	 * 본체의 컴포넌트 공간 ToolSocket을 1인칭 팔 월드 변환에 합성한다.
	 */
	void UpdateFirstPersonToolTransform(float DeltaSeconds);

	/** Focus 진입/해제 경계에서 1인칭 헤라를 팔 부착과 독립 뷰모델 사이로 전환한다. */
	void SetFirstPersonScrapeViewModelActive(bool bActive);

	/** 가상 포인터를 카메라 앞 평면으로 투영해 Blade_Tip을 그 위치에 배치한다. */
	bool UpdateFirstPersonScrapeViewModel(float DeltaSeconds);
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

	/** PlayerState의 복제 외형을 캐릭터 메시에 적용한다. */
	void ApplyCustomizationFromPlayerState();

	/** PlayerState의 외형 변경 콜백 등록을 관리한다. */
	void BindAppearanceDelegate();
	void UnbindAppearanceDelegate();

	UFUNCTION()
	void HandleAppearanceChanged();

	/** 직전에 사망 상태였는지. 부활 전이 판별용 */
	bool bWasDeadState = false;

	/** 수량 변경과 실제 장비 교체를 구분하기 위해 마지막 장착 도구를 기억한다. */
	TWeakObjectPtr<UMD_ToolDefinition> ScrapeFocusInputTool;

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

	/** StaticMesh contact frame. X=화면을 향한 normal, Z=포인터 진행 방향 계약이다. */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Scrape|ViewModel")
	FName ScrapeBladeTipSocketName = TEXT("Blade_Tip");

	/** 새 메시가 Blade_Tip 대신 명시적 접촉 이름을 쓸 때의 fallback. */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Scrape|ViewModel")
	FName ScrapeBladeContactFallbackSocketName = TEXT("Blade_Contact");

	/** Focus Ready에서 포인터를 따라가는 헤라의 카메라 전방 거리. */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Scrape|ViewModel|Pointer",
		meta = (ClampMin = "10.0", ClampMax = "200.0", Units = "cm"))
	float ScrapeViewModelDistanceCm = 75.0f;

	/** Focus Ready에서 접촉 소켓을 pivot으로 적용하는 시각 회전 보정. */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Scrape|ViewModel|Pointer",
		meta = (UIMin = "-180.0", UIMax = "180.0",
			ToolTip = "Pointer-follow rotation in the Blade_Tip/Blade_Contact local contact frame."))
	FRotator ScrapeViewModelRotationOffset = FRotator::ZeroRotator;

	/** Focus Ready에서 접촉 소켓을 pivot으로 적용하는 uniform 크기 보정. */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Scrape|ViewModel|Pointer",
		meta = (ClampMin = "0.01", UIMin = "0.25", UIMax = "2.0"))
	float ScrapeViewModelScaleMultiplier = 1.0f;

	/** 실제 LMB 긁기 중에만 쓰는 독립 카메라 전방 거리. */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Scrape|ViewModel|Stroke",
		meta = (ClampMin = "10.0", ClampMax = "200.0", Units = "cm"))
	float ScrapeStrokeViewModelDistanceCm = 75.0f;

	/** 실제 LMB 긁기 중에만 쓰는 독립 시각 회전 보정. */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Scrape|ViewModel|Stroke",
		meta = (UIMin = "-180.0", UIMax = "180.0",
			ToolTip = "Active-stroke rotation in the Blade_Tip/Blade_Contact local contact frame."))
	FRotator ScrapeStrokeViewModelRotationOffset = FRotator::ZeroRotator;

	/** 실제 LMB 긁기 중에만 쓰는 독립 uniform 크기 보정. */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Scrape|ViewModel|Stroke",
		meta = (ClampMin = "0.01", UIMin = "0.25", UIMax = "2.0"))
	float ScrapeStrokeViewModelScaleMultiplier = 0.25f;

	/** 위치는 즉시 따라가고 헤라 회전만 이 시간 동안 완화한다. */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Scrape|ViewModel",
		meta = (ClampMin = "0.0", ClampMax = "0.2", Units = "s"))
	float ScrapeViewModelRotationSmoothingSeconds = 0.04f;

	FVector2D LastScrapeViewModelPointerNormalized = FVector2D(0.5, 0.5);
	FVector2D ScrapeStrokeViewModelScreenDirection = FVector2D(0.0, -1.0);
	FVector LastScrapeStrokeViewModelStableWorldTangent = FVector::ZeroVector;
	FQuat ScrapePointerViewModelWorldRotation = FQuat::Identity;
	FQuat ScrapeStrokeViewModelWorldRotation = FQuat::Identity;
	FVector ScrapeViewModelWorldScale = FVector::OneVector;
	bool bFirstPersonScrapeViewModelActive = false;
	bool bFirstPersonArmsWereVisible = true;
	bool bScrapeStrokeViewModelDirectionLocked = false;
	bool bWasScrapingForViewModel = false;
	bool bHasScrapePointerViewModelRotation = false;
	bool bHasScrapeStrokeViewModelRotation = false;

	/** 접촉이 끊길 때 도구 회전을 AttachOffset으로 부드럽게 복귀시키는 현재 값. */
	FRotator ScrapeAnimationToolRelativeRotation = FRotator::ZeroRotator;

};
