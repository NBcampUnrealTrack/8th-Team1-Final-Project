// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Audio/MD_GameplayAudioFeedback.h"
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
class UMD_ItemDefinition;
class UMaterialInterface;
class UPostProcessComponent;
struct FOnAttributeChangeData;
struct FPostProcessSettings;

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

	/** 산소를 즉시 만충하는 외부 호출용 함수. 서버 전용 */
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

	/** 수면 위에서 산소를 순차적으로 회복하는 지속 GE. Ongoing 차단 조건 = State.Submerged */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Oxygen")
	TSubclassOf<UGameplayEffect> OxygenRecoveryEffectClass;

	/** 산소 0 + 잠수 중 HP를 깎는 지속 GE. Ongoing 조건 = State.Submerged + State.Oxygen.Depleted */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Oxygen")
	TSubclassOf<UGameplayEffect> OxygenDepletedDamageEffectClass;

	// 산소 캔. Instant, Oxygen +90 (기획 08 §5.2)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Oxygen")
	TSubclassOf<UGameplayEffect> OxygenCanEffectClass;

	// 공통 Hotbar 수량 조회와 소비에 사용할 산소 캔 정의
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Oxygen")
	TObjectPtr<UMD_ItemDefinition> OxygenCanToolDefinition;

	// 산소 확장 탱크. Infinite, MaxOxygen ×1.4
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Oxygen")
	TSubclassOf<UGameplayEffect> OxygenTankEffectClass;

	// 기존 저장 데이터의 Hotbar 탱크 효과 복구에 사용할 공통 정의
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Oxygen")
	TObjectPtr<UMD_ItemDefinition> OxygenTankToolDefinition;

	/** 이 비율 이상 잠기면 머리까지 잠긴 것으로 본다. 1.0이면 수면 경계에서 판정이 떨린다 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Oxygen")
	float SubmergedImmersionThreshold = 0.95f;

	/** 호흡 상태 재판정 주기. 매 프레임 돌 필요가 없어 Tick 대신 타이머를 쓴다 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Oxygen")
	float EvaluationInterval = 0.2f;

	/** 켜면 상태 전이와 잠긴 비율을 로그로 찍는다. 물 볼륨 설정 검증용 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Oxygen")
	bool bLogBreathState = true;

	// 현재 월드의 PostProcess에서 찾아 제어할 산소 효과 머티리얼
	// Blueprint 기본값에서 지정할 산소 효과 머티리얼
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Oxygen|PostProcess")
	TSoftObjectPtr<UMaterialInterface> OxygenPostProcessMaterial;

	// Blueprint 기본값에서 지정할 효과 강도 Scalar 파라미터 이름
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Oxygen|PostProcess")
	FName OxygenOpacityParameterName = NAME_None;

	// 산소 효과가 시작되는 최대 산소량 대비 잔량 비율
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Oxygen|PostProcess", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float OxygenEffectStartRatio = 0.3f;

	// 로컬 제어 종료 시 원본 머티리얼 복원이 필요한 상태
	bool bOxygenPostProcessApplied = false;

	// 맵의 참조 여부와 관계없이 로컬 플레이어가 로드한 효과 머티리얼
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> LoadedOxygenPostProcessMaterial;

	// 등록된 산소 효과가 없는 월드에서 사용할 로컬 전용 PostProcess
	UPROPERTY(Transient)
	TObjectPtr<UPostProcessComponent> LocalOxygenPostProcess;

protected:
	// 현재 월드의 모든 등록된 PostProcess에 산소 효과 반영 또는 복원
	void UpdateOxygenPostProcess(bool bRestore = false);

	// 등록 항목별 갱신 후 산소 효과 적용 여부 반환
	bool UpdateOxygenPostProcessSettings(FPostProcessSettings& Settings, float Opacity, bool bRestore);
		
	private:
	/** 타이머가 부르는 본체. 서버에서만 돈다 */
	void EvaluateBreathState();

	/** 물에 잠긴 정도로 상태를 계산한다 */
	EMD_BreathState CalculateBreathState() const;

	/** ASC가 유효해진 첫 순간에 지속 GE와 어트리뷰트 구독을 한 번만 건다 */
	void EnsureInitialized(UAbilitySystemComponent* ASC);

	FActiveGameplayEffectHandle ApplyPassiveEffect(UAbilitySystemComponent* ASC, const TSubclassOf<UGameplayEffect>& EffectClass);
	
	/** 산소가 실제로 변경될 때 고갈 상태 태그를 갱신한다 */
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
	void RefreshSurvivalAudio();
	bool bAudioOxygenLow = false;
	bool bAudioOxygenEmpty = false;
	float PreviousAudioOxygen = -1.f;
	FMD_AudioHandle LowOxygenAudio;
	FMD_AudioHandle HeartbeatAudio;

	FTimerHandle EvaluationTimerHandle;

	TWeakObjectPtr<UAbilitySystemComponent> BoundASC;

	FDelegateHandle OxygenChangedHandle;

	// 장착 중인 탱크 GE. 해제할 때 이 핸들로 제거한다
	FActiveGameplayEffectHandle OxygenTankEffectHandle;
};
