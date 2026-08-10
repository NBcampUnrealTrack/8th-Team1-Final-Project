#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"

#include "Component/Scrape/MD_ScrapeSurfaceComponentInternal.h"
#include "Gameplay/Scrape/MD_ScrapeEvaluator.h"
#include "Gameplay/Scrape/MD_ScrapeFaceProgress.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceLayout.h"
#include "World/Contamination/MD_Barnacle.h"

// 파일 분할 기준은 MD_ScrapeSurfaceComponentInternal.h 표 참고.
// 책임: 긁기 스트로크 적용 · 제거율 조회 · 따개비 경로 차단

using namespace MD_ScrapeSurfaceComponentInternal;

bool UMD_ScrapeSurfaceComponent::RegisterScrapeObstacle(AMD_Barnacle* const Barnacle)
{
	if (!IsValid(Barnacle))
	{
		return false;
	}

	const float RadiusCm = Barnacle->GetScrapeBlockRadiusCm();
	if (!FMath::IsFinite(RadiusCm) || RadiusCm <= 0.0f)
	{
		return false;
	}

	// 따개비의 Up이 곧 부착면의 법선이다. 배치기가 그렇게 세워 붙인다.
	FMD_ScrapeSurfaceObstacle Obstacle;
	Obstacle.Barnacle = Barnacle;
	Obstacle.RadiusCm = RadiusCm;
	if (!TryGetScrapeSurfacePositionFromWorldPoint(
			Barnacle->GetActorLocation(),
			Barnacle->GetActorUpVector(),
			Obstacle.CenterCm,
			Obstacle.SurfaceFace))
	{
		return false;
	}

	// 이미 등록된 것을 다시 넣으면 같은 자리를 두 번 막는다.
	for (FMD_ScrapeSurfaceObstacle& Existing : ScrapeObstacles)
	{
		if (Existing.Barnacle.Get() == Barnacle)
		{
			Existing = Obstacle;
			return true;
		}
	}

	ScrapeObstacles.Add(Obstacle);
	return true;
}


void UMD_ScrapeSurfaceComponent::UnregisterScrapeObstacle(AMD_Barnacle* const Barnacle)
{
	// 죽은 참조도 함께 정리한다. 따개비는 Destroy로 사라지므로 등록만 남기 쉽다.
	ScrapeObstacles.RemoveAll(
		[Barnacle](const FMD_ScrapeSurfaceObstacle& Obstacle)
		{
			const AMD_Barnacle* const Registered = Obstacle.Barnacle.Get();
			return Registered == nullptr || Registered == Barnacle;
		});
}


bool UMD_ScrapeSurfaceComponent::TryClampScrapeStrokeAgainstObstacles(
	const EMD_ScrapeSurfaceFace SurfaceFace,
	const FVector2D& StartPositionCm,
	const FVector2D& EndPositionCm,
	const float ToolHalfWidthCm,
	FVector2D& OutClampedEndPositionCm,
	AMD_Barnacle*& OutBlockingBarnacle) const
{
	OutClampedEndPositionCm = EndPositionCm;
	OutBlockingBarnacle = nullptr;

	if (ScrapeObstacles.IsEmpty()
		|| StartPositionCm.ContainsNaN()
		|| EndPositionCm.ContainsNaN()
		|| !FMath::IsFinite(ToolHalfWidthCm))
	{
		return false;
	}

	const FVector2D StrokeDelta = EndPositionCm - StartPositionCm;
	const double StrokeLengthSquared = StrokeDelta.SizeSquared();
	if (StrokeLengthSquared <= UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}

	const float SafeHalfWidthCm = FMath::Max(0.0f, ToolHalfWidthCm);

	// 여러 부착물이 경로에 걸리면 가장 먼저 만나는 것이 막는다.
	double EarliestHitAlpha = TNumericLimits<double>::Max();
	AMD_Barnacle* EarliestBarnacle = nullptr;

	for (const FMD_ScrapeSurfaceObstacle& Obstacle : ScrapeObstacles)
	{
		AMD_Barnacle* const Barnacle = Obstacle.Barnacle.Get();
		if (!IsValid(Barnacle) || Obstacle.SurfaceFace != SurfaceFace)
		{
			continue;
		}

		const double BlockRadius =
			static_cast<double>(Obstacle.RadiusCm) + SafeHalfWidthCm;
		if (BlockRadius <= 0.0)
		{
			continue;
		}

		const FVector2D StartToCenter = StartPositionCm - Obstacle.CenterCm;
		const double StartDistanceSquared = StartToCenter.SizeSquared();
		const double ApproachDot = FVector2D::DotProduct(StartToCenter, StrokeDelta);

		if (StartDistanceSquared <= BlockRadius * BlockRadius)
		{
			// 시작점이 이미 안쪽이다. 멀어지는 방향은 그대로 두고, 더 파고드는
			// 방향만 그 자리에 묶는다. 둘 다 막으면 스트로크가 영영 풀리지 않는다.
			if (ApproachDot < 0.0 && 0.0 < EarliestHitAlpha)
			{
				EarliestHitAlpha = 0.0;
				EarliestBarnacle = Barnacle;
			}
			continue;
		}

		// 선분 대 원. 바깥에서 출발했으므로 먼저 닿는 해만 의미가 있다.
		const double QuadraticB = 2.0 * ApproachDot;
		const double QuadraticC =
			StartDistanceSquared - BlockRadius * BlockRadius;
		const double Discriminant =
			QuadraticB * QuadraticB - 4.0 * StrokeLengthSquared * QuadraticC;
		if (Discriminant < 0.0)
		{
			continue;
		}

		const double HitAlpha =
			(-QuadraticB - FMath::Sqrt(Discriminant)) / (2.0 * StrokeLengthSquared);
		if (HitAlpha < 0.0 || HitAlpha > 1.0 || HitAlpha >= EarliestHitAlpha)
		{
			continue;
		}

		EarliestHitAlpha = HitAlpha;
		EarliestBarnacle = Barnacle;
	}

	if (!EarliestBarnacle)
	{
		return false;
	}

	// 경계에 정확히 붙이지 않고 조금 앞에서 세운다.
	const double StrokeLength = FMath::Sqrt(StrokeLengthSquared);
	const double MarginAlpha = ScrapeObstacleContactMarginCm / StrokeLength;
	const double ClampedAlpha =
		FMath::Clamp(EarliestHitAlpha - MarginAlpha, 0.0, 1.0);

	OutClampedEndPositionCm = StartPositionCm + StrokeDelta * ClampedAlpha;
	OutBlockingBarnacle = EarliestBarnacle;
	return true;
}


FMD_ScrapeResult UMD_ScrapeSurfaceComponent::ApplyLocalScrapeStrokeWithToolSpec(
	const FMD_ScrapeStroke& Stroke,
	const FMD_ScrapeToolSpec& ToolSpec)
{
	SCOPE_CYCLE_COUNTER(STAT_MD_ScrapeStrokeTotal);

	if (!HasAuthority())
	{
		return FMD_ScrapeResult();
	}

	// Spawn 직후 호출도 지원
	if (!bContaminationSurfaceInitialized)
	{
		InitializeContaminationSurface();
	}

	if (!bContaminationSurfaceInitialized)
	{
		return FMD_ScrapeResult();
	}

	bool bWasFullyRemoved = false;
	{
		SCOPE_CYCLE_COUNTER(STAT_MD_ScrapeFullyRemovedCheck);
		bWasFullyRemoved = IsContaminationFullyRemoved();
	}

	TArrayView<FMD_ScrapeSurfaceCell> FaceCells =
		GetContaminationCellsForFace(Stroke.SurfaceFace);
	if (FaceCells.IsEmpty())
	{
		FMD_ScrapeResult InvalidResult;
		InvalidResult.SurfaceFace = Stroke.SurfaceFace;
		return InvalidResult;
	}

	FMD_ScrapeResult Result;
	{
		SCOPE_CYCLE_COUNTER(STAT_MD_ScrapeEvaluate);
		Result = FMD_ScrapeEvaluator::ApplyStrokeToGrid(
			Stroke,
			ToolSpec,
			ScrapeSettings,
			ContaminationGridDimensions,
			ResolvedContaminationSurfaceSizeCm,
			FaceCells);
	}

	// 잘못된 입력은 상태를 변경하지 않음
	if (!Result.bIsInputValid)
	{
		return Result;
	}

	const int32 CellsPerFace = FaceCells.Num();
	for (FMD_ScrapeCellChange& CellChange : Result.CellChanges)
	{
		CellChange.CellIndex = FMD_ScrapeSurfaceLayout::ToCombinedCellIndex(
			Stroke.SurfaceFace,
			CellChange.CellIndex,
			CellsPerFace);
	}

	// 스트로크는 한 면만 건드리므로 그 면만 다시 계산한다.
	RefreshContaminationCleanRatioCache(Stroke.SurfaceFace);
	UpdateContaminationMask(Result.CellChanges);
	UpdateContaminationVisualization(Result.CellChanges);
	PublishContaminationChanges(Result.CellChanges);

	// 기획 02 §1 규칙 3: 긁어낸 오염물은 월드 물리 오브젝트로 떨어진다.
	// 긁기가 끝난 지점에 떨군다. 스트로크는 짧아서 시작점과 큰 차이가 없다.
	DropContaminationItems(
		Stroke.SurfaceFace,
		Result.RemovedContaminationAmount,
		Stroke.EndPositionCm);

	// 어느 한 면이 처음 완료되는 전환에서 한 번만 방송
	SCOPE_CYCLE_COUNTER(STAT_MD_ScrapeFullyRemovedCheck);
	const bool bIsFullyRemoved = IsContaminationFullyRemoved();
	if (!bWasFullyRemoved
		&& bIsFullyRemoved
		&& !bContaminationCompletionBroadcast)
	{
		bContaminationCompletionBroadcast = true;
		OnContaminationFullyRemoved.Broadcast(this);
	}

	return Result;
}


float UMD_ScrapeSurfaceComponent::GetContaminationCleanRatio() const
{
	if (!bContaminationSurfaceInitialized)
	{
		return 0.0f;
	}

	return FMD_ScrapeFaceProgress::CalculateOverallCleanRatio(
		CachedFrontContaminationCleanRatio,
		CachedBackContaminationCleanRatio);
}

float UMD_ScrapeSurfaceComponent::GetContaminationCleanRatio(
	const EMD_ScrapeSurfaceFace Face) const
{
	if (!bContaminationSurfaceInitialized)
	{
		return 0.0f;
	}

	switch (Face)
	{
	case EMD_ScrapeSurfaceFace::Front:
		return FMath::Clamp(
			CachedFrontContaminationCleanRatio,
			0.0f,
			1.0f);

	case EMD_ScrapeSurfaceFace::Back:
		return FMath::Clamp(
			CachedBackContaminationCleanRatio,
			0.0f,
			1.0f);

	default:
		return 0.0f;
	}
}

bool UMD_ScrapeSurfaceComponent::IsContaminationFullyRemoved() const
{
	return IsContaminationFullyRemoved(EMD_ScrapeSurfaceFace::Front)
		|| IsContaminationFullyRemoved(EMD_ScrapeSurfaceFace::Back);
}

bool UMD_ScrapeSurfaceComponent::IsContaminationFullyRemoved(
	const EMD_ScrapeSurfaceFace Face) const
{
	const TConstArrayView<FMD_ScrapeSurfaceCell> FaceCells =
		GetContaminationCellsForFace(Face);
	return bContaminationSurfaceInitialized
		&& !FaceCells.IsEmpty()
		&& FMD_ScrapeEvaluator::IsSurfaceFullyClean(
			FaceCells,
			ScrapeSettings.CleanTolerance);
}

void UMD_ScrapeSurfaceComponent::RefreshContaminationCleanRatioCache()
{
	SCOPE_CYCLE_COUNTER(STAT_MD_ScrapeCleanRatio);
	RefreshContaminationCleanRatioCache(EMD_ScrapeSurfaceFace::Front);
	RefreshContaminationCleanRatioCache(EMD_ScrapeSurfaceFace::Back);
}

void UMD_ScrapeSurfaceComponent::RefreshContaminationCleanRatioCache(
	const EMD_ScrapeSurfaceFace Face)
{
	SCOPE_CYCLE_COUNTER(STAT_MD_ScrapeCleanRatio);

	// 정의되지 않은 면을 그대로 넘기면 빈 슬라이스가 "오염 없음"으로 계산돼
	// 제거율이 1.0이 되고 철거 게이트가 열린다. 반드시 먼저 막는다.
	if (Face != EMD_ScrapeSurfaceFace::Front
		&& Face != EMD_ScrapeSurfaceFace::Back)
	{
		return;
	}

	// 제거 비율 계산은 면 전체를 훑으므로, 바뀌지 않은 면까지 다시 계산하지 않는다.
	float& CachedCleanRatio = Face == EMD_ScrapeSurfaceFace::Back
		? CachedBackContaminationCleanRatio
		: CachedFrontContaminationCleanRatio;
	CachedCleanRatio = FMD_ScrapeEvaluator::CalculateCleanRatio(
		GetContaminationCellsForFace(Face));
}


float UMD_ScrapeSurfaceComponent::GetContaminationDemolitionReadyRatio() const
{
	if (!bContaminationSurfaceInitialized)
	{
		return 0.0f;
	}

	return FMD_ScrapeFaceProgress::CalculateDemolitionReadyCleanRatio(
		CachedFrontContaminationCleanRatio,
		CachedBackContaminationCleanRatio);
}
