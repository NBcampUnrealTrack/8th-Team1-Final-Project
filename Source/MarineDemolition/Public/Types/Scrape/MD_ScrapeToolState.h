#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "Gameplay/Scrape/MD_ScrapeBladeAxis.h"
#include "Types/Scrape/MD_ScrapeTypes.h"

class UMD_ScrapeSurfaceComponent;

/**
 * 헤라 도구 컴포넌트가 프레임 사이에 들고 있는 내부 진행 상태.
 *
 * 리플리케이트되거나 에디터에 노출되는 값은 컴포넌트에 그대로 두고, 순수하게
 * 진행 중인 작업을 기억하기 위한 값만 관심사별로 묶는다. 각 구조체는 기본
 * 생성 상태가 곧 "아무것도 진행하지 않는 상태"이므로, 초기화가 필요한 곳은
 * 멤버를 하나씩 대입하는 대신 구조체 하나를 새로 대입하면 된다.
 */

/** pending candidate가 다른 표면·spec으로 바뀌 후에도 생성 시점 정보를 보존한다. */
struct FMD_PendingScrapeContaminationDab
{
	FMD_ScrapeContaminationDab Dab;
	TWeakObjectPtr<UMD_ScrapeSurfaceComponent> Surface;
	FMD_ScrapeToolSpec ToolSpec;
	float StampAmount = 0.0f;
	float StampDepthCm = 0.0f;
};

/** 첫 Reliable aim보다 먼저 검증된 continuous 표본의 bounded replay 항목. */
struct FMD_BufferedAuthoritativeScrapeAim
{
	FMD_ScrapeAimNetSample Sample;
	FMD_ScrapeAimQueryResult ValidatedAim;
};

/** 집중 조준 세션 하나의 신원과 협상된 화면 정보. */
struct FMD_ScrapeFocusSessionState
{
	uint32 ScrapeFocusGeneration = 0;
	uint64 ActiveScrapeFocusSessionId = 0;
	double FocusPendingElapsedSeconds = 0.0;
	double LocalFocusMonotonicStartSeconds = 0.0;
	float ScrapeFocusAspectRatio = 16.0f / 9.0f;
	/** Client projection used to keep the reticle and generated screen ray aligned. */
	float ScrapeFocusHorizontalHalfTangent = 1.0f;
	float ScrapeFocusVerticalHalfTangent = 9.0f / 16.0f;
	/** Server-owned cone. It can only be equal to or narrower than the projection. */
	float ScrapeFocusAuthorityHorizontalHalfTangent = 1.0f;
	float ScrapeFocusAuthorityVerticalHalfTangent = 9.0f / 16.0f;
	bool bAuthoritativeScrapeFocusAccepted = false;
	/** LMB를 누른 순간의 포인터 위치. 잠긴 임의 각도 직선의 원점이다. */
	FVector2D ScrapeFocusPointerStrokeAnchorNormalized = FVector2D(0.5, 0.5);
	/** viewport pixel과 같은 등방 metric에서 누적한 LMB 제스처. */
	FVector2D ScrapeFocusPointerGestureDisplacement = FVector2D::ZeroVector;
	/** viewport pixel metric에서 고정된 방향. 부호 반전 왕복은 같은 축이다. */
	FVector2D ScrapeFocusPointerGestureAxis = FVector2D::ZeroVector;
	/** normalized 포인터 직선 방향. authority rectangle 교차·clamp에 사용한다. */
	FVector2D ScrapeFocusPointerStrokeAxis = FVector2D::ZeroVector;
	bool bHasScrapeFocusPointerStrokeAxis = false;
};

/** 권위 서버가 클라이언트 조준 표본을 받아들이는 과정의 진행 상태. */
struct FMD_ScrapeAuthoritativeAimState
{
	/** 초당 받아들이는 권위 조준 표본 수 상한. */
	static constexpr double MaximumSampleRateHz = 30.0;

	/** 한꺼번에 몰린 표본을 흡수하는 토큰 버킷 용량. */
	static constexpr double BurstCapacity = 3.0;

	/** 첫 Reliable aim 전에 보관해 둘 수 있는 표본 수 상한. */
	static constexpr int32 MaximumBufferedSamples = 8;

	uint32 NextLocalAimSequence = 0;
	uint32 LastAuthoritativeAimSequence = 0;
	double LocalAimSendAccumulatorSeconds = 0.0;
	double LastAuthoritativeAimReceiveSeconds = 0.0;
	double LastAuthoritativeClientSampleSeconds = 0.0;
	double AuthoritativeClientClockOffsetSeconds = 0.0;
	bool bAuthoritativeClientClockInitialized = false;
	bool bAuthoritativeFirstScrapeAimApproved = false;
	uint64 AuthoritativeFirstAimSessionId = 0;
	uint32 AuthoritativeFirstAimSequence = 0;
	double AuthoritativeFirstAimClientSampleSeconds = 0.0;
	bool bLastAuthoritativeAimWasActionable = false;
	double AuthoritativeAimRateTokens = BurstCapacity;
	double LastAuthoritativeAimRateRefillSeconds = -1.0;
	TArray<FMD_BufferedAuthoritativeScrapeAim>
		BufferedPreFirstAuthoritativeAims;
	uint32 HighestDroppedPreFirstAimSequence = 0;
	bool bBufferedPreFirstAimReplayDiscontinuous = false;
};

/** 진행 중인 stroke 하나의 경로 누적과 날 방향축. */
struct FMD_ScrapeStrokePathState
{
	bool bHasPreviousSurfacePosition;
	bool bHasLockedStrokeAxis;
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
	FMD_ScrapeBladeAxisWindow BladeAxisWindow;
	double ScrapePathArcLengthCm = 0.0;
	double LastEmittedScrapePathArcLengthCm = 0.0;
	/** Fixed-arc 경계 전의 실제 1D 이동 순서. 같은 방향 leg만 합치고 반전은 보존한다. */
	TArray<FVector2D> PendingResolvedScrapePathPointsCm;
	double PendingResolvedScrapePathTravelCm = 0.0;
	FVector2D LastEmittedScrapePathPositionCm = FVector2D::ZeroVector;
	FVector2D LastEmittedBladeDepthAxis = FVector2D::ZeroVector;
	FVector2D StableBladeDepthAxis = FVector2D::ZeroVector;
	FVector2D LastRawTravelTangent = FVector2D::ZeroVector;
	bool bHasStableBladeDepthAxis = false;
	bool bHasLastRawTravelTangent = false;
	/** 고정 arc 경계 사이에 남은 마지막 유효 경로를 종료 시 적용하기 위한 표면 hit. */
	FHitResult LastScrapeSurfaceHit;
	bool bHasLastScrapeSurfaceHit = false;
};

/** 조준을 놓친 구간과 다시 이어 붙일 후보 지점. */
struct FMD_ScrapeAimRecoveryState
{
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
};

/** 포화된 헤라가 다시 묻히는 오염 도장의 누적 상태. */
struct FMD_ScrapeContaminationPaintState
{
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
	uint32 Stage3SubStrokeGeneration = 0;
	uint64 Stage3SampleSequence = 0;
	/** TASK 7 권위 session id가 배정되면 owner placeholder 대신 사용한다. */
	uint64 Stage3FocusSessionIdentityOverride = 0;
	int32 SaturatedContaminationStampPatternSeed;
};
