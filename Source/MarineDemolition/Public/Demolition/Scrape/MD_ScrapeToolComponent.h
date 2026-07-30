// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Demolition/Scrape/MD_ScrapeTypes.h"
#include "MD_ScrapeToolComponent.generated.h"

class AMD_CuttableWall;
class USoundBase;
class UParticleSystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMD_OnScrapeApplied,
	AMD_CuttableWall*,
	Wall,
	FMD_ScrapeResult,
	Result);

/**
 * 플레이어 시점에서 연속 충돌점을 수집해 오염벽에 헤라 스트로크를 전달한다.
 *
 * 컴포넌트는 헤라를 누른 동안에만 Tick하며, 같은 벽의 연속된 두 충돌점만 연결한다.
 * 벽을 놓치거나 다른 벽으로 조준을 옮기면 이전 충돌점을 폐기하므로 벽 사이를 가로지르는
 * 비정상적으로 긴 스트로크가 생성되지 않는다.
 */
UCLASS(ClassGroup = (MD), meta = (BlueprintSpawnableComponent))
class MARINEDEMOLITION_API UMD_ScrapeToolComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMD_ScrapeToolComponent();

	/** 헤라 입력을 시작하고 현재 조준점을 첫 스트로크 기준점으로 기록한다. */
	UFUNCTION(BlueprintCallable, Category = "MD|Scrape")
	void StartScraping();

	/** 헤라 입력을 종료하고 이전 충돌점을 폐기한다. */
	UFUNCTION(BlueprintCallable, Category = "MD|Scrape")
	void StopScraping();

	UFUNCTION(BlueprintPure, Category = "MD|Scrape")
	bool IsScraping() const { return bIsScraping; }

	/**
	 * 유효한 스트로크가 벽 판정기에 전달될 때마다 발생한다.
	 * Blueprint는 ContactType을 이용해 임시 사운드나 이펙트를 추가할 수 있다.
	 */
	UPROPERTY(BlueprintAssignable, Category = "MD|Scrape")
	FMD_OnScrapeApplied OnScrapeApplied;

protected:
	virtual void BeginPlay() override;

	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/** 플레이어 시점에서 헤라가 닿을 수 있는 최대 거리. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape",
		meta = (ClampMin = "1.0", Units = "cm"))
	float TraceDistanceCm;

	/** 벽 충돌을 검사할 채널. 기본 벽과 동일한 Visibility 채널을 사용한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape")
	TEnumAsByte<ECollisionChannel> TraceChannel;

	/** 오염막을 실제로 제거한 스트로크에 재생할 임시 사운드. 미지정 가능. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Feedback")
	TObjectPtr<USoundBase> ContaminationScrapeSound;

	/** 이미 깨끗한 벽면을 긁었을 때 재생할 금속 마찰 사운드. 미지정 가능. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Feedback")
	TObjectPtr<USoundBase> BareSurfaceScrapeSound;

	/** 오염막 제거 접촉 위치에 생성할 임시 파티클. 미지정 가능. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Feedback")
	TObjectPtr<UParticleSystem> ContaminationScrapeEffect;

	/** 맨 벽면 접촉 위치에 생성할 임시 파티클. 미지정 가능. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Feedback")
	TObjectPtr<UParticleSystem> BareSurfaceScrapeEffect;

	/** 프레임마다 사운드가 겹치지 않도록 피드백 재생 간격을 제한한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Feedback",
		meta = (ClampMin = "0.0", Units = "s"))
	float MinimumFeedbackIntervalSeconds;

	/** 프로토타입에서 제거율과 접촉 종류를 화면에 표시한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Debug")
	bool bShowDebugFeedback;

private:
	/** 디버그 표시용 접촉 종류 문자열. 이 컴포넌트에서만 사용한다. */
	static const TCHAR* GetContactTypeLabel(EMD_ScrapeContactType ContactType);

	void ProcessScrapeTrace();
	bool TryGetCurrentWallHit(FHitResult& OutHit, AMD_CuttableWall*& OutWall) const;
	void ResetPreviousHit();
	void EmitFeedback(
		AMD_CuttableWall* Wall,
		const FMD_ScrapeResult& Result,
		const FVector& ContactLocation);

	bool bIsScraping;
	bool bHasPreviousHit;

	FHitResult PreviousHit;
	TWeakObjectPtr<AMD_CuttableWall> PreviousWall;

	double LastFeedbackWorldSeconds;
};
