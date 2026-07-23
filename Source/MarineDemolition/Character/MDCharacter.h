// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "MDCharacter.generated.h"

class UMDInputConfig;
class UInputMappingContext;
class UInputAction;
class UCameraComponent;
class USpringArmComponent;
class USkeletalMeshComponent;

UCLASS()
class MARINEDEMOLITION_API AMDCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	AMDCharacter();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

#pragma region Components

protected:

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USkeletalMeshComponent> FirstPersonMesh;

#pragma endregion

#pragma region Input

protected:

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> PlayerMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UMDInputConfig> InputConfig;

	UPROPERTY(EditAnywhere, Category = "Interact")
	float InteractTraceDistance = 200.f;

private:

	void Input_Move(const FInputActionValue& Value);
	void Input_Look(const FInputActionValue& Value);
	void Input_Interact(const FInputActionValue& Value);
	void Input_Ability(const FInputActionValue& Value);
	void Input_SwapEquipment(const FInputActionValue& Value);

#pragma endregion
};
