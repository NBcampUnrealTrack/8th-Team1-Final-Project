// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/MD_Character.h"
#include "Input/MD_InputConfig.h"
#include "Component/MD_CharacterMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

// Sets default values
AMD_Character::AMD_Character(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UMD_CharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

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
}

void AMD_Character::ForceEnterWalkState()
{
	// TODO(김동연): GAS 스태미나 고갈 이벤트에서 호출 연결 예정
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
}

// Called when the game starts or when spawned
void AMD_Character::BeginPlay()
{
	Super::BeginPlay();

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
		EIC->BindAction(InputConfig->SwapEquipmentAction, ETriggerEvent::Triggered, this, &AMD_Character::HandleSwapEquipmentInput);
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
	const FVector2D LookInput = Value.Get<FVector2D>();
	AddControllerYawInput(LookInput.X);
	AddControllerPitchInput(LookInput.Y);
}

void AMD_Character::HandleInteractInput(const FInputActionValue& Value)
{
	if (!Controller)
	{
		return;
	}

	//라인트레이스 시작위치
	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);

	const FVector TraceEnd = ViewLocation + ViewRotation.Vector() * InteractTraceDistance;

	//히트판정
	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);

	if (GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, TraceEnd, ECC_Visibility, Params))
	{
		if (AActor* HitActor = Hit.GetActor())
		{
			GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Green, FString::Printf(TEXT("Interact Hit: %s"), *HitActor->GetName()));
		}
		else
		{
			GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Red, TEXT("Interact: No Target"));
		}
	}
}

void AMD_Character::HandleAbilityInput(const FInputActionValue& Value)
{
	GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Red, TEXT("Ability: Activated"));
}

void AMD_Character::HandleSwapEquipmentInput(const FInputActionValue& Value)
{
	GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Red, TEXT("SwapEquipment: Activated"));
}

void AMD_Character::HandleJumpOrSwimInput(const FInputActionValue& Value)
{
	UMD_CharacterMovementComponent* MDMovement = Cast<UMD_CharacterMovementComponent>(GetCharacterMovement());
	if (!IsValid(MDMovement))
	{
		return;
	}

	if (MDMovement->IsMovingOnGround())
	{
		MDMovement->SetWantsToSwim(true);
		MDMovement->SetMovementMode(MOVE_Swimming);
		return;
	}

	Jump();
}

void AMD_Character::HandleJumpOrSwimReleasedInput(const FInputActionValue& Value)
{
	StopJumping();
}
