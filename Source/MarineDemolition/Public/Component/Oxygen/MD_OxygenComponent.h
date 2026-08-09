// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayEffectTypes.h"        
#include "MD_OxygenComponent.generated.h"


/** 호흡 상태. 물 밖이면 숨을 쉬고, 잠기면 산소를 쓴다 */
UENUM(BlueprintType)
enum class EMD_BreathState : uint8
{
	Surface UMETA(DisplayName = "수면 위"), //산소 소모 멈춤, 완충
	Submerged UMETA(DisplayName = "잠수") //산소 소모 진행
};

class UAbilitySystemComponent;
class UGameplayEffect;
class UMD_CharacterAttributeSet;
struct FOnAttributeChangeData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMD_OnBreathStateChanged, EMD_BreathState, NewState);

/**
 * 산소 소모 조건 판정 담당.
 * 수치는 UMD_CharacterAttributeSet이 들고 GE가 깎는다.
 * 이 컴포넌트는 "지금 깎아야 하는가"만 판정해 GameplayTag를 켜고 끈다.
 */
UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class MARINEDEMOLITION_API UMD_OxygenComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	UMD_OxygenComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:	
	UFUNCTION(BlueprintPure, Category = "MD|Oxygen")
	EMD_BreathState GetBreathState() const { return BreathState; }

	/** 상태 전이 방송. HUD 경고·연출이 구독한다 */
	UPROPERTY(BlueprintAssignable, Category = "MD|Oxygen")
	FMD_OnBreathStateChanged OnBreathStateChanged;

	/** 산소를 즉시 만충. 수면 복귀와 산소 캔 양쪽에서 쓴다. 서버 전용 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Oxygen")
	void RefillOxygen();

	// 산소 캔(IT-O1) 1개 사용. 핫바에서 호출한다
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Oxygen")
	void UseOxygenCan();

	// 산소 확장 탱크(IT-O2) 장착/해제. 로드아웃에서 호출한다
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Oxygen")
	void SetOxygenTankEquipped(bool bEquipped);

protected:

	/** 잠수 중 산소를 깎는 지속 GE. Ongoing 조건 = State.Submerged */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Oxygen")
	TSubclassOf<UGameplayEffect> OxygenDrainEffectClass;

	/** 산소 0 + 잠수 중 HP를 깎는 지속 GE. Ongoing 조건 = State.Submerged + State.Oxygen.Depleted */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Oxygen")
	TSubclassOf<UGameplayEffect> OxygenDepletedDamageEffectClass;

	// 산소 캔. Instant, Oxygen +90 (기획 08 §5.2)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Oxygen")
	TSubclassOf<UGameplayEffect> OxygenCanEffectClass;

	// 산소 확장 탱크. Infinite, MaxOxygen ×1.4
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Oxygen")
	TSubclassOf<UGameplayEffect> OxygenTankEffectClass;

	/** 이 비율 이상 잠기면 머리까지 잠긴 것으로 본다. 1.0이면 수면 경계에서 판정이 떨린다 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Oxygen")
	float SubmergedImmersionThreshold = 0.95f;

	/** 호흡 상태 재판정 주기. 매 프레임 돌 필요가 없어 Tick 대신 타이머를 쓴다 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Oxygen")
	float EvaluationInterval = 0.2f;

	/** 켜면 상태 전이와 잠긴 비율을 로그로 찍는다. 물 볼륨 설정 검증용 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Oxygen")
	bool bLogBreathState = true;
		
	private:
	/** 타이머가 부르는 본체. 서버에서만 돈다 */
	void EvaluateBreathState();

	/** 물에 잠긴 정도로 상태를 계산한다 */
	EMD_BreathState CalculateBreathState() const;

	/** ASC가 유효해진 첫 순간에 지속 GE 두 개와 어트리뷰트 구독을 한 번만 건다 */
	void EnsureInitialized(UAbilitySystemComponent* ASC);

	FActiveGameplayEffectHandle ApplyPassiveEffect(UAbilitySystemComponent* ASC, const TSubclassOf<UGameplayEffect>& EffectClass);
	
	/** 산소가 0을 오갈 때만 태그를 토글한다 */
	void HandleOxygenChanged(const FOnAttributeChangeData& ChangeData);

	/** 서버·클라이언트가 같은 태그 상태를 갖도록 한 곳에서 처리한다 */
	void ApplyBreathStateTag();

	UFUNCTION()
	void OnRep_BreathState();

	UAbilitySystemComponent* GetAbilitySystemComponent() const;

	UMD_CharacterAttributeSet* GetAttributeSet() const;

	/** 서버가 판정하고 클라이언트는 복제로 받는다. 태그·HUD가 양쪽에서 같아진다 */
	UPROPERTY(ReplicatedUsing = OnRep_BreathState)
	EMD_BreathState BreathState = EMD_BreathState::Surface;

	bool bInitialized = false;

	/** 산소 0 여부. 태그를 매번 다시 붙이지 않으려고 직전 값을 기억한다 */
	bool bOxygenDepleted = false;

	FTimerHandle EvaluationTimerHandle;

	TWeakObjectPtr<UAbilitySystemComponent> BoundASC;

	FDelegateHandle OxygenChangedHandle;

	// 장착 중인 탱크 GE. 해제할 때 이 핸들로 제거한다
	FActiveGameplayEffectHandle OxygenTankEffectHandle;
};
