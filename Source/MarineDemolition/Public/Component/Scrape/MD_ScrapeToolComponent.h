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
struct FMinimalViewInfo;
enum EAspectRatioAxisConstraint : int;

/** Component 내부에서만 사용하는 자유 2D 경로의 한 적용 선분. */
struct FMD_ScrapeToolPathSegment
{
	FVector2D StartPositionCm = FVector2D::ZeroVector;
	FVector2D EndPositionCm = FVector2D::ZeroVector;
	FVector2D StartBladeDepthAxis = FVector2D::ZeroVector;
	FVector2D EndBladeDepthAxis = FVector2D::ZeroVector;
	float TravelDistanceCm = 0.0f;
	/** 이 segment를 적용하기 직전, 이전 generation tail을 flush할 경계. */
	bool bStartsNewSubStroke = false;
};

/** 날 방향만 프레임 분할과 무관하게 계산하기 위한 고정 arc-length 표본. */
struct FMD_ScrapeToolDirectionSample
{
	FVector2D PositionCm = FVector2D::ZeroVector;
	double ArcLengthCm = 0.0;
};

/** pending candidate가 다른 표면·spec으로 바뀌 후에도 생성 시점 정보를 보존한다. */
struct FMD_PendingScrapeContaminationDab
{
	FMD_ScrapeContaminationDab Dab;
	TWeakObjectPtr<UMD_ScrapeSurfaceComponent> Surface;
	FMD_ScrapeToolSpec ToolSpec;
	float StampAmount = 0.0f;
	float StampDepthCm = 0.0f;
};

enum class EMD_ScrapeReacquireDecision : uint8
{
	KeepWaiting,
	ContinueBridge,
	StartNewStroke,
	TerminateFocus
};

struct FMD_ScrapeReacquireEvaluation
{
	EMD_ScrapeReacquireDecision Decision =
		EMD_ScrapeReacquireDecision::KeepWaiting;
	bool bIsReconnectCandidate = false;
	bool bCanBridge = false;
};

/** 첫 Reliable aim보다 먼저 검증된 continuous 표본의 bounded replay 항목. */
struct FMD_BufferedAuthoritativeScrapeAim
{
	FMD_ScrapeAimNetSample Sample;
	FMD_ScrapeAimQueryResult ValidatedAim;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMD_OnScrapeApplied, AMD_CuttableWall*, Wall, FMD_ScrapeResult, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMD_OnScrapeSurfaceApplied, UMD_ScrapeSurfaceComponent*, Surface, FMD_ScrapeResult, Result);

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

	/** 중앙 ray가 유효할 때만 우클릭 Focus 진입을 시작한다. */
	UFUNCTION(BlueprintCallable, Category = "MD|Scrape|Focus")
	bool TryBeginScrapeFocus();

	/** Focus와 진행 중인 긁기를 함께 정리한다. 여러 번 호출해도 안전하다. */
	UFUNCTION(BlueprintCallable, Category = "MD|Scrape|Focus")
	void EndScrapeFocus();

	UFUNCTION(BlueprintPure, Category = "MD|Scrape|Focus")
	bool IsScrapeFocusActive() const
	{
		return ScrapeFocusState != EMD_ScrapeFocusState::Free;
	}

	/** 진입 승인이 끝난 Ready/Scraping 상태를 반환한다. */
	UFUNCTION(BlueprintPure, Category = "MD|Scrape|Focus")
	bool IsScrapeFocusReady() const
	{
		return ScrapeFocusState == EMD_ScrapeFocusState::FocusedReady
			|| ScrapeFocusState == EMD_ScrapeFocusState::FocusedScraping;
	}

	UFUNCTION(BlueprintPure, Category = "MD|Scrape|Focus")
	bool IsScrapeFocusAimValid() const
	{
		return IsScrapeFocusActive() && CurrentScrapeAim.IsActionable();
	}

	/** 현재 고정 View ray를 동기 query해 cache하고 즉시 유효성을 반환한다. */
	bool RefreshScrapeFocusAim();

	UFUNCTION(BlueprintPure, Category = "MD|Scrape|Focus")
	EMD_ScrapeFocusState GetScrapeFocusState() const
	{
		return ScrapeFocusState;
	}

	/** 성공한 Focus 진입마다 증가하는 로컬 세대 번호. TASK 7의 session id 정본이 된다. */
	UFUNCTION(BlueprintPure, Category = "MD|Scrape|Focus")
	int64 GetScrapeFocusGeneration() const
	{
		return static_cast<int64>(ScrapeFocusGeneration);
	}

	/** 현재 클라이언트/서버가 합의한 Focus session. 0은 승인 session 없음. */
	uint64 GetActiveScrapeFocusSessionId() const
	{
		return ActiveScrapeFocusSessionId;
	}

	/** GAS 첫 TargetData에 넣을 fresh aim과 단조 sequence를 예약한다. */
	bool BuildScrapeAbilityFirstAim(
		uint64& OutSessionId,
		uint32& OutAimSequence,
		double& OutSampleTimeSeconds,
		FVector& OutCameraRelativeAimDirection);

	/** Reliable 첫 TargetData를 continuous aim과 같은 서버 검증 core로 승인한다. */
	bool AuthorizeScrapeAbilityFirstAim(
		uint64 FocusSessionId,
		uint32 AimSequence,
		double SampleTimeSeconds,
		const FVector& CameraRelativeAimDirection);

	bool CanActivateAuthoritativeScrape(uint64 FocusSessionId) const;
	bool IsAuthoritativeFirstScrapeAimApproved(uint64 FocusSessionId) const;
	void ClearScrapeAbilityFirstAimApproval(uint64 FocusSessionId);

	/** Reject malformed first-aim TargetData only for the matching active server session. */
	void RejectScrapeAbilityFirstAimProtocol(uint64 FocusSessionId);

	/** Focus 중 Look 입력을 카메라 대신 해상도 독립 가상 포인터에 누적한다. */
	bool ConsumeScrapeFocusLookInput(const FVector2D& LookInput);

	/** Character::CalcCamera가 Focus 중 저장된 회전/FOV를 표시 View에 적용할 때 사용한다. */
	bool ApplyScrapeFocusCameraView(FMinimalViewInfo& InOutViewInfo) const;

	UFUNCTION(BlueprintPure, Category = "MD|Scrape|Focus")
	FVector2D GetScrapeFocusPointerNormalized() const
	{
		return ScrapeFocusPointerNormalized;
	}

	/** Mouse2D 한 단위를 화면 폭 기준 이동량으로 바꾸는 Focus 감도. */
	UFUNCTION(BlueprintPure, Category = "MD|Scrape|Focus")
	float GetScrapeFocusPointerSensitivity() const
	{
		return ScrapeFocusPointerSensitivity;
	}

	/** 로컬 Focus 포인터 감도를 런타임에 조절한다. */
	UFUNCTION(BlueprintCallable, Category = "MD|Scrape|Focus")
	void SetScrapeFocusPointerSensitivity(float NewSensitivity);

	/** 진입·조준·서버 검증에서 함께 사용할 현재 헤라 도달 거리. */
	UFUNCTION(BlueprintPure, Category = "MD|Scrape|Tool")
	float GetEffectiveScrapeReachCm() const;

	/** 임의의 월드 ray를 현재 도구의 도달 거리로 조회한다. */
	UFUNCTION(BlueprintCallable, Category = "MD|Scrape|Aim")
	FMD_ScrapeAimQueryResult QueryScrapeAim(
		const FVector& TraceStart,
		const FVector& AimDirection) const;

	/** 현재 플레이어 View 중앙 ray를 공통 조회 계약으로 반환한다. */
	UFUNCTION(BlueprintCallable, Category = "MD|Scrape|Aim")
	FMD_ScrapeAimQueryResult QueryCurrentScrapeAim() const;

	/**
	 * 이번 프레임 헤라가 닿은 표면의 월드 위치를 반환한다.
	 *
	 * 긁고 있지 않거나 표면을 놓친 프레임에는 false를 반환하며 출력값을
	 * 건드리지 않는다. 로컬 플레이어는 자신의 트레이스 결과를 사용하고,
	 * 시뮬레이티드 프록시는 서버에서 복제된 위치를 사용한다.
	 */
	UFUNCTION(BlueprintPure, Category = "MD|Scrape")
	bool GetCurrentScrapeContact(FVector& OutWorldLocation) const;

	/** 서버가 확정한 접촉점과 blade 진행축을 함께 반환한다. 방향 미확정이면 false. */
	UFUNCTION(BlueprintPure, Category = "MD|Scrape")
	bool GetCurrentScrapeContactPose(
		FVector& OutWorldLocation,
		FVector& OutBladeWorldDirection) const;

	/**
	 * legacy non-Focus 호출에서 잠긴 임의 축과 다른 방향의 시선 입력을 제거한다.
	 * Focus 중 입력은 ConsumeScrapeFocusLookInput이 포인터 공간에서 직접 처리한다.
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
	UPROPERTY(meta = (DeprecatedProperty,
		DeprecationMessage = "Use FMD_ScrapeToolSpec::ReachCm via GetEffectiveScrapeReachCm."))
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

	/** 표면 시작점에서 이 거리만큼 움직이면 최초 이동 방향 축을 잠근다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Motion",
		meta = (ClampMin = "0.1", Units = "cm"))
	float DirectionLockThresholdCm;

	/** 날 방향 계산 전용 고정 arc-length 재표본 간격. 실제 판정 경로는 원본 점을 유지한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Motion|Path",
		meta = (ClampMin = "0.01", Units = "cm"))
	float PathDirectionResampleSpacingCm;

	/** 안정 날 방향 후보를 계산할 최근 경로 길이. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Motion|Path",
		meta = (ClampMin = "0.1", Units = "cm"))
	float BladeDirectionWindowCm;

	/** 경로 1cm당 안정 날 축이 회전할 수 있는 최대 각도. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Motion|Path",
		meta = (ClampMin = "0.0"))
	float MaximumBladeAxisTurnDegreesPerCm;

	/** smoothing 전에 새 sub-stroke로 나누는 180도 동치 방향 변화각. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Motion|Path",
		meta = (ClampMin = "0.0", ClampMax = "90.0", Units = "deg"))
	float LargeCornerSplitDegrees;

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

	/** Mouse2D 한 단위를 화면 폭 기준 포인터 이동량으로 바꾸는 감도. 값이 클수록 빠르다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Focus",
		meta = (ClampMin = "0.0001", ClampMax = "0.1",
			UIMin = "0.0005", UIMax = "0.01"))
	float ScrapeFocusPointerSensitivity;

	/**
	 * LMB 시작점에서 화면 폭 기준으로 이 거리만큼 움직이면 임의 각도의 포인터 축을 고정한다.
	 * 고정 뒤에는 같은 직선의 앞/뒤 성분만 허용하며 LMB 종료 때 다시 해제한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Focus",
		meta = (ClampMin = "0.001", ClampMax = "0.25",
			UIMin = "0.005", UIMax = "0.05"))
	float ScrapeFocusPointerDirectionLockThresholdNormalized;

	/** 가상 포인터가 화면 가장자리에서 유지할 normalized 안전 여백. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Focus",
		meta = (ClampMin = "0.0", ClampMax = "0.49"))
	float ScrapeFocusPointerSafeMargin;

	/**
	 * Focus 시작 뒤 viewport 종횡비가 이 절대 오차를 넘게 바뀌면 session을 종료한다.
	 * 같은 비율의 해상도/DPI 변경은 유지하되 서버의 시작 시점 cone과 다른 ray를 보내지 않는다.
	 * 4:3 이상에서 0.0045는 모든 FOV의 최악 조건에서도 방향 차이를 0.1도 안에 둔다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Focus",
		meta = (ClampMin = "0.0"))
	float ScrapeFocusAspectRatioAbsoluteTolerance;

	/** Focus 시작 원점에서 이 거리보다 큰 Pawn/카메라 이동은 외부 이동으로 보고 Focus를 종료한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Focus",
		meta = (ClampMin = "0.0", Units = "cm"))
	float ScrapeFocusViewOriginDriftToleranceCm;

	/** A short trace miss keeps the current surface path alive for this long. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape|Motion|Reacquisition",
		meta = (ClampMin = "0.0", ClampMax = "0.15", Units = "s"))
	float TransientMissGraceSeconds;

	/** Maximum same-surface distance that may be considered for reacquisition. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape|Motion|Reacquisition",
		meta = (ClampMin = "0.0", ClampMax = "25.0", Units = "cm"))
	float ReconnectCandidateMaxCm;

	/** User cap; the actual bridge also cannot exceed evaluator sample spacing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape|Motion|Reacquisition",
		meta = (ClampMin = "0.0", Units = "cm"))
	float ReconnectBridgeMaxCm;

	/** TASK 7 applies this to authoritative aim sequence gaps. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape|Motion|Reacquisition",
		meta = (ClampMin = "0", ClampMax = "8"))
	int32 MaxMissingAimSequences;

	/** A continuously invalid Focus exits after this timeout. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape|Motion|Reacquisition",
		meta = (ClampMin = "0.01", Units = "s"))
	float PersistentInvalidExitSeconds;

	/** Begin 기준 server View rotation 허용 오차. 초과 시 조용히 보정하지 않고 거부한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Network",
		meta = (ClampMin = "0.0", Units = "deg"))
	float BeginBasisRotationToleranceDegrees;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Network",
		meta = (ClampMin = "0.0", Units = "deg"))
	float BeginFOVToleranceDegrees;

	/** normalized safe-area frustum에 더하는 검증 여유. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Network",
		meta = (ClampMin = "0.0", Units = "deg"))
	float AimViewConeMarginDegrees;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Network",
		meta = (ClampMin = "0.01", Units = "s"))
	float BeginAckTimeoutSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Network",
		meta = (ClampMin = "0.01", Units = "s"))
	float MaxSampleAgeSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Network",
		meta = (ClampMin = "0.01", Units = "s"))
	float AimSilenceTimeoutSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Network",
		meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float ScrapingAimSendRateHz;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Network",
		meta = (ClampMin = "1.0", ClampMax = "5.0"))
	float ReadyAimHeartbeatRateHz;

private:
	friend class FMD_ScrapeFootprintCharacterizationTest;
	friend class FMD_ScrapeFocusCameraLockTest;
	friend class FMD_ScrapeToolBarnacleOcclusionTest;
	friend class FMD_ScrapeLoadTest;
	friend class FMD_ScrapeStage3PathTest;
	friend class FMD_ScrapeReacquisitionTest;
	friend class FMD_ScrapeNetworkSessionTest;

	/** 디버그 표시용 접촉 종류 문자열. 이 컴포넌트에서만 사용한다. */
	static const TCHAR* GetContactTypeLabel(EMD_ScrapeContactType ContactType);

	void SetCurrentScrapeContact(const FVector& WorldLocation);
	void SetCurrentScrapeBladeDirection(
		UMD_ScrapeSurfaceComponent* Surface,
		EMD_ScrapeSurfaceFace SurfaceFace,
		const FVector2D& SurfacePositionCm,
		const FVector2D& BladeDepthAxis,
		const FVector& ReferenceWorldPosition);
	void ClearCurrentScrapeContact();
	void ProcessScrapeTrace(float DeltaTime);
	void ProcessScrapeAimResult(
		const FMD_ScrapeAimQueryResult& AimResult,
		float SampleDeltaTime,
		bool bSequenceGapKnown,
		int32 MissingAimSequences);
	void ProcessReadyScrapeAim(float DeltaTime);
	void ProcessReadyScrapeAimResult(
		const FMD_ScrapeAimQueryResult& AimResult,
		float SampleDeltaTime);
	bool HandleInvalidScrapeAim(float DeltaTime, bool bMayCloseStroke);
	void BeginInvalidScrapeAim();
	void ResetInvalidScrapeAim();
	bool PrepareReacquiredSurface(
		const FMD_ScrapeAimQueryResult& AimResult,
		bool bSequenceGapKnown = false,
		int32 MissingAimSequences = 0);
	bool ValidateReconnectBridge(
		const FMD_ScrapeAimQueryResult& AimResult,
		double EffectiveBridgeMaxCm) const;
	static FMD_ScrapeReacquireEvaluation EvaluateScrapeReacquisition(
		bool bHasValidSurface,
		bool bHasPriorStroke,
		bool bSameSurfaceAndFace,
		double InvalidElapsedSeconds,
		double ReconnectDistanceCm,
		double CandidateMaxCm,
		double EffectiveBridgeMaxCm,
		bool bBridgeVisibilityValid,
		bool bSequenceGapKnown,
		int32 MissingAimSequences,
		int32 MaximumMissingAimSequences,
		double GraceSeconds,
		double PersistentExitSeconds);
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
	void SetScrapeFocusState(EMD_ScrapeFocusState NewState);
	void ResetScrapeFocusSessionState();
	void EndScrapeFocusInternal(bool bSendServerEnd);
	bool CaptureScrapeFocusView();
	bool TryGetAuthoritativeScrapeView(FMinimalViewInfo& OutViewInfo) const;
	bool IsScrapeFocusViewOriginValid() const;
	void TerminateScrapeFocusFromComponent();
	void TerminateAuthoritativeScrapeFocus(
		EMD_ScrapeFocusTerminationReason Reason);
	FVector GetScrapeFocusAimDirection() const;
	bool IsCurrentScrapeFocusViewportAspectCompatible() const;
	static bool IsScrapeFocusViewportAspectCompatible(
		float AgreedAspectRatio,
		const FIntPoint& CurrentViewportSize,
		float AbsoluteTolerance);
	bool IsScrapeToolCurrentlyEligible() const;
	bool BuildLocalAimSample(FMD_ScrapeAimNetSample& OutSample);
	void SendLocalAimIfDue(float DeltaTime);
	bool ValidateAndCacheAuthoritativeAim(
		const FMD_ScrapeAimNetSample& Sample,
		bool bIsReliableFirstAim,
		FMD_ScrapeAimQueryResult& OutAim,
		float& OutSampleDeltaTime,
		int32& OutMissingSequences);
	bool ConsumeAuthoritativeAimRateToken(double ServerNowSeconds);
	void BufferPreFirstAuthoritativeAim(
		const FMD_ScrapeAimNetSample& Sample,
		const FMD_ScrapeAimQueryResult& ValidatedAim);
	void PrepareBufferedAuthoritativeAimReplay(uint32 FirstAimSequence);
	void ReplayBufferedAuthoritativeAims();
	float CalculateAuthoritativeSampleDeltaTime(
		double PreviousClientSeconds,
		double CurrentClientSeconds,
		bool& bOutIsDiscontinuous) const;
	static bool IsAuthoritativeAimPositionDiscontinuous(
		const FMD_ScrapeAimQueryResult& PreviousAim,
		const FMD_ScrapeAimQueryResult& CurrentAim,
		double MaximumDistanceCm);
	bool IsCameraRelativeAimInsideSessionCone(
		const FVector& CameraRelativeDirection) const;
	void ClampScrapeFocusPointerToAuthorityCone();
	void ResetScrapeFocusPointerStrokeConstraint();
	void TryLatchScrapeFocusPointerStrokeAxis();
	float GetScrapeFocusPointerAspectRatio() const;
	static float CalculateScrapeFocusPointerNormalizedHalfRange(
		float ProjectionHalfTangent,
		float AuthorityHalfTangent,
		float PointerSafeMargin);
	static bool CalculateScrapeFocusProjectionHalfTangents(
		const FMinimalViewInfo& ViewInfo,
		EAspectRatioAxisConstraint DefaultAxisConstraint,
		float ViewportAspectRatio,
		bool bUseLegacyMaintainYFOV,
		float& OutHorizontalHalfTangent,
		float& OutVerticalHalfTangent);
	static bool IsLegacyMaintainYFOVProjectionEnabled();
	static double GetScrapeMonotonicSeconds();
	static float CalculateRotatorErrorDegrees(
		const FRotator& A,
		const FRotator& B);

	static constexpr double MaximumAuthoritativeAimRateHz = 30.0;
	static constexpr double AuthoritativeAimBurstCapacity = 3.0;
	static constexpr int32 MaximumBufferedPreFirstAimSamples = 8;
	/** Supports both landscape and portrait PIE/client windows up to 32:9 either way. */
	static constexpr float MinimumSupportedFocusAspectRatio = 9.0f / 32.0f;
	static constexpr float MaximumSupportedFocusAspectRatio = 32.0f / 9.0f;
	/**
	 * Trusted server policy used when a remote viewport is unavailable (including
	 * dedicated servers). A client projection may narrow this envelope, but its
	 * claimed aspect ratio must never widen authoritative aim acceptance.
	 */
	static constexpr float AuthoritativeFocusAspectRatio = 16.0f / 9.0f;
	static constexpr float ProjectionHalfTangentTolerance = 0.001f;

	UFUNCTION(Server, Reliable)
	void ServerBeginScrapeFocus(
		uint64 FocusSessionId,
		uint32 InitialAimSequence,
		FRotator RequestedViewRotation,
		float RequestedFOV,
		float RequestedAspectRatio,
		float RequestedHorizontalHalfTangent,
		float RequestedVerticalHalfTangent);

	UFUNCTION(Client, Reliable)
	void ClientScrapeFocusBeginResult(
		uint64 FocusSessionId,
		EMD_ScrapeFocusBeginResult Result,
		FRotator AgreedViewRotation,
		float AgreedFOV,
		float AgreedHorizontalHalfTangent,
		float AgreedVerticalHalfTangent);

	UFUNCTION(Server, Reliable)
	void ServerEndScrapeFocus(uint64 FocusSessionId);

	UFUNCTION(Client, Reliable)
	void ClientTerminateScrapeFocus(
		uint64 FocusSessionId,
		EMD_ScrapeFocusTerminationReason Reason);

	UFUNCTION(Server, Unreliable)
	void ServerUpdateScrapeAim(const FMD_ScrapeAimNetSample& Sample);

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
	bool ConsumeRawScrapePathPoint(
		const FVector2D& RawSurfacePositionCm,
		TArray<FMD_ScrapeToolPathSegment>& OutSegments);
	void ResolvePendingScrapePath(
		TArray<FMD_ScrapeToolPathSegment>& OutSegments);
	void AppendResolvedScrapePathSegment(
		const FVector2D& StartPositionCm,
		const FVector2D& EndPositionCm,
		TArray<FMD_ScrapeToolPathSegment>& OutSegments);
	void FlushResolvedScrapePathTail(
		const FVector2D& EndPositionCm,
		TArray<FMD_ScrapeToolPathSegment>& OutSegments);
	void FlushPendingScrapePathApplication();
	void SplitScrapeSubStrokeAtCorner(const FVector2D& CornerPositionCm);
	void ResetScrapePathAccumulator(const FVector2D& StartPositionCm);
	void RebaseScrapePathAfterObstacle(const FVector2D& BlockedPositionCm);
	void AddDirectionSample(
		const FVector2D& PositionCm,
		double ArcLengthCm);
	FVector2D CalculateWindowedBladeAxis() const;
	void UpdateStableBladeAxis(double ArcStepCm);
	bool ApplyScrapePathSegment(
		UMD_ScrapeSurfaceComponent* Surface,
		EMD_ScrapeSurfaceFace SurfaceFace,
		const FHitResult& CurrentHit,
		const FMD_ScrapeToolPathSegment& PathSegment,
		float DeltaTime,
		FVector2D& OutAppliedEndPositionCm,
		AMD_Barnacle*& OutBlockingBarnacle);
	static FMD_ScrapeBladeBasis MakeBladeBasis(
		const FVector2D& BladeDepthAxis);
	static FVector2D InterpolateBladeAxis(
		const FVector2D& StartAxis,
		const FVector2D& EndAxis,
		double Alpha);
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
	void StartStage3PathAccumulator(
		UMD_ScrapeSurfaceComponent* Surface,
		EMD_ScrapeSurfaceFace SurfaceFace,
		const FVector2D& StartPositionCm,
		const FVector2D& StartBladeDepthAxis);
	void AccumulateStage3PathSegment(
		UMD_ScrapeSurfaceComponent* Surface,
		EMD_ScrapeSurfaceFace SurfaceFace,
		const FMD_ScrapeToolPathSegment& PathSegment,
		double AppliedRatio);
	void CommitProtectedStage3Dabs();
	void FlushStage3PathTail();
	void ResetStage3PathAccumulator();
	void AdvanceStage3SubStrokeGeneration();
	void BreakStage3SubStroke();
	uint64 GetStage3FocusSessionIdentity() const;
	FMD_ScrapeContaminationDab BuildStage3DabAtArcLength(
		const FMD_ScrapeToolPathSegment& PathSegment,
		double SegmentStartArcCm,
		double SegmentTravelCm,
		double CandidateArcCm,
		int32 DistanceBin,
		EMD_ScrapeSurfaceFace SurfaceFace);
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

	/**
	 * Local prediction on the owning client and authoritative state on the server.
	 * The server replicates this only to non-owners so simulated proxies can drive
	 * the remote scrape pose without mistaking FocusReady contact for an active LMB.
	 */
	UPROPERTY(Replicated)
	bool bIsScraping;

	UPROPERTY(Transient)
	EMD_ScrapeFocusState ScrapeFocusState = EMD_ScrapeFocusState::Free;

	UPROPERTY(Transient)
	FMD_ScrapeAimQueryResult CurrentScrapeAim;

	uint32 ScrapeFocusGeneration = 0;
	uint64 ActiveScrapeFocusSessionId = 0;
	uint32 NextLocalAimSequence = 0;
	uint32 LastAuthoritativeAimSequence = 0;
	double FocusPendingElapsedSeconds = 0.0;
	double LocalAimSendAccumulatorSeconds = 0.0;
	double LocalFocusMonotonicStartSeconds = 0.0;
	double LastAuthoritativeAimReceiveSeconds = 0.0;
	double LastAuthoritativeClientSampleSeconds = 0.0;
	double AuthoritativeClientClockOffsetSeconds = 0.0;
	float ScrapeFocusAspectRatio = 16.0f / 9.0f;
	/** Client projection used to keep the reticle and generated screen ray aligned. */
	float ScrapeFocusHorizontalHalfTangent = 1.0f;
	float ScrapeFocusVerticalHalfTangent = 9.0f / 16.0f;
	/** Server-owned cone. It can only be equal to or narrower than the projection. */
	float ScrapeFocusAuthorityHorizontalHalfTangent = 1.0f;
	float ScrapeFocusAuthorityVerticalHalfTangent = 9.0f / 16.0f;
	bool bAuthoritativeScrapeFocusAccepted = false;
	bool bAuthoritativeClientClockInitialized = false;
	bool bAuthoritativeFirstScrapeAimApproved = false;
	uint64 AuthoritativeFirstAimSessionId = 0;
	uint32 AuthoritativeFirstAimSequence = 0;
	double AuthoritativeFirstAimClientSampleSeconds = 0.0;
	bool bLastAuthoritativeAimWasActionable = false;
	double AuthoritativeAimRateTokens = AuthoritativeAimBurstCapacity;
	double LastAuthoritativeAimRateRefillSeconds = -1.0;
	TArray<FMD_BufferedAuthoritativeScrapeAim>
		BufferedPreFirstAuthoritativeAims;
	uint32 HighestDroppedPreFirstAimSequence = 0;
	bool bBufferedPreFirstAimReplayDiscontinuous = false;

	UPROPERTY(Transient)
	FVector ScrapeFocusViewOrigin = FVector::ZeroVector;

	UPROPERTY(Transient)
	FRotator ScrapeFocusViewRotation = FRotator::ZeroRotator;

	UPROPERTY(Transient)
	float ScrapeFocusViewFOV = 90.0f;

	UPROPERTY(Transient)
	FIntPoint ScrapeFocusViewportSize = FIntPoint::ZeroValue;

	UPROPERTY(Transient)
	FVector2D ScrapeFocusPointerNormalized = FVector2D(0.5, 0.5);

	/** LMB를 누른 순간의 포인터 위치. 잠긴 임의 각도 직선의 원점이다. */
	FVector2D ScrapeFocusPointerStrokeAnchorNormalized = FVector2D(0.5, 0.5);

	/** viewport pixel과 같은 등방 metric에서 누적한 LMB 제스처. */
	FVector2D ScrapeFocusPointerGestureDisplacement = FVector2D::ZeroVector;

	/** viewport pixel metric에서 고정된 방향. 부호 반전 왕복은 같은 축이다. */
	FVector2D ScrapeFocusPointerGestureAxis = FVector2D::ZeroVector;

	/** normalized 포인터 직선 방향. authority rectangle 교차·clamp에 사용한다. */
	FVector2D ScrapeFocusPointerStrokeAxis = FVector2D::ZeroVector;

	bool bHasScrapeFocusPointerStrokeAxis = false;

	UPROPERTY(Transient)
	bool bHasScrapeFocusView = false;

	bool bHasPreviousSurfacePosition;
	bool bHasLockedStrokeAxis;
	int32 SaturatedContaminationStampPatternSeed;
	uint32 LmbStrokeGeneration = 0;
	uint32 Stage3SubStrokeGeneration = 0;
	uint32 ScrapeToolSpecGeneration = 1;
	uint64 Stage3SampleSequence = 0;
	/** TASK 7 권위 session id가 배정되면 owner placeholder 대신 사용한다. */
	uint64 Stage3FocusSessionIdentityOverride = 0;

	/** 마지막으로 표면에 닿은 월드 위치. 접촉이 끊기면 무효로 표시한다. */
	UPROPERTY(Replicated)
	FVector_NetQuantize10 LastContactWorldLocation = FVector::ZeroVector;

	/** 위 위치가 이번 프레임 기준으로 유효한지 여부. */
	UPROPERTY(Replicated)
	bool bHasLastContact = false;

	/** 서버 승인 경로의 blade 진행축. 위치와 함께 원격 애니메이션이 소비한다. */
	UPROPERTY(Replicated)
	FVector_NetQuantizeNormal LastContactBladeWorldDirection = FVector::ForwardVector;

	UPROPERTY(Replicated)
	bool bHasLastContactBladeDirection = false;

	FVector2D PreviousSurfacePositionCm;
	EMD_ScrapeSurfaceFace PreviousSurfaceFace;
	FVector2D StrokeAnchorPositionCm;
	FVector2D FurthestSurfacePositionCm;
	FVector2D LockedStrokeAxis;
	TWeakObjectPtr<UMD_ScrapeSurfaceComponent> PreviousSurface;

	/** 초기 basis가 확정될 때까지 원래 raw 점 순서를 그대로 보관한다. */
	TArray<FVector2D> PendingScrapePolylineCm;
	double PendingScrapePathArcLengthCm = 0.0;

	/** 방향 계산에만 사용하는 고정 arc-length 표본. 판정 선분과는 별개다. */
	TArray<FMD_ScrapeToolDirectionSample> ScrapeDirectionSamples;
	double ScrapePathArcLengthCm = 0.0;
	double LastEmittedScrapePathArcLengthCm = 0.0;
	/** Fixed-arc 경계 전의 실제 1D 이동 순서. 같은 방향 leg만 합치고 반전은 보존한다. */
	TArray<FVector2D> PendingResolvedScrapePathPointsCm;
	double PendingResolvedScrapePathTravelCm = 0.0;
	/** 아직 fixed-arc segment로 방출되지 않은 경로가 보유한 승인 sample time. */
	double PendingScrapePathSampleTimeSeconds = 0.0;
	FVector2D LastEmittedScrapePathPositionCm = FVector2D::ZeroVector;
	FVector2D LastEmittedBladeDepthAxis = FVector2D::ZeroVector;
	FVector2D StableBladeDepthAxis = FVector2D::ZeroVector;
	FVector2D LastRawTravelTangent = FVector2D::ZeroVector;
	bool bHasStableBladeDepthAxis = false;
	bool bHasLastRawTravelTangent = false;

	/** 고정 arc 경계 사이에 남은 마지막 유효 경로를 종료 시 적용하기 위한 표면 hit. */
	FHitResult LastScrapeSurfaceHit;
	bool bHasLastScrapeSurfaceHit = false;

	/** Invalid-aim lease and the last valid endpoint retained during its grace. */
	bool bHasInvalidScrapeAim = false;
	double InvalidScrapeAimElapsedSeconds = 0.0;
	bool bClosedStrokeForInvalidScrapeAim = false;
	bool bHasScrapeReconnectAnchor = false;
	TWeakObjectPtr<UMD_ScrapeSurfaceComponent> ScrapeReconnectSurface;
	EMD_ScrapeSurfaceFace ScrapeReconnectSurfaceFace =
		EMD_ScrapeSurfaceFace::Front;
	FVector2D ScrapeReconnectPositionCm = FVector2D::ZeroVector;
	FHitResult ScrapeReconnectSurfaceHit;

	/** Stage3에 진입한 지점부터만 누적하는 ordered path state. */
	bool bStage3PathActive = false;
	double Stage3PathArcLengthCm = 0.0;
	int32 Stage3NextDistanceBin = 0;
	double Stage3ProtectionDistanceCm = 0.0;
	TWeakObjectPtr<UMD_ScrapeSurfaceComponent> Stage3PathSurface;
	EMD_ScrapeSurfaceFace Stage3PathSurfaceFace =
		EMD_ScrapeSurfaceFace::Front;
	FMD_ScrapeToolSpec Stage3PathToolSpec;
	FVector2D Stage3PathStartPositionCm = FVector2D::ZeroVector;
	FMD_ScrapeBladeBasis Stage3PathStartBladeBasis;
	TArray<FMD_PendingScrapeContaminationDab> PendingStage3Dabs;
	/** 자동화 스냅샷과 재전송 안전성 확인용. */
	TArray<FMD_ScrapeContaminationDab> CommittedStage3Dabs;

	/** 지금 때리고 있는 따개비. 직접 겨냥한 것이든 경로를 막은 것이든 여기 담긴다 */
	TWeakObjectPtr<AMD_Barnacle> TrackedBarnacle;

	/** 직접 접촉에서 속도를 재는 기준점 */
	FVector PreviousBarnacleHitLocation = FVector::ZeroVector;
	bool bHasPreviousBarnacleHitLocation = false;

	/** 다음 타격을 넣을 수 있는 상태인지. 한 스윙에 한 번만 때리게 하는 빗장이다 */
	bool bBarnacleImpactArmed = true;

	double LastFeedbackWorldSeconds;
};
