// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MD_PlayerStatusWidget.generated.h"

class UAbilitySystemComponent;
class UProgressBar;
class UHorizontalBox;
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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> HealthBar;

	// 현재 공통 상태 UI의 산소 단계 위젯 컨테이너 바인딩
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> HB_OxygenContainer;

	// 직전 산소 단계와 비교하여 감소한 Child에만 Pop 애니메이션을 재생한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	int32 PreviousOxygenIndex = INDEX_NONE;

protected:
	// PlayerState/ASC 준비 전이면 0.5초 후 재시도
	void TryBindToAbilitySystem();

	void HandleHealthChanged(const FOnAttributeChangeData& Data);

	void RefreshHealth();

	void HandleOxygenChanged(const FOnAttributeChangeData& Data);

	void RefreshOxygen();

	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
	void UpdateOxygenWidget(int32 NewOxygenIndex);

	FTimerHandle BindRetryTimerHandle;

	// 언바인드용. 어트리뷰트 변경 델리게이트는 다이내믹이 아니라 핸들로 제거
	TWeakObjectPtr<UAbilitySystemComponent> BoundASC;
	FDelegateHandle HealthChangedHandle;
	FDelegateHandle MaxHealthChangedHandle;

	FDelegateHandle OxygenChangedHandle;     //산소포화도 
	FDelegateHandle MaxOxygenChangedHandle;
};
