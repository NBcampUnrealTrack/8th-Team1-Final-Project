// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/MD_Character.h"
#include "Player/MD_PlayerState.h"
#include "Data/Input/MD_InputConfig.h"
#include "Component/Character/MD_CharacterMovementComponent.h"
#include "Component/Scrape/MD_ScrapeToolComponent.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Component/Support/MD_SupplyComponent.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraComponent.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "GameplayTagAssetInterface.h"
#include "Net/UnrealNetwork.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagContainer.h"
#include "Kismet/GameplayStatics.h"

// Sets default values
AMD_Character::AMD_Character(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UMD_CharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = true;
	bUseControllerRotationRoll = false;

	GetCharacterMovement()->bUseControllerDesiredRotation = false;
	GetCharacterMovement()->bOrientRotationToMovement = false;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 540.0f, 0.0f);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	Camera->SetRelativeLocation(FVector(0.f, 0.f, BaseEyeHeight)); //SkeltalMesh 눈높이
	Camera->bUsePawnControlRotation = true; // Camera Pitch Yaw == Controller Pitch Yaw, 기존3인칭의 SpringArm과 달리 카메라가 담당

	GetMesh()->SetOwnerNoSee(true);
	GetMesh()->SetOnlyOwnerSee(false);

	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonMesh"));
	FirstPersonMesh->SetupAttachment(Camera);
	FirstPersonMesh->SetOnlyOwnerSee(true);
	FirstPersonMesh->SetOwnerNoSee(false);
	FirstPersonMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	ScrapeToolComponent = CreateDefaultSubobject<UMD_ScrapeToolComponent>(TEXT("ScrapeToolComponent"));

	EquipHotbarComponent = CreateDefaultSubobject<UMD_EquipHotbarComponent>(TEXT("EquipHotbarComponent"));

	ThirdPersonToolMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ThirdPersonToolMesh"));
	ThirdPersonToolMesh->SetupAttachment(GetMesh(), ToolAttachSocketName);
	ThirdPersonToolMesh->SetOwnerNoSee(true);
	ThirdPersonToolMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HoldPoint = CreateDefaultSubobject<USceneComponent>(TEXT("HoldPoint"));
	HoldPoint->SetupAttachment(Camera);
	HoldPoint->SetRelativeLocation(FVector(150.f, 0.f, 0.f));

	PhysicsHandle = CreateDefaultSubobject<UPhysicsHandleComponent>(TEXT("PhysicsHandle"));

	SupplyComponent = CreateDefaultSubobject<UMD_SupplyComponent>(TEXT("SupplyComponent"));
}

void AMD_Character::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateInteractionFocus();

	if (HasAuthority())
	{
		// 대상이 파괴되면 핸들이 조용히 비므로 복제 상태도 같이 정리한다
		if (HeldActor && !IsHolding())
		{
			ReleaseTarget();
		}

		UpdateHeldTarget();
		UpdateCarrySettle(DeltaSeconds);
	}
	else
	{
		UpdateFollowedHeldActor(DeltaSeconds);
	}
}

void AMD_Character::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 다른 플레이어가 운반하는 물체도 그 머신에서 따라가야 하므로 모두에게 보낸다
	DOREPLIFETIME(AMD_Character, HeldActor);
}

void AMD_Character::ForceEnterWalkState()
{
	// TODO(김동연): GAS 스태미나 고갈 이벤트에서 호출 연결 예정
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
}

UAbilitySystemComponent* AMD_Character::GetAbilitySystemComponent() const
{
	AMD_PlayerState* MDPlayerState = GetPlayerState<AMD_PlayerState>();

	if (!IsValid(MDPlayerState))
	{
		return nullptr;
	}

	return MDPlayerState->GetAbilitySystemComponent();
}

// Called when the game starts or when spawned
void AMD_Character::BeginPlay()
{
	Super::BeginPlay();
	CaptureDefaultHoldProfile();

	if (!IsValid(PlayerMappingContext)) return;

	if (IsLocallyControlled())
	{
		APlayerController* PlayerController = Cast<APlayerController>(GetController());

		if (IsValid(PlayerController))
		{
			UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer());

			if (IsValid(Subsystem))
			{
				Subsystem->AddMappingContext(PlayerMappingContext, 0);
			}
		}
	}

	if (IsValid(EquipHotbarComponent))
	{
		EquipHotbarComponent->OnHotbarChanged.AddDynamic(this, &AMD_Character::HandleHotbarChanged);
		RefreshToolMesh();
	}
}

void AMD_Character::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetCurrentTarget(nullptr);
	ReleaseTarget();
	StopFollowingHeldActor();

	Super::EndPlay(EndPlayReason);
}

void AMD_Character::OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PrevMovementMode, PreviousCustomMode);

	if (UMD_CharacterMovementComponent* MDMovement = Cast<UMD_CharacterMovementComponent>(GetCharacterMovement()))
	{
		if (MDMovement->MovementMode == MOVE_Walking)
		{
			MDMovement->SetWantsToSwim(false);
		}
	}

	const EMDMovementState NewState = GetCharacterMovement()->IsSwimming() ? EMDMovementState::Swimming : EMDMovementState::Walking;

	if (NewState == CurrentMovementState)
	{
		return;
	}

	CurrentMovementState = NewState;
	OnMovementStateChanged.Broadcast(CurrentMovementState);

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{

		if (NewState == EMDMovementState::Swimming)
		{
			ASC->AddLooseGameplayTag(MDGameplayTags::State_Swimming);
		}
		else
		{
			ASC->RemoveLooseGameplayTag(MDGameplayTags::State_Swimming);
		}
	}
}

void AMD_Character::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	InitializeAbilitySystem();
	GrantDefaultAbilities();
}

void AMD_Character::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	InitializeAbilitySystem();
}

// Called to bind functionality to input
void AMD_Character::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (!IsValid(InputConfig)) return;

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);

	if (IsValid(EIC))
	{
		EIC->BindAction(InputConfig->MoveAction, ETriggerEvent::Triggered, this, &AMD_Character::HandleMoveInput);
		EIC->BindAction(InputConfig->LookAction, ETriggerEvent::Triggered, this, &AMD_Character::HandleLookInput);
		EIC->BindAction(InputConfig->JumpAction, ETriggerEvent::Started, this, &AMD_Character::HandleJumpOrSwimInput);
		EIC->BindAction(InputConfig->JumpAction, ETriggerEvent::Completed, this, &AMD_Character::HandleJumpOrSwimReleasedInput);
		EIC->BindAction(InputConfig->InteractAction, ETriggerEvent::Started, this, &AMD_Character::HandleInteractInput);
		EIC->BindAction(InputConfig->AbilityAction, ETriggerEvent::Started, this, &AMD_Character::HandleAbilityInput);
		EIC->BindAction(InputConfig->AbilityAction, ETriggerEvent::Completed, this, &AMD_Character::HandleAbilityReleasedInput);
		EIC->BindAction(InputConfig->AbilityAction, ETriggerEvent::Canceled, this, &AMD_Character::HandleAbilityReleasedInput);
		EIC->BindAction(InputConfig->SwapEquipmentAction, ETriggerEvent::Triggered, this, &AMD_Character::HandleSwapEquipmentInput);
		//EIC->BindAction(InputConfig->WallCutAction, ETriggerEvent::Started, this, &AMD_Character::HandleWallCutInput);
		//EIC->BindAction(InputConfig->WallScanAction, ETriggerEvent::Started, this, &AMD_Character::HandleWallScanInput);
	}

}

void AMD_Character::HandleMoveInput(const FInputActionValue& Value)
{
	if (!Controller)
	{
		return;
	}

	const FVector2D MoveInput = Value.Get<FVector2D>();

	const FRotator ControlRotation = Controller->GetControlRotation();
	const FRotator MovementRotation = (CurrentMovementState == EMDMovementState::Swimming) ? ControlRotation : FRotator(0.f, ControlRotation.Yaw, 0.f);

	const FVector ForwardDirection = FRotationMatrix(MovementRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(MovementRotation).GetUnitAxis(EAxis::Y);

	AddMovementInput(ForwardDirection, MoveInput.X);
	AddMovementInput(RightDirection, MoveInput.Y);
}

void AMD_Character::HandleLookInput(const FInputActionValue& Value)
{
	FVector2D LookInput = Value.Get<FVector2D>();
	if (IsValid(ScrapeToolComponent))
	{
		// 입력을 먼저 고정해 카메라 되튐을 막는다.
		LookInput = ScrapeToolComponent->ConstrainLookInput(LookInput);
	}

	AddControllerYawInput(LookInput.X);
	AddControllerPitchInput(LookInput.Y);
}

void AMD_Character::HandleInteractInput(const FInputActionValue& Value)
{
	AActor* Target = CurrentTarget.Get();

	// 픽업 태그가 없는 대상(이동 콘솔 등 로컬 UI 상호작용)은 서버를 거치지 않고 로컬에서 실행
	if (IsValid(Target) && Target->Implements<UMD_Interactable>())
	{
		const IGameplayTagAssetInterface* TagAsset = Cast<IGameplayTagAssetInterface>(Target);
		const bool bPickupTarget = TagAsset
				&& TagAsset->HasMatchingGameplayTag(MDGameplayTags::Interaction_Pickup_Physics);

		if (!bPickupTarget)
		{
			IMD_Interactable::Execute_Interact(Target, this);
			return;
		}
	}

	ServerInteract(Target);
}

void AMD_Character::ServerInteract_Implementation(AActor* RequestedActor)
{
	if (IsHolding())
	{
		ReleaseTarget();
		return;
	}

	if (!IsValid(RequestedActor)
			|| !RequestedActor->Implements<UMD_Interactable>()
			|| TraceInteractionTarget() != RequestedActor) return;

	// 핫바는 장비 전용 — 월드 오브젝트 픽업은 물리 운반만 허용 (기획 02 §2.5)
	const IGameplayTagAssetInterface* TagAsset = Cast<IGameplayTagAssetInterface>(RequestedActor);
	if (TagAsset
			&& TagAsset->HasMatchingGameplayTag(MDGameplayTags::Interaction_Pickup_Physics))
	{
		TryHoldTarget(RequestedActor);
		return;
	}

	IMD_Interactable::Execute_Interact(RequestedActor, this);
}

bool AMD_Character::TryHoldTarget(AActor* Target)
{
	if (!HasAuthority() || !IsValid(Target) || !IsValid(PhysicsHandle)) return false;

	// 들고 있는 물체도 InteractTrace를 막아 남이 조준할 수 있다. 두 핸들이 한 컴포넌트를 잡으면
	// 먼저 놓는 쪽이 SetReplicateMovement(true)로 되돌려 남은 쪽의 운반이 깨진다
	if (IsHeldByOtherCharacter(Target)) return false;

	UPrimitiveComponent* Component = nullptr;
	FVector GrabLocation = FVector::ZeroVector;
	FVector HoldOffset = FVector::ZeroVector;
	FMD_HoldProfile HoldProfile = DefaultHoldProfile;

	if (Target->Implements<UMD_Grabbable>())
	{
		Component = IMD_Grabbable::Execute_GetGrabComponent(Target);
		const USceneComponent* GrabPoint = IMD_Grabbable::Execute_GetGrabPoint(Target);

		if (!IsValid(Component) || !Component->IsSimulatingPhysics() || !IsValid(GrabPoint)) return false;

		GrabLocation = GrabPoint->GetComponentLocation();
		HoldOffset = IMD_Grabbable::Execute_GetHoldOffset(Target);
		HoldProfile = IMD_Grabbable::Execute_GetHoldProfile(Target);
	}
	else
	{
		Component = Cast<UPrimitiveComponent>(Target->GetRootComponent());
		if (!IsValid(Component) || !Component->IsSimulatingPhysics()) return false;

		GrabLocation = Component->GetCenterOfMass();
	}

	ApplyHoldProfile(HoldProfile);

	if (HoldProfile.bConstrainRotation)
	{
		PhysicsHandle->GrabComponentAtLocationWithRotation(
			Component,
			NAME_None,
			GrabLocation,
			Component->GetComponentRotation());
	}
	else
	{
		PhysicsHandle->GrabComponentAtLocation(
			Component,
			NAME_None,
			GrabLocation);
	}

	if (PhysicsHandle->GetGrabbedComponent() != Component)
	{
		ApplyHoldProfile(DefaultHoldProfile);
		ActiveHoldProfile = DefaultHoldProfile;
		return false;
	}

	HeldTargetOffset = HoldOffset;
	HeldGrabPointOffset = Component->GetComponentTransform().InverseTransformPositionNoScale(GrabLocation);
	ActiveHoldProfile = HoldProfile;
	HeldActor = Target;
	bHeldActorSettled = false;
	HeldCarrySeconds = 0.f;

	// 운반 중에는 위치를 복제하지 않는다. 스프링 속도가 복제되면 클라이언트가 그 속도로
	// 자유 낙하를 이어받아 물체가 튀어오른다. 대신 각 머신이 HoldPoint를 따라간다
	Target->SetReplicateMovement(false);

	if (Target->Implements<UMD_Grabbable>())
	{
		IMD_Grabbable::Execute_OnCarryStarted(Target);
	}

	UpdateHeldTarget();
	return true;
}

bool AMD_Character::IsHolding() const
{
	return IsValid(PhysicsHandle) && IsValid(PhysicsHandle->GetGrabbedComponent());
}

bool AMD_Character::IsHeldByOtherCharacter(const AActor* Target) const
{
	const UWorld* World = GetWorld();
	if (!IsValid(Target) || !World) return false;

	// 운반은 ServerInteract에서만 시작되므로 조종 중인 캐릭터만 보면 된다. 협동 인원수라 순회로 충분하다
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PlayerController = It->Get();
		if (!IsValid(PlayerController)) continue;

		const AMD_Character* OtherCharacter = Cast<AMD_Character>(PlayerController->GetPawn());
		if (OtherCharacter && OtherCharacter != this && OtherCharacter->HeldActor == Target)
		{
			return true;
		}
	}

	return false;
}

void AMD_Character::ReleaseTarget()
{
	HeldTargetOffset = FVector::ZeroVector;
	HeldGrabPointOffset = FVector::ZeroVector;
	bHeldActorSettled = false;
	HeldCarrySeconds = 0.f;

	if (AActor* PreviousHeldActor = HeldActor)
	{
		HeldActor = nullptr;

		if (IsValid(PreviousHeldActor))
		{
			if (PreviousHeldActor->Implements<UMD_Grabbable>())
			{
				IMD_Grabbable::Execute_OnCarryEnded(PreviousHeldActor);
			}

			// 낙하·투척 궤적은 다시 서버가 알려준다. 즉시 보내 따라가기와의 공백을 줄인다
			PreviousHeldActor->SetReplicateMovement(true);
			PreviousHeldActor->ForceNetUpdate();
		}
	}

	if (!IsValid(PhysicsHandle)) return;

	if (PhysicsHandle->GetGrabbedComponent())
	{
		PhysicsHandle->ReleaseComponent();
	}

	ApplyHoldProfile(DefaultHoldProfile);
	ActiveHoldProfile = DefaultHoldProfile;
}

FTransform AMD_Character::GetHoldPointTransform() const
{
	if (!IsValid(HoldPoint)) return FTransform::Identity;

	// Camera의 bUsePawnControlRotation 갱신은 시점을 그리는 머신에서만 일어난다.
	// 원격 캐릭터에는 낡은 회전이 남으므로 GetBaseAimRotation()으로 다시 만든다.
	// 위치는 캡슐에 붙어 있어 어디서나 정확하다
	if (!IsValid(Camera)) return HoldPoint->GetComponentTransform();

	const FTransform ViewTransform(GetBaseAimRotation(), Camera->GetComponentLocation());
	return HoldPoint->GetRelativeTransform() * ViewTransform;
}

void AMD_Character::UpdateHeldTarget()
{
	if (!HasAuthority() || !IsHolding() || !IsValid(HoldPoint)) return;

	const FTransform HoldTransform = GetHoldPointTransform();
	const FVector TargetLocation = HoldTransform.TransformPositionNoScale(HeldTargetOffset);

	if (ActiveHoldProfile.bConstrainRotation)
	{
		PhysicsHandle->SetTargetLocationAndRotation(
			TargetLocation,
			HoldTransform.Rotator());
	}
	else
	{
		PhysicsHandle->SetTargetLocation(TargetLocation);
	}
}

void AMD_Character::UpdateCarrySettle(float DeltaSeconds)
{
	if (!HasAuthority() || bHeldActorSettled || !IsValid(HeldActor) || !IsValid(HoldPoint)) return;

	// 알림을 받을 대상이 아니면 계속 재볼 이유가 없다
	if (!HeldActor->Implements<UMD_Grabbable>())
	{
		bHeldActorSettled = true;
		return;
	}

	HeldCarrySeconds += DeltaSeconds;

	const UPrimitiveComponent* Component = PhysicsHandle ? PhysicsHandle->GetGrabbedComponent() : nullptr;
	if (!IsValid(Component)) return;

	// 물리 핸들이 맞추는 지점은 컴포넌트 원점이 아니라 GrabPoint다
	const FVector CurrentGrabPoint =
		Component->GetComponentTransform().TransformPositionNoScale(HeldGrabPointOffset);
	const FVector TargetLocation =
		GetHoldPointTransform().TransformPositionNoScale(HeldTargetOffset);

	const bool bReachedTarget =
		FVector::DistSquared(CurrentGrabPoint, TargetLocation) <= FMath::Square(CarrySettleTolerance);

	// 벽에 끼면 영영 못 닿는다. 내용물이 영구히 굳는 것을 막는 상한
	const bool bTimedOut = HeldCarrySeconds >= CarrySettleTimeout;

	if (!bReachedTarget && !bTimedOut) return;

	bHeldActorSettled = true;
	IMD_Grabbable::Execute_OnCarrySettled(HeldActor);
}

void AMD_Character::OnRep_HeldActor()
{
	StopFollowingHeldActor();
	StartFollowingHeldActor();
}

void AMD_Character::StartFollowingHeldActor()
{
	if (HasAuthority() || !IsValid(HeldActor)) return;

	UPrimitiveComponent* Component = nullptr;
	FVector GrabLocation = FVector::ZeroVector;

	// 서버 TryHoldTarget과 같은 기준이라야 두 머신의 잡는 지점이 어긋나지 않는다
	if (HeldActor->Implements<UMD_Grabbable>())
	{
		Component = IMD_Grabbable::Execute_GetGrabComponent(HeldActor);
		const USceneComponent* GrabPoint = IMD_Grabbable::Execute_GetGrabPoint(HeldActor);

		if (!IsValid(Component) || !IsValid(GrabPoint)) return;

		GrabLocation = GrabPoint->GetComponentLocation();
		HeldTargetOffset = IMD_Grabbable::Execute_GetHoldOffset(HeldActor);
		ActiveHoldProfile = IMD_Grabbable::Execute_GetHoldProfile(HeldActor);
	}
	else
	{
		Component = Cast<UPrimitiveComponent>(HeldActor->GetRootComponent());
		if (!IsValid(Component)) return;

		GrabLocation = Component->GetCenterOfMass();
		HeldTargetOffset = FVector::ZeroVector;
		ActiveHoldProfile = DefaultHoldProfile;
	}

	HeldGrabPointOffset = Component->GetComponentTransform().InverseTransformPositionNoScale(GrabLocation);

	// 물리를 켠 채로 두면 중력이 계속 끌어내려 따라가기와 싸운다
	Component->SetSimulatePhysics(false);
	FollowedComponent = Component;
}

void AMD_Character::StopFollowingHeldActor()
{
	UPrimitiveComponent* Component = FollowedComponent.Get();

	FollowedComponent = nullptr;
	HeldTargetOffset = FVector::ZeroVector;
	HeldGrabPointOffset = FVector::ZeroVector;
	ActiveHoldProfile = DefaultHoldProfile;

	if (!IsValid(Component)) return;

	// 서버의 위치 복제가 다시 오기 전까지도 자연스럽게 떨어지도록 물리를 되돌린다
	Component->SetSimulatePhysics(true);
}

void AMD_Character::UpdateFollowedHeldActor(float DeltaSeconds)
{
	UPrimitiveComponent* Component = FollowedComponent.Get();

	if (!IsValid(Component) || !IsValid(HoldPoint)) return;

	const FTransform HoldTransform = GetHoldPointTransform();
	const FVector GrabTargetLocation = HoldTransform.TransformPositionNoScale(HeldTargetOffset);

	// 회전 비제약 프로필은 서버에서 자유롭게 구른다. 재현할 수 없어 자세를 고정하고 위치만 따라간다
	FQuat TargetRotation = ActiveHoldProfile.bConstrainRotation
		? HoldTransform.GetRotation()
		: Component->GetComponentQuat();

	// 물리 핸들은 GrabPoint를 목표 지점에 맞춘다. 컴포넌트 원점은 그만큼 물러나 있어야 한다.
	FVector TargetLocation = GrabTargetLocation - TargetRotation.RotateVector(HeldGrabPointOffset);

	// 서버 스프링이 만드는 지연을 대신한다. 잡는 순간의 이동도 이 속도로 흡수돼 튀지 않는다.
	if (ActiveHoldProfile.RemoteFollowSpeed > 0.f)
	{
		const float Alpha = FMath::Clamp(DeltaSeconds * ActiveHoldProfile.RemoteFollowSpeed, 0.f, 1.f);
		TargetLocation = FMath::Lerp(Component->GetComponentLocation(), TargetLocation, Alpha);
		TargetRotation = FQuat::Slerp(Component->GetComponentQuat(), TargetRotation, Alpha).GetNormalized();
	}

	// Teleport가 아닌 이동이라야 키네마틱 바디가 속도를 갖고, 쓰레기통 안의 내용물도 같이 실려 간다
	Component->SetWorldLocationAndRotation(TargetLocation, TargetRotation, false, nullptr, ETeleportType::None);
}

void AMD_Character::CaptureDefaultHoldProfile()
{
	if (!IsValid(PhysicsHandle)) return;

	DefaultHoldProfile.bConstrainRotation = true;
	DefaultHoldProfile.bSoftLinear = PhysicsHandle->bSoftLinearConstraint;
	DefaultHoldProfile.bSoftAngular = PhysicsHandle->bSoftAngularConstraint;
	DefaultHoldProfile.bInterpolate = PhysicsHandle->bInterpolateTarget;
	DefaultHoldProfile.LinearStiffness = PhysicsHandle->LinearStiffness;
	DefaultHoldProfile.LinearDamping = PhysicsHandle->LinearDamping;
	DefaultHoldProfile.AngularStiffness = PhysicsHandle->AngularStiffness;
	DefaultHoldProfile.AngularDamping = PhysicsHandle->AngularDamping;
	DefaultHoldProfile.InterpolationSpeed = PhysicsHandle->InterpolationSpeed;
	ActiveHoldProfile = DefaultHoldProfile;
}

void AMD_Character::ApplyHoldProfile(const FMD_HoldProfile& Profile)
{
	if (!IsValid(PhysicsHandle)) return;

	PhysicsHandle->bSoftLinearConstraint = Profile.bSoftLinear;
	PhysicsHandle->bSoftAngularConstraint = Profile.bSoftAngular;
	PhysicsHandle->bInterpolateTarget = Profile.bInterpolate;
	PhysicsHandle->SetLinearStiffness(FMath::Max(0.f, Profile.LinearStiffness));
	PhysicsHandle->SetLinearDamping(FMath::Max(0.f, Profile.LinearDamping));
	PhysicsHandle->SetAngularStiffness(FMath::Max(0.f, Profile.AngularStiffness));
	PhysicsHandle->SetAngularDamping(FMath::Max(0.f, Profile.AngularDamping));
	PhysicsHandle->SetInterpolationSpeed(FMath::Max(0.f, Profile.InterpolationSpeed));
}

void AMD_Character::HandleAbilityInput(const FInputActionValue& Value)
{
	if (!IsValid(EquipHotbarComponent))
	{
		return;
	}

	UMD_ToolDefinition* CurrentTool = EquipHotbarComponent->GetCurrentTool();
	if (!IsValid(CurrentTool) || !CurrentTool->ToolAbilityTag.IsValid())
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Yellow, TEXT("No tool equipped"));
		return;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!IsValid(ASC))
	{
		return;
	}

	FGameplayTagContainer AbilityTags;
	AbilityTags.AddTag(CurrentTool->ToolAbilityTag);
	ASC->TryActivateAbilitiesByTag(AbilityTags);
}

void AMD_Character::HandleAbilityReleasedInput(const FInputActionValue& Value)
{
	if (!IsValid(EquipHotbarComponent))
	{
		return;
	}

	UMD_ToolDefinition* CurrentTool = EquipHotbarComponent->GetCurrentTool();
	if (!IsValid(CurrentTool) || !CurrentTool->ToolAbilityTag.IsValid())
	{
		return;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!IsValid(ASC))
	{
		return;
	}

	FGameplayTagContainer AbilityTags;
	AbilityTags.AddTag(CurrentTool->ToolAbilityTag);
	ASC->CancelAbilities(&AbilityTags);
}

void AMD_Character::HandleSwapEquipmentInput(const FInputActionValue& Value)
{
	//GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Red, TEXT("SwapEquipment: Activated"));
	if (!IsValid(EquipHotbarComponent))
	{
		return;
	}

	const float ScrollValue = Value.Get<float>();

	UE_LOG(LogTemp, Log, TEXT("[SwapEquipment] ScrollValue: %.2f"), ScrollValue);

	if (FMath::IsNearlyZero(ScrollValue))
	{
		return;
	}

	EquipHotbarComponent->CycleSlot(ScrollValue > 0.f ? 1 : -1);
	UE_LOG(LogTemp, Log, TEXT("[SwapEquipment] Direction: %s"), ScrollValue > 0.f ? TEXT("Next") : TEXT("Prev"));
}

void AMD_Character::HandleJumpOrSwimInput(const FInputActionValue& Value)
{
	UMD_CharacterMovementComponent* MDMovement = Cast<UMD_CharacterMovementComponent>(GetCharacterMovement());
	if (!IsValid(MDMovement))
	{
		return;
	}

	if (MDMovement->IsMovingOnGround() && MDMovement->IsInWater())
	{
		MDMovement->EnterSwimmingFromGround();
		return;
	}

	Jump();
}

void AMD_Character::HandleJumpOrSwimReleasedInput(const FInputActionValue& Value)
{
	StopJumping();
}

/*void AMD_Character::HandleWallCutInput(const FInputActionValue& Value)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!IsValid(ASC))
	{
		return;
	}

	if (!WallCutAbilityClass)
	{
		return;
	}

	ASC->TryActivateAbilityByClass(WallCutAbilityClass);
} */

/*void AMD_Character::HandleWallScanInput(const FInputActionValue& Value)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!IsValid(ASC))
	{
		return;
	}

	if (!WallScanAbilityClass)
	{
		return;
	}

	ASC->TryActivateAbilityByClass(WallScanAbilityClass);
}*/

void AMD_Character::UpdateInteractionFocus()
{
	if (!IsLocallyControlled() || !Controller || !GetWorld())
	{
		SetCurrentTarget(nullptr);
		return;
	}

	// 들고 있는 물체 너머의 대상이 계속 반짝이면 헷갈리므로 강조를 끈다.
	// 놓기는 CurrentTarget이 비어도 ServerInteract에서 처리된다.
	if (HeldActor)
	{
		SetCurrentTarget(nullptr);
		return;
	}

	SetCurrentTarget(TraceInteractionTarget());
}

AActor* AMD_Character::TraceInteractionTarget() const
{
	if (!Controller || !GetWorld()) return nullptr;

	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);

	const FVector TraceEnd = ViewLocation + ViewRotation.Vector() * InteractTraceDistance;

	FHitResult Hit;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	if (!GetWorld()->LineTraceSingleByChannel(
		Hit,
		ViewLocation,
		TraceEnd,
		MDCollisionChannels::InteractTrace,
		QueryParams)) return nullptr;

	AActor* HitActor = Hit.GetActor();
	return IsValid(HitActor) && HitActor->Implements<UMD_Interactable>() ? HitActor : nullptr;
}

void AMD_Character::SetCurrentTarget(AActor* NewTarget)
{
	if (CurrentTarget.Get() == NewTarget) return;

	if (AActor* PreviousTarget = CurrentTarget.Get())
	{
		if (PreviousTarget->Implements<UMD_Interactable>()) IMD_Interactable::Execute_SetInteractionHighlighted(PreviousTarget, false);
	}

	CurrentTarget = NewTarget;

	if (IsValid(NewTarget) && NewTarget->Implements<UMD_Interactable>()) IMD_Interactable::Execute_SetInteractionHighlighted(NewTarget, true);
}

void AMD_Character::InitializeAbilitySystem()
{
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->InitAbilityActorInfo(GetPlayerState(), this);
	}
	
	//PlayerState가 호출되는 시점에 체력변화 델리게이트를 구독.
	AMD_PlayerState* MDPlayerState = GetPlayerState<AMD_PlayerState>();
	if (!IsValid(MDPlayerState))
	{
		return;
	}

	if (!MDPlayerState->OnHealthStateChanged.IsAlreadyBound(this, &AMD_Character::HandleHealthStateChanged))
	{
		MDPlayerState->OnHealthStateChanged.AddDynamic(this, &AMD_Character::HandleHealthStateChanged);
	}

}

void AMD_Character::GrantDefaultAbilities()
{
	if (!HasAuthority())
	{
		return;
	}
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!IsValid(ASC))
	{
		return;
	}

	for (const TSubclassOf<UGameplayAbility>& AbilityClass : DefaultAbilities)
	{
		if (AbilityClass)
		{
			ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, this));
		}
	}
}

void AMD_Character::HandleHotbarChanged()
{
	RefreshToolMesh();

	if (IsValid(EquipHotbarComponent))
	{
		UMD_ToolDefinition* CurrentTool = EquipHotbarComponent->GetCurrentTool();
		UE_LOG(LogTemp, Log, TEXT("[Hotbar] Slot %d/%d, Tool: %s"),
			EquipHotbarComponent->GetCurrentSlotIndex(),
			EquipHotbarComponent->GetSlotCount(),
			IsValid(CurrentTool) ? *CurrentTool->ToolName.ToString() : TEXT("Empty"));
	}
}

void AMD_Character::RefreshToolMesh()
{
	if (!IsValid(EquipHotbarComponent))
	{
		return;
	}

	UMD_ToolDefinition* CurrentTool = EquipHotbarComponent->GetCurrentTool();
	UStaticMesh* NewMesh = IsValid(CurrentTool) ? CurrentTool->ToolMesh : nullptr;

	// 도구별 소켓이 지정돼 있으면 그 소켓, 없으면 기본 소켓
	const FName SocketName =
		(IsValid(CurrentTool) && !CurrentTool->AttachSocketName.IsNone())
			? CurrentTool->AttachSocketName
			: ToolAttachSocketName;

	ThirdPersonToolMesh->SetStaticMesh(NewMesh);

	// 소켓의 위치·회전·스케일을 그대로 따르도록 스냅 부착
	ThirdPersonToolMesh->AttachToComponent(
		GetMesh(), FAttachmentTransformRules::SnapToTargetIncludingScale, SocketName);
}

void AMD_Character::HandleHealthStateChanged(EMD_HealthState NewState)
{
	const bool bIsDead = (NewState == EMD_HealthState::Dead);

	// 이동 차단은 서버·클라 양쪽에서 걸어 입력 반응을 즉시 없앤다.
	ApplyDeathMovementLock(bIsDead);

	// 모선 이동은 사망→부활 전이에만. 건강→부상 전이에서 옮기면 안 된다.
	// 위치 이동은 서버 권한이다. 클라이언트가 옮기면 곧 서버 위치로 되돌아간다.
	if (!bIsDead && bWasDeadState && HasAuthority())
	{
		TeleportToMothership();
	}

	bWasDeadState = bIsDead;
}

void AMD_Character::ApplyDeathMovementLock(bool bLocked)
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!IsValid(Movement))
	{
		return;
	}

	if (bLocked)
	{
		Movement->DisableMovement();
		Movement->StopMovementImmediately();
		return;
	}

	// 수중·지상이 섞인 맵이라 특정 모드를 지정하지 않고 기본 판정에 맡긴다.
	Movement->SetDefaultMovementMode();
}

void AMD_Character::TeleportToMothership()
{
	TArray<AActor*> FoundStarts;
	UGameplayStatics::GetAllActorsOfClassWithTag(GetWorld(), APlayerStart::StaticClass(), MothershipRespawnTag, FoundStarts);

	if (FoundStarts.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Health] 리스폰 지점(%s)을 찾을 수 없다"),*MothershipRespawnTag.ToString());
		return;
	}

	const AActor* RespawnPoint = FoundStarts[0];

	// 붕괴로 막힌 방에 되살아나지 않도록 죽은 자리가 아니라 모선으로 옮긴다. (기획 02 §6)
	SetActorLocationAndRotation(
		RespawnPoint->GetActorLocation(),
		RespawnPoint->GetActorRotation(),
		/*bSweep=*/false,
		nullptr,
		ETeleportType::TeleportPhysics);
}
