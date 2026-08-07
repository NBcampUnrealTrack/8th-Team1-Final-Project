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

	auto SelectSmallestAxis = [](const FVector& Size)
	{
		EMD_ScrapeSurfaceNormalAxis Axis =
			EMD_ScrapeSurfaceNormalAxis::Y;
		double SmallestSize = Size.Y;
		if (Size.X < SmallestSize - UE_KINDA_SMALL_NUMBER)
		{
			Axis = EMD_ScrapeSurfaceNormalAxis::X;
			SmallestSize = Size.X;
		}
		if (Size.Z < SmallestSize - UE_KINDA_SMALL_NUMBER)
		{
			Axis = EMD_ScrapeSurfaceNormalAxis::Z;
		}
		return Axis;
	};
	auto GetAxisSize = [](const FVector& Size,
		const EMD_ScrapeSurfaceNormalAxis Axis)
	{
		switch (Axis)
		{
		case EMD_ScrapeSurfaceNormalAxis::X:
			return static_cast<double>(Size.X);
		case EMD_ScrapeSurfaceNormalAxis::Y:
			return static_cast<double>(Size.Y);
		case EMD_ScrapeSurfaceNormalAxis::Z:
			return static_cast<double>(Size.Z);
		default:
			return TNumericLimits<double>::Max();
		}
	};

	const EMD_ScrapeSurfaceNormalAxis LocalSmallestAxis =
		SelectSmallestAxis(LocalSize);
	const double LocalSmallestSize = GetAxisSize(
		LocalSize,
		LocalSmallestAxis);
	double LocalSecondSmallestSize = TNumericLimits<double>::Max();
	for (const EMD_ScrapeSurfaceNormalAxis Axis : {
		EMD_ScrapeSurfaceNormalAxis::X,
		EMD_ScrapeSurfaceNormalAxis::Y,
		EMD_ScrapeSurfaceNormalAxis::Z })
	{
		if (Axis != LocalSmallestAxis)
		{
			LocalSecondSmallestSize = FMath::Min(
				LocalSecondSmallestSize,
				GetAxisSize(LocalSize, Axis));
		}
	}

	// 이미 판재로 모델링된 메시는 Blueprint의 기존 비균일 Scale보다 원본 두께를 우선한다.
	constexpr double ClearlyThinRatio = 0.75;
	OutResolvedNormalAxis =
		LocalSmallestSize <= LocalSecondSmallestSize * ClearlyThinRatio
		? LocalSmallestAxis
		: SelectSmallestAxis(WorldSize);
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
