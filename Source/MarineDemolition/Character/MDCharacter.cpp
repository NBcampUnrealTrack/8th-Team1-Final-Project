// Fill out your copyright notice in the Description page of Project Settings.


#include "MarineDemolition/Character/MDCharacter.h"
#include "MarineDemolition/Input/MDInputConfig.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

// Sets default values
AMDCharacter::AMDCharacter()
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

// Called when the game starts or when spawned
void AMDCharacter::BeginPlay()
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

// Called to bind functionality to input
void AMDCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (!IsValid(InputConfig)) return;

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);

	if (IsValid(EIC))
	{
		EIC->BindAction(InputConfig->IA_Move, ETriggerEvent::Triggered, this, &AMDCharacter::Input_Move);
		EIC->BindAction(InputConfig->IA_Look, ETriggerEvent::Triggered, this, &AMDCharacter::Input_Look);
		EIC->BindAction(InputConfig->IA_Jump, ETriggerEvent::Started, this, &ACharacter::Jump);
		EIC->BindAction(InputConfig->IA_Jump, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
		EIC->BindAction(InputConfig->IA_Interact, ETriggerEvent::Started, this, &AMDCharacter::Input_Interact);
		EIC->BindAction(InputConfig->IA_Ability, ETriggerEvent::Started, this, &AMDCharacter::Input_Ability);
		EIC->BindAction(InputConfig->IA_SwapEquipment, ETriggerEvent::Triggered, this, &AMDCharacter::Input_SwapEquipment);
	}

}

void AMDCharacter::Input_Move(const FInputActionValue& Value)
{

	if (!Controller)
	{
		return;
	}

	const FVector2D Moveinput = Value.Get<FVector2D>();

	const FRotator ControlRotation = Controller->GetControlRotation();
	const FRotator ControlYawRotation(0.0f, ControlRotation.Yaw, 0.0f);

	const FVector ForwardDirection = FRotationMatrix(ControlYawRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(ControlYawRotation).GetUnitAxis(EAxis::Y);

	AddMovementInput(ForwardDirection, Moveinput.X);
	AddMovementInput(RightDirection, Moveinput.Y);
}

void AMDCharacter::Input_Look(const FInputActionValue& Value)
{
	const FVector2D LookInput = Value.Get<FVector2D>();
	AddControllerYawInput(LookInput.X);
	AddControllerPitchInput(LookInput.Y);
}

void AMDCharacter::Input_Interact(const FInputActionValue& Value)
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

void AMDCharacter::Input_Ability(const FInputActionValue& Value)
{
	GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Red, TEXT("Ability: Activated"));
}

void AMDCharacter::Input_SwapEquipment(const FInputActionValue& Value)
{
	GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Red, TEXT("SwapEquipment: Activated"));
}
