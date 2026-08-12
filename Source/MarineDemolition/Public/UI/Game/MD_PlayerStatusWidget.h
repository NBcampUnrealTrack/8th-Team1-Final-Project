// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MD_PlayerStatusWidget.generated.h"

class UAbilitySystemComponent;
class UProgressBar;
struct FOnAttributeChangeData;
/**
 * 
 */
UCLASS()
class MARINEDEMOLITION_API UMD_PlayerStatusWidget : public UUserWidget
{
	GENERATED_BODY()
	
protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> HealthBar;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> Pb_Oxygen;

private:
	// PlayerState/ASC 준비 전이면 0.5초 후 재시도
	void TryBindToAbilitySystem();

	void HandleHealthChanged(const FOnAttributeChangeData& Data);

	void RefreshHealth();

	void HandleOxygenChanged(const FOnAttributeChangeData& Data);

	void RefreshOxygen();


	FTimerHandle BindRetryTimerHandle;

	// 언바인드용. 어트리뷰트 변경 델리게이트는 다이내믹이 아니라 핸들로 제거
	TWeakObjectPtr<UAbilitySystemComponent> BoundASC;
	FDelegateHandle HealthChangedHandle;
	FDelegateHandle MaxHealthChangedHandle;

	FDelegateHandle OxygenChangedHandle;     //산소포화도 
	FDelegateHandle MaxOxygenChangedHandle;
};
