#include "Gameplay/Scrape/MD_ScrapeSurfaceGeometry.h"

namespace
{
	constexpr double MaximumCurveSweepRadians = UE_PI;
	constexpr double MaximumRadialScaleDifferenceRatio = 0.01;

	bool HasCylindricalFrame(const FMD_ScrapeSurfaceMapping& SurfaceMapping)
	{
		return SurfaceMapping.Mode
			== EMD_ScrapeSurfaceMappingMode::CylindricalArc
			|| SurfaceMapping.Mode
				== EMD_ScrapeSurfaceMappingMode::CurvedFrontPlanarBack;
	}

	bool UsesCylindricalAddress(
		const FMD_ScrapeSurfaceMapping& SurfaceMapping,
		const EMD_ScrapeSurfaceFace SurfaceFace)
	{
		return SurfaceMapping.Mode
			== EMD_ScrapeSurfaceMappingMode::CylindricalArc
			|| (SurfaceMapping.Mode
					== EMD_ScrapeSurfaceMappingMode::CurvedFrontPlanarBack
				&& SurfaceFace == EMD_ScrapeSurfaceFace::Front);
	}
}

bool FMD_ScrapeSurfaceGeometry::TryResolveSurfaceNormalAxis(
	const EMD_ScrapeSurfaceNormalAxis RequestedNormalAxis,
	const FVector& SurfaceBoundsMinimumLocal,
	const FVector& SurfaceBoundsMaximumLocal,
	const FVector& SurfaceAbsoluteScale,
	EMD_ScrapeSurfaceNormalAxis& OutResolvedNormalAxis)
{
	OutResolvedNormalAxis = EMD_ScrapeSurfaceNormalAxis::Y;
	const FVector LocalSize =
		SurfaceBoundsMaximumLocal - SurfaceBoundsMinimumLocal;
	const FVector AbsoluteScale = SurfaceAbsoluteScale.GetAbs();
	const FVector WorldSize = LocalSize * AbsoluteScale;
	if (SurfaceBoundsMinimumLocal.ContainsNaN()
		|| SurfaceBoundsMaximumLocal.ContainsNaN()
		|| SurfaceAbsoluteScale.ContainsNaN()
		|| LocalSize.X <= UE_SMALL_NUMBER
		|| LocalSize.Y <= UE_SMALL_NUMBER
		|| LocalSize.Z <= UE_SMALL_NUMBER
		|| AbsoluteScale.X <= UE_SMALL_NUMBER
		|| AbsoluteScale.Y <= UE_SMALL_NUMBER
		|| AbsoluteScale.Z <= UE_SMALL_NUMBER
		|| WorldSize.ContainsNaN())
	{
		return false;
	}

	if (RequestedNormalAxis == EMD_ScrapeSurfaceNormalAxis::X
		|| RequestedNormalAxis == EMD_ScrapeSurfaceNormalAxis::Y
		|| RequestedNormalAxis == EMD_ScrapeSurfaceNormalAxis::Z)
	{
		OutResolvedNormalAxis = RequestedNormalAxis;
		return true;
	}
	if (RequestedNormalAxis != EMD_ScrapeSurfaceNormalAxis::Auto)
	{
		return false;
	}

	auto SelectNarrowerHorizontalAxis = [](const FVector& Size)
	{
		return Size.X < Size.Y - UE_KINDA_SMALL_NUMBER
			? EMD_ScrapeSurfaceNormalAxis::X
			: EMD_ScrapeSurfaceNormalAxis::Y;
	};
	auto TrySelectClearlyThinAxis = [&SelectNarrowerHorizontalAxis](
		const FVector& Size,
		EMD_ScrapeSurfaceNormalAxis& OutAxis)
	{
		// 바닥·천장은 Z가 두 가로축 모두에 비해 확실히 얇을 때만 인정한다.
		// 코너/T자/십자 벽은 전체 Bounds의 Z가 우연히 가장 짧을 수 있으므로,
		// 일반적인 "세 축 중 최솟값" 규칙으로는 수직 Decal이 바닥에 투영된다.
		constexpr double MaximumHorizontalSurfaceThicknessRatio = 0.5;
		const double SmallerHorizontalSize = FMath::Min(Size.X, Size.Y);
		if (Size.Z <= SmallerHorizontalSize
			* MaximumHorizontalSurfaceThicknessRatio)
		{
			OutAxis = EMD_ScrapeSurfaceNormalAxis::Z;
			return true;
		}

		// 수직 판재는 더 얇은 가로축을 앞/뒤 축으로 사용한다.
		constexpr double MaximumVerticalSurfaceThicknessRatio = 0.75;
		const EMD_ScrapeSurfaceNormalAxis HorizontalAxis =
			SelectNarrowerHorizontalAxis(Size);
		const double HorizontalThickness =
			HorizontalAxis == EMD_ScrapeSurfaceNormalAxis::X
				? static_cast<double>(Size.X)
				: static_cast<double>(Size.Y);
		const double OtherHorizontalSize =
			HorizontalAxis == EMD_ScrapeSurfaceNormalAxis::X
				? static_cast<double>(Size.Y)
				: static_cast<double>(Size.X);
		if (HorizontalThickness <= OtherHorizontalSize
			* MaximumVerticalSurfaceThicknessRatio)
		{
			OutAxis = HorizontalAxis;
			return true;
		}

		return false;
	};

	// 이미 판재로 모델링된 메시는 Blueprint의 기존 비균일 Scale보다 원본 형상을
	// 우선하고, Cube처럼 원본이 모호한 경우에만 월드 스케일로 다시 판정한다.
	if (TrySelectClearlyThinAxis(LocalSize, OutResolvedNormalAxis)
		|| TrySelectClearlyThinAxis(WorldSize, OutResolvedNormalAxis))
	{
		return true;
	}

	// 세 축이 비슷한 조립형 메시에는 명확한 두께 축이 없다. 이때 Z를 고르면 벽용
	// Decal이 바닥/천장 방향으로 투영되므로, 기존 기본값과 같은 수직면을 택한다.
	OutResolvedNormalAxis = SelectNarrowerHorizontalAxis(WorldSize);
	return true;
}

bool FMD_ScrapeSurfaceGeometry::TryCalculateSurfaceSize(
	const EMD_ScrapeSurfaceNormalAxis SurfaceNormalAxis,
	const FVector& SurfaceBoundsMinimumLocal,
	const FVector& SurfaceBoundsMaximumLocal,
	const FVector& SurfaceAbsoluteScale,
	FVector2D& OutSurfaceSizeCm)
{
	OutSurfaceSizeCm = FVector2D::ZeroVector;
	const FVector LocalSize =
		SurfaceBoundsMaximumLocal - SurfaceBoundsMinimumLocal;
	const FVector AbsoluteScale = SurfaceAbsoluteScale.GetAbs();
	FVector HorizontalAxis;
	FVector FrontNormal;
	FVector VerticalAxis;
	if (SurfaceBoundsMinimumLocal.ContainsNaN()
		|| SurfaceBoundsMaximumLocal.ContainsNaN()
		|| SurfaceAbsoluteScale.ContainsNaN()
		|| !TryGetSurfaceLocalAxes(
			SurfaceNormalAxis,
			HorizontalAxis,
			FrontNormal,
			VerticalAxis))
	{
		return false;
	}

	OutSurfaceSizeCm = FVector2D(
		FMath::Abs(FVector::DotProduct(LocalSize, HorizontalAxis))
			* FMath::Abs(FVector::DotProduct(AbsoluteScale, HorizontalAxis)),
		FMath::Abs(FVector::DotProduct(LocalSize, VerticalAxis))
			* FMath::Abs(FVector::DotProduct(AbsoluteScale, VerticalAxis)));
	return FMath::IsFinite(OutSurfaceSizeCm.X)
		&& FMath::IsFinite(OutSurfaceSizeCm.Y)
		&& OutSurfaceSizeCm.X > UE_SMALL_NUMBER
		&& OutSurfaceSizeCm.Y > UE_SMALL_NUMBER;
}

bool FMD_ScrapeSurfaceGeometry::TryCalculateMappedSurfaceSize(
	const FTransform& SurfaceTransform,
	const FMD_ScrapeSurfaceMapping& SurfaceMapping,
	const FVector& SurfaceBoundsMinimumLocal,
	const FVector& SurfaceBoundsMaximumLocal,
	FVector2D& OutSurfaceSizeCm)
{
	OutSurfaceSizeCm = FVector2D::ZeroVector;
	FMD_ScrapeCylindricalSurfaceFrame Frame;
	if (!HasCylindricalFrame(SurfaceMapping)
		|| SurfaceBoundsMinimumLocal.ContainsNaN()
		|| SurfaceBoundsMaximumLocal.ContainsNaN()
		|| !TryResolveCylindricalSurfaceFrame(
			SurfaceTransform,
			SurfaceMapping,
			Frame))
	{
		return false;
	}

	double MinimumAxisCoordinate = TNumericLimits<double>::Max();
	double MaximumAxisCoordinate = TNumericLimits<double>::Lowest();
	for (int32 XIndex = 0; XIndex < 2; ++XIndex)
	{
		for (int32 YIndex = 0; YIndex < 2; ++YIndex)
		{
			for (int32 ZIndex = 0; ZIndex < 2; ++ZIndex)
			{
				const FVector CornerLocal(
					XIndex == 0
						? SurfaceBoundsMinimumLocal.X
						: SurfaceBoundsMaximumLocal.X,
					YIndex == 0
						? SurfaceBoundsMinimumLocal.Y
						: SurfaceBoundsMaximumLocal.Y,
					ZIndex == 0
						? SurfaceBoundsMinimumLocal.Z
						: SurfaceBoundsMaximumLocal.Z);
				const double AxisCoordinate = FVector::DotProduct(
					SurfaceTransform.TransformPosition(CornerLocal),
					Frame.AxisWorld);
				if (!FMath::IsFinite(AxisCoordinate))
				{
					return false;
				}
				MinimumAxisCoordinate = FMath::Min(
					MinimumAxisCoordinate,
					AxisCoordinate);
				MaximumAxisCoordinate = FMath::Max(
					MaximumAxisCoordinate,
					AxisCoordinate);
			}
		}
	}

	OutSurfaceSizeCm = FVector2D(
		FMath::Abs(Frame.SweepRadians) * Frame.RadiusCm,
		MaximumAxisCoordinate - MinimumAxisCoordinate);
	return FMath::IsFinite(OutSurfaceSizeCm.X)
		&& FMath::IsFinite(OutSurfaceSizeCm.Y)
		&& OutSurfaceSizeCm.X > UE_SMALL_NUMBER
		&& OutSurfaceSizeCm.Y > UE_SMALL_NUMBER;
}

bool FMD_ScrapeSurfaceGeometry::TryGetSurfaceLocalAxes(
	const EMD_ScrapeSurfaceNormalAxis SurfaceNormalAxis,
	FVector& OutHorizontalAxis,
	FVector& OutFrontNormal,
	FVector& OutVerticalAxis)
{
	switch (SurfaceNormalAxis)
	{
	case EMD_ScrapeSurfaceNormalAxis::X:
		OutHorizontalAxis = -FVector::YAxisVector;
		OutFrontNormal = FVector::XAxisVector;
		OutVerticalAxis = FVector::ZAxisVector;
		return true;

	case EMD_ScrapeSurfaceNormalAxis::Y:
		OutHorizontalAxis = FVector::XAxisVector;
		OutFrontNormal = FVector::YAxisVector;
		OutVerticalAxis = FVector::ZAxisVector;
		return true;

	case EMD_ScrapeSurfaceNormalAxis::Z:
		OutHorizontalAxis = FVector::XAxisVector;
		OutFrontNormal = FVector::ZAxisVector;
		OutVerticalAxis = -FVector::YAxisVector;
		return true;

	default:
		OutHorizontalAxis = FVector::ZeroVector;
		OutFrontNormal = FVector::ZeroVector;
		OutVerticalAxis = FVector::ZeroVector;
		return false;
	}
}

bool FMD_ScrapeSurfaceGeometry::TryResolveSurfaceFaceFromImpactNormal(
	const FVector& WorldPosition,
	const FVector& ImpactNormal,
	const FTransform& SurfaceTransform,
	const EMD_ScrapeSurfaceNormalAxis SurfaceNormalAxis,
	const FMD_ScrapeSurfaceMapping& SurfaceMapping,
	const float MinimumFaceNormalDot,
	EMD_ScrapeSurfaceFace& OutSurfaceFace)
{
	OutSurfaceFace = EMD_ScrapeSurfaceFace::Front;
	if (WorldPosition.ContainsNaN()
		|| ImpactNormal.ContainsNaN()
		|| !FMath::IsFinite(MinimumFaceNormalDot)
		|| MinimumFaceNormalDot <= 0.0f
		|| MinimumFaceNormalDot > 1.0f)
	{
		return false;
	}

	if (SurfaceMapping.Mode
		== EMD_ScrapeSurfaceMappingMode::CurvedFrontPlanarBack)
	{
		FVector SplitOriginWorld;
		FVector FrontHalfNormalWorld;
		if (!TryResolveCurvedFrontPlanarBackSplitPlane(
				SurfaceTransform,
				SurfaceMapping,
				SplitOriginWorld,
				FrontHalfNormalWorld))
		{
			return false;
		}

		OutSurfaceFace = FVector::DotProduct(
			WorldPosition - SplitOriginWorld,
			FrontHalfNormalWorld) >= 0.0
			? EMD_ScrapeSurfaceFace::Front
			: EMD_ScrapeSurfaceFace::Back;
		return true;
	}

	const FVector SafeImpactNormal = ImpactNormal.GetSafeNormal();
	if (HasCylindricalFrame(SurfaceMapping))
	{
		FMD_ScrapeCylindricalSurfaceFrame Frame;
		if (SafeImpactNormal.IsNearlyZero()
			|| !TryResolveCylindricalSurfaceFrame(
				SurfaceTransform,
				SurfaceMapping,
				Frame))
		{
			return false;
		}

		const FVector CurveDelta = WorldPosition - Frame.CenterWorld;
		const FVector PlanarRadial = CurveDelta
			- Frame.AxisWorld * FVector::DotProduct(
				CurveDelta,
				Frame.AxisWorld);
		const FVector RadialDirection = PlanarRadial.GetSafeNormal();
		if (RadialDirection.IsNearlyZero())
		{
			return false;
		}

		const double SignedRadialNormalDot = FVector::DotProduct(
			SafeImpactNormal,
			RadialDirection);
		if (FMath::Abs(SignedRadialNormalDot) < MinimumFaceNormalDot)
		{
			return false;
		}

		const bool bHitOuterFace = SignedRadialNormalDot >= 0.0;
		OutSurfaceFace = bHitOuterFace == Frame.bFrontIsOuter
			? EMD_ScrapeSurfaceFace::Front
			: EMD_ScrapeSurfaceFace::Back;
		return true;
	}

	FVector HorizontalAxis;
	FVector FrontNormal;
	FVector VerticalAxis;
	if (SafeImpactNormal.IsNearlyZero()
		|| !TryGetSurfaceWorldAxes(
			SurfaceTransform,
			SurfaceNormalAxis,
			HorizontalAxis,
			FrontNormal,
			VerticalAxis))
	{
		return false;
	}

	const float SignedFaceNormalDot = FVector::DotProduct(
		SafeImpactNormal,
		FrontNormal);
	if (FMath::Abs(SignedFaceNormalDot) < MinimumFaceNormalDot)
	{
		return false;
	}

	OutSurfaceFace = SignedFaceNormalDot >= 0.0f
		? EMD_ScrapeSurfaceFace::Front
		: EMD_ScrapeSurfaceFace::Back;
	return true;
}

bool FMD_ScrapeSurfaceGeometry::TryCalculateSurfaceWorldPosition(
	const FVector2D& SurfacePositionCm,
	const EMD_ScrapeSurfaceFace SurfaceFace,
	const FTransform& SurfaceTransform,
	const EMD_ScrapeSurfaceNormalAxis SurfaceNormalAxis,
	const FVector& SurfaceCenterLocal,
	const FVector& SurfaceBoundsMinimumLocal,
	const FVector& SurfaceBoundsMaximumLocal,
	const FMD_ScrapeSurfaceMapping& SurfaceMapping,
	const FVector2D& SurfaceSizeCm,
	FVector& OutWorldPosition)
{
	OutWorldPosition = FVector::ZeroVector;
	// MeshUV requires the baked triangle data; it has no planar fallback.
	if (SurfaceMapping.Mode == EMD_ScrapeSurfaceMappingMode::MeshUV) { return false; }

	if (!FMath::IsFinite(SurfacePositionCm.X)
		|| !FMath::IsFinite(SurfacePositionCm.Y)
		|| SurfaceCenterLocal.ContainsNaN()
		|| SurfaceBoundsMinimumLocal.ContainsNaN()
		|| SurfaceBoundsMaximumLocal.ContainsNaN())
	{
		return false;
	}

	if (UsesCylindricalAddress(SurfaceMapping, SurfaceFace))
	{
		FMD_ScrapeCylindricalSurfaceFrame Frame;
		if (!FMath::IsFinite(SurfaceSizeCm.X)
			|| !FMath::IsFinite(SurfaceSizeCm.Y)
			|| SurfaceSizeCm.X <= UE_SMALL_NUMBER
			|| SurfaceSizeCm.Y <= UE_SMALL_NUMBER
			|| !TryResolveCylindricalSurfaceFrame(
				SurfaceTransform,
				SurfaceMapping,
				Frame))
		{
			return false;
		}

		const double CanonicalU =
			SurfacePositionCm.X / SurfaceSizeCm.X + 0.5;
		const double CurveAngle = CanonicalU * Frame.SweepRadians;
		const FVector RadialDirection =
			Frame.StartDirectionWorld * FMath::Cos(CurveAngle)
			+ Frame.TangentDirectionWorld * FMath::Sin(CurveAngle);
		const bool bUseOuterRadius =
			(SurfaceFace == EMD_ScrapeSurfaceFace::Front
				&& Frame.bFrontIsOuter)
			|| (SurfaceFace == EMD_ScrapeSurfaceFace::Back
				&& !Frame.bFrontIsOuter);
		if (SurfaceFace != EMD_ScrapeSurfaceFace::Front
			&& SurfaceFace != EMD_ScrapeSurfaceFace::Back)
		{
			return false;
		}

		const double FaceRadiusCm = Frame.RadiusCm
			+ (bUseOuterRadius ? 0.5 : -0.5) * Frame.ThicknessCm;
		if (!FMath::IsFinite(FaceRadiusCm)
			|| FaceRadiusCm <= UE_SMALL_NUMBER)
		{
			return false;
		}

		const FVector SurfaceOriginWorld =
			SurfaceTransform.TransformPosition(SurfaceCenterLocal);
		const FVector AxisOriginWorld = Frame.CenterWorld
			+ Frame.AxisWorld * FVector::DotProduct(
				SurfaceOriginWorld - Frame.CenterWorld,
				Frame.AxisWorld);
		OutWorldPosition = AxisOriginWorld
			+ RadialDirection * FaceRadiusCm
			+ Frame.AxisWorld * SurfacePositionCm.Y;
		return !OutWorldPosition.ContainsNaN();
	}

	FVector HorizontalAxisLocal;
	FVector FrontNormalLocal;
	FVector VerticalAxisLocal;
	if (!TryGetSurfaceLocalAxes(
			SurfaceNormalAxis,
			HorizontalAxisLocal,
			FrontNormalLocal,
			VerticalAxisLocal))
	{
		return false;
	}
	const double MinimumNormalCoordinate = FVector::DotProduct(
		SurfaceBoundsMinimumLocal,
		FrontNormalLocal);
	const double MaximumNormalCoordinate = FVector::DotProduct(
		SurfaceBoundsMaximumLocal,
		FrontNormalLocal);
	if (!FMath::IsFinite(MinimumNormalCoordinate)
		|| !FMath::IsFinite(MaximumNormalCoordinate)
		|| MinimumNormalCoordinate > MaximumNormalCoordinate)
	{
		return false;
	}

	FVector SurfaceFaceCenterLocal = SurfaceCenterLocal;
	double TargetNormalCoordinate = 0.0;
	switch (SurfaceFace)
	{
	case EMD_ScrapeSurfaceFace::Front:
		TargetNormalCoordinate = MaximumNormalCoordinate;
		break;

	case EMD_ScrapeSurfaceFace::Back:
		TargetNormalCoordinate = MinimumNormalCoordinate;
		break;

	default:
		return false;
	}
	SurfaceFaceCenterLocal += FrontNormalLocal
		* (TargetNormalCoordinate - FVector::DotProduct(
			SurfaceFaceCenterLocal,
			FrontNormalLocal));

	FVector HorizontalAxis;
	FVector FrontNormal;
	FVector VerticalAxis;
	if (!TryGetSurfaceWorldAxes(
			SurfaceTransform,
			SurfaceNormalAxis,
			HorizontalAxis,
			FrontNormal,
			VerticalAxis))
	{
		return false;
	}

	const FVector SurfaceCenterWorld =
		SurfaceTransform.TransformPosition(SurfaceFaceCenterLocal);
	double HorizontalPositionCm = SurfacePositionCm.X;
	if (SurfaceMapping.Mode
		== EMD_ScrapeSurfaceMappingMode::CurvedFrontPlanarBack)
	{
		FVector2D PlanarSurfaceSizeCm = FVector2D::ZeroVector;
		if (!FMath::IsFinite(SurfaceSizeCm.X)
			|| SurfaceSizeCm.X <= UE_SMALL_NUMBER
			|| !TryCalculateSurfaceSize(
				SurfaceNormalAxis,
				SurfaceBoundsMinimumLocal,
				SurfaceBoundsMaximumLocal,
				SurfaceTransform.GetScale3D().GetAbs(),
				PlanarSurfaceSizeCm))
		{
			return false;
		}

		const double CanonicalU =
			SurfacePositionCm.X / SurfaceSizeCm.X + 0.5;
		HorizontalPositionCm =
			(CanonicalU - 0.5) * PlanarSurfaceSizeCm.X;
	}
	OutWorldPosition = SurfaceCenterWorld
		+ HorizontalAxis * HorizontalPositionCm
		+ VerticalAxis * SurfacePositionCm.Y;
	return !OutWorldPosition.ContainsNaN();
}

bool FMD_ScrapeSurfaceGeometry::TryProjectWorldPositionToSurface(
	const FVector& WorldPosition,
	const FTransform& SurfaceTransform,
	const EMD_ScrapeSurfaceNormalAxis SurfaceNormalAxis,
	const EMD_ScrapeSurfaceFace SurfaceFace,
	const FVector& SurfaceCenterLocal,
	const FVector& SurfaceBoundsMinimumLocal,
	const FVector& SurfaceBoundsMaximumLocal,
	const FMD_ScrapeSurfaceMapping& SurfaceMapping,
	const FVector2D& SurfaceSizeCm,
	FVector2D& OutSurfacePositionCm)
{
	OutSurfacePositionCm = FVector2D::ZeroVector;
	if (WorldPosition.ContainsNaN()
		|| SurfaceCenterLocal.ContainsNaN())
	{
		return false;
	}

	if (UsesCylindricalAddress(SurfaceMapping, SurfaceFace))
	{
		FMD_ScrapeCylindricalSurfaceFrame Frame;
		if (!FMath::IsFinite(SurfaceSizeCm.X)
			|| !FMath::IsFinite(SurfaceSizeCm.Y)
			|| SurfaceSizeCm.X <= UE_SMALL_NUMBER
			|| SurfaceSizeCm.Y <= UE_SMALL_NUMBER
			|| !TryResolveCylindricalSurfaceFrame(
				SurfaceTransform,
				SurfaceMapping,
				Frame))
		{
			return false;
		}

		const FVector CurveDelta = WorldPosition - Frame.CenterWorld;
		const double AxisDistance = FVector::DotProduct(
			CurveDelta,
			Frame.AxisWorld);
		const FVector RadialDirection = (
			CurveDelta - Frame.AxisWorld * AxisDistance).GetSafeNormal();
		if (RadialDirection.IsNearlyZero())
		{
			return false;
		}

		const double SinAngle = FVector::DotProduct(
			FVector::CrossProduct(
				Frame.StartDirectionWorld,
				RadialDirection),
			Frame.AxisWorld);
		const double CosAngle = FVector::DotProduct(
			Frame.StartDirectionWorld,
			RadialDirection);
		const double CurveAngle = FMath::Atan2(SinAngle, CosAngle);
		const double CanonicalU = CurveAngle / Frame.SweepRadians;
		if (!FMath::IsFinite(CanonicalU)
			|| CanonicalU < 0.0
			|| CanonicalU > 1.0)
		{
			// The material rejects CanonicalURaw outside 0..1. CPU must reject the
			// same point instead of collapsing a wide region onto the edge cell.
			return false;
		}
		const FVector SurfaceOriginWorld =
			SurfaceTransform.TransformPosition(SurfaceCenterLocal);
		OutSurfacePositionCm = FVector2D(
			(CanonicalU - 0.5) * SurfaceSizeCm.X,
			FVector::DotProduct(
				WorldPosition - SurfaceOriginWorld,
				Frame.AxisWorld));
		return FMath::IsFinite(OutSurfacePositionCm.X)
			&& FMath::IsFinite(OutSurfacePositionCm.Y);
	}

	if (SurfaceMapping.Mode == EMD_ScrapeSurfaceMappingMode::MeshUV)
	{
		// MeshUV의 주소는 히트한 삼각형의 UV 속성에만 존재한다. 월드 위치
		// 하나로 되돌릴 방법이 없으므로 평면 공식으로 대신 채우지 않고 실패한다.
		// 조용히 근사하면 셀 주소와 렌더링 주소가 갈라지고, 그게 이 모드를
		// 도입한 원인이었던 결함 그 자체다. 호출부는
		// UMD_ScrapeSurfaceComponent::TryGetScrapeSurfacePositionFromHit를 써야 한다.
		return false;
	}

	FVector HorizontalAxis;
	FVector FrontNormal;
	FVector VerticalAxis;
	if (!TryGetSurfaceWorldAxes(
			SurfaceTransform,
			SurfaceNormalAxis,
			HorizontalAxis,
			FrontNormal,
			VerticalAxis))
	{
		return false;
	}

	const FVector SurfaceCenterWorld =
		SurfaceTransform.TransformPosition(SurfaceCenterLocal);
	const FVector CenterToPoint = WorldPosition - SurfaceCenterWorld;
	double HorizontalPositionCm = FVector::DotProduct(
		CenterToPoint,
		HorizontalAxis);
	if (SurfaceMapping.Mode
		== EMD_ScrapeSurfaceMappingMode::CurvedFrontPlanarBack)
	{
		FVector2D PlanarSurfaceSizeCm = FVector2D::ZeroVector;
		if (!FMath::IsFinite(SurfaceSizeCm.X)
			|| SurfaceSizeCm.X <= UE_SMALL_NUMBER
			|| !TryCalculateSurfaceSize(
				SurfaceNormalAxis,
				SurfaceBoundsMinimumLocal,
				SurfaceBoundsMaximumLocal,
				SurfaceTransform.GetScale3D().GetAbs(),
				PlanarSurfaceSizeCm))
		{
			return false;
		}

		const double CanonicalU =
			HorizontalPositionCm / PlanarSurfaceSizeCm.X + 0.5;
		if (!FMath::IsFinite(CanonicalU)
			|| CanonicalU < 0.0
			|| CanonicalU > 1.0)
		{
			return false;
		}
		HorizontalPositionCm =
			(CanonicalU - 0.5) * SurfaceSizeCm.X;
	}
	OutSurfacePositionCm = FVector2D(
		HorizontalPositionCm,
		FVector::DotProduct(CenterToPoint, VerticalAxis));

	return FMath::IsFinite(OutSurfacePositionCm.X)
		&& FMath::IsFinite(OutSurfacePositionCm.Y);
}

bool FMD_ScrapeSurfaceGeometry::TryCalculateSurfaceWorldNormal(
	const FVector2D& SurfacePositionCm,
	const EMD_ScrapeSurfaceFace SurfaceFace,
	const FTransform& SurfaceTransform,
	const EMD_ScrapeSurfaceNormalAxis SurfaceNormalAxis,
	const FMD_ScrapeSurfaceMapping& SurfaceMapping,
	const FVector2D& SurfaceSizeCm,
	FVector& OutWorldNormal)
{
	OutWorldNormal = FVector::ZeroVector;
	// MeshUV requires the baked triangle data; it has no planar fallback.
	if (SurfaceMapping.Mode == EMD_ScrapeSurfaceMappingMode::MeshUV) { return false; }

	if (!FMath::IsFinite(SurfacePositionCm.X)
		|| !FMath::IsFinite(SurfacePositionCm.Y)
		|| (SurfaceFace != EMD_ScrapeSurfaceFace::Front
			&& SurfaceFace != EMD_ScrapeSurfaceFace::Back))
	{
		return false;
	}

	if (UsesCylindricalAddress(SurfaceMapping, SurfaceFace))
	{
		FMD_ScrapeCylindricalSurfaceFrame Frame;
		if (!FMath::IsFinite(SurfaceSizeCm.X)
			|| SurfaceSizeCm.X <= UE_SMALL_NUMBER
			|| !TryResolveCylindricalSurfaceFrame(
				SurfaceTransform,
				SurfaceMapping,
				Frame))
		{
			return false;
		}

		const double CanonicalU =
			SurfacePositionCm.X / SurfaceSizeCm.X + 0.5;
		const double CurveAngle = CanonicalU * Frame.SweepRadians;
		OutWorldNormal =
			Frame.StartDirectionWorld * FMath::Cos(CurveAngle)
			+ Frame.TangentDirectionWorld * FMath::Sin(CurveAngle);
		const bool bFaceIsOuter =
			(SurfaceFace == EMD_ScrapeSurfaceFace::Front
				&& Frame.bFrontIsOuter)
			|| (SurfaceFace == EMD_ScrapeSurfaceFace::Back
				&& !Frame.bFrontIsOuter);
		if (!bFaceIsOuter)
		{
			OutWorldNormal = -OutWorldNormal;
		}
		return !OutWorldNormal.IsNearlyZero()
			&& !OutWorldNormal.ContainsNaN();
	}

	FVector HorizontalAxis;
	FVector FrontNormal;
	FVector VerticalAxis;
	if (!TryGetSurfaceWorldAxes(
			SurfaceTransform,
			SurfaceNormalAxis,
			HorizontalAxis,
			FrontNormal,
			VerticalAxis))
	{
		return false;
	}

	OutWorldNormal = SurfaceFace == EMD_ScrapeSurfaceFace::Back
		? -FrontNormal
		: FrontNormal;
	return true;
}

bool FMD_ScrapeSurfaceGeometry::TryConvertMeshUVToSurfacePositionCm(
	const FVector2D& MeshUV,
	const FVector2D& SurfaceSizeCm,
	FVector2D& OutSurfacePositionCm)
{
	OutSurfacePositionCm = FVector2D::ZeroVector;
	if (!FMath::IsFinite(MeshUV.X)
		|| !FMath::IsFinite(MeshUV.Y)
		|| !FMath::IsFinite(SurfaceSizeCm.X)
		|| !FMath::IsFinite(SurfaceSizeCm.Y)
		|| SurfaceSizeCm.X <= UE_SMALL_NUMBER
		|| SurfaceSizeCm.Y <= UE_SMALL_NUMBER
		|| MeshUV.X < 0.0
		|| MeshUV.X > 1.0
		|| MeshUV.Y < 0.0
		|| MeshUV.Y > 1.0)
	{
		return false;
	}

	OutSurfacePositionCm = FVector2D(
		(MeshUV.X - 0.5) * SurfaceSizeCm.X,
		(MeshUV.Y - 0.5) * SurfaceSizeCm.Y);
	return true;
}

bool FMD_ScrapeSurfaceGeometry::TryConvertSurfacePositionCmToMeshUV(
	const FVector2D& SurfacePositionCm,
	const FVector2D& SurfaceSizeCm,
	FVector2D& OutMeshUV)
{
	OutMeshUV = FVector2D::ZeroVector;
	if (!FMath::IsFinite(SurfacePositionCm.X)
		|| !FMath::IsFinite(SurfacePositionCm.Y)
		|| !FMath::IsFinite(SurfaceSizeCm.X)
		|| !FMath::IsFinite(SurfaceSizeCm.Y)
		|| SurfaceSizeCm.X <= UE_SMALL_NUMBER
		|| SurfaceSizeCm.Y <= UE_SMALL_NUMBER)
	{
		return false;
	}

	OutMeshUV = FVector2D(
		SurfacePositionCm.X / SurfaceSizeCm.X + 0.5,
		SurfacePositionCm.Y / SurfaceSizeCm.Y + 0.5);
	return OutMeshUV.X >= 0.0
		&& OutMeshUV.X <= 1.0
		&& OutMeshUV.Y >= 0.0
		&& OutMeshUV.Y <= 1.0;
}

bool FMD_ScrapeSurfaceGeometry::TryResolveCurvedFrontPlanarBackSplitPlane(
	const FTransform& SurfaceTransform,
	const FMD_ScrapeSurfaceMapping& SurfaceMapping,
	FVector& OutSplitOriginWorld,
	FVector& OutFrontHalfNormalWorld)
{
	OutSplitOriginWorld = FVector::ZeroVector;
	OutFrontHalfNormalWorld = FVector::ZeroVector;
	if (SurfaceMapping.Mode
		!= EMD_ScrapeSurfaceMappingMode::CurvedFrontPlanarBack)
	{
		return false;
	}

	FMD_ScrapeCylindricalSurfaceFrame Frame;
	if (!TryResolveCylindricalSurfaceFrame(
			SurfaceTransform,
			SurfaceMapping,
			Frame))
	{
		return false;
	}

	const double MidAngle = Frame.SweepRadians * 0.5;
	OutSplitOriginWorld = Frame.CenterWorld;
	OutFrontHalfNormalWorld = (
		Frame.StartDirectionWorld * FMath::Cos(MidAngle)
		+ Frame.TangentDirectionWorld * FMath::Sin(MidAngle)).GetSafeNormal();
	return !OutSplitOriginWorld.ContainsNaN()
		&& !OutFrontHalfNormalWorld.ContainsNaN()
		&& !OutFrontHalfNormalWorld.IsNearlyZero();
}


bool FMD_ScrapeSurfaceGeometry::TryResolveCylindricalSurfaceFrame(
	const FTransform& SurfaceTransform,
	const FMD_ScrapeSurfaceMapping& SurfaceMapping,
	FMD_ScrapeCylindricalSurfaceFrame& OutFrame)
{
	OutFrame = FMD_ScrapeCylindricalSurfaceFrame();
	if (!HasCylindricalFrame(SurfaceMapping)
		|| SurfaceMapping.CurveCenterLocal.ContainsNaN()
		|| SurfaceMapping.CurveAxisLocal.ContainsNaN()
		|| SurfaceMapping.CurveStartDirectionLocal.ContainsNaN()
		|| !FMath::IsFinite(SurfaceMapping.CurveSweepDegrees)
		|| !FMath::IsFinite(SurfaceMapping.CurveRadiusCm)
		|| !FMath::IsFinite(SurfaceMapping.CurveThicknessCm)
		|| SurfaceMapping.CurveRadiusCm <= UE_SMALL_NUMBER
		|| SurfaceMapping.CurveThicknessCm < 0.0f)
	{
		return false;
	}

	const double SweepRadians = FMath::DegreesToRadians(
		static_cast<double>(SurfaceMapping.CurveSweepDegrees));
	if (!FMath::IsFinite(SweepRadians)
		|| FMath::Abs(SweepRadians) <= UE_SMALL_NUMBER
		|| FMath::Abs(SweepRadians) > MaximumCurveSweepRadians
			+ UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const FVector AxisLocal = SurfaceMapping.CurveAxisLocal.GetSafeNormal();
	const FVector StartDirectionLocal = (
		SurfaceMapping.CurveStartDirectionLocal
		- AxisLocal * FVector::DotProduct(
			SurfaceMapping.CurveStartDirectionLocal,
			AxisLocal)).GetSafeNormal();
	const FVector TangentDirectionLocal = FVector::CrossProduct(
		AxisLocal,
		StartDirectionLocal).GetSafeNormal();
	if (AxisLocal.IsNearlyZero()
		|| StartDirectionLocal.IsNearlyZero()
		|| TangentDirectionLocal.IsNearlyZero())
	{
		return false;
	}

	const FVector AxisWorld = SurfaceTransform
		.TransformVector(AxisLocal)
		.GetSafeNormal();
	const FVector TransformedStartDirection = SurfaceTransform.TransformVector(
		StartDirectionLocal);
	const FVector ProjectedStartDirection =
		TransformedStartDirection
		- AxisWorld * FVector::DotProduct(
			TransformedStartDirection,
			AxisWorld);
	const double StartScale = ProjectedStartDirection.Size();
	const FVector TransformedTangentDirection =
		SurfaceTransform.TransformVector(TangentDirectionLocal);
	const FVector ProjectedTangentDirection =
		TransformedTangentDirection
		- AxisWorld * FVector::DotProduct(
			TransformedTangentDirection,
			AxisWorld);
	const double TangentScale = ProjectedTangentDirection.Size();
	if (AxisWorld.IsNearlyZero()
		|| !FMath::IsFinite(StartScale)
		|| !FMath::IsFinite(TangentScale)
		|| StartScale <= UE_SMALL_NUMBER
		|| TangentScale <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const double MaximumRadialScale = FMath::Max(
		StartScale,
		TangentScale);
	if (FMath::Abs(StartScale - TangentScale)
		> MaximumRadialScale * MaximumRadialScaleDifferenceRatio)
	{
		// 월드 공간에서 타원이 되면 머티리얼의 atan2 원통 공식과 일치하지 않는다.
		return false;
	}

	const FVector StartDirectionWorld =
		ProjectedStartDirection.GetSafeNormal();
	const FVector TangentDirectionWorld = FVector::CrossProduct(
		AxisWorld,
		StartDirectionWorld).GetSafeNormal();
	const double RadialScale = (StartScale + TangentScale) * 0.5;
	const FVector CenterWorld = SurfaceTransform.TransformPosition(
		SurfaceMapping.CurveCenterLocal);
	const double RadiusCm =
		static_cast<double>(SurfaceMapping.CurveRadiusCm) * RadialScale;
	const double ThicknessCm =
		static_cast<double>(SurfaceMapping.CurveThicknessCm) * RadialScale;
	if (CenterWorld.ContainsNaN()
		|| StartDirectionWorld.IsNearlyZero()
		|| TangentDirectionWorld.IsNearlyZero()
		|| !FMath::IsFinite(RadiusCm)
		|| !FMath::IsFinite(ThicknessCm)
		|| RadiusCm <= UE_SMALL_NUMBER
		|| ThicknessCm < 0.0)
	{
		return false;
	}

	OutFrame.CenterWorld = CenterWorld;
	OutFrame.AxisWorld = AxisWorld;
	OutFrame.StartDirectionWorld = StartDirectionWorld;
	OutFrame.TangentDirectionWorld = TangentDirectionWorld;
	OutFrame.SweepRadians = SweepRadians;
	OutFrame.RadiusCm = RadiusCm;
	OutFrame.ThicknessCm = ThicknessCm;
	OutFrame.bFrontIsOuter = SurfaceMapping.bCurveFrontIsOuter;
	return true;
}

bool FMD_ScrapeSurfaceGeometry::TryGetSurfaceWorldAxes(
	const FTransform& SurfaceTransform,
	const EMD_ScrapeSurfaceNormalAxis SurfaceNormalAxis,
	FVector& OutHorizontalAxis,
	FVector& OutFrontNormal,
	FVector& OutVerticalAxis)
{
	FVector HorizontalAxisLocal;
	FVector FrontNormalLocal;
	FVector VerticalAxisLocal;
	if (!TryGetSurfaceLocalAxes(
			SurfaceNormalAxis,
			HorizontalAxisLocal,
			FrontNormalLocal,
			VerticalAxisLocal))
	{
		return false;
	}

	OutHorizontalAxis = SurfaceTransform
		.TransformVector(HorizontalAxisLocal)
		.GetSafeNormal();
	OutFrontNormal = SurfaceTransform
		.TransformVector(FrontNormalLocal)
		.GetSafeNormal();
	OutVerticalAxis = SurfaceTransform
		.TransformVector(VerticalAxisLocal)
		.GetSafeNormal();

	return !OutHorizontalAxis.IsNearlyZero()
		&& !OutFrontNormal.IsNearlyZero()
		&& !OutVerticalAxis.IsNearlyZero()
		&& !OutHorizontalAxis.ContainsNaN()
		&& !OutFrontNormal.ContainsNaN()
		&& !OutVerticalAxis.ContainsNaN();
}
