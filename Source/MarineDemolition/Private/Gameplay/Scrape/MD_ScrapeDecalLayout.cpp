#include "Gameplay/Scrape/MD_ScrapeDecalLayout.h"

#include "Gameplay/Scrape/MD_ScrapeSurfaceGeometry.h"
#include "Math/RotationMatrix.h"

bool FMD_ScrapeDecalLayout::TryCalculateFaceLayout(
	const EMD_ScrapeSurfaceFace Face,
	const EMD_ScrapeSurfaceNormalAxis SurfaceNormalAxis,
	const FVector2D& SurfaceSizeCm,
	const FVector& MeshLocalBoundsMinimum,
	const FVector& MeshLocalBoundsMaximum,
	const FVector& MeshAbsoluteScale,
	const float RequestedInwardDepthCm,
	const float RequestedOutwardMarginCm,
	FMD_ScrapeDecalFaceLayout& OutLayout)
{
	OutLayout = FMD_ScrapeDecalFaceLayout();

	const bool bIsSupportedFace =
		Face == EMD_ScrapeSurfaceFace::Front
		|| Face == EMD_ScrapeSurfaceFace::Back;
	FVector HorizontalAxis;
	FVector FrontNormal;
	FVector VerticalAxis;
	if (!bIsSupportedFace
		|| !FMD_ScrapeSurfaceGeometry::TryGetSurfaceLocalAxes(
			SurfaceNormalAxis,
			HorizontalAxis,
			FrontNormal,
			VerticalAxis)
		|| !FMath::IsFinite(SurfaceSizeCm.X)
		|| !FMath::IsFinite(SurfaceSizeCm.Y)
		|| SurfaceSizeCm.X <= UE_SMALL_NUMBER
		|| SurfaceSizeCm.Y <= UE_SMALL_NUMBER
		|| MeshLocalBoundsMinimum.ContainsNaN()
		|| MeshLocalBoundsMaximum.ContainsNaN()
		|| MeshAbsoluteScale.ContainsNaN()
		|| !FMath::IsFinite(RequestedInwardDepthCm)
		|| RequestedInwardDepthCm <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(RequestedOutwardMarginCm)
		|| RequestedOutwardMarginCm < 0.0f)
	{
		return false;
	}

	const FVector LocalBoundsSize =
		MeshLocalBoundsMaximum - MeshLocalBoundsMinimum;
	const double LocalThickness = FVector::DotProduct(
		LocalBoundsSize,
		FrontNormal);
	const double NormalScale = FMath::Abs(FVector::DotProduct(
		MeshAbsoluteScale,
		FrontNormal));
	if (!FMath::IsFinite(LocalThickness)
		|| LocalThickness <= static_cast<double>(UE_SMALL_NUMBER)
		|| !FMath::IsFinite(NormalScale)
		|| NormalScale <= static_cast<double>(UE_SMALL_NUMBER))
	{
		return false;
	}

	// 상자가 벽 뒤 공간까지 뚫고 나가지 않게 하는 마지막 안전선이다.
	// 앞/뒤 상자가 서로 겹치지 않도록 두께를 나누는 일은 호출부가 미리 처리한다.
	const double WorldThickness = LocalThickness
		* NormalScale;
	const double MaximumInwardDepth = WorldThickness;
	const double SafeInwardDepth = FMath::Min(
		static_cast<double>(RequestedInwardDepthCm),
		MaximumInwardDepth);
	if (!FMath::IsFinite(SafeInwardDepth)
		|| SafeInwardDepth <= static_cast<double>(UE_SMALL_NUMBER))
	{
		return false;
	}

	// 표면이 투영 상자 안에 확실히 들어오면서도 벽 앞 공간을 침범하지 않도록 제한한다.
	const double SafeOutwardMargin = FMath::Min(
		static_cast<double>(RequestedOutwardMarginCm),
		SafeInwardDepth * 0.5);

	// 상자는 [표면 - 안쪽 깊이, 표면 + 바깥 여유] 구간만 덮는다.
	const double HalfExtent = (SafeInwardDepth + SafeOutwardMargin) * 0.5;
	const double LocalCenterOffset =
		(SafeOutwardMargin - HalfExtent) / NormalScale;
	const FVector BoundsCenter =
		(MeshLocalBoundsMinimum + MeshLocalBoundsMaximum) * 0.5f;

	const bool bIsFront = Face == EMD_ScrapeSurfaceFace::Front;
	const double SurfaceCoordinate = FVector::DotProduct(
		bIsFront ? MeshLocalBoundsMaximum : MeshLocalBoundsMinimum,
		FrontNormal);
	const double CenterOffsetCoordinate = bIsFront
		? LocalCenterOffset
		: -LocalCenterOffset;
	OutLayout.RelativeLocation = BoundsCenter + FrontNormal
		* (SurfaceCoordinate + CenterOffsetCoordinate
			- FVector::DotProduct(BoundsCenter, FrontNormal));

	const FVector ProjectionDirection = bIsFront
		? -FrontNormal
		: FrontNormal;
	OutLayout.RelativeRotation = FRotationMatrix::MakeFromXY(
		ProjectionDirection,
		VerticalAxis).ToQuat();
	OutLayout.DecalSize = FVector(
		HalfExtent,
		static_cast<double>(SurfaceSizeCm.Y) * 0.5,
		static_cast<double>(SurfaceSizeCm.X) * 0.5);
	OutLayout.AppliedInwardDepthCm = SafeInwardDepth;
	OutLayout.MaskVOffset = bIsFront ? 0.0f : 0.5f;
	OutLayout.MaskUFlip = bIsFront ? 1.0f : 0.0f;
	return true;
}
