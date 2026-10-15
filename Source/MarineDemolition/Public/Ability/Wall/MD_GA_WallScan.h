#pragma once

#include "CoreMinimal.h"
#include "Ability/Wall/MD_GA_WallBase.h"
#include "Engine/EngineTypes.h"
#include "Gameplay/Scanning/MD_ScanTypes.h"
#include "MD_GA_WallScan.generated.h"

class UMaterialParameterCollection;

/** One scan action; selection and the three-second display outlive this ability. */
UCLASS()
class MARINEDEMOLITION_API UMD_GA_WallScan : public UMD_GA_WallBase
{
	GENERATED_BODY()
public:
	UMD_GA_WallScan();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	// 쿨타임 중에는 활성 자체를 막는다. 예측 클라와 서버가 각자 자기 시계로 판정한다
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

protected:
	// 스캔 시작 후 다음 스캔까지 기다리는 시간. 0이면 쿨타임 없음
	UPROPERTY(EditDefaultsOnly, Category="Ability|스캔", meta=(ClampMin="0.0", Units="s"))
	float ScanCooldownSeconds = 3.f;

	UPROPERTY(EditDefaultsOnly, Category="Ability|스캔", meta=(ClampMin="0.0", Units="cm"))
	float ScanRadius = 1000.f;

	UPROPERTY(EditDefaultsOnly, Category="Ability|스캔", meta=(ClampMin="0.0", ClampMax="180.0", Units="deg"))
	float ScanConeHalfAngleDegrees = 45.f;

	UPROPERTY(EditDefaultsOnly, Category="Ability|스캔")
	TArray<TEnumAsByte<EObjectTypeQuery>> ScanObjectTypes;

	UPROPERTY(EditDefaultsOnly, Category="Ability")
	float ScanDuration = 3.f;

	UPROPERTY(EditDefaultsOnly, Category="Ability")
	TObjectPtr<UMaterialParameterCollection> ScanMPC;

	UPROPERTY(EditDefaultsOnly, Category="Ability|사운드")
	FGameplayTag ScanPulseSoundTag;

	UPROPERTY(EditDefaultsOnly, Category="Ability|사운드", meta=(ClampMin="0.0"))
	float ScanPulseFadeOutSeconds = 0.15f;

	UPROPERTY(EditDefaultsOnly, Category="Ability|스캔", meta=(ClampMin="0.1"))
	float ModeRequestTimeoutSeconds = 2.f;

private:
	bool FindScanTargets(const FGameplayAbilityActorInfo* ActorInfo, EMD_ScanMode Mode,
		TArray<AActor*>& OutTargets, FVector& OutOrigin, FVector& OutForward) const;
	bool PerformScan(EMD_ScanMode Mode, uint32 RequestId);
	void ReceiveMode(const FGameplayAbilityTargetDataHandle& Data, FGameplayTag ApplicationTag);
	void CancelPendingMode();
	void ClearModeWait();

	FDelegateHandle ModeDelegate;
	FDelegateHandle CancelDelegate;
	FTimerHandle ModeTimeout;
	FGameplayAbilitySpecHandle PendingHandle;
	FPredictionKey PendingKey;
	uint32 LocalRequestId = 0;
	bool bWaitingForMode = false;
	// 마지막으로 스캔을 시작한 월드 시간. 인스턴스별(클라·서버 각각)로 기록한다
	double LastScanStartedAt = -1.0e9;
};
