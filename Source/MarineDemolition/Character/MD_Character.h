// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "MD_Character.generated.h"

class UMD_InputConfig;
class UInputMappingContext;
class UInputAction;
class UCameraComponent;
class USpringArmComponent;
class USkeletalMeshComponent;

UCLASS()
class MARINEDEMOLITION_API AMD_Character : public ACharacter
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	AMD_Character();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

protected:

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USkeletalMeshComponent> FirstPersonMesh;

protected:

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> PlayerMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UMD_InputConfig> InputConfig;

	UPROPERTY(EditAnywhere, Category = "Interact")
	float InteractTraceDistance = 200.f;

private:

	void Input_Move(const FInputActionValue& Value);
	void Input_Look(const FInputActionValue& Value);
	void Input_Interact(const FInputActionValue& Value);
	void Input_Ability(const FInputActionValue& Value);
	void Input_SwapEquipment(const FInputActionValue& Value);

};
