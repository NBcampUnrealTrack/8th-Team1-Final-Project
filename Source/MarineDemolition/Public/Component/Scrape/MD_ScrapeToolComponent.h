// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Types/Scrape/MD_ScrapeTypes.h"
#include "MD_ScrapeToolComponent.generated.h"

class AMD_Barnacle;
class AMD_CuttableWall;
class UMD_ScrapeSurfaceComponent;
class USoundBase;
class UParticleSystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMD_OnScrapeApplied,
	AMD_CuttableWall*,
	Wall,
	FMD_ScrapeResult,
	Result);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMD_OnScrapeSurfaceApplied,
	UMD_ScrapeSurfaceComponent*,
	Surface,
	FMD_ScrapeResult,
	Result);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FMD_OnScrapeLoadChanged,
	float,
	ScrapeLoad);

/**
 * 플레이어 시점에서 연속 충돌점을 수집해 오염 표면에 헤라 스트로크를 전달한다.
 *
 * 컴포넌트는 헤라를 누른 동안에만 Tick하며, 같은 표면 컴포넌트의 연속된 두 충돌점만
 * 연결한다. 표면을 놓치거나 다른 표면으로 조준을 옮기면 이전 충돌점을 폐기하므로
 * 서로 다른 물체 사이를 가로지르는 비정상적으로 긴 스트로크가 생성되지 않는다.
 */
UCLASS(ClassGroup = (MD), meta = (BlueprintSpawnableComponent))
class MARINEDEMOLITION_API UMD_ScrapeToolComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMD_ScrapeToolComponent();
	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 헤라 입력을 시작하고 현재 조준점을 첫 스트로크 기준점으로 기록한다. */
	UFUNCTION(BlueprintCallable, Category = "MD|Scrape")
	void StartScraping();

	/** 헤라 입력을 종료하고 이전 충돌점을 폐기한다. */
	UFUNCTION(BlueprintCallable, Category = "MD|Scrape")
	void StopScraping();

	UFUNCTION(BlueprintPure, Category = "MD|Scrape")
	bool IsScraping() const { return bIsScraping; }

	/**
	 * 이번 프레임 헤라가 닿은 표면의 월드 위치를 반환한다.
	 *
	 * 긁고 있지 않거나 표면을 놓친 프레임에는 false를 반환하며 출력값을
	 * 건드리지 않는다. 로컬 플레이어는 자신의 트레이스 결과를 사용하고,
	 * 시뮬레이티드 프록시는 서버에서 복제된 위치를 사용한다.
	 */
	UFUNCTION(BlueprintPure, Category = "MD|Scrape")
	bool GetCurrentScrapeContact(FVector& OutWorldLocation) const;

	/**
	 * 잠긴 스트로크와 다른 방향의 시선 입력을 제거한다.
	 * 헤라는 수평 또는 수직으로만 긁으므로 이 제한은 끌 수 없다.
	 */
	FVector2D ConstrainLookInput(const FVector2D& LookInput) const;

	/** 현재 장착한 헤라의 판정 성능을 반환한다. */
	UFUNCTION(BlueprintPure, Category = "MD|Scrape|Tool")
	FMD_ScrapeToolSpec GetScrapeToolSpec() const { return ScrapeToolSpec; }

	static constexpr float MaximumScrapeLoad = 100.0f;
	static constexpr float ScrapeLoadStage2Threshold = 34.0f;
	static constexpr float ScrapeLoadStage3Threshold = 67.0f;

	/** 적재량을 0/1/2/3단계 중 하나로 즉시 분류한다. */
	static EMD_ScrapeLoadStage CalculateScrapeLoadStage(float Load);

	/** CleanRatioDelta 0.01(1%)를 기획 수치에 따라 ScrapeLoad 증가량으로 바꾼다. */
	static float CalculateScrapeLoadGainFromCleanRatioDelta(
		float CleanRatioDelta,
		float LoadPerRemovedPercent);

	/** 현재 헤라 인스턴스의 적재량을 0~100으로 반환한다. */
	UFUNCTION(BlueprintPure, Category = "MD|Scrape|Tool|Load")
	float GetScrapeLoad() const
	{
		return FMath::IsFinite(ScrapeLoad)
			? FMath::Clamp(ScrapeLoad, 0.0f, MaximumScrapeLoad)
			: 0.0f;
	}

	/** 현재 적재량에 대응하는 날 오염 메시 단계를 반환한다. */
	UFUNCTION(BlueprintPure, Category = "MD|Scrape|Tool|Load")
	EMD_ScrapeLoadStage GetScrapeLoadStage() const
	{
		return CalculateScrapeLoadStage(GetScrapeLoad());
	}

	/** 3단계 헤라의 직사각형 도장 한 번이 더하는 최대 오염량을 0~1로 반환한다. */
	UFUNCTION(BlueprintPure, Category = "MD|Scrape|Tool|Load")
	float GetSaturatedContaminationStampRatio() const
	{
		return FMath::IsFinite(SaturatedContaminationStampRatio)
			? FMath::Clamp(SaturatedContaminationStampRatio, 0.0f, 1.0f)
			: 0.0f;
	}

	/** 3단계 포화 헤라의 제거력을 일반 헤라 대비 배율로 반환한다. */
	UFUNCTION(BlueprintPure, Category = "MD|Scrape|Tool|Load")
	float GetSaturatedRemovalMultiplier() const
	{
		return FMath::IsFinite(SaturatedRemovalMultiplier)
			? FMath::Clamp(SaturatedRemovalMultiplier, 0.01f, 1.0f)
			: 0.05f;
	}

	/** 3단계 헤라의 무작위 도장 후보 간격을 cm로 반환한다. */
	UFUNCTION(BlueprintPure, Category = "MD|Scrape|Tool|Load")
	float GetSaturatedContaminationStampSpacingCm() const
	{
		return FMath::IsFinite(SaturatedContaminationStampSpacingCm)
			? FMath::Max(0.1f, SaturatedContaminationStampSpacingCm)
			: 0.1f;
	}

	/** 3단계 헤라가 남기는 직사각형 오염 도장의 진행 방향 깊이를 cm로 반환한다. */
	UFUNCTION(BlueprintPure, Category = "MD|Scrape|Tool|Load")
	float GetSaturatedContaminationStampDepthCm() const
	{
		return FMath::IsFinite(SaturatedContaminationStampDepthCm)
			? FMath::Max(0.1f, SaturatedContaminationStampDepthCm)
			: 0.1f;
	}

	/** 3단계 헤라의 각 도장 후보가 실제 자국이 될 확률을 0~1로 반환한다. */
	UFUNCTION(BlueprintPure, Category = "MD|Scrape|Tool|Load")
	float GetSaturatedContaminationStampCoverage() const
	{
		return FMath::IsFinite(SaturatedContaminationStampCoverage)
			? FMath::Clamp(SaturatedContaminationStampCoverage, 0.0f, 1.0f)
			: 0.0f;
	}

	/**
	 * 조준 보정으로 이번 프레임에 적용할 컨트롤 회전을 계산한다.
	 *
	 * 지금 조준하고 있는 방향과 목표 방향 사이의 각도 오차에서, 매 프레임
	 * 일정 비율만 없앤다. 오차를 한 번에 전부 없애거나 임계값 미만에서 아예
	 * 보정을 멈추면, 프레임마다 "보정함 / 안 함"이 번갈아 나타나면서 시선이
	 * 빨라졌다 느려졌다 하는 떨림이 생긴다. 비율 보정은 그 경계가 없다.
	 *
	 * 보정량은 항상 오차보다 작으므로 목표를 지나치지 않는다.
	 * 오차가 MaximumErrorDegrees보다 크면 시선이 표면을 너무 스치는 각도라
	 * 조금만 돌려도 조준점이 크게 튀므로 그 프레임은 보정하지 않는다.
	 */
	static FRotator CalculateAimCorrectionRotation(
		const FRotator& CurrentControlRotation,
		const FVector& ViewToCurrentAim,
		const FVector& ViewToTarget,
		float CorrectionRatio,
		float MaximumStepDegrees,
		float MaximumErrorDegrees);

	/**
	 * 업그레이드 시스템이 새 헤라 성능을 적용한다.
	 * 잘못된 값은 거부하며 기존 성능을 유지한다.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Scrape|Tool")
	bool SetScrapeToolSpec(const FMD_ScrapeToolSpec& NewToolSpec);

	/** 제거 성능은 유지하고 사각 날의 폭 또는 원형 판정의 지름만 교체한다. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Scrape|Tool")
	bool SetScrapeBladeWidthCm(float NewBladeWidthCm);

	/** 서버에서 ScrapeLoad를 직접 설정하고 0~100으로 보정한다. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
		Category = "MD|Scrape|Tool|Load")
	bool SetScrapeLoad(float NewLoad);

	/** 서버에서 양수 적재량을 더하고, 100 상한까지 실제로 들어간 양을 반환한다. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
		Category = "MD|Scrape|Tool|Load")
	float AddScrapeLoad(float RequestedAmount);

	/** 서버 변경 또는 복제 수신으로 ScrapeLoad가 실제로 바뀔 때 발생한다. */
	UPROPERTY(BlueprintAssignable, Category = "MD|Scrape|Tool|Load")
	FMD_OnScrapeLoadChanged OnScrapeLoadChanged;

	/** 유효한 스트로크가 어느 오염 표면에든 전달될 때마다 발생한다. */
	UPROPERTY(BlueprintAssignable, Category = "MD|Scrape")
	FMD_OnScrapeSurfaceApplied OnScrapeSurfaceApplied;

	/**
	 * 기존 철거벽 Blueprint를 위한 하위 호환 이벤트.
	 * 일반 벽·바닥·설비 표면에서는 OnScrapeSurfaceApplied만 발생한다.
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

	/**
	 * 현재 장착한 헤라의 판정 성능.
	 * 기본 헤라는 12cm × 12cm 사각형이며 업그레이드 시 형태와 크기를 교체할 수 있다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_ScrapeToolSpec,
		Category = "MD|Scrape|Tool")
	FMD_ScrapeToolSpec ScrapeToolSpec;

	/** 헤라 인스턴스별 적재량. 자연 감소하지 않으며 서버만 변경한다. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly,
		ReplicatedUsing = OnRep_ScrapeLoad,
		Category = "MD|Scrape|Tool|Load",
		meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float ScrapeLoad;

	/** 오염 마스크 실제 제거율 1%당 적재량. 정본 기본값은 +0.6이다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "MD|Scrape|Tool|Load",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ScrapeLoadPerRemovedPercent;

	/** 따개비 한 개를 완전히 제거했을 때 적재량. 정본 기본값은 +8이다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "MD|Scrape|Tool|Load",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ScrapeLoadPerRemovedBarnacle;

	/** 선택된 직사각형 도장 한 번이 셀에 더하는 최대 오염량. 덧칠하면 1까지 누적된다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "MD|Scrape|Tool|Load",
		meta = (ClampMin = "0.0", ClampMax = "1.0",
			UIMin = "0.0", UIMax = "1.0"))
	float SaturatedContaminationStampRatio;

	/** 3단계 포화 헤라의 제거력 배율. 낮을수록 기존 오염과 전이 페인트를 덜 긁어낸다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "MD|Scrape|Tool|Load",
		meta = (ClampMin = "0.01", ClampMax = "1.0",
			UIMin = "0.01", UIMax = "1.0"))
	float SaturatedRemovalMultiplier;

	/** 경로의 각 무작위 도장 후보가 실제 오염 자국이 될 확률. 나머지는 빈 간격으로 남긴다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "MD|Scrape|Tool|Load",
		meta = (ClampMin = "0.0", ClampMax = "1.0",
			UIMin = "0.0", UIMax = "1.0"))
	float SaturatedContaminationStampCoverage;

	/** 오염 도장의 진행 방향 깊이. 폭은 장착한 헤라 폭을 그대로 사용한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "MD|Scrape|Tool|Load",
		meta = (ClampMin = "0.1", UIMin = "1.0", Units = "cm"))
	float SaturatedContaminationStampDepthCm;

	/** 경로를 따라 무작위 도장 후보를 배치하는 기준 간격. 각 후보는 약간 위치가 흔들린다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "MD|Scrape|Tool|Load",
		meta = (ClampMin = "0.1", UIMin = "1.0", Units = "cm"))
	float SaturatedContaminationStampSpacingCm;

	/** 이 거리만큼 움직이면 수평·수직 중 한 축을 잠근다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Motion",
		meta = (ClampMin = "0.1", Units = "cm"))
	float DirectionLockThresholdCm;

	/** 잠긴 축에서 허용하는 작은 옆 흔들림이다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Motion",
		meta = (ClampMin = "0.0", Units = "cm"))
	float MaximumPerpendicularDriftCm;

	/** 이 거리보다 옆으로 벗어나면 연결 없이 새 스트로크를 시작한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Motion",
		meta = (ClampMin = "0.1", Units = "cm"))
	float BreakStrokePerpendicularDistanceCm;

	/**
	 * 조준 보정을 사용할지 여부.
	 *
	 * 끄면 시선은 플레이어 입력만 따라간다. 긁는 자국은 여전히 잠긴 축을 지키므로
	 * 조준점과 자국이 최대 MaximumPerpendicularDriftCm만큼 어긋날 뿐이다.
	 * 떨림 원인이 보정 루프인지 아닌지 가려낼 때 이 값을 꺼서 확인한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Motion")
	bool bUseAimCorrection;

	/**
	 * 조준 오차가 1초에 줄어드는 비율을 정하는 수렴 속도.
	 * 값이 클수록 조준점이 자국을 빨리 따라가고, 작을수록 시선 입력을 덜 방해한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Motion",
		meta = (EditCondition = "bUseAimCorrection", ClampMin = "0.0"))
	float AimCorrectionInterpolationSpeed;

	/** 조준 보정이 1초에 돌릴 수 있는 최대 각도. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Motion",
		meta = (EditCondition = "bUseAimCorrection", ClampMin = "0.0", Units = "deg/s"))
	float AimCorrectionSpeedDegreesPerSecond;

	/**
	 * 이보다 큰 조준 오차는 보정하지 않는다.
	 * 시선이 벽을 스치듯 보는 각도에서는 작은 보정도 조준점을 크게 움직이므로
	 * 무리하게 따라가지 않고 그 프레임을 넘긴다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Motion",
		meta = (EditCondition = "bUseAimCorrection", ClampMin = "0.0", Units = "deg"))
	float AimCorrectionMaximumErrorDegrees;

	/**
	 * 조준 보정 목표가 다른 표면에 가려져 있는지 검사할 때 쓰는 여유 거리.
	 * 목표 표면 자신을 가림으로 오판하지 않도록 이 거리만큼 앞까지만 검사한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Motion",
		meta = (ClampMin = "0.1", Units = "cm"))
	float AimCorrectionVisibilityToleranceCm;

	/** 표면 충돌을 검사할 채널. 기본 환경 메시와 같은 Visibility 채널을 사용한다. */
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
	friend class FMD_ScrapeToolBarnacleOcclusionTest;
	friend class FMD_ScrapeLoadTest;

	/** 디버그 표시용 접촉 종류 문자열. 이 컴포넌트에서만 사용한다. */
	static const TCHAR* GetContactTypeLabel(EMD_ScrapeContactType ContactType);

	void SetCurrentScrapeContact(const FVector& WorldLocation);
	void ClearCurrentScrapeContact();
	void ProcessScrapeTrace(float DeltaTime);
	void DispatchAuthoritativeScrapeResult(
		UMD_ScrapeSurfaceComponent* Surface,
		const FMD_ScrapeResult& Result,
		const FVector& ContactLocation);

	UFUNCTION(Client, Unreliable)
	void ClientReceiveScrapeResult(
		UMD_ScrapeSurfaceComponent* Surface,
		const FMD_ScrapeResult& Result,
		FVector_NetQuantize10 ContactLocation);

	UFUNCTION()
	void OnRep_ScrapeToolSpec();

	UFUNCTION()
	void OnRep_ScrapeLoad();

	void NotifyScrapeLoadChanged();

	bool TryGetCurrentSurfaceHit(
		FHitResult& OutHit,
		UMD_ScrapeSurfaceComponent*& OutSurface,
		AMD_Barnacle*& OutOccludingBarnacle,
		FHitResult& OutBarnacleHit) const;
	bool TryTraceScrapeTarget(
		const FVector& TraceStart,
		const FVector& TraceEnd,
		const AActor* IgnoredActor,
		FHitResult& OutHit,
		UMD_ScrapeSurfaceComponent*& OutSurface,
		AMD_Barnacle*& OutOccludingBarnacle,
		FHitResult& OutBarnacleHit) const;
	void BeginStrokeTracking(
		UMD_ScrapeSurfaceComponent* Surface,
		const FVector2D& SurfacePositionCm,
		EMD_ScrapeSurfaceFace SurfaceFace);
	bool TryConstrainStrokePosition(
		const FVector2D& RawSurfacePositionCm,
		FVector2D& OutConstrainedPositionCm);
	void KeepAimOnScrapePosition(
		const FVector& ViewLocation,
		const FVector& CurrentAimWorldPosition,
		const FVector& TargetWorldPosition,
		float DeltaTime) const;

	/** 보정 목표가 다른 표면에 가려지지 않고 시점에서 바로 보이는지 확인한다. */
	bool IsAimCorrectionTargetVisible(
		const FVector& ViewLocation,
		const FVector& WorldPosition,
		const AActor* IgnoredActor = nullptr) const;
	void FlushPendingContaminationPaintTail();
	void ResetStrokeTracking();
	void EmitFeedback(
		UMD_ScrapeSurfaceComponent* Surface,
		const FMD_ScrapeResult& Result,
		const FVector& ContactLocation);

	/**
	 * 따개비를 직접 겨냥해 긁을 때의 타격 경로. 벽과 완전히 다른 계통이다.
	 *
	 * 벽은 표면형이라 2D 투영·면 판정·축 고정·조준 보정이 필요하지만, 따개비는
	 * 물체형이라 내구도 숫자 하나뿐이다. 두 경로를 한 인터페이스로 묶으면 따개비가
	 * 쓰지도 않는 기계장치를 끌고 다니게 된다.
	 *
	 * 따개비 메시를 직접 맞히면 뒤 표면의 종류와 무관하게 이 경로를 탄다.
	 */
	void ProcessDirectBarnacleContact(
		AMD_Barnacle* Barnacle,
		const FHitResult& Hit,
		float DeltaTime);

	/**
	 * 헤라 속도를 따개비 타격으로 바꿔 전달한다. 직접 접촉과 경로 차단이 공유한다.
	 *
	 * 한 번 때리면 스윙이 끝나 속도가 충분히 떨어질 때까지 다시 때리지 않는다.
	 * 이것이 없으면 헤라를 대고 미는 동안 매 프레임 타격이 들어가 한순간에 떨어진다.
	 */
	void DeliverBarnacleImpact(
		AMD_Barnacle* Barnacle,
		float ImpactSpeedCmPerSecond,
		const FVector& ImpactWorldDirection);

	/**
	 * 지금 상대하고 있는 따개비를 바꾼다.
	 *
	 * 대상이 바뀌면 타격 준비 상태와 기준점을 함께 초기화한다. 안 그러면 따개비 A에서
	 * 벽을 거쳐 따개비 B로 옮길 때 그 사이 이동이 B에 들어간다.
	 */
	void SetTrackedBarnacle(AMD_Barnacle* Barnacle);

	/** 프레임 간 이동 거리를 속도로 바꾼다. DeltaTime이 0인 첫 프레임은 0을 반환한다 */
	static float CalculateImpactSpeedCmPerSecond(float TravelCentimeters, float DeltaTime);

	bool bIsScraping;
	bool bHasPreviousSurfacePosition;
	bool bHasLockedStrokeAxis;
	int32 SaturatedContaminationStampPatternSeed;

	/** 마지막으로 표면에 닿은 월드 위치. 접촉이 끊기면 무효로 표시한다. */
	UPROPERTY(Replicated)
	FVector_NetQuantize10 LastContactWorldLocation = FVector::ZeroVector;

	/** 위 위치가 이번 프레임 기준으로 유효한지 여부. */
	UPROPERTY(Replicated)
	bool bHasLastContact = false;

	FVector2D PreviousSurfacePositionCm;
	EMD_ScrapeSurfaceFace PreviousSurfaceFace;
	FVector2D StrokeAnchorPositionCm;
	FVector2D FurthestSurfacePositionCm;
	FVector2D LockedStrokeAxis;
	TWeakObjectPtr<UMD_ScrapeSurfaceComponent> PreviousSurface;

	/** 지금 때리고 있는 따개비. 직접 겨냥한 것이든 경로를 막은 것이든 여기 담긴다 */
	TWeakObjectPtr<AMD_Barnacle> TrackedBarnacle;

	/** 직접 접촉에서 속도를 재는 기준점 */
	FVector PreviousBarnacleHitLocation = FVector::ZeroVector;
	bool bHasPreviousBarnacleHitLocation = false;

	/** 다음 타격을 넣을 수 있는 상태인지. 한 스윙에 한 번만 때리게 하는 빗장이다 */
	bool bBarnacleImpactArmed = true;

	double LastFeedbackWorldSeconds;
};
