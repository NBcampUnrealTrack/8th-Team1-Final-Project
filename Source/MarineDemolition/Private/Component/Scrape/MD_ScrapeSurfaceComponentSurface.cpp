#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Gameplay/Scrape/MD_ContaminationNoise.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceGeometry.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceLayout.h"

// 파일 분할 기준은 MD_ScrapeSurfaceComponentInternal.h 표 참고.
// 책임: 작업면 기하 해석 · 셀 초기화 · 좌표 변환

bool UMD_ScrapeSurfaceComponent::TryGetScrapeSurfacePositionFromHit(
	const FHitResult& Hit,
	FVector2D& OutSurfacePositionCm,
	EMD_ScrapeSurfaceFace& OutSurfaceFace)
{
	OutSurfacePositionCm = FVector2D::ZeroVector;
	OutSurfaceFace = EMD_ScrapeSurfaceFace::Front;
	if (!bContaminationEnabled)
	{
		return false;
	}
	if (!bContaminationSurfaceInitialized)
	{
		InitializeContaminationSurface();
	}
	if (!bContaminationSurfaceInitialized)
	{
		return false;
	}

	return TryGetContaminationFaceFromHit(Hit, OutSurfaceFace)
		&& TryProjectWorldPointToContaminationSurface(
			Hit.ImpactPoint,
			OutSurfacePositionCm);
}


bool UMD_ScrapeSurfaceComponent::TryGetScrapeSurfacePositionFromWorldPoint(
	const FVector& WorldPoint,
	const FVector& WorldNormal,
	FVector2D& OutSurfacePositionCm,
	EMD_ScrapeSurfaceFace& OutSurfaceFace)
{
	OutSurfacePositionCm = FVector2D::ZeroVector;
	OutSurfaceFace = EMD_ScrapeSurfaceFace::Front;
	if (!bContaminationEnabled)
	{
		return false;
	}

	if (!bContaminationSurfaceInitialized)
	{
		InitializeContaminationSurface();
	}
	if (!bContaminationSurfaceInitialized)
	{
		return false;
	}

	if (!SurfaceMesh || WorldPoint.ContainsNaN() || WorldNormal.ContainsNaN())
	{
		return false;
	}

	return TryResolveContaminationFace(
			WorldPoint,
			WorldNormal,
			OutSurfaceFace)
		&& TryProjectWorldPointToContaminationSurface(
			WorldPoint,
			OutSurfacePositionCm);
}


bool UMD_ScrapeSurfaceComponent::TryGetScrapeSurfaceWorldPosition(
	const FVector2D& SurfacePositionCm,
	const EMD_ScrapeSurfaceFace SurfaceFace,
	FVector& OutWorldPosition) const
{
	OutWorldPosition = FVector::ZeroVector;
	if (!bContaminationEnabled
		|| !bContaminationSurfaceInitialized
		|| !SurfaceMesh
		|| !FMD_ScrapeSurfaceGeometry::TryCalculateSurfaceWorldPosition(
			SurfacePositionCm,
			SurfaceFace,
			SurfaceMesh->GetComponentTransform(),
			ResolvedContaminationSurfaceNormalAxis,
			ContaminationSurfaceCenterLocal,
			ContaminationMeshLocalBoundsMinimum,
			ContaminationMeshLocalBoundsMaximum,
			OutWorldPosition))
	{
		return false;
	}

	return true;
}


bool UMD_ScrapeSurfaceComponent::TryGetScrapeSurfaceWorldPositionAtDepth(
	const FVector2D& SurfacePositionCm,
	const EMD_ScrapeSurfaceFace SurfaceFace,
	const FVector& ReferenceWorldPosition,
	FVector& OutWorldPosition) const
{
	if (!TryGetScrapeSurfaceWorldPosition(
			SurfacePositionCm,
			SurfaceFace,
			OutWorldPosition))
	{
		return false;
	}

	FVector HorizontalAxisLocal;
	FVector FrontNormalLocal;
	FVector VerticalAxisLocal;
	if (ReferenceWorldPosition.ContainsNaN()
		|| !FMD_ScrapeSurfaceGeometry::TryGetSurfaceLocalAxes(
			ResolvedContaminationSurfaceNormalAxis,
			HorizontalAxisLocal,
			FrontNormalLocal,
			VerticalAxisLocal))
	{
		// 깊이 보정만 실패한 경우이므로 Bounds 평면 위치를 그대로 사용한다.
		return true;
	}

	const FVector WorldFrontNormal = SurfaceMesh->GetComponentTransform()
		.TransformVector(FrontNormalLocal)
		.GetSafeNormal();
	if (WorldFrontNormal.IsNearlyZero())
	{
		return true;
	}

	// 앞/뒤 축 성분만 참조 위치의 깊이로 바꾼다. 가로/세로 위치는 유지한다.
	const double DepthDifference = FVector::DotProduct(
		ReferenceWorldPosition - OutWorldPosition,
		WorldFrontNormal);
	if (!FMath::IsFinite(DepthDifference))
	{
		return true;
	}

	OutWorldPosition += WorldFrontNormal * DepthDifference;
	return !OutWorldPosition.ContainsNaN();
}


bool UMD_ScrapeSurfaceComponent::ResolveContaminationSurfaceGeometry()
{
	if (!SurfaceMesh)
	{
		ResolvedContaminationSurfaceSizeCm = FVector2D::ZeroVector;
		ResolvedContaminationSurfaceNormalAxis =
			EMD_ScrapeSurfaceNormalAxis::Y;
		ContaminationSurfaceCenterLocal = FVector::ZeroVector;
		return false;
	}

	SurfaceMesh->GetLocalBounds(
		ContaminationMeshLocalBoundsMinimum,
		ContaminationMeshLocalBoundsMaximum);

	// 피벗과 무관하게 Bounds 중심 사용
	ContaminationSurfaceCenterLocal =
		(ContaminationMeshLocalBoundsMinimum
			+ ContaminationMeshLocalBoundsMaximum)
		* 0.5f;
	const bool bHasValidOverride =
		FMath::IsFinite(ContaminationSurfaceSizeOverrideCm.X)
		&& FMath::IsFinite(ContaminationSurfaceSizeOverrideCm.Y)
		&& ContaminationSurfaceSizeOverrideCm.X > UE_SMALL_NUMBER
		&& ContaminationSurfaceSizeOverrideCm.Y > UE_SMALL_NUMBER;
	const FVector AbsoluteComponentScale =
		SurfaceMesh->GetComponentScale().GetAbs();
	if (!FMD_ScrapeSurfaceGeometry::TryResolveSurfaceNormalAxis(
			ContaminationSurfaceNormalAxis,
			ContaminationMeshLocalBoundsMinimum,
			ContaminationMeshLocalBoundsMaximum,
			AbsoluteComponentScale,
			ResolvedContaminationSurfaceNormalAxis))
	{
		// 메시 없는 계산 테스트처럼 크기 Override만 제공한 기존 경로는 Y축으로 유지한다.
		if (!bHasValidOverride
			|| ContaminationSurfaceNormalAxis
				!= EMD_ScrapeSurfaceNormalAxis::Auto)
		{
			ResolvedContaminationSurfaceSizeCm = FVector2D::ZeroVector;
			return false;
		}
		ResolvedContaminationSurfaceNormalAxis =
			EMD_ScrapeSurfaceNormalAxis::Y;
	}

	if (bHasValidOverride)
	{
		ResolvedContaminationSurfaceSizeCm =
			ContaminationSurfaceSizeOverrideCm;
		return true;
	}

	return FMD_ScrapeSurfaceGeometry::TryCalculateSurfaceSize(
		ResolvedContaminationSurfaceNormalAxis,
		ContaminationMeshLocalBoundsMinimum,
		ContaminationMeshLocalBoundsMaximum,
		AbsoluteComponentScale,
		ResolvedContaminationSurfaceSizeCm);
}

TArrayView<FMD_ScrapeSurfaceCell>
UMD_ScrapeSurfaceComponent::GetContaminationCellsForFace(
	const EMD_ScrapeSurfaceFace Face)
{
	int32 CellsPerFace = 0;
	int32 TotalCellCount = 0;
	if (!FMD_ScrapeSurfaceLayout::TryCalculateCellCounts(
			ContaminationGridDimensions,
			CellsPerFace,
			TotalCellCount)
		|| ContaminationCells.Num() != TotalCellCount)
	{
		return TArrayView<FMD_ScrapeSurfaceCell>();
	}

	const int32 FaceOffset = FMD_ScrapeSurfaceLayout::GetFaceCellOffset(
		Face,
		CellsPerFace);
	if (FaceOffset == INDEX_NONE)
	{
		return TArrayView<FMD_ScrapeSurfaceCell>();
	}

	return MakeArrayView(ContaminationCells).Slice(
		FaceOffset,
		CellsPerFace);
}

TConstArrayView<FMD_ScrapeSurfaceCell>
UMD_ScrapeSurfaceComponent::GetContaminationCellsForFace(
	const EMD_ScrapeSurfaceFace Face) const
{
	int32 CellsPerFace = 0;
	int32 TotalCellCount = 0;
	if (!FMD_ScrapeSurfaceLayout::TryCalculateCellCounts(
			ContaminationGridDimensions,
			CellsPerFace,
			TotalCellCount)
		|| ContaminationCells.Num() != TotalCellCount)
	{
		return TConstArrayView<FMD_ScrapeSurfaceCell>();
	}

	const int32 FaceOffset = FMD_ScrapeSurfaceLayout::GetFaceCellOffset(
		Face,
		CellsPerFace);
	if (FaceOffset == INDEX_NONE)
	{
		return TConstArrayView<FMD_ScrapeSurfaceCell>();
	}

	return MakeArrayView(ContaminationCells).Slice(
		FaceOffset,
		CellsPerFace);
}

void UMD_ScrapeSurfaceComponent::InitializeContaminationSurface()
{
	bContaminationSurfaceInitialized = false;
	bContaminationDecalsConfigured = false;
	SetContaminationDecalsVisible(false);
	bContaminationCompletionBroadcast = false;
	CachedFrontContaminationCleanRatio = 0.0f;
	CachedBackContaminationCleanRatio = 0.0f;
	AppliedContaminationStampIdentities.Reset();
	AppliedContaminationStampIdentityOrder.Reset();
	ContaminationCells.Reset();
	ContaminationGeneratedInitialAmounts.Reset();
	// 표면을 새로 만들면 아직 덩어리가 되지 못한 제거량도 같이 버린다.
	// 남겨 두면 Day가 바뀐 직후 첫 스트로크에 오염물이 갑자기 튀어나온다.
	PendingContaminationItemAmounts[0] = 0.0f;
	PendingContaminationItemAmounts[1] = 0.0f;
	ReleaseContaminationMask();
	if (HasAuthority())
	{
		ReplicatedContaminationState.ResetState();
	}

	if (!bContaminationEnabled)
	{
		return;
	}

	if (!ResolveContaminationSurfaceGeometry())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("MD_CuttableWall::InitializeContaminationSurface - %s의 오염 작업면 크기를 계산하지 못했습니다."),
			*GetName());
		return;
	}

	// 큰 구조물에서도 12cm 헤라 폭이 최소 4~5셀에 걸리도록 물리 크기를 기준으로
	// 오래된 64x64 인스턴스 설정을 자동 보강한다. uint16 양면 복제 상한은 유지한다.
	static constexpr int32 MinimumGridAxisForAutomaticResolution = 16;
	if (FMath::Min(
			ContaminationGridDimensions.X,
			ContaminationGridDimensions.Y)
		>= MinimumGridAxisForAutomaticResolution)
	{
	const int32 MinimumGridWidth = FMath::CeilToInt(
		ResolvedContaminationSurfaceSizeCm.X
			/ MaximumContaminationCellSizeForPaintCm);
	const int32 MinimumGridHeight = FMath::CeilToInt(
		ResolvedContaminationSurfaceSizeCm.Y
			/ MaximumContaminationCellSizeForPaintCm);
	ContaminationGridDimensions.X = FMath::Clamp(
		FMath::Max(ContaminationGridDimensions.X, MinimumGridWidth),
		1,
		MaximumContaminationGridAxis);
	ContaminationGridDimensions.Y = FMath::Clamp(
		FMath::Max(ContaminationGridDimensions.Y, MinimumGridHeight),
		1,
		MaximumContaminationGridAxis);

	const int32 MaximumCellsPerFace =
		FMD_ScrapeSurfaceLayout::MaximumCombinedCellCount
		/ FMD_ScrapeSurfaceLayout::FaceCount;
	const int64 RequestedCellsPerFace =
		static_cast<int64>(ContaminationGridDimensions.X)
		* static_cast<int64>(ContaminationGridDimensions.Y);
	if (RequestedCellsPerFace > MaximumCellsPerFace)
	{
		const double Scale = FMath::Sqrt(
			static_cast<double>(MaximumCellsPerFace)
				/ static_cast<double>(RequestedCellsPerFace));
		ContaminationGridDimensions.X = FMath::Max(
			1,
			FMath::FloorToInt(ContaminationGridDimensions.X * Scale));
		ContaminationGridDimensions.Y = FMath::Max(
			1,
			FMath::FloorToInt(ContaminationGridDimensions.Y * Scale));
		while (static_cast<int64>(ContaminationGridDimensions.X)
			* static_cast<int64>(ContaminationGridDimensions.Y)
			> MaximumCellsPerFace)
		{
			if (ContaminationGridDimensions.X >= ContaminationGridDimensions.Y)
			{
				--ContaminationGridDimensions.X;
			}
			else
			{
				--ContaminationGridDimensions.Y;
			}
		}
	}
	}

	int32 CellsPerFace = 0;
	int32 TotalCellCount = 0;
	if (!FMD_ScrapeSurfaceLayout::TryCalculateCellCounts(
			ContaminationGridDimensions,
			CellsPerFace,
			TotalCellCount))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("MD_CuttableWall::InitializeContaminationSurface - %s의 앞뒤 총 오염 셀 수가 uint16 범위를 벗어났습니다. Grid=(%d, %d)"),
			*GetName(),
			ContaminationGridDimensions.X,
			ContaminationGridDimensions.Y);
		return;
	}

	ContaminationCells.SetNum(TotalCellCount);

	// 잘못된 초기값은 완전 오염으로 처리
	const float SafeInitialContamination =
		FMath::IsFinite(InitialContaminationPerCell)
		? FMath::Clamp(InitialContaminationPerCell, 0.0f, 1.0f)
		: 1.0f;

	for (FMD_ScrapeSurfaceCell& Cell : ContaminationCells)
	{
		Cell.InitialContamination = SafeInitialContamination;
		Cell.RemainingContamination = SafeInitialContamination;
		Cell.TransferredContamination = 0.0f;
		Cell.TransferredPaintPatternDensity = 0.0f;
	}
	if (bUseProceduralContamination
		&& !TryApplyProceduralInitialContamination(
			CellsPerFace,
			SafeInitialContamination))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("MD_CuttableWall::InitializeContaminationSurface - %s의 절차형 초기 오염 생성에 실패해 균일한 초기값을 유지합니다."),
			*GetName());
	}

	// 여기까지가 "생성이 만들어 낸 값"이다. 밤샘 분출은 이 뒤에 총량을 올리므로,
	// 무엇이 생성값이었는지 알아야 Day 스냅샷에 실을 셀을 가려낼 수 있다.
	ContaminationGeneratedInitialAmounts.Reset(ContaminationCells.Num());
	for (const FMD_ScrapeSurfaceCell& Cell : ContaminationCells)
	{
		ContaminationGeneratedInitialAmounts.Add(Cell.InitialContamination);
	}

	if (!InitializeContaminationMask())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("MD_CuttableWall::InitializeContaminationSurface - %s의 런타임 G8 오염 마스크를 만들지 못했습니다."),
			*GetName());
	}

	RefreshContaminationCleanRatioCache();
	bContaminationSurfaceInitialized = true;

	// 초기 완료 상태의 중복 방송 방지
	bContaminationCompletionBroadcast =
		IsContaminationFullyRemoved();
	ConfigureContaminationDecals();
}


bool UMD_ScrapeSurfaceComponent::TryApplyProceduralInitialContamination(
	const int32 CellsPerFace,
	const float BaseInitialContamination)
{
	if (!SurfaceMesh
		|| CellsPerFace <= 0
		|| ContaminationCells.Num()
			!= CellsPerFace * FMD_ScrapeSurfaceLayout::FaceCount
		|| ContaminationGridDimensions.X <= 0
		|| ContaminationGridDimensions.Y <= 0
		|| ResolvedContaminationSurfaceSizeCm.X <= UE_SMALL_NUMBER
		|| ResolvedContaminationSurfaceSizeCm.Y <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const FVector2D CellSizeCm(
		ResolvedContaminationSurfaceSizeCm.X
			/ static_cast<double>(ContaminationGridDimensions.X),
		ResolvedContaminationSurfaceSizeCm.Y
			/ static_cast<double>(ContaminationGridDimensions.Y));
	const FVector2D SurfaceMinimumCm =
		ResolvedContaminationSurfaceSizeCm * -0.5;
	TArray<float> InitialAmounts;
	InitialAmounts.SetNumUninitialized(ContaminationCells.Num());

	for (int32 FaceIndex = 0;
		FaceIndex < FMD_ScrapeSurfaceLayout::FaceCount;
		++FaceIndex)
	{
		const EMD_ScrapeSurfaceFace Face = FaceIndex == 0
			? EMD_ScrapeSurfaceFace::Front
			: EMD_ScrapeSurfaceFace::Back;
		const int32 FaceSeed = ContaminationNoiseSeed
			+ FaceIndex * 1013;
		for (int32 LocalCellIndex = 0;
			LocalCellIndex < CellsPerFace;
			++LocalCellIndex)
		{
			const int32 CellX =
				LocalCellIndex % ContaminationGridDimensions.X;
			const int32 CellY =
				LocalCellIndex / ContaminationGridDimensions.X;
			const FVector2D SurfacePositionCm = SurfaceMinimumCm
				+ FVector2D(
					(static_cast<double>(CellX) + 0.5) * CellSizeCm.X,
					(static_cast<double>(CellY) + 0.5) * CellSizeCm.Y);
			FVector WorldPosition = FVector::ZeroVector;
			float InitialAmount = 0.0f;
			if (!FMD_ScrapeSurfaceGeometry::TryCalculateSurfaceWorldPosition(
					SurfacePositionCm,
					Face,
					SurfaceMesh->GetComponentTransform(),
					ResolvedContaminationSurfaceNormalAxis,
					ContaminationSurfaceCenterLocal,
					ContaminationMeshLocalBoundsMinimum,
					ContaminationMeshLocalBoundsMaximum,
					WorldPosition)
				|| !FMD_ContaminationNoise::TryCalculateInitialContamination(
					WorldPosition,
					BaseInitialContamination,
					ContaminationNoiseScaleCm,
					FaceSeed,
					ContaminationCoverage,
					ContaminationMinimumAmount,
					InitialAmount))
			{
				return false;
			}

			const int32 CombinedCellIndex =
				FMD_ScrapeSurfaceLayout::ToCombinedCellIndex(
					Face,
					LocalCellIndex,
					CellsPerFace);
			if (!InitialAmounts.IsValidIndex(CombinedCellIndex))
			{
				return false;
			}
			InitialAmounts[CombinedCellIndex] = InitialAmount;
		}
	}

	for (int32 CellIndex = 0;
		CellIndex < ContaminationCells.Num();
		++CellIndex)
	{
		ContaminationCells[CellIndex].InitialContamination =
			InitialAmounts[CellIndex];
		ContaminationCells[CellIndex].RemainingContamination =
			InitialAmounts[CellIndex];
	}
	return true;
}


bool UMD_ScrapeSurfaceComponent::TryProjectWorldPointToContaminationSurface(
	const FVector& WorldPoint,
	FVector2D& OutSurfacePositionCm) const
{
	OutSurfacePositionCm = FVector2D::ZeroVector;

	if (!bContaminationEnabled
		|| !bContaminationSurfaceInitialized
		|| !SurfaceMesh
		|| WorldPoint.ContainsNaN())
	{
		return false;
	}

	return FMD_ScrapeSurfaceGeometry::TryProjectWorldPositionToSurface(
		WorldPoint,
		SurfaceMesh->GetComponentTransform(),
		ResolvedContaminationSurfaceNormalAxis,
		ContaminationSurfaceCenterLocal,
		OutSurfacePositionCm);
}


bool UMD_ScrapeSurfaceComponent::TryGetContaminationFaceFromHit(
	const FHitResult& Hit,
	EMD_ScrapeSurfaceFace& OutSurfaceFace) const
{
	OutSurfaceFace = EMD_ScrapeSurfaceFace::Front;
	if (!bContaminationEnabled
		|| !bContaminationSurfaceInitialized
		|| !SurfaceMesh
		|| !Hit.bBlockingHit
		|| Hit.GetActor() != GetOwner()
		|| Hit.GetComponent() != SurfaceMesh.Get()
		|| Hit.ImpactPoint.ContainsNaN()
		|| Hit.ImpactNormal.ContainsNaN())
	{
		return false;
	}

	return TryResolveContaminationFace(
		Hit.ImpactPoint,
		Hit.ImpactNormal,
		OutSurfaceFace);
}


bool UMD_ScrapeSurfaceComponent::TryResolveContaminationFace(
	const FVector& WorldPosition,
	const FVector& WorldNormal,
	EMD_ScrapeSurfaceFace& OutSurfaceFace) const
{
	OutSurfaceFace = EMD_ScrapeSurfaceFace::Front;
	if (!bContaminationEnabled
		|| !bContaminationSurfaceInitialized
		|| !SurfaceMesh
		|| WorldPosition.ContainsNaN()
		|| WorldNormal.ContainsNaN())
	{
		return false;
	}

	const float MinimumNormalDot = bResolvedFullCoverageContaminationDecals
		? MinimumFullCoverageFaceNormalDot
		: MinimumContaminationFaceNormalDot;
	if (FMD_ScrapeSurfaceGeometry::TryResolveSurfaceFaceFromImpactNormal(
			WorldNormal,
			SurfaceMesh->GetComponentTransform(),
			ResolvedContaminationSurfaceNormalAxis,
			MinimumNormalDot,
			OutSurfaceFace))
	{
		return true;
	}

	// Full coverage still needs a deterministic owner for tangent trims where
	// the receiver normal cannot distinguish the semantic front and back.
	return bResolvedFullCoverageContaminationDecals
		&& TryResolveContaminationFaceFromWorldPosition(
			WorldPosition,
			OutSurfaceFace);
}


bool UMD_ScrapeSurfaceComponent::
TryResolveContaminationFaceFromWorldPosition(
	const FVector& WorldPosition,
	EMD_ScrapeSurfaceFace& OutSurfaceFace) const
{
	OutSurfaceFace = EMD_ScrapeSurfaceFace::Front;
	if (!bContaminationEnabled
		|| !bContaminationSurfaceInitialized
		|| !SurfaceMesh
		|| WorldPosition.ContainsNaN()
		|| !FMath::IsFinite(
			ResolvedContaminationFaceSplitCoordinateLocal))
	{
		return false;
	}

	FVector HorizontalAxisLocal;
	FVector FrontNormalLocal;
	FVector VerticalAxisLocal;
	if (!FMD_ScrapeSurfaceGeometry::TryGetSurfaceLocalAxes(
			ResolvedContaminationSurfaceNormalAxis,
			HorizontalAxisLocal,
			FrontNormalLocal,
			VerticalAxisLocal))
	{
		return false;
	}

	const FVector LocalPosition = SurfaceMesh->GetComponentTransform()
		.InverseTransformPosition(WorldPosition);
	const double FaceCoordinate = FVector::DotProduct(
		LocalPosition,
		FrontNormalLocal);
	if (!FMath::IsFinite(FaceCoordinate))
	{
		return false;
	}

	// The equality case belongs to Front, giving the shared split plane one
	// deterministic owner on both authority and clients.
	OutSurfaceFace = FaceCoordinate
		>= ResolvedContaminationFaceSplitCoordinateLocal
		? EMD_ScrapeSurfaceFace::Front
		: EMD_ScrapeSurfaceFace::Back;
	return true;
}
