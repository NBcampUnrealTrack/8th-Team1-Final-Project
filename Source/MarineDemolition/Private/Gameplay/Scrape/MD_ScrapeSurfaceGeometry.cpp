#include "Gameplay/Scrape/MD_ScrapeSurfaceGeometry.h"

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
	const FVector& ImpactNormal,
	const FTransform& SurfaceTransform,
	const EMD_ScrapeSurfaceNormalAxis SurfaceNormalAxis,
	const float MinimumFaceNormalDot,
	EMD_ScrapeSurfaceFace& OutSurfaceFace)
{
	OutSurfaceFace = EMD_ScrapeSurfaceFace::Front;
	if (ImpactNormal.ContainsNaN()
		|| !FMath::IsFinite(MinimumFaceNormalDot)
		|| MinimumFaceNormalDot <= 0.0f
		|| MinimumFaceNormalDot > 1.0f)
	{
		return false;
	}

	const FVector SafeImpactNormal = ImpactNormal.GetSafeNormal();
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
	FVector& OutWorldPosition)
{
	OutWorldPosition = FVector::ZeroVector;
	if (!FMath::IsFinite(SurfacePositionCm.X)
		|| !FMath::IsFinite(SurfacePositionCm.Y)
		|| SurfaceCenterLocal.ContainsNaN()
		|| SurfaceBoundsMinimumLocal.ContainsNaN()
		|| SurfaceBoundsMaximumLocal.ContainsNaN())
	{
		return false;
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
	OutWorldPosition = SurfaceCenterWorld
		+ HorizontalAxis * SurfacePositionCm.X
		+ VerticalAxis * SurfacePositionCm.Y;
	return !OutWorldPosition.ContainsNaN();
}

bool FMD_ScrapeSurfaceGeometry::TryProjectWorldPositionToSurface(
	const FVector& WorldPosition,
	const FTransform& SurfaceTransform,
	const EMD_ScrapeSurfaceNormalAxis SurfaceNormalAxis,
	const FVector& SurfaceCenterLocal,
	FVector2D& OutSurfacePositionCm)
{
	OutSurfacePositionCm = FVector2D::ZeroVector;
	if (WorldPosition.ContainsNaN()
		|| SurfaceCenterLocal.ContainsNaN())
	{
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
	OutSurfacePositionCm = FVector2D(
		FVector::DotProduct(CenterToPoint, HorizontalAxis),
		FVector::DotProduct(CenterToPoint, VerticalAxis));

	return FMath::IsFinite(OutSurfacePositionCm.X)
		&& FMath::IsFinite(OutSurfacePositionCm.Y);
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
