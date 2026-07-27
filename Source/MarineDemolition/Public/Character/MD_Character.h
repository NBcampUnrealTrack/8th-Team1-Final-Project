// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "AbilitySystemInterface.h"
#include "Ability/MD_AbilityTypes.h"
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameplayAbilitySpec.h"
#include "InputActionValue.h"
#include "MD_Character.generated.h"

class UGameplayAbility;
class UMD_InputConfig;
class UInputMappingContext;
class UInputAction;
class UCameraComponent;
class USkeletalMeshComponent;
class UMD_CharacterMovementComponent;
class UMD_ScrapeToolComponent;
class UAbilitySystemComponent;

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

	UPROPERTY(BlueprintAssignable, Category = "Movement|Water")
	FMDOnMovementStateChanged OnMovementStateChanged;

	UFUNCTION(BlueprintPure, Category = "Movement|Water")
	EMDMovementState GetCurrentMovementState() const { return CurrentMovementState; }

	UFUNCTION(BlueprintCallable, Category = "Movement|Water")
	void ForceEnterWalkState();

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	UFUNCTION(BlueprintPure, Category = "MD|Scrape")
	UMD_ScrapeToolComponent* GetScrapeToolComponent() const
	{
		return ScrapeToolComponent;
	}

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	virtual void PossessedBy(AController* NewController) override;

	virtual void OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode) override;

public:
	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

protected:

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USkeletalMeshComponent> FirstPersonMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMD_ScrapeToolComponent> ScrapeToolComponent;

	/**
	 * 시작 시 지급할 Ability 목록. 캐릭터 Blueprint의 클래스 기본값에서 구성한다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Ability")
	TArray<FMD_StartupAbilityEntry> StartupAbilities;

	/**
	 * Enhanced Input의 AbilityAction으로 활성화할 Scrape Ability.
	 * MD_GA_Scrape를 상속한 Blueprint Ability를 캐릭터 Blueprint에서 지정한다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Ability")
	TSubclassOf<UGameplayAbility> ScrapeAbilityClass;

protected:

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> PlayerMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UMD_InputConfig> InputConfig;

	UPROPERTY(EditAnywhere, Category = "Interact")
	float InteractTraceDistance = 200.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Water")
	EMDMovementState CurrentMovementState = EMDMovementState::Walking;

private:

	void HandleMoveInput(const FInputActionValue& Value);
	void HandleLookInput(const FInputActionValue& Value);
	void HandleInteractInput(const FInputActionValue& Value);
	void HandleAbilityInput(const FInputActionValue& Value);
	void HandleAbilityReleasedInput(const FInputActionValue& Value);
	void HandleSwapEquipmentInput(const FInputActionValue& Value);
	void HandleJumpOrSwimInput(const FInputActionValue& Value);
	void HandleJumpOrSwimReleasedInput(const FInputActionValue& Value);

	void RefreshAbilityActorInfo();
	void GrantStartupAbilities();

	FGameplayAbilitySpecHandle ActiveScrapeAbilityHandle;
};
