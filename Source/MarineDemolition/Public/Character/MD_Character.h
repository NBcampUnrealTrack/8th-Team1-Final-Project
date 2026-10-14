// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "AbilitySystemInterface.h"
#include "Types/State/MD_HealthTypes.h"
#include "Types/Customization/MD_CharacterAppearanceTypes.h"
#include "Types/Scrape/MD_ScrapePresentationState.h"
#include "MD_Character.generated.h"

class UMD_InputConfig;
class UInputMappingContext;
class UEnhancedInputLocalPlayerSubsystem;
class UCameraComponent;
class UStaticMesh;
class UStaticMeshComponent;
class USceneComponent;
class USkeletalMeshComponent;
enum class EVisibilityBasedAnimTickOption : uint8;
class UPhysicsHandleComponent;
class UMD_CharacterMovementComponent;
class UMD_InteractionComponent;
class UMD_StainScraperComponent;
class UMD_ScannerComponent;
class UGameplayAbility;
class UMD_EquipHotbarComponent;
class UMD_ThrowComponent;
class UMD_GlassMarkerComponent;
class UMD_GlassDrawingFocusComponent;
class UMD_AudioRelayComponent;
class UMD_ToolDefinition;
class UMD_ItemDefinition;
class UMD_FoamGunComponent;
class UMD_OxygenComponent;
class UMD_ComfortComponent;
class UWidgetComponent;
class UNiagaraSystem;
class UNiagaraComponent;
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

	UFUNCTION(BlueprintPure, Category = "MD|Stain")
	UMD_StainScraperComponent* GetStainScraperComponent() const { return StainScraperComponent; }

	UFUNCTION(BlueprintPure, Category="MD|Scanner")
	UMD_ScannerComponent* GetScannerComponent() const { return ScannerComponent; }

	UFUNCTION(BlueprintPure, Category = "Equipment")
	UMD_EquipHotbarComponent* GetHotbarComponent() const { return EquipHotbarComponent; }

	// 숫자키와 인벤토리 클릭이 공유하는 도구 동작 취소 및 사용 슬롯 변경
	UFUNCTION(BlueprintCallable, Category = "Equipment")
	void SelectHotbarSlot(int32 SlotIndex);

	UFUNCTION(BlueprintPure, Category = "Throw")
	UMD_ThrowComponent* GetThrowComponent() const { return ThrowComponent; }

	UFUNCTION(BlueprintPure, Category = "Glass Marker")
	UMD_GlassMarkerComponent* GetGlassMarkerComponent() const { return GlassMarkerComponent; }

	UFUNCTION(BlueprintPure, Category = "Glass|Desk")
	UMD_GlassDrawingFocusComponent* GetGlassDrawingFocusComponent() const { return GlassDrawingFocusComponent; }

	/** Refresh the temporary desk marker or restore the selected inventory item. */
	void RefreshDrawingToolPresentation();

	UFUNCTION(BlueprintPure, Category = "MD|Audio")
	UMD_AudioRelayComponent* GetAudioRelayComponent() const { return AudioRelayComponent; }

	UFUNCTION(BlueprintPure, Category = "MD|Interaction")
	UMD_InteractionComponent* GetInteractionComponent() const { return InteractionComponent; }

	UFUNCTION(BlueprintPure, Category = "MD|Oxygen")
	UMD_OxygenComponent* GetOxygenComponent() const { return OxygenComponent; }

	UFUNCTION(BlueprintPure, Category = "MD|FoamGun")
	UMD_FoamGunComponent* GetFoamGunComponent() const { return FoamGunComponent; }

	/**
	 * 지금 이 화면에 보이는 도구 메시(로컬 1인칭 / 거울·타인·서버는 3인칭)의 소켓 월드 위치.
	 * 1인칭 메시는 FirstPersonScale로 카메라 쪽에 당겨 그려지므로 화면상 보이는 자리로 보정해 돌려준다.
	 * 도구 메시에 해당 소켓이 없으면 false.
	 */
	bool TryGetVisibleToolSocketLocation(FName SocketName, FVector& OutLocation) const;

	// 절단 루프 VFX 토글. 어빌리티가 서버·소유 클라 양쪽에서 부르고, 사본 프록시는 복제로 따라온다
	void SetCutterVFXActive(bool bActive);

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

	/** 배치된 오염물의 좌클릭 휘두르기 몽타주를 3인칭 메시에서 재생한다. */
	UFUNCTION(BlueprintCallable, Category = "MD|Scrape|Animation")
	float PlayBarnacleScrapeMontage();

	/** Existing cancellation hook also ends held glass marker/eraser use before input changes owners. */
	UFUNCTION(BlueprintCallable, Category = "MD|Stain")
	void CancelScrapeInteraction();

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
	virtual void OnRep_Controller() override;
public:
	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	/** 로스터·팔레트 인덱스로 메시·애님·메인 색을 적용한다. 로컬 미리보기에도 쓴다. */
	UFUNCTION(BlueprintCallable, Category = "MD|Customization")
	void ApplyAppearance(const FMD_CharacterAppearance& Appearance);

	/** PlayerState에 복제된 외형을 다시 적용한다. */
	UFUNCTION(BlueprintCallable, Category = "MD|Customization")
	void ApplyAppearanceFromPlayerState();

	UFUNCTION(BlueprintPure, Category = "MD|Customization")
	const FMD_CharacterAppearance& GetAppliedAppearance() const { return AppliedAppearance; }

	/** 현재 플레이어 화면에서 본인 이름표만 제외하기 위한 컴포넌트 조회. */
	UWidgetComponent* GetNameTagWidgetComponent() const { return NameTagWidgetComponent; }

	/** 거울 앞에서 로컬 플레이어가 자기 3인칭 몸을 보고 1인칭 팔은 숨기도록 전환한다. */
	void SetLocalThirdPersonViewEnabled(bool bEnabled);
	bool IsLocalThirdPersonViewEnabled() const { return bLocalThirdPersonViewEnabled; }
protected:
	/** 1인칭 프리미티브의 실제 월드 위치를 렌더에 보이는 위치(카메라 기준 FirstPersonScale·FOV 보정)로 변환한다. */
	FVector ToFirstPersonApparentWorldLocation(const FVector& WorldLocation) const;


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

	// 로컬 조작 해제 시 복원할 본체 애니메이션 갱신 설정
	TOptional<EVisibilityBasedAnimTickOption> OriginalBodyMeshAnimTickOption;

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

	// Crowbar presentation moves the visible arms only; restore before another tool or pose update.
	TOptional<FTransform> CrowbarArmsBaseRelativeTransform;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Stain", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMD_StainScraperComponent> StainScraperComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MD|Scanner")
	TObjectPtr<UMD_ScannerComponent> ScannerComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMD_EquipHotbarComponent> EquipHotbarComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMD_ThrowComponent> ThrowComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMD_GlassMarkerComponent> GlassMarkerComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMD_GlassDrawingFocusComponent> GlassDrawingFocusComponent;

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

	/** 다른 플레이어가 보는 전체 도구 메시. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ThirdPersonToolMesh;

	/** 로컬 플레이어가 보는 전체 도구 메시. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> FirstPersonToolMesh;

	/**
	 * 절단기 톱날. 도구 메시와 같은 시점 분리(OwnerNoSee / OnlyOwnerSee)를 쓰고,
	 * 각각 대응하는 도구 메시의 톱날 소켓에 붙는다. 절단 중 로컬 Y 를 축으로 돈다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ThirdPersonCutterBlade;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> FirstPersonCutterBlade;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	FName ToolAttachSocketName = TEXT("ToolSocket");

	/** 절단 중 톱날에 붙여 재생할 루프 이펙트 (NS_Cutter_Cut) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|VFX")
	TObjectPtr<UNiagaraSystem> CutterCutEffect;

	/** 도구 스태틱 메시에서 절단 이펙트를 붙일 소켓. 없으면 메시 원점에 붙는다 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|VFX")
	FName CutterVFXSocketName = TEXT("VFX_Blade");

	/** 배치된 오염물 타격 몽타주. BP에서 지정하며, 비어 있으면 재생하지 않는다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Animation")
	TObjectPtr<UAnimMontage> BarnacleScrapeMontage;

	/**
	 * 절단기 톱날. 본체(ToolMesh)와 분리된 메시라 절단 중에 혼자 회전할 수 있다.
	 * 본체 메시의 CutterBladePivotSocketName 소켓에 붙는다. 소켓이 없거나 이
	 * 메시가 비어 있으면 톱날을 표시하지 않는다 — 다른 도구도 같은 경로를 탄다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Cutter")
	TObjectPtr<UStaticMesh> CutterBladeMesh;

	/** 본체 메시에서 톱날 회전 중심을 표시하는 소켓. 원판 중심에 있어야 한다 */
	UPROPERTY(EditDefaultsOnly, Category = "Equipment|Cutter")
	FName CutterBladePivotSocketName = TEXT("Blade_Pivot");

	/**
	 * 절단 중 톱날 회전 속도. 톱날 메시는 두께축이 Y 라 로컬 Y 를 축으로 돈다.
	 * 회전은 bCutterVFXActive 를 그대로 따르므로 사본 프록시에서도 돌아간다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Equipment|Cutter",
		meta = (ClampMin = "0.0", Units = "deg/s"))
	float CutterBladeSpinDegreesPerSecond = 1440.0f;

	/**
	 * 폼건 RMB 조준 자세(AS_FoamGun_Pose_Montage). DefaultSlot 에 재생하며
	 * 조준을 놓을 때까지 유지한다. 한 프레임짜리 포즈 몽타주라 재생 직후
	 * 섹션이 자기 자신을 다음으로 가리키게 만들어 루프시킨다.
	 * 비어 있으면 조준해도 자세가 바뀌지 않는다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Animation")
	TObjectPtr<UAnimMontage> FoamAimMontage;

	/** 조준 자세를 걷어낼 때의 블렌드 아웃 시간 */
	UPROPERTY(EditDefaultsOnly, Category = "Equipment|Animation",
		meta = (ClampMin = "0.0", Units = "s"))
	float FoamAimMontageBlendOutSeconds = 0.15f;

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
	// 상호작용 키 해제. 홀드형 대상(데이터 덤프 단말 등)의 진행 취소에만 쓰인다
	void HandleInteractReleasedInput(const FInputActionValue& Value);
	void HandleAbilityInput(const FInputActionValue& Value);
	bool bInstructionClickCaptured=false;
	void HandleScannerModeSwitchInput(const FInputActionValue& Value);
	void HandleAbilityReleasedInput(const FInputActionValue& Value);
	// 폼건 RMB 조준. 누르면 궤적 프리뷰·조준 자세, 떼면 해제. 폼건이 아니면 무시한다
	void HandleAimStartedInput(const FInputActionValue& Value);
	void HandleAimReleasedInput(const FInputActionValue& Value);
	// 도구 전환·운반 시작 등 입력 밖 경로에서도 조준 상태를 정리한다
	void EndFoamAiming();
	void HandleThrowStartedInput(const FInputActionValue& Value);
	void HandleThrowReleasedInput(const FInputActionValue& Value);
	void HandleThrowCanceledInput(const FInputActionValue& Value);
	void HandleGlassMarkerEraseStartedInput(const FInputActionValue& Value);
	void HandleGlassMarkerEraseReleasedInput(const FInputActionValue& Value);
	void HandleGlassMarkerColorCycleInput(const FInputActionValue& Value);
	void HandleSwapEquipmentInput(const FInputActionValue& Value);
	void HandleJumpOrSwimInput(const FInputActionValue& Value);
	void HandleJumpOrSwimReleasedInput(const FInputActionValue& Value);
	// 조작 설정 키가 반영된 런타임 플레이어 입력 컨텍스트 조회
	UInputMappingContext* ResolvePlayerMappingContext();

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
	void HandleGlassMarkerModeChanged(bool bErasing);

	UFUNCTION()
	void HandleGlassMarkerInkColorChanged(uint8 ColorIndex, FLinearColor Color);

	/** 운반 시작·종료에 맞춰 도구 메시를 내리고 다시 든다 */
	UFUNCTION()
	void HandleHeldActorChanged(AActor* HeldActor);

	void RefreshToolMesh();
	UMD_ItemDefinition* GetPresentedItemDefinition() const;

	/**
	 * 절단기 톱날 메시와 표시 여부를 확정한다. 도구 메시가 바뀔 때마다 불러야
	 * 하며, 본체에 톱날 소켓이 없으면 톱날을 숨긴다.
	 */
	void RefreshCutterBladeMesh();

	/** 절단 중일 때만 톱날을 돌린다. Tick 에서 부른다 */
	void UpdateCutterBladeSpin(float DeltaSeconds);
	void RefreshToolVisual();
	void RefreshGlassMarkerInkIndicator();

	UFUNCTION()
	void OnRep_CutterVFXActive();

	// 로컬은 1인칭, 타인은 3인칭 도구 메시에 붙여 시점별 표시를 기존 OwnerSee 구조에 맡긴다
	void RefreshCutterVFX();

	/** 절단 어빌리티가 활성인지. 서버가 굳히고 사본 프록시가 복제로 받는다 */
	UPROPERTY(ReplicatedUsing = OnRep_CutterVFXActive)
	bool bCutterVFXActive = false;

	/**
	 * 폼건 RMB 조준 자세를 켜고 끈다. 소유 클라가 즉시 반영하고 서버에 올리며,
	 * 사본 프록시는 복제로 따라온다 — 그래야 남들 화면에도 조준 자세가 보인다.
	 * FoamGunComponent 의 bAimHeld 는 궤적 프리뷰용 로컬 상태라 복제되지 않는다.
	 */
	UFUNCTION(Server, Reliable)
	void ServerSetFoamAiming(bool bAiming);

	UFUNCTION()
	void OnRep_FoamAiming();

	/** 소유 클라·서버·사본 프록시가 모두 거치는 단일 적용 지점 */
	void RefreshFoamAimMontage();

	/** 조준 상태를 굳히고 로컬에 즉시 반영한 뒤 서버로 올린다 */
	void SetFoamAiming(bool bAiming);

	UPROPERTY(ReplicatedUsing = OnRep_FoamAiming)
	bool bFoamAiming = false;

	/** 현재 도구 메시에 붙어 있는 절단 루프 이펙트 인스턴스 */
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> CutterVFXComponent;

	/** 선택 도구가 바뀌면 진행 중인 오염물 타격을 취소한다. */
	void RefreshStainInputState();

	/** 현재 슬롯이 폼건인지. RMB 조준 컨텍스트 유지 판정에 쓴다 */
	bool IsFoamGunEquipped() const;

	/** 절단 어빌리티 태그로 판정한다. 톱날 표시 여부에 쓴다 */
	UFUNCTION(BlueprintPure, Category = "MD|Equipment")
	bool IsCutterEquipped() const;

	/**
	 * 인터랙트로 물건을 들고 있는지. 들고 있는 동안은 핫바 슬롯은 그대로 두고
	 * 도구 메시만 내려 맨손 연출을 한다. ABP에서 맨손 포즈 분기에도 쓴다
	 */
	UFUNCTION(BlueprintPure, Category = "MD|Interaction")
	bool IsCarryingItem() const;
	UEnhancedInputLocalPlayerSubsystem* GetEnhancedInputSubsystem() const;

	// 공통 아이템의 부착 오프셋과 메시 그립 소켓을 반영한 상대 트랜스폼 조회
	FTransform GetToolAttachTransform(const UStaticMesh* ToolMesh, UMD_ItemDefinition* Item) const;
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

	/** 본체 손 소켓을 기준으로 1인칭 도구를 배치한다. */
	void UpdateFirstPersonToolTransform(float DeltaSeconds);

	/** Keep the evaluated arms/tool grip together while aligning Blade_Tip to the center. */
	void SetFirstPersonScrapeViewModelActive(bool bActive);
	bool UpdateFirstPersonScrapeViewModel(float DeltaSeconds);
	bool ApplyFirstPersonScrapeRigTransform(const FTransform& DesiredToolWorld);
	void RestoreFirstPersonScrapeRigTransform();
	void ResetFirstPersonScrapePresentation();
	void SetScrapePresentationCenterViewActive(bool bActive);
	bool IsScrapeToolEquipped() const;
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

	/** PlayerState의 외형 변경 콜백 등록을 관리한다. */
	void BindAppearanceDelegate();
	void UnbindAppearanceDelegate();

	UFUNCTION()
	void HandleAppearanceChanged();

	/** 메시 컴포넌트의 모든 슬롯을 MID로 바꿔 메인 색을 넣는다. */
	void ApplyAppearanceColor(USkeletalMeshComponent* MeshComponent, FName ParameterName, const FLinearColor& Color);

	/** 마지막으로 적용한 외형. 선택 화면의 시작값으로 쓴다. */
	FMD_CharacterAppearance AppliedAppearance;

	bool bLocalThirdPersonViewEnabled = false;

	/** 직전에 사망 상태였는지. 부활 전이 판별용 */
	bool bWasDeadState = false;

	/** 같은 도구의 수량 변경은 진행 중인 타격을 취소하지 않는다. */
	TWeakObjectPtr<UMD_ToolDefinition> StainInputTool;

	/** 유한 타격은 LMB를 놓아도 끝까지 진행하며 새 도구 입력은 취소하지 않는다. */
	bool bStainStrokeInputCaptured = false;

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

	/** Display-only state; stroke/contact and legacy arm hiding have no producer. */
	FMD_ScrapePresentationState ScrapePresentationState;
	bool bScrapeViewModelHidArms = false;
	FTransform FirstPersonScrapeRigBaseRelativeTransform = FTransform::Identity;
	bool bHasFirstPersonScrapeRigBaseTransform = false;
	FTransform FirstPersonScrapeArmsBaseRelativeTransform = FTransform::Identity;
	TWeakObjectPtr<USceneComponent> FirstPersonScrapeArmsBaseAttachParent;
	FName FirstPersonScrapeArmsBaseAttachSocketName = NAME_None;
	bool bHasFirstPersonScrapeArmsBaseTransform = false;
	FTransform LastScrapeViewModelToolWorldTransform = FTransform::Identity;
	bool bHasLastScrapeViewModelToolWorldTransform = false;


};
