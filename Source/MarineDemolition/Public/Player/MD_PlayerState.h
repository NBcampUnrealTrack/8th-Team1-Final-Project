// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "MD_PlayerState.generated.h"

UENUM(BlueprintType)
enum class EMD_ReturnStatus : uint8
{
    Pending  UMETA(DisplayName = "Pending"),
    Returned UMETA(DisplayName = "Returned"),
    Failed   UMETA(DisplayName = "Failed")
};


UCLASS()
class MARINEDEMOLITION_API AMD_PlayerState : public APlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AMD_PlayerState();

    void SetReturnStatus(EMD_ReturnStatus NewStatus);

    UFUNCTION(BlueprintPure, Category = "Return")
    EMD_ReturnStatus GetReturnStatus() const;

protected:
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
    UPROPERTY(ReplicatedUsing = OnRep_ReturnStatus)
    EMD_ReturnStatus ReturnStatus =
        EMD_ReturnStatus::Pending;

    UFUNCTION()
    void OnRep_ReturnStatus();

    void HandleReturnStatusChanged();

protected:
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AbilitySystem")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

};
