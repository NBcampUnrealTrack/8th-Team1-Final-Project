// Copyright Epic Games, Inc. All Rights Reserved.

#include "Component/Scrape/MD_ScrapeToolComponent.h"

#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"
#include "Camera/CameraTypes.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "World/Contamination/MD_Barnacle.h"
#include "World/Wall/MD_CuttableWall.h"

void UMD_ScrapeToolComponent::ProcessReadyScrapeAim(const float DeltaTime)
{
	ProcessReadyScrapeAimResult(QueryCurrentScrapeAim(), DeltaTime);
}

void UMD_ScrapeToolComponent::ProcessReadyScrapeAimResult(
	const FMD_ScrapeAimQueryResult& AimResult,
	const float SampleDeltaTime)
{
	CurrentScrapeAim = AimResult;
	if (AimResult.IsActionable())
	{
		SetCurrentScrapeContact(AimResult.WorldContactLocation);
		ResetInvalidScrapeAim();
		return;
	}

	ClearCurrentScrapeContact();
	HandleInvalidScrapeAim(SampleDeltaTime, false);
}

void UMD_ScrapeToolComponent::BeginInvalidScrapeAim()
{
	if (AimRecovery.bHasInvalidScrapeAim)
	{
		return;
	}

	AimRecovery.bHasInvalidScrapeAim = true;
	AimRecovery.InvalidScrapeAimElapsedSeconds = 0.0;
	AimRecovery.bClosedStrokeForInvalidScrapeAim = false;
	AimRecovery.bHasScrapeReconnectAnchor = StrokePath.bHasPreviousSurfacePosition
		&& StrokePath.PreviousSurface.IsValid();
	AimRecovery.ScrapeReconnectSurface = AimRecovery.bHasScrapeReconnectAnchor
		? StrokePath.PreviousSurface
		: TWeakObjectPtr<UMD_ScrapeSurfaceComponent>();
	AimRecovery.ScrapeReconnectSurfaceFace = StrokePath.PreviousSurfaceFace;
	AimRecovery.ScrapeReconnectPositionCm = AimRecovery.bHasScrapeReconnectAnchor
		? StrokePath.PreviousSurfacePositionCm
		: FVector2D::ZeroVector;
	AimRecovery.ScrapeReconnectSurfaceHit = StrokePath.bHasLastScrapeSurfaceHit
		? StrokePath.LastScrapeSurfaceHit
		: FHitResult();
}

void UMD_ScrapeToolComponent::ResetInvalidScrapeAim()
{
	// 기본 생성 상태가 곧 "놓친 조준이 없고 이어 붙일 지점도 없음"이다.
	AimRecovery = FMD_ScrapeAimRecoveryState();
}

bool UMD_ScrapeToolComponent::HandleInvalidScrapeAim(
	const float DeltaTime,
	const bool bMayCloseStroke)
{
	BeginInvalidScrapeAim();
	if (FMath::IsFinite(DeltaTime) && DeltaTime > 0.0f)
	{
		AimRecovery.InvalidScrapeAimElapsedSeconds += static_cast<double>(DeltaTime);
	}

	const double GraceSeconds = FMath::IsFinite(TransientMissGraceSeconds)
		? FMath::Max(static_cast<double>(TransientMissGraceSeconds), 0.0)
		: 0.0;
	if (bMayCloseStroke
		&& !AimRecovery.bClosedStrokeForInvalidScrapeAim
		&& AimRecovery.InvalidScrapeAimElapsedSeconds > GraceSeconds)
	{
		ResetStrokeTracking();
		AimRecovery.bClosedStrokeForInvalidScrapeAim = true;
	}

	const FMD_ScrapeReacquireEvaluation Evaluation =
		FMD_ScrapeReacquirePolicy::Evaluate(
			false,
			AimRecovery.bHasScrapeReconnectAnchor,
			false,
			AimRecovery.InvalidScrapeAimElapsedSeconds,
			0.0,
			ReconnectCandidateMaxCm,
			0.0,
			false,
			false,
			0,
			MaxMissingAimSequences,
			TransientMissGraceSeconds,
			PersistentInvalidExitSeconds);
	if (Evaluation.Decision == EMD_ScrapeReacquireDecision::TerminateFocus)
	{
		TerminateScrapeFocusFromComponent();
		return true;
	}

	return false;
}

bool UMD_ScrapeToolComponent::ValidateReconnectBridge(
	const FMD_ScrapeAimQueryResult& AimResult,
	const double EffectiveBridgeMaxCm) const
{
	UMD_ScrapeSurfaceComponent* const Surface = AimResult.Surface;
	if (!AimRecovery.bHasScrapeReconnectAnchor
		|| !IsValid(Surface)
		|| AimRecovery.ScrapeReconnectSurface.Get() != Surface
		|| AimRecovery.ScrapeReconnectSurfaceFace != AimResult.SurfaceFace
		|| IsValid(AimResult.Barnacle)
		|| !FMath::IsFinite(EffectiveBridgeMaxCm)
		|| EffectiveBridgeMaxCm <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const FVector2D MidpointCm =
		(AimRecovery.ScrapeReconnectPositionCm + AimResult.SurfacePositionCm) * 0.5;
	const FVector ReferenceWorldPosition =
		(AimRecovery.ScrapeReconnectSurfaceHit.ImpactPoint + AimResult.Hit.ImpactPoint) * 0.5;
	FVector MidpointWorldPosition = FVector::ZeroVector;
	if (!Surface->TryGetScrapeSurfaceWorldPositionAtDepth(
			MidpointCm,
			AimResult.SurfaceFace,
			ReferenceWorldPosition,
			MidpointWorldPosition))
	{
		return false;
	}

	const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
	const AController* const OwnerController = OwnerPawn
		? OwnerPawn->GetController()
		: nullptr;
	if (!OwnerController)
	{
		return false;
	}

	FVector ViewLocation = FVector::ZeroVector;
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		FMinimalViewInfo ServerViewInfo;
		if (!TryGetAuthoritativeScrapeView(ServerViewInfo))
		{
			return false;
		}
		ViewLocation = ServerViewInfo.Location;
	}
	else
	{
		FRotator ViewRotation = FRotator::ZeroRotator;
		OwnerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	}
	const FVector AimDirection = MidpointWorldPosition - ViewLocation;
	if (ViewLocation.ContainsNaN()
		|| AimDirection.ContainsNaN()
		|| AimDirection.IsNearlyZero())
	{
		return false;
	}

	const FMD_ScrapeAimQueryResult MidpointAim =
		QueryScrapeAim(ViewLocation, AimDirection.GetSafeNormal());
	const double PositionToleranceCm = FMath::Max(
		EffectiveBridgeMaxCm,
		0.25) + 0.01;
	return MidpointAim.Type == EMD_ScrapeAimQueryType::SurfaceHit
		&& MidpointAim.Surface == Surface
		&& MidpointAim.SurfaceFace == AimResult.SurfaceFace
		&& !IsValid(MidpointAim.Barnacle)
		&& FVector2D::Distance(
			MidpointAim.SurfacePositionCm,
			MidpointCm) <= PositionToleranceCm;
}

bool UMD_ScrapeToolComponent::PrepareReacquiredSurface(
	const FMD_ScrapeAimQueryResult& AimResult,
	const bool bSequenceGapKnown,
	const int32 MissingAimSequences)
{
	if (!AimRecovery.bHasInvalidScrapeAim)
	{
		return true;
	}

	UMD_ScrapeSurfaceComponent* const Surface = AimResult.Surface;
	const bool bSameSurfaceAndFace = AimRecovery.bHasScrapeReconnectAnchor
		&& AimRecovery.ScrapeReconnectSurface.Get() == Surface
		&& AimRecovery.ScrapeReconnectSurfaceFace == AimResult.SurfaceFace;
	const double ReconnectDistanceCm = AimRecovery.bHasScrapeReconnectAnchor
		? FVector2D::Distance(
			AimRecovery.ScrapeReconnectPositionCm,
			AimResult.SurfacePositionCm)
		: TNumericLimits<double>::Max();
	const double EvaluatorSpacingCm = IsValid(Surface)
		? Surface->GetScrapeSampleSpacingCm(ScrapeToolSpec)
		: 0.0;
	const double UserBridgeMaxCm = FMath::IsFinite(ReconnectBridgeMaxCm)
		? FMath::Max(static_cast<double>(ReconnectBridgeMaxCm), 0.0)
		: 0.0;
	const double EffectiveBridgeMaxCm =
		FMath::IsFinite(EvaluatorSpacingCm) && EvaluatorSpacingCm > 0.0
		? FMath::Min(EvaluatorSpacingCm, UserBridgeMaxCm)
		: 0.0;
	const bool bCouldBridge = bSameSurfaceAndFace
		&& AimRecovery.InvalidScrapeAimElapsedSeconds
			<= static_cast<double>(FMath::Max(TransientMissGraceSeconds, 0.0f))
		&& ReconnectDistanceCm
			<= static_cast<double>(FMath::Max(ReconnectCandidateMaxCm, 0.0f))
		&& ReconnectDistanceCm <= EffectiveBridgeMaxCm;
	const bool bBridgeVisibilityValid = bCouldBridge
		&& ValidateReconnectBridge(AimResult, EffectiveBridgeMaxCm);

	const FMD_ScrapeReacquireEvaluation Evaluation =
		FMD_ScrapeReacquirePolicy::Evaluate(
			IsValid(Surface),
			AimRecovery.bHasScrapeReconnectAnchor,
			bSameSurfaceAndFace,
			AimRecovery.InvalidScrapeAimElapsedSeconds,
			ReconnectDistanceCm,
			ReconnectCandidateMaxCm,
			EffectiveBridgeMaxCm,
			bBridgeVisibilityValid,
			bSequenceGapKnown,
			MissingAimSequences,
			MaxMissingAimSequences,
			TransientMissGraceSeconds,
			PersistentInvalidExitSeconds);

	if (Evaluation.Decision == EMD_ScrapeReacquireDecision::ContinueBridge)
	{
		ResetInvalidScrapeAim();
		return true;
	}
	if (Evaluation.Decision == EMD_ScrapeReacquireDecision::TerminateFocus)
	{
		TerminateScrapeFocusFromComponent();
		return false;
	}

	if (!AimRecovery.bClosedStrokeForInvalidScrapeAim)
	{
		ResetStrokeTracking();
	}
	ResetInvalidScrapeAim();
	return true;
}

void UMD_ScrapeToolComponent::ProcessScrapeTrace(const float DeltaTime)
{
	ProcessScrapeAimResult(
		QueryCurrentScrapeAim(),
		DeltaTime,
		false,
		0);
}

void UMD_ScrapeToolComponent::ApplyAimSequenceGapPolicy(
	const bool bSequenceGapKnown,
	const int32 MissingAimSequences,
	const float SampleDeltaTime)
{
	if (bSequenceGapKnown
		&& MissingAimSequences > FMath::Max(MaxMissingAimSequences, 0))
	{
		// 유실 구간을 작은 공간 jump라는 이유로 연결하지 않는다.
		ResetStrokeTracking();
		ResetInvalidScrapeAim();
	}
	else if (bSequenceGapKnown
		&& MissingAimSequences > 0
		&& StrokePath.bHasPreviousSurfacePosition)
	{
		// 명시 NoContact가 오지 않았어도 유실 sequence 자체를 transient miss로
		// 취급해 sample-spacing/중간 Trace bridge 조건을 동일하게 적용한다.
		BeginInvalidScrapeAim();
		AimRecovery.InvalidScrapeAimElapsedSeconds = FMath::Max(
			AimRecovery.InvalidScrapeAimElapsedSeconds,
			static_cast<double>(FMath::Max(SampleDeltaTime, 0.0f)));
	}
}

void UMD_ScrapeToolComponent::ResetStrokeOnDiscontinuousSurfaceJump(
	const FMD_ScrapeAimQueryResult& AimResult)
{
	const bool bHasContinuousSurfaceTarget = StrokePath.bHasPreviousSurfacePosition
		&& StrokePath.PreviousSurface.Get() == AimResult.Surface
		&& StrokePath.PreviousSurfaceFace == AimResult.SurfaceFace;
	const double ValidSampleJumpCm = bHasContinuousSurfaceTarget
		? FVector2D::Distance(
			StrokePath.PreviousSurfacePositionCm,
			AimResult.SurfacePositionCm)
		: 0.0;
	const double MaximumContinuousJumpCm =
		FMath::IsFinite(ReconnectCandidateMaxCm)
		? FMath::Max(static_cast<double>(ReconnectCandidateMaxCm), 0.0)
		: 0.0;
	if (bHasContinuousSurfaceTarget
		&& (!FMath::IsFinite(ValidSampleJumpCm)
			|| ValidSampleJumpCm > MaximumContinuousJumpCm))
	{
		// A large valid-to-valid jump is also a discontinuity. Never fill it with
		// a long segment merely because no intermediate miss sample was observed.
		ResetStrokeTracking();
	}
}

bool UMD_ScrapeToolComponent::TryStartNewSurfaceStroke(
	UMD_ScrapeSurfaceComponent* CurrentSurface,
	const EMD_ScrapeSurfaceFace CurrentSurfaceFace,
	const FVector2D& RawSurfacePositionCm,
	const FHitResult& CurrentHit)
{
	const bool bNeedsNewStroke = !StrokePath.bHasPreviousSurfacePosition
		|| !StrokePath.PreviousSurface.IsValid()
		|| StrokePath.PreviousSurface.Get() != CurrentSurface
		|| StrokePath.PreviousSurfaceFace != CurrentSurfaceFace;
	if (bNeedsNewStroke)
	{
		if (StrokePath.bHasPreviousSurfacePosition
			|| StrokePath.PreviousSurface.IsValid())
		{
			ResetStrokeTracking();
		}
		BeginStrokeTracking(
			CurrentSurface,
			RawSurfacePositionCm,
			CurrentSurfaceFace);
	}
	StrokePath.LastScrapeSurfaceHit = CurrentHit;
	StrokePath.bHasLastScrapeSurfaceHit = true;
	return bNeedsNewStroke;
}

bool UMD_ScrapeToolComponent::TryApplyResolvedScrapePath(
	UMD_ScrapeSurfaceComponent* CurrentSurface,
	const EMD_ScrapeSurfaceFace CurrentSurfaceFace,
	const FHitResult& CurrentHit,
	const TArray<FMD_ScrapeToolPathSegment>& PathSegments)
{
	for (const FMD_ScrapeToolPathSegment& PathSegment : PathSegments)
	{
		if (PathSegment.bStartsNewSubStroke)
		{
			SplitScrapeSubStrokeAtCorner(PathSegment.StartPositionCm);
			continue;
		}

		FVector2D AppliedEndPositionCm = PathSegment.EndPositionCm;
		AMD_Barnacle* BlockingBarnacle = nullptr;
		SetCurrentScrapeBladeDirection(
			CurrentSurface,
			CurrentSurfaceFace,
			PathSegment.EndPositionCm,
			PathSegment.EndBladeDepthAxis,
			CurrentHit.ImpactPoint);
		if (ApplyScrapePathSegment(
				CurrentSurface,
				CurrentSurfaceFace,
				CurrentHit,
				PathSegment,
				AppliedEndPositionCm,
				BlockingBarnacle))
		{
			RebaseScrapePathAfterObstacle(AppliedEndPositionCm);
			return true;
		}
	}
	return false;
}

void UMD_ScrapeToolComponent::ProcessScrapeAimResult(
	const FMD_ScrapeAimQueryResult& AimResult,
	const float SampleDeltaTime,
	const bool bSequenceGapKnown,
	const int32 MissingAimSequences)
{
	ApplyAimSequenceGapPolicy(
		bSequenceGapKnown,
		MissingAimSequences,
		SampleDeltaTime);
	if (IsScrapeFocusActive())
	{
		CurrentScrapeAim = AimResult;
	}

	if (AimResult.Type == EMD_ScrapeAimQueryType::DirectBarnacleOnly
		&& IsValid(AimResult.Barnacle))
	{
		// A direct barnacle remains actionable, but it cannot stay connected to
		// the previous surface path.
		ResetStrokeTracking();
		ResetInvalidScrapeAim();
		return;
	}

	// 오염 표면이 아니어도 따개비일 수 있다. 둘은 성격이 달라 경로를 갈라 둔다.
	if (AimResult.Type != EMD_ScrapeAimQueryType::SurfaceHit)
	{
		ClearCurrentScrapeContact();
		HandleInvalidScrapeAim(SampleDeltaTime, true);
		return;
	}

	FHitResult CurrentHit = AimResult.Hit;
	UMD_ScrapeSurfaceComponent* const CurrentSurface = AimResult.Surface;
	AMD_Barnacle* const OccludingBarnacle = AimResult.Barnacle;
	if (!IsValid(CurrentSurface))
	{
		ClearCurrentScrapeContact();
		HandleInvalidScrapeAim(SampleDeltaTime, true);
		return;
	}

	if (!PrepareReacquiredSurface(
			AimResult,
			bSequenceGapKnown,
			MissingAimSequences))
	{
		return;
	}

	ResetStrokeOnDiscontinuousSurfaceJump(AimResult);
	SetCurrentScrapeContact(CurrentHit.ImpactPoint);

	if (IsValid(OccludingBarnacle))
	{
		// 클라이언트에서 부착 복제가 BeginPlay보다 늦어도 직접 맞은 순간 복구한다.
		// 장애물 등록이 있어야 아래 경로 차단과 조준 보정이 함께 작동한다.
		OccludingBarnacle->RegisterWithScrapeSurface(CurrentSurface);
	}

	const FVector2D RawSurfacePositionCm = AimResult.SurfacePositionCm;
	const EMD_ScrapeSurfaceFace CurrentSurfaceFace = AimResult.SurfaceFace;
	if (TryStartNewSurfaceStroke(
			CurrentSurface,
			CurrentSurfaceFace,
			RawSurfacePositionCm,
			CurrentHit))
	{
		return;
	}

	TArray<FMD_ScrapeToolPathSegment> PathSegments;
	if (!ConsumeRawScrapePathPoint(RawSurfacePositionCm, PathSegments))
	{
		StrokePath.PreviousSurfacePositionCm = RawSurfacePositionCm;
		return;
	}

	if (TryApplyResolvedScrapePath(
			CurrentSurface,
			CurrentSurfaceFace,
			CurrentHit,
			PathSegments))
	{
		return;
	}

	StrokePath.PreviousSurfacePositionCm = RawSurfacePositionCm;
}

void UMD_ScrapeToolComponent::DispatchAuthoritativeScrapeResult(
	UMD_ScrapeSurfaceComponent* Surface,
	const FMD_ScrapeResult& Result,
	const FVector& ContactLocation)
{
	// 서버 게임 로직은 권한 인스턴스의 공통 표면 이벤트를 구독할 수 있다.
	OnScrapeSurfaceApplied.Broadcast(Surface, Result);
	if (AMD_CuttableWall* const Wall = Surface
		? Cast<AMD_CuttableWall>(Surface->GetOwner())
		: nullptr)
	{
		OnScrapeApplied.Broadcast(Wall, Result);
	}

	// 다른 플레이어 화면에도 접촉 이펙트·사운드를 재생한다. 소유자는 아래 전용 경로가 담당한다
	MulticastReceiveScrapeFeedback(Surface, Result, ContactLocation);

	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (OwnerPawn
		&& OwnerPawn->IsLocallyControlled()
		&& GetNetMode() != NM_DedicatedServer)
	{
		EmitFeedback(Surface, Result, ContactLocation);
		return;
	}

	// 원격 소유자의 사운드/이펙트와 Blueprint 피드백은 해당 클라이언트에서 실행한다.
	ClientReceiveScrapeResult(Surface, Result, ContactLocation);
}

void UMD_ScrapeToolComponent::MulticastReceiveScrapeFeedback_Implementation(
	UMD_ScrapeSurfaceComponent* Surface,
	const FMD_ScrapeResult& Result,
	const FVector_NetQuantize10 ContactLocation)
{
	// 소유 클라이언트는 로컬 즉시 재생 또는 Client RPC로 이미 받으므로 중복을 막는다
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (OwnerPawn && OwnerPawn->IsLocallyControlled())
	{
		return;
	}

	EmitFeedback(Surface, Result, ContactLocation);
}

void UMD_ScrapeToolComponent::ClientReceiveScrapeResult_Implementation(
	UMD_ScrapeSurfaceComponent* Surface,
	const FMD_ScrapeResult& Result,
	const FVector_NetQuantize10 ContactLocation)
{
	OnScrapeSurfaceApplied.Broadcast(Surface, Result);
	if (AMD_CuttableWall* const Wall = Surface
		? Cast<AMD_CuttableWall>(Surface->GetOwner())
		: nullptr)
	{
		OnScrapeApplied.Broadcast(Wall, Result);
	}
	EmitFeedback(Surface, Result, ContactLocation);
}

FMD_ScrapeAimQueryResult UMD_ScrapeToolComponent::QueryCurrentScrapeAim() const
{
	const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->GetController() || !GetWorld())
	{
		return FMD_ScrapeAimQueryResult();
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	OwnerPawn->GetController()->GetPlayerViewPoint(ViewLocation, ViewRotation);
	if (IsScrapeFocusActive() && bHasScrapeFocusView)
	{
		return QueryScrapeAim(ViewLocation, GetScrapeFocusAimDirection());
	}
	return QueryScrapeAim(ViewLocation, ViewRotation.Vector());
}

FMD_ScrapeAimQueryResult UMD_ScrapeToolComponent::QueryScrapeAim(
	const FVector& TraceStart,
	const FVector& AimDirection) const
{
	FMD_ScrapeAimQueryResult Result;
	const float EffectiveReachCm = GetEffectiveScrapeReachCm();
	if (!GetWorld()
		|| TraceStart.ContainsNaN()
		|| AimDirection.ContainsNaN()
		|| EffectiveReachCm <= UE_SMALL_NUMBER)
	{
		return Result;
	}

	const FVector SafeAimDirection = AimDirection.GetSafeNormal();
	if (SafeAimDirection.IsNearlyZero())
	{
		return Result;
	}

	FHitResult SurfaceHit;
	FHitResult BarnacleHit;
	UMD_ScrapeSurfaceComponent* Surface = nullptr;
	AMD_Barnacle* Barnacle = nullptr;
	const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
	const bool bHasSurface = TryTraceScrapeTarget(
		TraceStart,
		TraceStart + (SafeAimDirection * EffectiveReachCm),
		OwnerPawn,
		SurfaceHit,
		Surface,
		Barnacle,
		BarnacleHit);

	Result.Surface = Surface;
	Result.Barnacle = Barnacle;
	Result.BarnacleHit = BarnacleHit;
	if (bHasSurface && IsValid(Surface))
	{
		Result.Hit = SurfaceHit;
		if (!Surface->TryGetScrapeSurfacePositionFromHit(
				SurfaceHit,
				Result.SurfacePositionCm,
				Result.SurfaceFace))
		{
			// 충돌은 작업면이었지만 같은 표면 좌표계로 해석할 수 없으므로 진입 불가다.
			Result.Surface = nullptr;
			Result.bWasBlocked = true;
			return Result;
		}

		Result.Type = EMD_ScrapeAimQueryType::SurfaceHit;
		Result.WorldContactLocation = SurfaceHit.ImpactPoint;
		Result.WorldContactNormal = SurfaceHit.ImpactNormal;
		Result.TraceDistanceCm = static_cast<float>(FVector::Distance(
			TraceStart,
			SurfaceHit.ImpactPoint));
		return Result;
	}

	if (IsValid(Barnacle) && BarnacleHit.bBlockingHit)
	{
		// 뒤 trace가 빗나가거나 일반 물체에 다시 막혀도 첫 따개비 직접 접촉은 유효하다.
		Result.Type = EMD_ScrapeAimQueryType::DirectBarnacleOnly;
		Result.Hit = BarnacleHit;
		Result.WorldContactLocation = BarnacleHit.ImpactPoint;
		Result.WorldContactNormal = BarnacleHit.ImpactNormal;
		Result.TraceDistanceCm = static_cast<float>(FVector::Distance(
			TraceStart,
			BarnacleHit.ImpactPoint));
		return Result;
	}

	// TryTraceScrapeTarget는 일반 물체를 첫 충돌로 보존해 NoTarget과 가림을 구분한다.
	Result.Hit = SurfaceHit;
	Result.bWasBlocked = SurfaceHit.bBlockingHit;
	if (Result.bWasBlocked)
	{
		Result.WorldContactLocation = SurfaceHit.ImpactPoint;
		Result.WorldContactNormal = SurfaceHit.ImpactNormal;
		Result.TraceDistanceCm = static_cast<float>(FVector::Distance(
			TraceStart,
			SurfaceHit.ImpactPoint));
	}
	return Result;
}

bool UMD_ScrapeToolComponent::TryGetCurrentSurfaceHit(
	FHitResult& OutHit,
	UMD_ScrapeSurfaceComponent*& OutSurface,
	AMD_Barnacle*& OutOccludingBarnacle,
	FHitResult& OutBarnacleHit) const
{
	OutHit = FHitResult();
	OutSurface = nullptr;
	OutOccludingBarnacle = nullptr;
	OutBarnacleHit = FHitResult();

	const FMD_ScrapeAimQueryResult Result = QueryCurrentScrapeAim();
	OutHit = Result.Hit;
	OutSurface = Result.Surface;
	OutOccludingBarnacle = Result.Barnacle;
	OutBarnacleHit = Result.BarnacleHit;
	return Result.Type == EMD_ScrapeAimQueryType::SurfaceHit;
}

bool UMD_ScrapeToolComponent::TryTraceScrapeTarget(
	const FVector& TraceStart,
	const FVector& TraceEnd,
	const AActor* const IgnoredActor,
	FHitResult& OutHit,
	UMD_ScrapeSurfaceComponent*& OutSurface,
	AMD_Barnacle*& OutOccludingBarnacle,
	FHitResult& OutBarnacleHit) const
{
	OutHit = FHitResult();
	OutSurface = nullptr;
	OutOccludingBarnacle = nullptr;
	OutBarnacleHit = FHitResult();
	if (!GetWorld() || TraceStart.ContainsNaN() || TraceEnd.ContainsNaN())
	{
		return false;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MD_ScrapeTrace), true);
	QueryParams.AddIgnoredActor(IgnoredActor);

	FHitResult FirstHit;
	if (!GetWorld()->LineTraceSingleByChannel(
			FirstHit,
			TraceStart,
			TraceEnd,
			TraceChannel,
			QueryParams))
	{
		return false;
	}

	OutHit = FirstHit;
	OutSurface = UMD_ScrapeSurfaceComponent::FindForPrimitiveComponent(
		FirstHit.GetComponent());
	if (IsValid(OutSurface))
	{
		return true;
	}

	OutOccludingBarnacle = Cast<AMD_Barnacle>(FirstHit.GetActor());
	if (!IsValid(OutOccludingBarnacle))
	{
		return false;
	}
	OutBarnacleHit = FirstHit;

	// 따개비가 Visibility를 막아도 그 뒤의 작업면 좌표가 있어야 헤라 경로를
	// 따개비 앞에서 자를 수 있다. 첫 따개비만 무시하고 같은 선을 다시 쏜다.
	QueryParams.AddIgnoredActor(OutOccludingBarnacle);
	FHitResult SurfaceHit;
	if (!GetWorld()->LineTraceSingleByChannel(
			SurfaceHit,
			TraceStart,
			TraceEnd,
			TraceChannel,
			QueryParams))
	{
		return false;
	}

	OutSurface = UMD_ScrapeSurfaceComponent::FindForPrimitiveComponent(
		SurfaceHit.GetComponent());
	if (!IsValid(OutSurface))
	{
		return false;
	}

	OutHit = SurfaceHit;
	return true;
}

void UMD_ScrapeToolComponent::ApplyBarnacleClick(
	const FMD_ScrapeAimQueryResult& FirstAim)
{
	AMD_Barnacle* const Barnacle = FirstAim.Barnacle;
	if (!bIsScraping
		|| !GetOwner()
		|| !GetOwner()->HasAuthority()
		|| !IsValid(Barnacle)
		|| Barnacle->IsFullyRemoved()
		|| !FirstAim.BarnacleHit.bBlockingHit
		|| FirstAim.BarnacleHit.GetActor() != Barnacle
		|| (FirstAim.Type != EMD_ScrapeAimQueryType::DirectBarnacleOnly
			&& FirstAim.Type != EMD_ScrapeAimQueryType::SurfaceHit))
	{
		return;
	}

	if (Barnacle->ApplyScrapeClickDamage(
			ScrapeToolSpec.BarnacleDamagePerClick,
			ResolveBarnacleClickWobbleDirection(
				FirstAim.BarnacleHit,
				*Barnacle)))
	{
		AddScrapeLoad(ScrapeLoadPerRemovedBarnacle);
	}
}

FVector UMD_ScrapeToolComponent::ResolveBarnacleClickWobbleDirection(
	const FHitResult& BarnacleHit,
	const AMD_Barnacle& Barnacle)
{
	const FVector AttachmentNormal = Barnacle.GetActorUpVector().GetSafeNormal();
	const FVector ViewDirection =
		(BarnacleHit.TraceEnd - BarnacleHit.TraceStart).GetSafeNormal();
	const FVector TangentialViewDirection = FVector::VectorPlaneProject(
		ViewDirection,
		AttachmentNormal).GetSafeNormal();
	if (!TangentialViewDirection.IsNearlyZero())
	{
		return TangentialViewDirection;
	}

	// 정면 클릭은 투영 방향이 0이므로 부착면의 안정적인 접선축으로 흔들어 준다.
	return Barnacle.GetActorRightVector().GetSafeNormal();
}
