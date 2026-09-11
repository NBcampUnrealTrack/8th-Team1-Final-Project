// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Engine/NetSerialization.h"
#include "MD_GA_Scrape.generated.h"

class UMD_ScrapeToolComponent;

/**
 * The first focus-relative aim sent on the ability activation prediction key.
 *
 * This is intentionally separate from the lossy continuous aim stream. The server
 * does not commit the scrape ability until this exact payload has been validated
 * against the accepted focus session.
 */
USTRUCT()
struct MARINEDEMOLITION_API FGameplayAbilityTargetData_ScrapeFirstAim
	: public FGameplayAbilityTargetData
{
	GENERATED_BODY()

	UPROPERTY()
	uint64 FocusSessionId = 0;

	UPROPERTY()
	uint32 AimSequence = 0;

	UPROPERTY()
	double SampleTimeSeconds = 0.0;

	UPROPERTY()
	FVector_NetQuantizeNormal CameraRelativeAimDirection =
		FVector_NetQuantizeNormal(ForceInitToZero);

	virtual UScriptStruct* GetScriptStruct() const override
	{
		return StaticStruct();
	}

	virtual FString ToString() const override;

	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FGameplayAbilityTargetData_ScrapeFirstAim>
	: public TStructOpsTypeTraitsBase2<FGameplayAbilityTargetData_ScrapeFirstAim>
{
	enum
	{
		WithNetSerializer = true
	};
};

/**
 * Scrape 동작만 구현하는 Blueprint Ability용 C++ 베이스.
 * 인스턴싱, 네트워크 정책 등 데이터 옵션은 파생 Blueprint에서 구성한다.
 */
UCLASS(Abstract, Blueprintable)
class MARINEDEMOLITION_API UMD_GA_Scrape : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UMD_GA_Scrape();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	void FinishStainStroke();
	FTimerHandle StainStrokeTimer;
	bool bStainStroke = false;

	bool CommitAndStartScraping(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo& ActivationInfo,
		UMD_ScrapeToolComponent* ScrapeTool);

	void WaitForReplicatedFirstAim(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo& ActivationInfo);

	void HandleReplicatedFirstAim(
		const FGameplayAbilityTargetDataHandle& TargetData,
		FGameplayTag ApplicationTag);

	void HandleReplicatedFirstAimCancelled();
	void HandleFirstAimTimeout();
	void RemoveFirstAimDelegates(UAbilitySystemComponent* AbilitySystem);
	void EndCurrentAbilityAsCancelled();

	bool SendFirstAimTargetData(
		UAbilitySystemComponent* AbilitySystem,
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActivationInfo& ActivationInfo,
		uint64 FocusSessionId,
		uint32 AimSequence,
		double SampleTimeSeconds,
		const FVector& CameraRelativeAimDirection);

	/** Server wait bound. Timeout cancels only this LMB ability; Focus stays Ready. */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Scrape|Network",
		meta = (ClampMin = "0.05", Units = "s"))
	float StartAimTimeoutSeconds = 0.50f;

	FGameplayAbilitySpecHandle RegisteredAbilityHandle;
	FPredictionKey RegisteredActivationPredictionKey;
	uint64 RegisteredFocusSessionId = 0;
	FDelegateHandle FirstAimSetDelegateHandle;
	FDelegateHandle FirstAimCancelledDelegateHandle;
	FTimerHandle FirstAimTimeoutHandle;
	bool bWaitingForFirstAim = false;
};
