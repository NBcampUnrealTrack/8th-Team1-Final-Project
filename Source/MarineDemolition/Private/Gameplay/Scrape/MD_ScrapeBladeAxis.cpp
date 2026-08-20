#include "Gameplay/Scrape/MD_ScrapeBladeAxis.h"

void FMD_ScrapeBladeAxisWindow::Reset()
{
	Samples.Reset();
}

void FMD_ScrapeBladeAxisWindow::AddSample(
	const FVector2D& PositionCm,
	const double ArcLengthCm,
	const double WindowCm,
	const double ResampleSpacingCm)
{
	FMD_ScrapeBladeAxisSample& Sample = Samples.AddDefaulted_GetRef();
	Sample.PositionCm = PositionCm;
	Sample.ArcLengthCm = ArcLengthCm;

	const double KeepAfterArcCm = ArcLengthCm
		- FMath::Max(WindowCm, 0.1)
		- FMath::Max(ResampleSpacingCm, 0.01);
	while (Samples.Num() > 2
		&& Samples[1].ArcLengthCm < KeepAfterArcCm)
	{
		Samples.RemoveAt(0, 1, EAllowShrinking::No);
	}
}

bool FMD_ScrapeBladeAxisWindow::TryCalculateWindowedAxis(
	const double WindowCm,
	FVector2D& OutAxis) const
{
	if (Samples.Num() < 2)
	{
		return false;
	}

	const FMD_ScrapeBladeAxisSample& Latest = Samples.Last();
	const double TargetArcCm = Latest.ArcLengthCm
		- FMath::Max(WindowCm, 0.1);
	FVector2D WindowStartCm = Samples[0].PositionCm;
	for (int32 Index = 1; Index < Samples.Num(); ++Index)
	{
		const FMD_ScrapeBladeAxisSample& Previous = Samples[Index - 1];
		const FMD_ScrapeBladeAxisSample& Current = Samples[Index];
		if (TargetArcCm <= Current.ArcLengthCm)
		{
			const double SpanCm = Current.ArcLengthCm - Previous.ArcLengthCm;
			const double Alpha = SpanCm > UE_SMALL_NUMBER
				? FMath::Clamp((TargetArcCm - Previous.ArcLengthCm) / SpanCm, 0.0, 1.0)
				: 0.0;
			WindowStartCm = FMath::Lerp(
				Previous.PositionCm,
				Current.PositionCm,
				Alpha);
			break;
		}
	}

	const FVector2D Candidate =
		(Latest.PositionCm - WindowStartCm).GetSafeNormal();
	if (Candidate.IsNearlyZero())
	{
		return false;
	}

	OutAxis = Candidate;
	return true;
}

int32 FMD_ScrapeBladeAxisWindow::GetSampleCount() const
{
	return Samples.Num();
}

FMD_ScrapeBladeBasis FMD_ScrapeBladeAxis::MakeBasis(
	const FVector2D& BladeDepthAxis)
{
	const FVector2D SafeDepthAxis = BladeDepthAxis.GetSafeNormal();
	return FMD_ScrapeBladeBasis(
		SafeDepthAxis,
		FVector2D(-SafeDepthAxis.Y, SafeDepthAxis.X));
}

FVector2D FMD_ScrapeBladeAxis::Interpolate(
	const FVector2D& StartAxis,
	const FVector2D& EndAxis,
	const double Alpha)
{
	const FVector2D SafeStart = StartAxis.GetSafeNormal();
	FVector2D SafeEnd = EndAxis.GetSafeNormal();
	if (SafeStart.IsNearlyZero() || SafeEnd.IsNearlyZero())
	{
		return SafeStart.IsNearlyZero() ? SafeEnd : SafeStart;
	}
	if (FVector2D::DotProduct(SafeStart, SafeEnd) < 0.0)
	{
		SafeEnd *= -1.0;
	}
	const double Dot = FMath::Clamp(
		FVector2D::DotProduct(SafeStart, SafeEnd), -1.0, 1.0);
	const double Cross = SafeStart.X * SafeEnd.Y - SafeStart.Y * SafeEnd.X;
	const double AngleRadians = FMath::Atan2(Cross, Dot)
		* FMath::Clamp(Alpha, 0.0, 1.0);
	const double CosAngle = FMath::Cos(AngleRadians);
	const double SinAngle = FMath::Sin(AngleRadians);
	return FVector2D(
		SafeStart.X * CosAngle - SafeStart.Y * SinAngle,
		SafeStart.X * SinAngle + SafeStart.Y * CosAngle).GetSafeNormal();
}

FVector2D FMD_ScrapeBladeAxis::RotateTowards(
	const FVector2D& CurrentAxis,
	const FVector2D& TargetAxis,
	const double MaximumTurnRadians)
{
	const FVector2D SafeCurrent = CurrentAxis.GetSafeNormal();
	FVector2D SafeTarget = TargetAxis.GetSafeNormal();
	if (SafeCurrent.IsNearlyZero() || SafeTarget.IsNearlyZero())
	{
		return SafeCurrent.IsNearlyZero() ? SafeTarget : SafeCurrent;
	}

	// 날 방향은 180도 동치다. 같은 반구로 맞춰 더 가까운 쪽으로 돌린다.
	if (FVector2D::DotProduct(SafeTarget, SafeCurrent) < 0.0)
	{
		SafeTarget *= -1.0;
	}

	const double Dot = FMath::Clamp(
		FVector2D::DotProduct(SafeCurrent, SafeTarget),
		0.0,
		1.0);
	const double AngleRadians = FMath::Acos(Dot);
	if (AngleRadians <= UE_SMALL_NUMBER)
	{
		return SafeCurrent;
	}

	const double Cross = SafeCurrent.X * SafeTarget.Y
		- SafeCurrent.Y * SafeTarget.X;
	const double AppliedRadians =
		FMath::Min(AngleRadians, FMath::Max(MaximumTurnRadians, 0.0))
		* (Cross < 0.0 ? -1.0 : 1.0);
	const double CosAngle = FMath::Cos(AppliedRadians);
	const double SinAngle = FMath::Sin(AppliedRadians);
	return FVector2D(
		SafeCurrent.X * CosAngle - SafeCurrent.Y * SinAngle,
		SafeCurrent.X * SinAngle + SafeCurrent.Y * CosAngle).GetSafeNormal();
}
