#include "Gameplay/Scrape/MD_ScrapeFocusProjection.h"

#include "Camera/CameraTypes.h"

bool FMD_ScrapeFocusProjection::IsSupportedAspectRatio(const float AspectRatio)
{
	return FMath::IsFinite(AspectRatio)
		&& AspectRatio >= MinimumSupportedAspectRatio
		&& AspectRatio <= MaximumSupportedAspectRatio;
}

bool FMD_ScrapeFocusProjection::TryCalculateHalfTangents(
	const FMinimalViewInfo& ViewInfo,
	const EAspectRatioAxisConstraint DefaultAxisConstraint,
	const float ViewportAspectRatio,
	const bool bUseLegacyMaintainYFOV,
	float& OutHorizontalHalfTangent,
	float& OutVerticalHalfTangent)
{
	OutHorizontalHalfTangent = 0.0f;
	OutVerticalHalfTangent = 0.0f;
	if (!FMath::IsFinite(ViewInfo.FOV)
		|| ViewInfo.FOV <= UE_SMALL_NUMBER
		|| ViewInfo.FOV >= 179.0f
		|| !FMath::IsFinite(ViewInfo.AspectRatio)
		|| ViewInfo.AspectRatio <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(ViewportAspectRatio)
		|| ViewportAspectRatio <= UE_SMALL_NUMBER
		|| ViewInfo.bConstrainAspectRatio
		|| ViewInfo.ProjectionMode != ECameraProjectionMode::Perspective
		|| !ViewInfo.OffCenterProjectionOffset.IsNearlyZero(UE_SMALL_NUMBER))
	{
		return false;
	}

	const EAspectRatioAxisConstraint AxisConstraint =
		ViewInfo.AspectRatioAxisConstraint.Get(DefaultAxisConstraint);
	const bool bMaintainXFOV =
		AxisConstraint == AspectRatio_MaintainXFOV
		|| (AxisConstraint == AspectRatio_MajorAxisFOV
			&& ViewportAspectRatio > 1.0f);
	if (!bMaintainXFOV
		&& AxisConstraint != AspectRatio_MaintainYFOV
		&& AxisConstraint != AspectRatio_MajorAxisFOV)
	{
		return false;
	}

	const double RawHorizontalHalfTangent = FMath::Tan(
		FMath::DegreesToRadians(static_cast<double>(ViewInfo.FOV) * 0.5));
	double HorizontalHalfTangent = 0.0;
	double VerticalHalfTangent = 0.0;
	if (bMaintainXFOV)
	{
		HorizontalHalfTangent = RawHorizontalHalfTangent;
		VerticalHalfTangent = RawHorizontalHalfTangent
			/ static_cast<double>(ViewportAspectRatio);
	}
	else if (bUseLegacyMaintainYFOV)
	{
		VerticalHalfTangent = RawHorizontalHalfTangent;
		HorizontalHalfTangent = RawHorizontalHalfTangent
			* static_cast<double>(ViewportAspectRatio);
	}
	else
	{
		VerticalHalfTangent = RawHorizontalHalfTangent
			/ static_cast<double>(ViewInfo.AspectRatio);
		HorizontalHalfTangent = VerticalHalfTangent
			* static_cast<double>(ViewportAspectRatio);
	}

	if (!FMath::IsFinite(HorizontalHalfTangent)
		|| !FMath::IsFinite(VerticalHalfTangent)
		|| HorizontalHalfTangent <= UE_DOUBLE_SMALL_NUMBER
		|| VerticalHalfTangent <= UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}
	OutHorizontalHalfTangent = static_cast<float>(HorizontalHalfTangent);
	OutVerticalHalfTangent = static_cast<float>(VerticalHalfTangent);
	return FMath::IsFinite(OutHorizontalHalfTangent)
		&& FMath::IsFinite(OutVerticalHalfTangent);
}

bool FMD_ScrapeFocusProjection::IsViewportAspectCompatible(
	const float AgreedAspectRatio,
	const FIntPoint& CurrentViewportSize,
	const float AbsoluteTolerance)
{
	if (!FMath::IsFinite(AgreedAspectRatio)
		|| AgreedAspectRatio <= UE_SMALL_NUMBER
		|| CurrentViewportSize.X <= 0
		|| CurrentViewportSize.Y <= 0)
	{
		return false;
	}

	const float CurrentAspectRatio = static_cast<float>(CurrentViewportSize.X)
		/ static_cast<float>(CurrentViewportSize.Y);
	return FMath::IsNearlyEqual(
		CurrentAspectRatio,
		AgreedAspectRatio,
		FMath::Max(AbsoluteTolerance, 0.0f));
}

float FMD_ScrapeFocusProjection::CalculatePointerNormalizedHalfRange(
	const float ProjectionHalfTangent,
	const float AuthorityHalfTangent,
	const float PointerSafeMargin)
{
	if (!FMath::IsFinite(ProjectionHalfTangent)
		|| !FMath::IsFinite(AuthorityHalfTangent)
		|| ProjectionHalfTangent <= UE_SMALL_NUMBER
		|| AuthorityHalfTangent <= UE_SMALL_NUMBER)
	{
		return 0.0f;
	}

	const float MaxScreenCoordinate = 1.0f - 2.0f * FMath::Clamp(
		PointerSafeMargin,
		0.0f,
		0.49f);
	const float AuthorityToProjectionRatio = FMath::Clamp(
		AuthorityHalfTangent / ProjectionHalfTangent,
		0.0f,
		1.0f);
	return 0.5f * MaxScreenCoordinate * AuthorityToProjectionRatio;
}
