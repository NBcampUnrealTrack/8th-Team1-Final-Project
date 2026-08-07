#include "Gameplay/Scrape/MD_ScrapeMeshSurfaceProfile.h"

#include "Engine/StaticMesh.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceGeometry.h"
#include "StaticMeshResources.h"

bool FMD_ScrapeMeshSurfaceProfile::TryMeasureFaceProfile(
	const UStaticMesh* MeshAsset,
	const EMD_ScrapeSurfaceFace Face,
	const EMD_ScrapeSurfaceNormalAxis SurfaceNormalAxis,
	const FVector& MeshAbsoluteScale,
	const FIntPoint& GridDimensions,
	const FVector2D& SurfaceSizeCm,
	const float MinimumFaceNormalDot,
	FMD_ScrapeFaceSurfaceProfile& OutProfile)
{
	OutProfile = FMD_ScrapeFaceSurfaceProfile();

	const bool bIsSupportedFace =
		Face == EMD_ScrapeSurfaceFace::Front
		|| Face == EMD_ScrapeSurfaceFace::Back;
	FVector HorizontalAxisLocal;
	FVector FrontNormalLocal;
	FVector VerticalAxisLocal;
	if (MeshAsset == nullptr
		|| !bIsSupportedFace
		|| MeshAbsoluteScale.ContainsNaN()
		|| GridDimensions.X <= 0
		|| GridDimensions.Y <= 0
		|| !FMath::IsFinite(SurfaceSizeCm.X)
		|| !FMath::IsFinite(SurfaceSizeCm.Y)
		|| SurfaceSizeCm.X <= UE_SMALL_NUMBER
		|| SurfaceSizeCm.Y <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(MinimumFaceNormalDot)
		|| MinimumFaceNormalDot <= 0.0f
		|| MinimumFaceNormalDot > 1.0f
		|| !FMD_ScrapeSurfaceGeometry::TryGetSurfaceLocalAxes(
			SurfaceNormalAxis,
			HorizontalAxisLocal,
			FrontNormalLocal,
			VerticalAxisLocal))
	{
		return false;
	}

	const FStaticMeshRenderData* RenderData = MeshAsset->GetRenderData();
	if (RenderData == nullptr || RenderData->LODResources.Num() <= 0)
	{
		return false;
	}

	const FStaticMeshLODResources& LODResource = RenderData->LODResources[0];
	const FPositionVertexBuffer& PositionBuffer =
		LODResource.VertexBuffers.PositionVertexBuffer;
	const FStaticMeshVertexBuffer& VertexBuffer =
		LODResource.VertexBuffers.StaticMeshVertexBuffer;

	// 쿠킹 빌드는 bAllowCPUAccess를 켜지 않은 메시의 정점 데이터를 GPU로 넘긴 뒤 해제한다.
	// 해제된 버퍼를 읽으면 크래시하므로 여기서 반드시 막고 호출부가 대체 경로를 쓰게 한다.
	if (PositionBuffer.GetNumVertices() == 0
		|| PositionBuffer.GetVertexData() == nullptr
		|| !PositionBuffer.GetAllowCPUAccess()
		|| VertexBuffer.GetNumVertices() != PositionBuffer.GetNumVertices()
		|| VertexBuffer.GetTangentData() == nullptr
		|| !VertexBuffer.GetAllowCPUAccess()
		|| LODResource.IndexBuffer.GetNumIndices() < 3
		|| !LODResource.IndexBuffer.GetAllowCPUAccess())
	{
		return false;
	}

	// 이 면의 바깥 방향. 뒷면은 앞면 법선의 반대다.
	const FVector OutwardNormalLocal = Face == EMD_ScrapeSurfaceFace::Front
		? FrontNormalLocal
		: -FrontNormalLocal;
	const FVector SafeScale = MeshAbsoluteScale.GetAbs();
	const double HorizontalScale =
		FMath::Abs(FVector::DotProduct(SafeScale, HorizontalAxisLocal));
	const double VerticalScale =
		FMath::Abs(FVector::DotProduct(SafeScale, VerticalAxisLocal));
	const double NormalScale =
		FMath::Abs(FVector::DotProduct(SafeScale, FrontNormalLocal));
	if (HorizontalScale <= static_cast<double>(UE_SMALL_NUMBER)
		|| VerticalScale <= static_cast<double>(UE_SMALL_NUMBER)
		|| NormalScale <= static_cast<double>(UE_SMALL_NUMBER))
	{
		return false;
	}

	// 이 면의 Bounds 바깥 평면. 여기서부터 안쪽으로 얼마나 들어가는지를 잰다.
	const FBoxSphereBounds Bounds = MeshAsset->GetBounds();
	const double OuterPlaneCoordinate = FVector::DotProduct(
		Bounds.Origin + Bounds.BoxExtent * OutwardNormalLocal.GetAbs(),
		OutwardNormalLocal) * NormalScale;
	if (!FMath::IsFinite(OuterPlaneCoordinate))
	{
		return false;
	}

	// 벽이 쓰는 것과 같은 셀 좌표계. 원점은 Bounds 중심이고 단위는 월드 cm다.
	const FVector2D CellSizeCm(
		SurfaceSizeCm.X / static_cast<double>(GridDimensions.X),
		SurfaceSizeCm.Y / static_cast<double>(GridDimensions.Y));
	const FVector2D SurfaceMinimumCm = SurfaceSizeCm * -0.5;
	const int32 CellCount = GridDimensions.X * GridDimensions.Y;

	// 셀마다 정면에서 제일 먼저 만나는 표면의 깊이만 남긴다.
	TArray<double> NearestCellDepths;
	NearestCellDepths.Init(TNumericLimits<double>::Max(), CellCount);

	const FIndexArrayView Indices = LODResource.IndexBuffer.GetArrayView();
	auto ProjectVertex = [&](const uint32 VertexIndex)
	{
		const FVector Position(PositionBuffer.VertexPosition(VertexIndex));
		const FVector Offset = Position - Bounds.Origin;
		return FVector(
			FVector::DotProduct(Offset, HorizontalAxisLocal) * HorizontalScale,
			FVector::DotProduct(Offset, VerticalAxisLocal) * VerticalScale,
			OuterPlaneCoordinate
				- FVector::DotProduct(Position, OutwardNormalLocal)
					* NormalScale);
	};

	// 삼각형이 이 면을 향하는지는 정점 법선으로 판단한다.
	// 감기 순서에 의존하지 않고 긁기 판정과 같은 기준을 쓰기 위해서다.
	auto IsFacingTriangle = [&](
		const uint32 IndexA,
		const uint32 IndexB,
		const uint32 IndexC)
	{
		const FVector AverageNormal = (
			FVector(FVector4(VertexBuffer.VertexTangentZ(IndexA)))
			+ FVector(FVector4(VertexBuffer.VertexTangentZ(IndexB)))
			+ FVector(FVector4(VertexBuffer.VertexTangentZ(IndexC))))
			.GetSafeNormal();
		return !AverageNormal.IsNearlyZero()
			&& FVector::DotProduct(AverageNormal, OutwardNormalLocal)
				>= static_cast<double>(MinimumFaceNormalDot);
	};

	for (int32 IndexOffset = 0;
		IndexOffset + 2 < Indices.Num();
		IndexOffset += 3)
	{
		if (!IsFacingTriangle(
				Indices[IndexOffset],
				Indices[IndexOffset + 1],
				Indices[IndexOffset + 2]))
		{
			continue;
		}

		const FVector VertexA = ProjectVertex(Indices[IndexOffset]);
		const FVector VertexB = ProjectVertex(Indices[IndexOffset + 1]);
		const FVector VertexC = ProjectVertex(Indices[IndexOffset + 2]);
		const FVector2D EdgeAB(VertexB.X - VertexA.X, VertexB.Y - VertexA.Y);
		const FVector2D EdgeAC(VertexC.X - VertexA.X, VertexC.Y - VertexA.Y);
		const double DoubleArea = EdgeAB.X * EdgeAC.Y - EdgeAB.Y * EdgeAC.X;
		if (!FMath::IsFinite(DoubleArea)
			|| FMath::Abs(DoubleArea) <= static_cast<double>(UE_SMALL_NUMBER))
		{
			continue;
		}

		// 삼각형이 걸치는 셀 범위만 훑는다.
		const int32 FirstCellX = FMath::Clamp(
			FMath::FloorToInt(
				(FMath::Min3(VertexA.X, VertexB.X, VertexC.X)
					- SurfaceMinimumCm.X) / CellSizeCm.X),
			0,
			GridDimensions.X - 1);
		const int32 LastCellX = FMath::Clamp(
			FMath::FloorToInt(
				(FMath::Max3(VertexA.X, VertexB.X, VertexC.X)
					- SurfaceMinimumCm.X) / CellSizeCm.X),
			0,
			GridDimensions.X - 1);
		const int32 FirstCellY = FMath::Clamp(
			FMath::FloorToInt(
				(FMath::Min3(VertexA.Y, VertexB.Y, VertexC.Y)
					- SurfaceMinimumCm.Y) / CellSizeCm.Y),
			0,
			GridDimensions.Y - 1);
		const int32 LastCellY = FMath::Clamp(
			FMath::FloorToInt(
				(FMath::Max3(VertexA.Y, VertexB.Y, VertexC.Y)
					- SurfaceMinimumCm.Y) / CellSizeCm.Y),
			0,
			GridDimensions.Y - 1);

		for (int32 CellY = FirstCellY; CellY <= LastCellY; ++CellY)
		{
			for (int32 CellX = FirstCellX; CellX <= LastCellX; ++CellX)
			{
				const FVector2D CellCenterCm(
					SurfaceMinimumCm.X
						+ (static_cast<double>(CellX) + 0.5) * CellSizeCm.X,
					SurfaceMinimumCm.Y
						+ (static_cast<double>(CellY) + 0.5) * CellSizeCm.Y);

				// 무게중심 좌표로 셀 중심이 삼각형 안인지 보고 깊이를 보간한다.
				// 두 삼각형이 공유하는 모서리 위의 셀을 놓치지 않도록 허용 오차를 둔다.
				constexpr double BarycentricTolerance = 1.0e-6;
				const FVector2D ToCenter(
					CellCenterCm.X - VertexA.X,
					CellCenterCm.Y - VertexA.Y);
				const double WeightB =
					(ToCenter.X * EdgeAC.Y - ToCenter.Y * EdgeAC.X)
					/ DoubleArea;
				const double WeightC =
					(EdgeAB.X * ToCenter.Y - EdgeAB.Y * ToCenter.X)
					/ DoubleArea;
				const double WeightA = 1.0 - WeightB - WeightC;
				if (WeightA < -BarycentricTolerance
					|| WeightB < -BarycentricTolerance
					|| WeightC < -BarycentricTolerance)
				{
					continue;
				}

				const double DepthCm = VertexA.Z * WeightA
					+ VertexB.Z * WeightB
					+ VertexC.Z * WeightC;
				if (!FMath::IsFinite(DepthCm))
				{
					continue;
				}

				double& NearestDepthCm =
					NearestCellDepths[CellX + CellY * GridDimensions.X];
				NearestDepthCm = FMath::Min(NearestDepthCm, DepthCm);
			}
		}
	}

	for (const double NearestDepthCm : NearestCellDepths)
	{
		if (NearestDepthCm == TNumericLimits<double>::Max())
		{
			continue;
		}

		++OutProfile.CoveredCellCount;
		OutProfile.VisibleSurfaceDepthCm = FMath::Max(
			OutProfile.VisibleSurfaceDepthCm,
			NearestDepthCm);
	}

	if (OutProfile.CoveredCellCount <= 0)
	{
		OutProfile = FMD_ScrapeFaceSurfaceProfile();
		return false;
	}

	OutProfile.VisibleSurfaceDepthCm = FMath::Max(
		OutProfile.VisibleSurfaceDepthCm,
		0.0);
	return true;
}

double FMD_ScrapeMeshSurfaceProfile::CalculateFittedProjectionDepthCm(
	const FMD_ScrapeFaceSurfaceProfile& Profile,
	const double RequestedProjectionDepthCm,
	const double SurfaceMarginCm)
{
	const double SafeRequestedDepthCm = FMath::IsFinite(RequestedProjectionDepthCm)
		? FMath::Max(RequestedProjectionDepthCm, 0.0)
		: 0.0;
	const double SafeMarginCm = FMath::IsFinite(SurfaceMarginCm)
		? FMath::Max(SurfaceMarginCm, 0.0)
		: 0.0;
	if (!Profile.IsValid()
		|| !FMath::IsFinite(Profile.VisibleSurfaceDepthCm))
	{
		return SafeRequestedDepthCm;
	}

	// 정면에서 보이는 표면을 모두 덮되, 수동 설정값보다 얕아지지 않는다.
	return FMath::Max(
		SafeRequestedDepthCm,
		Profile.VisibleSurfaceDepthCm + SafeMarginCm);
}

void FMD_ScrapeMeshSurfaceProfile::BalanceFaceDepths(
	const double WallThicknessCm,
	double& InOutFrontDepthCm,
	double& InOutBackDepthCm)
{
	if (!FMath::IsFinite(WallThicknessCm)
		|| WallThicknessCm <= 0.0
		|| !FMath::IsFinite(InOutFrontDepthCm)
		|| !FMath::IsFinite(InOutBackDepthCm))
	{
		return;
	}

	InOutFrontDepthCm = FMath::Max(InOutFrontDepthCm, 0.0);
	InOutBackDepthCm = FMath::Max(InOutBackDepthCm, 0.0);
	const double TotalDepthCm = InOutFrontDepthCm + InOutBackDepthCm;
	if (TotalDepthCm <= WallThicknessCm
		|| TotalDepthCm <= static_cast<double>(UE_SMALL_NUMBER))
	{
		return;
	}

	// 요구량 비율대로 함께 줄이면 두 상자는 정확히 벽 두께를 나눠 갖는다.
	const double ScaleFactor = WallThicknessCm / TotalDepthCm;
	InOutFrontDepthCm *= ScaleFactor;
	InOutBackDepthCm *= ScaleFactor;
}
