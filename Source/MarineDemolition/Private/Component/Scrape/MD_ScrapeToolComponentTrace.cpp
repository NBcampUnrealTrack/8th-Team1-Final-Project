// Copyright Epic Games, Inc. All Rights Reserved.

#include "Component/Scrape/MD_ScrapeToolComponent.h"

#include "Character/MD_Character.h"
#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"
#include "Gameplay/Scrape/MD_ScrapeEvaluator.h"
#include "Gameplay/Scrape/MD_ScrapeMeshUVData.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraTypes.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Misc/ScopeExit.h"
#include "World/Contamination/MD_Barnacle.h"
#include "World/Contamination/MD_BarnacleSpawner.h"
#include "World/Wall/MD_CuttableWall.h"

namespace
{
double ScrapeContactDistanceCm(const UMD_ScrapeSurfaceComponent* Surface,
	const FVector2D& Start, const FVector2D& End, EMD_ScrapeSurfaceFace Face)
{
	if (!Surface || !Surface->UsesMeshUVSurface())
	{
		return FVector2D::Distance(Start, End);
	}
	FVector StartWorld, EndWorld;
	return Surface->TryGetScrapeSurfaceWorldPosition(Start, Face, StartWorld)
		&& Surface->TryGetScrapeSurfaceWorldPosition(End, Face, EndWorld)
		? FVector::Distance(StartWorld, EndWorld) : TNumericLimits<double>::Max();
}
void StopCurrentScrapeForPersistentInvalidAim(
	UMD_ScrapeToolComponent& ScrapeTool)
{
	const uint64 FocusSessionId =
		ScrapeTool.GetActiveScrapeFocusSessionId();
	if (AMD_Character* const Character =
		Cast<AMD_Character>(ScrapeTool.GetOwner()))
	{
		Character->CancelScrapeAbilityForSession(FocusSessionId);
	}

	// Ability cancellation normally reaches StopScraping through EndAbility.
	// Keep this fallback so missing/stale GA registration cannot leave a stroke
	// active across the invalid interval.
	ScrapeTool.StopScraping();
}
}

void UMD_ScrapeToolComponent::ProcessReadyScrapeAim(const float DeltaTime)
{
	ProcessReadyScrapeAimResult(QueryCurrentScrapeAim(), DeltaTime);
}

void UMD_ScrapeToolComponent::ProcessReadyScrapeAimResult(
	const FMD_ScrapeAimQueryResult& AimResult,
	const float SampleDeltaTime)
{
	(void)SampleDeltaTime;
	CurrentScrapeAim = AimResult;
	if (AimResult.Type == EMD_ScrapeAimQueryType::BarnacleSpawnerSurface)
	{
		ClearCurrentScrapeContact();
		ResetInvalidScrapeAim();
		return;
	}
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
		StopCurrentScrapeForPersistentInvalidAim(*this);
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
		|| !FMath::IsFinite(EffectiveBridgeMaxCm)
		|| EffectiveBridgeMaxCm <= UE_SMALL_NUMBER)
	{
		return false;
	}

	FVector2D MidpointCm = FVector2D::ZeroVector;
	FVector MidpointWorldPosition = FVector::ZeroVector;
	if (Surface->UsesMeshUVSurface())
	{
		FMD_ScrapeStroke AddressStroke;
		AddressStroke.SurfaceFace = AimResult.SurfaceFace;
		AddressStroke.StartPositionCm = AimRecovery.ScrapeReconnectPositionCm;
		AddressStroke.EndPositionCm = AimResult.SurfacePositionCm;
		FMD_ScrapeMeshUVStroke WorldStroke;
		FMD_ScrapeMeshUVSample Sample;
		FVector Axis;
		const UMD_ScrapeMeshUVData* Data = Surface->GetContaminationMeshUVData();
		if (!Data || !Surface->GetSurfaceMesh() || !Surface->TryBuildMeshUVStroke(AddressStroke, WorldStroke)
			|| !FMD_ScrapeEvaluator::TryInterpolateMeshUVStroke(*Data,
				Surface->GetSurfaceMesh()->GetComponentTransform(), WorldStroke, 0.5, Sample, Axis))
		{
			return false;
		}
		MidpointWorldPosition = Surface->GetSurfaceMesh()->GetComponentTransform().TransformPosition(Sample.PositionLocal);
	}
	else
	{
		MidpointCm =
		(AimRecovery.ScrapeReconnectPositionCm + AimResult.SurfacePositionCm) * 0.5;
	const FVector ReferenceWorldPosition =
		(AimRecovery.ScrapeReconnectSurfaceHit.ImpactPoint + AimResult.Hit.ImpactPoint) * 0.5;
	if (!Surface->TryGetScrapeSurfaceWorldPositionAtDepth(
			MidpointCm,
			AimResult.SurfaceFace,
			ReferenceWorldPosition,
			MidpointWorldPosition))
	{
		return false;
	}

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
		&& (Surface->UsesMeshUVSurface()
			? FVector::Distance(MidpointAim.WorldContactLocation, MidpointWorldPosition)
			: FVector2D::Distance(MidpointAim.SurfacePositionCm, MidpointCm)) <= PositionToleranceCm;
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
		? ScrapeContactDistanceCm(Surface, AimRecovery.ScrapeReconnectPositionCm,
			AimResult.SurfacePositionCm, AimResult.SurfaceFace)
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
		StopCurrentScrapeForPersistentInvalidAim(*this);
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
		? ScrapeContactDistanceCm(AimResult.Surface, StrokePath.PreviousSurfacePositionCm,
			AimResult.SurfacePositionCm, AimResult.SurfaceFace)
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

void UMD_ScrapeToolComponent::ApplyResolvedScrapePath(
	UMD_ScrapeSurfaceComponent* CurrentSurface,
	const EMD_ScrapeSurfaceFace CurrentSurfaceFace,
	const FHitResult& CurrentHit,
	const TArray<FMD_ScrapeToolPathSegment>& PathSegments)
{
	const bool bBatchAuthoritativeVisualUpdates =
		IsValid(CurrentSurface)
		&& IsValid(CurrentSurface->GetOwner())
		&& CurrentSurface->GetOwner()->HasAuthority()
		&& PathSegments.Num() > 1;
	if (bBatchAuthoritativeVisualUpdates)
	{
		CurrentSurface->BeginContaminationVisualUpdateBatch();
	}
	ON_SCOPE_EXIT
	{
		if (bBatchAuthoritativeVisualUpdates && IsValid(CurrentSurface))
		{
			CurrentSurface->EndContaminationVisualUpdateBatch();
		}
	};
	for (const FMD_ScrapeToolPathSegment& PathSegment : PathSegments)
	{
		if (PathSegment.bStartsNewSubStroke)
		{
			SplitScrapeSubStrokeAtCorner(PathSegment.StartPositionCm);
			continue;
		}

		if (CurrentSurface->UsesMeshUVSurface())
		{
			const FVector Axis = PathSegment.EndWorldBladeDepthAxis.GetSafeNormal();
			if (!Axis.IsNearlyZero())
			{
				const bool bChanged = !bHasLastContactBladeDirection || !LastContactBladeWorldDirection.Equals(Axis, 0.001);
				LastContactBladeWorldDirection = Axis;
				bHasLastContactBladeDirection = true;
				if (bChanged) { RequestScrapePoseNetUpdate(); }
			}
		}
		else
		{
		SetCurrentScrapeBladeDirection(
			CurrentSurface,
			CurrentSurfaceFace,
			PathSegment.EndPositionCm,
			PathSegment.EndBladeDepthAxis,
			CurrentHit.ImpactPoint);
		}
		ApplyScrapePathSegment(
			CurrentSurface,
			CurrentSurfaceFace,
			CurrentHit,
			PathSegment);
	}
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

	if (AimResult.Type == EMD_ScrapeAimQueryType::BarnacleSpawnerSurface)
	{
		// A focus-only target never bridges two wall strokes or produces wall feedback.
		ClearCurrentScrapeContact();
		ResetStrokeTracking();
		ResetInvalidScrapeAim();
		return;
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

	// 직접 따개비가 아닌 잘못된 조준은 한 경로로 정리한다.
	if (AimResult.Type != EMD_ScrapeAimQueryType::SurfaceHit
		|| !IsValid(AimResult.Surface))
	{
		ClearCurrentScrapeContact();
		HandleInvalidScrapeAim(SampleDeltaTime, true);
		return;
	}

	FHitResult CurrentHit = AimResult.Hit;
	UMD_ScrapeSurfaceComponent* const CurrentSurface = AimResult.Surface;
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

	ApplyResolvedScrapePath(
		CurrentSurface,
		CurrentSurfaceFace,
		CurrentHit,
		PathSegments);

	StrokePath.PreviousSurfacePositionCm = RawSurfacePositionCm;
}

void UMD_ScrapeToolComponent::DispatchAuthoritativeScrapeResult(
	UMD_ScrapeSurfaceComponent* Surface,
	const FMD_ScrapeResult& Result,
	const FVector& ContactLocation)
{
	FMD_ScrapeAuthoritativeVisualDelta VisualDelta;
	const bool bBuiltVisualDelta = IsValid(Surface)
		&& Surface->BuildAuthoritativeVisualDelta(
			Result.CellChanges,
			ScrapeLoad,
			VisualDelta);
	ensureMsgf(
		bBuiltVisualDelta,
		TEXT("A successful scrape transaction could not build its visual delta."));
	if (bBuiltVisualDelta)
	{
		AccumulateAuthoritativeScrapePredictionResolution(VisualDelta);
	}

	// 서버 게임 로직은 권한 인스턴스의 공통 표면 이벤트를 구독할 수 있다.
	OnScrapeSurfaceApplied.Broadcast(Surface, Result);
	if (AMD_CuttableWall* const Wall = Surface
		? Cast<AMD_CuttableWall>(Surface->GetOwner())
		: nullptr)
	{
		OnScrapeApplied.Broadcast(Wall, Result);
	}

	FMD_ScrapeResult CompactNetworkResult;
	const FMD_ScrapeResult* NetworkResult = &Result;
	if (Surface && Surface->UsesMeshUVSurface()
		&& (VisualDelta.CellStates.Num() > MaximumImmediateMeshUVCellStates
			|| Result.CellChanges.Num() > MaximumImmediateMeshUVCellStates))
	{
		// Large edits converge through the acknowledged MeshUV journal transport.
		// Keep feedback totals, but never serialize a whole wall in one result RPC.
		CompactNetworkResult = Result;
		CompactNetworkResult.CellChanges.Reset();
		CompactNetworkResult.NetRemovedRecoveryLots.Reset();
		VisualDelta.CellStates.Reset();
		NetworkResult = &CompactNetworkResult;
	}
	if (bBuiltVisualDelta)
	{
		const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
		if (OwnerPawn
			&& OwnerPawn->IsPlayerControlled()
			&& !OwnerPawn->IsLocallyControlled())
		{
			ClientReceiveScrapeResult(
				VisualDelta,
				*NetworkResult,
				ContactLocation);
		}
		MulticastReceiveScrapeFeedback(
			VisualDelta,
			*NetworkResult,
			ContactLocation);
	}

	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (OwnerPawn
		&& OwnerPawn->IsLocallyControlled()
		&& GetNetMode() != NM_DedicatedServer)
	{
		EmitFeedback(Surface, Result, ContactLocation);
	}
}

void UMD_ScrapeToolComponent::ClientReceiveScrapeResult_Implementation(
	const FMD_ScrapeAuthoritativeVisualDelta& VisualDelta,
	const FMD_ScrapeResult& Result,
	const FVector_NetQuantize10 ContactLocation)
{
	ReceiveAuthoritativeScrapeResult(VisualDelta, Result, ContactLocation);
}

void UMD_ScrapeToolComponent::ReceiveAuthoritativeScrapeResult(
	const FMD_ScrapeAuthoritativeVisualDelta& VisualDelta,
	const FMD_ScrapeResult& Result,
	const FVector& ContactLocation)
{
	if (!ApplyAuthoritativeScrapeVisualDelta(VisualDelta))
	{
		return;
	}

	const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
	if (OwnerPawn && OwnerPawn->IsLocallyControlled())
	{
		OnScrapeSurfaceApplied.Broadcast(VisualDelta.Surface, Result);
		if (AMD_CuttableWall* const Wall = VisualDelta.Surface
			? Cast<AMD_CuttableWall>(VisualDelta.Surface->GetOwner())
			: nullptr)
		{
			OnScrapeApplied.Broadcast(Wall, Result);
		}
	}
	EmitFeedback(VisualDelta.Surface, Result, ContactLocation);
}

void UMD_ScrapeToolComponent::MulticastReceiveScrapeFeedback_Implementation(
	const FMD_ScrapeAuthoritativeVisualDelta& VisualDelta,
	const FMD_ScrapeResult& Result,
	const FVector_NetQuantize10 ContactLocation)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const bool bIsLocallyControlledOwner =
		OwnerPawn && OwnerPawn->IsLocallyControlled();
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		// 서버에는 상태가 이미 적용되어 원격 Pawn의 피드백만 재생한다.
		if (!bIsLocallyControlledOwner)
		{
			EmitFeedback(
				VisualDelta.Surface,
				Result,
				ContactLocation);
		}
		return;
	}
	ReceiveAuthoritativeScrapeResult(VisualDelta, Result, ContactLocation);
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

	if (AMD_BarnacleSpawner::IsSpawnerSurfaceHit(SurfaceHit))
	{
		Result.Type = EMD_ScrapeAimQueryType::BarnacleSpawnerSurface;
		Result.Hit = SurfaceHit;
		Result.WorldContactLocation = SurfaceHit.ImpactPoint;
		Result.WorldContactNormal = SurfaceHit.ImpactNormal;
		Result.TraceDistanceCm = static_cast<float>(FVector::Distance(TraceStart, SurfaceHit.ImpactPoint));
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

bool UMD_ScrapeToolComponent::TryTraceScrapeTarget(
	const FVector& TraceStart,
	const FVector& TraceEnd,
	const AActor* const IgnoredActor,
	FHitResult& OutHit,
	UMD_ScrapeSurfaceComponent*& OutSurface,
	AMD_Barnacle*& OutFirstHitBarnacle,
	FHitResult& OutBarnacleHit) const
{
	OutHit = FHitResult();
	OutSurface = nullptr;
	OutFirstHitBarnacle = nullptr;
	OutBarnacleHit = FHitResult();
	if (!GetWorld() || TraceStart.ContainsNaN() || TraceEnd.ContainsNaN())
	{
		return false;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MD_ScrapeTrace), true);
	QueryParams.AddIgnoredActor(IgnoredActor);
	// MeshUV 모드의 셀 주소는 히트한 삼각형의 UV에서 나온다. FaceIndex 없이는
	// FindCollisionUV가 어느 삼각형을 보간할지 알 수 없다.
	QueryParams.bReturnFaceIndex = true;

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

	OutFirstHitBarnacle = Cast<AMD_Barnacle>(FirstHit.GetActor());
	if (!IsValid(OutFirstHitBarnacle))
	{
		return false;
	}
	OutBarnacleHit = FirstHit;

	// 한 번의 입력에서 따개비 클릭과 뒤 작업면의 오염 처리를 함께 판정한다.
	// 첫 따개비만 무시하고 같은 선을 다시 쏴 작업면 좌표를 얻는다.
	QueryParams.AddIgnoredActor(OutFirstHitBarnacle);
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
			ScrapeToolSpec.BarnacleDamagePerClick))
	{
		AddScrapeLoad(ScrapeLoadPerRemovedBarnacle);
	}
}
