// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Ability/Wall/MD_GA_WallBase.h"
#include "Audio/MD_AudioSubsystem.h"
#include "Engine/EngineTypes.h"
#include "MD_GA_WallScan.generated.h"

class UMaterialParameterCollection;

// 조준한 격벽 스캔. 타입별 하이라이트 표시. 즉발 어빌리티
UCLASS()
class MARINEDEMOLITION_API UMD_GA_WallScan : public UMD_GA_WallBase
{
	GENERATED_BODY()

public:
	UMD_GA_WallScan();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	                             const FGameplayAbilityActivationInfo ActivationInfo,
	                             const FGameplayEventData* TriggerEventData) override;

protected:
	/** 카메라를 중심으로 1차 후보를 수집할 최대 거리. 100 UU = 1 m. */
	UPROPERTY(EditDefaultsOnly, Category = "Ability|스캔", meta = (ClampMin = "0.0", Units = "cm"))
	float ScanRadius = 1000.0f;

	/** 카메라 전방을 기준으로 한 원뿔의 반각. */
	UPROPERTY(EditDefaultsOnly, Category = "Ability|스캔",
		meta = (ClampMin = "0.0", ClampMax = "180.0", Units = "deg"))
	float ScanConeHalfAngleDegrees = 45.0f;

	/** 스캔 후보로 조회할 콜리전 오브젝트 타입. 기본값은 정적·동적·물리 오브젝트다. */
	UPROPERTY(EditDefaultsOnly, Category = "Ability|스캔")
	TArray<TEnumAsByte<EObjectTypeQuery>> ScanObjectTypes;

	// 하이라이트 지속시간
	UPROPERTY(EditDefaultsOnly, Category="Ability")
	float ScanDuration = 3.0f;

	// 스캔 스윕 연출 구동용 컬렉션 (MPC_Scan 에디터 할당)
	UPROPERTY(EditDefaultsOnly, Category="Ability")
	TObjectPtr<UMaterialParameterCollection> ScanMPC;

	/**
	 * 스캔 펄스음. 사운드 에셋이 아니라 태그다 — 어떤 소재가 나갈지는 DA_MD_Audio_Tool 이 정한다.
	 * 해당 항목이 루프라서 ScanDuration 뒤에 직접 끊는다 (하이라이트가 사라지는 시점과 같다).
	 */
	UPROPERTY(EditDefaultsOnly, Category="Ability|사운드")
	FGameplayTag ScanPulseSoundTag;

	/** 펄스 정지 페이드(초). 0 이면 즉시 끊긴다 */
	UPROPERTY(EditDefaultsOnly, Category="Ability|사운드", meta=(ClampMin="0.0"))
	float ScanPulseFadeOutSeconds = 0.15f;

private:
	/** 카메라 시점의 구형 오버랩 결과를 거리·원뿔·태그·인터페이스 순으로 필터링한다. */
	bool FindScanTargets(
		const FGameplayAbilityActorInfo* ActorInfo,
		TArray<AActor*>& OutTargets,
		FVector& OutScanOrigin,
		FVector& OutScanForward) const;

	/** 월드 위치 하나가 주어진 전방 원뿔 안에 있는지 판정하는 순수 함수. */
	static bool IsLocationInsideScanCone(
		const FVector& ViewLocation,
		const FVector& ViewForward,
		const FVector& TargetLocation,
		float Radius,
		float ConeHalfAngleDegrees);

	/** 펄스 루프를 아바타에 붙여 재생하고 ScanDuration 뒤에 정지 예약한다 */
	void PlayScanPulse();

	/** 재생 중인 펄스를 끊고 예약 타이머를 지운다. 타이머 콜백이면서 재진입 가드도 겸한다 */
	void StopScanPulse();

	/** 재생 중인 펄스 루프. 즉발 어빌리티가 끝난 뒤에도 살아 있어야 한다 */
	FMD_AudioHandle ScanPulseHandle;

	FTimerHandle ScanPulseStopTimer;
};
