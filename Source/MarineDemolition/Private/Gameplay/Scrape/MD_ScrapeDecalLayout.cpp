#include "Gameplay/Scrape/MD_ScrapeDecalLayout.h"

#include "Gameplay/Scrape/MD_ScrapeMaskAtlas.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceGeometry.h"
#include "Math/RotationMatrix.h"

double FMD_ScrapeDecalLayout::ClampEdgeInsetCm(
	const FVector2D& SurfaceSizeCm,
	const float RequestedEdgeInsetCm)
{
	if (!FMath::IsFinite(RequestedEdgeInsetCm)
		|| RequestedEdgeInsetCm <= 0.0f
		|| !FMath::IsFinite(SurfaceSizeCm.X)
		|| !FMath::IsFinite(SurfaceSizeCm.Y)
		|| SurfaceSizeCm.X <= UE_SMALL_NUMBER
		|| SurfaceSizeCm.Y <= UE_SMALL_NUMBER)
	{
		return 0.0;
	}

	// 짧은 축을 기준으로 잘라야 좁은 벽에서도 상자가 사라지지 않는다.
	const double MaximumInsetCm =
		FMath::Min(SurfaceSizeCm.X, SurfaceSizeCm.Y)
		* MaximumEdgeInsetRatio;
	return FMath::Min(
		static_cast<double>(RequestedEdgeInsetCm),
		MaximumInsetCm);
}

int32 FMD_ScrapeDecalLayout::CalculateHorizontalPatchCount(
	const double SurfaceWidthCm,
	const double TargetPatchWidthCm,
	const int32 MaximumPatchCount)
{
	if (!FMath::IsFinite(SurfaceWidthCm)
		|| !FMath::IsFinite(TargetPatchWidthCm)
		|| SurfaceWidthCm <= static_cast<double>(UE_SMALL_NUMBER)
		|| TargetPatchWidthCm <= static_cast<double>(UE_SMALL_NUMBER)
		|| MaximumPatchCount <= 0)
	{
		return 0;
	}

	return FMath::Clamp(
		FMath::CeilToInt(SurfaceWidthCm / TargetPatchWidthCm),
		1,
		MaximumPatchCount);
}

bool FMD_ScrapeDecalLayout::TryCalculateHorizontalPatchLayout(
	const FMD_ScrapeDecalFaceLayout& FaceLayout,
	const EMD_ScrapeSurfaceNormalAxis SurfaceNormalAxis,
	const FVector& MeshAbsoluteScale,
	const int32 PatchIndex,
	const int32 PatchCount,
	FMD_ScrapeDecalFaceLayout& OutPatchLayout)
{
	OutPatchLayout = FMD_ScrapeDecalFaceLayout();

	FVector HorizontalAxis;
	FVector FrontNormal;
	FVector VerticalAxis;
	if (PatchCount <= 0
		|| PatchIndex < 0
		|| PatchIndex >= PatchCount
		|| FaceLayout.RelativeLocation.ContainsNaN()
		|| FaceLayout.RelativeRotation.ContainsNaN()
		|| FaceLayout.DecalSize.ContainsNaN()
		|| FaceLayout.DecalSize.Z <= UE_SMALL_NUMBER
		|| MeshAbsoluteScale.ContainsNaN()
		|| !FMD_ScrapeSurfaceGeometry::TryGetSurfaceLocalAxes(
			SurfaceNormalAxis,
			HorizontalAxis,
			FrontNormal,
			VerticalAxis))
	{
		return false;
	}

	const double HorizontalScale = FMath::Abs(FVector::DotProduct(
		MeshAbsoluteScale.GetAbs(),
		HorizontalAxis));
	const double UsableSurfaceWidthCm = FaceLayout.DecalSize.Z * 2.0;
	if (!FMath::IsFinite(HorizontalScale)
		|| HorizontalScale <= static_cast<double>(UE_SMALL_NUMBER)
		|| !FMath::IsFinite(UsableSurfaceWidthCm)
		|| UsableSurfaceWidthCm <= static_cast<double>(UE_SMALL_NUMBER))
	{
		return false;
	}

	const double PatchWidthCm = UsableSurfaceWidthCm
		/ static_cast<double>(PatchCount);
	const double PatchCenterOffsetCm = -UsableSurfaceWidthCm * 0.5
		+ (static_cast<double>(PatchIndex) + 0.5) * PatchWidthCm;

	OutPatchLayout = FaceLayout;
	OutPatchLayout.RelativeLocation += HorizontalAxis
		* (PatchCenterOffsetCm / HorizontalScale);
	OutPatchLayout.DecalSize.Z = PatchWidthCm * 0.5;
	OutPatchLayout.PatchUVScaleU = 1.0f
		/ static_cast<float>(PatchCount);
	OutPatchLayout.PatchUVOffsetU = static_cast<float>(PatchIndex)
		* OutPatchLayout.PatchUVScaleU;
	return true;
}

bool FMD_ScrapeDecalLayout::TryCalculateCylindricalPatchLayout(
	const EMD_ScrapeSurfaceFace Face,
	const FTransform& SurfaceTransform,
	const FVector& SurfaceCenterLocal,
	const FVector2D& SurfaceSizeCm,
	const FMD_ScrapeCylindricalSurfaceFrame& CurveFrame,
	const float RequestedInwardDepthCm,
	const float RequestedOutwardMarginCm,
	const float RequestedEdgeInsetCm,
	const int32 PatchIndex,
	const int32 PatchCount,
	FMD_ScrapeDecalFaceLayout& OutPatchLayout)
{
	OutPatchLayout = FMD_ScrapeDecalFaceLayout();

	const bool bIsFront = Face == EMD_ScrapeSurfaceFace::Front;
	const bool bIsBack = Face == EMD_ScrapeSurfaceFace::Back;
	if ((!bIsFront && !bIsBack)
		|| PatchCount <= 0
		|| PatchIndex < 0
		|| PatchIndex >= PatchCount
		|| SurfaceTransform.ContainsNaN()
		|| SurfaceCenterLocal.ContainsNaN()
		|| !FMath::IsFinite(SurfaceSizeCm.X)
		|| !FMath::IsFinite(SurfaceSizeCm.Y)
		|| SurfaceSizeCm.X <= UE_SMALL_NUMBER
		|| SurfaceSizeCm.Y <= UE_SMALL_NUMBER
		|| CurveFrame.CenterWorld.ContainsNaN()
		|| CurveFrame.AxisWorld.ContainsNaN()
		|| CurveFrame.StartDirectionWorld.ContainsNaN()
		|| CurveFrame.TangentDirectionWorld.ContainsNaN()
		|| CurveFrame.AxisWorld.IsNearlyZero()
		|| CurveFrame.StartDirectionWorld.IsNearlyZero()
		|| CurveFrame.TangentDirectionWorld.IsNearlyZero()
		|| !FMath::IsFinite(CurveFrame.SweepRadians)
		|| FMath::Abs(CurveFrame.SweepRadians) <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(CurveFrame.RadiusCm)
		|| CurveFrame.RadiusCm <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(CurveFrame.ThicknessCm)
		|| CurveFrame.ThicknessCm < 0.0
		|| !FMath::IsFinite(RequestedInwardDepthCm)
		|| RequestedInwardDepthCm <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(RequestedOutwardMarginCm)
		|| RequestedOutwardMarginCm < 0.0f
		|| !FMath::IsFinite(RequestedEdgeInsetCm)
		|| RequestedEdgeInsetCm < 0.0f)
	{
		return false;
	}

	const bool bFaceIsOuter =
		(bIsFront && CurveFrame.bFrontIsOuter)
		|| (bIsBack && !CurveFrame.bFrontIsOuter);
	const double FaceRadiusCm = CurveFrame.RadiusCm
		+ (bFaceIsOuter ? 0.5 : -0.5) * CurveFrame.ThicknessCm;
	if (!FMath::IsFinite(FaceRadiusCm)
		|| FaceRadiusCm <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const double AppliedEdgeInsetCm = ClampEdgeInsetCm(
		SurfaceSizeCm,
		RequestedEdgeInsetCm);
	const double MaximumAngularInset = FMath::Abs(
		CurveFrame.SweepRadians) * 0.25
		/ static_cast<double>(PatchCount);
	const double AngularInset = FMath::Min(
		AppliedEdgeInsetCm / FaceRadiusCm,
		MaximumAngularInset);
	const double SweepDirection = FMath::Sign(CurveFrame.SweepRadians);
	double PatchStartAngle = CurveFrame.SweepRadians
		* static_cast<double>(PatchIndex)
		/ static_cast<double>(PatchCount);
	double PatchEndAngle = CurveFrame.SweepRadians
		* static_cast<double>(PatchIndex + 1)
		/ static_cast<double>(PatchCount);
	if (PatchIndex == 0)
	{
		PatchStartAngle += SweepDirection * AngularInset;
	}
	if (PatchIndex == PatchCount - 1)
	{
		PatchEndAngle -= SweepDirection * AngularInset;
	}

	const double PatchCenterAngle =
		(PatchStartAngle + PatchEndAngle) * 0.5;
	const double PatchHalfAngle =
		FMath::Abs(PatchEndAngle - PatchStartAngle) * 0.5;
	if (!FMath::IsFinite(PatchCenterAngle)
		|| !FMath::IsFinite(PatchHalfAngle)
		|| PatchHalfAngle <= UE_SMALL_NUMBER
		|| PatchHalfAngle >= UE_HALF_PI)
	{
		return false;
	}

	const FVector RadialDirection = (
		CurveFrame.StartDirectionWorld * FMath::Cos(PatchCenterAngle)
		+ CurveFrame.TangentDirectionWorld
			* FMath::Sin(PatchCenterAngle)).GetSafeNormal();
	const FVector OutwardNormal = bFaceIsOuter
		? RadialDirection
		: -RadialDirection;
	const FVector ProjectionDirection = -OutwardNormal;
	if (RadialDirection.IsNearlyZero()
		|| ProjectionDirection.IsNearlyZero())
	{
		return false;
	}

	// A tangent box needs the sagitta on the concave side of its center plane.
	// Outer faces bend into the projection depth; inner faces bend into the
	// exterior margin. The material's radial gates keep that larger box on the
	// intended shell and face.
	const double PatchSagittaCm = FaceRadiusCm
		* (1.0 - FMath::Cos(PatchHalfAngle));
	double InwardDepthCm = RequestedInwardDepthCm;
	double OutwardMarginCm = RequestedOutwardMarginCm;
	if (bFaceIsOuter)
	{
		InwardDepthCm += PatchSagittaCm;
	}
	else
	{
		OutwardMarginCm += PatchSagittaCm;
	}
	const double ProjectionHalfExtentCm =
		(InwardDepthCm + OutwardMarginCm) * 0.5;
	const double TangentHalfWidthCm =
		FaceRadiusCm * FMath::Sin(PatchHalfAngle);
	const double VerticalHalfHeightCm =
		SurfaceSizeCm.Y * 0.5 - AppliedEdgeInsetCm;
	if (!FMath::IsFinite(PatchSagittaCm)
		|| !FMath::IsFinite(ProjectionHalfExtentCm)
		|| !FMath::IsFinite(TangentHalfWidthCm)
		|| !FMath::IsFinite(VerticalHalfHeightCm)
		|| ProjectionHalfExtentCm <= UE_SMALL_NUMBER
		|| TangentHalfWidthCm <= UE_SMALL_NUMBER
		|| VerticalHalfHeightCm <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const FVector SurfaceOriginWorld =
		SurfaceTransform.TransformPosition(SurfaceCenterLocal);
	const FVector AxisOriginWorld = CurveFrame.CenterWorld
		+ CurveFrame.AxisWorld * FVector::DotProduct(
			SurfaceOriginWorld - CurveFrame.CenterWorld,
			CurveFrame.AxisWorld);
	const FVector PatchSurfaceCenterWorld = AxisOriginWorld
		+ RadialDirection * FaceRadiusCm;
	const double VolumeCenterFromSurfaceCm =
		(OutwardMarginCm - InwardDepthCm) * 0.5;
	const FVector VolumeCenterWorld = PatchSurfaceCenterWorld
		+ OutwardNormal * VolumeCenterFromSurfaceCm;
	const FQuat WorldRotation = FRotationMatrix::MakeFromXY(
		ProjectionDirection,
		CurveFrame.AxisWorld).ToQuat();

	OutPatchLayout.RelativeLocation =
		SurfaceTransform.InverseTransformPosition(VolumeCenterWorld);
	OutPatchLayout.RelativeRotation = (
		SurfaceTransform.GetRotation().Inverse()
		* WorldRotation).GetNormalized();
	OutPatchLayout.DecalSize = FVector(
		ProjectionHalfExtentCm,
		VerticalHalfHeightCm,
		TangentHalfWidthCm);
	OutPatchLayout.MaskVOffset = bIsFront ? 0.0f : 0.5f;
	OutPatchLayout.MaskUFlip = bIsFront ? 1.0f : 0.0f;
	OutPatchLayout.PatchUVScaleU =
		1.0f / static_cast<float>(PatchCount);
	OutPatchLayout.PatchUVOffsetU =
		static_cast<float>(PatchIndex)
		* OutPatchLayout.PatchUVScaleU;
	OutPatchLayout.AppliedInwardDepthCm = InwardDepthCm;
	OutPatchLayout.AppliedOutwardMarginCm = OutwardMarginCm;
	OutPatchLayout.AppliedEdgeInsetCm = AppliedEdgeInsetCm;
	return !OutPatchLayout.RelativeLocation.ContainsNaN()
		&& !OutPatchLayout.RelativeRotation.ContainsNaN()
		&& !OutPatchLayout.DecalSize.ContainsNaN();
}

bool FMD_ScrapeDecalLayout::TryCalculateMaskHalfTexelUV(
	const FIntPoint& AtlasDimensions,
	const FVector2D& SurfaceSizeCm,
	const float RequestedEdgeInsetCm,
	FVector2D& OutMaskHalfTexelUV)
{
	if (!FMD_ScrapeMaskAtlas::TryCalculateHalfTexelUV(
			AtlasDimensions,
			OutMaskHalfTexelUV))
	{
		return false;
	}

	const double AppliedEdgeInsetCm = ClampEdgeInsetCm(
		SurfaceSizeCm,
		RequestedEdgeInsetCm);
	if (AppliedEdgeInsetCm <= 0.0)
	{
		return true;
	}

	// U는 면의 가로 전체가 아틀라스 U 전체에 대응한다.
	// V는 한 면이 아틀라스의 절반(MaskVScale)만 쓰므로 비율에 0.5를 곱한다.
	// 반 텍셀보다 작아지면 Bilinear이 앞뒤 경계를 넘으므로 더 큰 쪽을 쓴다.
	OutMaskHalfTexelUV.X = FMath::Max(
		OutMaskHalfTexelUV.X,
		AppliedEdgeInsetCm / SurfaceSizeCm.X);
	OutMaskHalfTexelUV.Y = FMath::Max(
		OutMaskHalfTexelUV.Y,
		0.5 * AppliedEdgeInsetCm / SurfaceSizeCm.Y);
	return FMath::IsFinite(OutMaskHalfTexelUV.X)
		&& FMath::IsFinite(OutMaskHalfTexelUV.Y);
}

bool FMD_ScrapeDecalLayout::TryCalculateFaceLayout(
	const EMD_ScrapeSurfaceFace Face,
	const EMD_ScrapeSurfaceNormalAxis SurfaceNormalAxis,
	const FVector2D& SurfaceSizeCm,
	const FVector& MeshLocalBoundsMinimum,
	const FVector& MeshLocalBoundsMaximum,
	const FVector& MeshAbsoluteScale,
	const float RequestedInwardDepthCm,
	const float RequestedOutwardMarginCm,
	const float RequestedEdgeInsetCm,
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
		|| RequestedOutwardMarginCm < 0.0f
		|| !FMath::IsFinite(RequestedEdgeInsetCm)
		|| RequestedEdgeInsetCm < 0.0f)
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
	// 상자 옆면을 표면 테두리에서 물려 두께 면·바닥처럼 투영축과 평행한 면이
	// 경계에 걸리지 않게 한다. 마스크 UV는 호출부가 같은 비율로 좁혀 맞춘다.
	const double AppliedEdgeInsetCm = ClampEdgeInsetCm(
		SurfaceSizeCm,
		RequestedEdgeInsetCm);
	OutLayout.DecalSize = FVector(
		HalfExtent,
		static_cast<double>(SurfaceSizeCm.Y) * 0.5 - AppliedEdgeInsetCm,
		static_cast<double>(SurfaceSizeCm.X) * 0.5 - AppliedEdgeInsetCm);
	OutLayout.AppliedInwardDepthCm = SafeInwardDepth;
	OutLayout.AppliedOutwardMarginCm = SafeOutwardMargin;
	OutLayout.AppliedEdgeInsetCm = AppliedEdgeInsetCm;
	OutLayout.MaskVOffset = bIsFront ? 0.0f : 0.5f;
	OutLayout.MaskUFlip = bIsFront ? 1.0f : 0.0f;
	return true;
}
