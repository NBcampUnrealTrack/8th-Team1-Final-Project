// Copyright Epic Games, Inc. All Rights Reserved.

#include "Component/Scrape/MD_ScrapeToolComponent.h"

#include "Gameplay/Scrape/MD_ScrapeFocusProjection.h"
#include "Camera/CameraTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

bool UMD_ScrapeToolComponent::CaptureScrapeFocusView()
{
	const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
	APlayerController* const PlayerController = OwnerPawn
		? Cast<APlayerController>(OwnerPawn->GetController())
		: nullptr;
	if (!PlayerController)
	{
		return false;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	PlayerController->GetViewportSize(ViewportWidth, ViewportHeight);
	const APlayerCameraManager* const PlayerCameraManager =
		PlayerController->PlayerCameraManager;
	const ULocalPlayer* const LocalPlayer = PlayerController->GetLocalPlayer();
	const float ViewFOV = PlayerCameraManager
		? PlayerCameraManager->GetFOVAngle()
		: 0.0f;
	if (ViewLocation.ContainsNaN()
		|| ViewRotation.ContainsNaN()
		|| !FMath::IsFinite(ViewFOV)
		|| ViewFOV <= UE_SMALL_NUMBER
		|| !PlayerCameraManager
		|| !LocalPlayer
		|| ViewportWidth <= 0
		|| ViewportHeight <= 0)
	{
		return false;
	}

	const float ViewportAspectRatio = static_cast<float>(ViewportWidth)
		/ static_cast<float>(ViewportHeight);
	FMinimalViewInfo ProjectionView =
		PlayerCameraManager->GetCameraCacheView();
	ProjectionView.FOV = ViewFOV;
	float HorizontalHalfTangent = 0.0f;
	float VerticalHalfTangent = 0.0f;
	if (!FMD_ScrapeFocusProjection::TryCalculateHalfTangents(
		ProjectionView,
		static_cast<EAspectRatioAxisConstraint>(
			LocalPlayer->AspectRatioAxisConstraint.GetValue()),
		ViewportAspectRatio,
		IsLegacyMaintainYFOVProjectionEnabled(),
		HorizontalHalfTangent,
		VerticalHalfTangent))
	{
		return false;
	}

	ScrapeFocusViewOrigin = ViewLocation;
	ScrapeFocusViewRotation = ViewRotation;
	ScrapeFocusViewFOV = ViewFOV;
	ScrapeFocusSession.ScrapeFocusAspectRatio = ViewportAspectRatio;
	ScrapeFocusSession.ScrapeFocusHorizontalHalfTangent = HorizontalHalfTangent;
	ScrapeFocusSession.ScrapeFocusVerticalHalfTangent = VerticalHalfTangent;
	ScrapeFocusSession.ScrapeFocusAuthorityHorizontalHalfTangent = HorizontalHalfTangent;
	ScrapeFocusSession.ScrapeFocusAuthorityVerticalHalfTangent = VerticalHalfTangent;
	ScrapeFocusViewportSize = FIntPoint(ViewportWidth, ViewportHeight);
	ScrapeFocusPointerNormalized = FVector2D(0.5, 0.5);
	ResetScrapeFocusPointerStrokeConstraint();
	bHasScrapeFocusView = true;
	return true;
}

bool UMD_ScrapeToolComponent::ConsumeScrapeFocusLookInput(
	const FVector2D& LookInput)
{
	if (!IsScrapeFocusActive())
	{
		return false;
	}
	if (!LookInput.ContainsNaN())
	{
		const float Sensitivity = FMath::Max(
			ScrapeFocusPointerSensitivity,
			0.0f);
		FVector2D GestureDelta = LookInput * Sensitivity;
		// 이 프로젝트의 Look 매핑은 마우스를 위로 움직일 때 Y가 음수다.
		// 화면 normalized Y도 위쪽이 작으므로 부호를 다시 뒤집지 않는다.
		if (bIsScraping && ScrapeFocusSession.bHasScrapeFocusPointerStrokeAxis)
		{
			GestureDelta = ScrapeFocusSession.ScrapeFocusPointerGestureAxis
				* FVector2D::DotProduct(
					GestureDelta,
					ScrapeFocusSession.ScrapeFocusPointerGestureAxis);
		}
		else if (bIsScraping)
		{
			const FVector2D OriginalGestureDelta = GestureDelta;
			const FVector2D PreviousGestureDisplacement =
				ScrapeFocusSession.ScrapeFocusPointerGestureDisplacement;
			const FVector2D RequestedGestureDisplacement =
				PreviousGestureDisplacement + OriginalGestureDelta;
			const double Threshold = FMath::Max(
				static_cast<double>(
					ScrapeFocusPointerDirectionLockThresholdNormalized),
				static_cast<double>(UE_KINDA_SMALL_NUMBER));
			if (RequestedGestureDisplacement.SizeSquared()
				>= Threshold * Threshold)
			{
				// 임계점을 건넌 정확한 위치에서 축을 잡고, 같은 입력의 남은
				// 부분부터 투영해 입력 표본 밀도에 따른 각도 차이를 없앤다.
				const double A = OriginalGestureDelta.SizeSquared();
				const double B = 2.0 * FVector2D::DotProduct(
					PreviousGestureDisplacement,
					OriginalGestureDelta);
				const double C = PreviousGestureDisplacement.SizeSquared()
					- Threshold * Threshold;
				double CrossingAlpha = 1.0;
				const double Discriminant = B * B - 4.0 * A * C;
				if (A > UE_DOUBLE_SMALL_NUMBER && Discriminant >= 0.0)
				{
					const double Root = FMath::Sqrt(Discriminant);
					const double First = (-B - Root) / (2.0 * A);
					const double Second = (-B + Root) / (2.0 * A);
					if (First >= 0.0 && First <= 1.0)
					{
						CrossingAlpha = First;
					}
					else if (Second >= 0.0 && Second <= 1.0)
					{
						CrossingAlpha = Second;
					}
				}
				ScrapeFocusSession.ScrapeFocusPointerGestureDisplacement =
					PreviousGestureDisplacement
					+ OriginalGestureDelta * CrossingAlpha;
				TryLatchScrapeFocusPointerStrokeAxis();
				if (ScrapeFocusSession.bHasScrapeFocusPointerStrokeAxis)
				{
					const FVector2D RemainingGestureDelta =
						OriginalGestureDelta * (1.0 - CrossingAlpha);
					GestureDelta = OriginalGestureDelta * CrossingAlpha
						+ ScrapeFocusSession.ScrapeFocusPointerGestureAxis
							* FVector2D::DotProduct(
								RemainingGestureDelta,
								ScrapeFocusSession.ScrapeFocusPointerGestureAxis);
				}
			}
			else
			{
				ScrapeFocusSession.ScrapeFocusPointerGestureDisplacement =
					RequestedGestureDisplacement;
			}
		}
		const float AspectRatio = GetScrapeFocusPointerAspectRatio();
		const FVector2D PointerDelta(
			GestureDelta.X,
			GestureDelta.Y * static_cast<double>(AspectRatio));
		ScrapeFocusPointerNormalized += PointerDelta;
		ClampScrapeFocusPointerToAuthorityCone();
		if (bIsScraping && !ScrapeFocusSession.bHasScrapeFocusPointerStrokeAxis)
		{
			TryLatchScrapeFocusPointerStrokeAxis();
			ClampScrapeFocusPointerToAuthorityCone();
		}

		// Look과 LMB Started가 같은 프레임에 들어와도 이전 Tick의 aim을 사용하지 않는다.
		RefreshScrapeFocusAim();
	}
	return true;
}

void UMD_ScrapeToolComponent::SetScrapeFocusPointerSensitivity(
	const float NewSensitivity)
{
	if (!FMath::IsFinite(NewSensitivity))
	{
		return;
	}
	ScrapeFocusPointerSensitivity = FMath::Clamp(
		NewSensitivity,
		0.0001f,
		0.1f);
}

void UMD_ScrapeToolComponent::ClampScrapeFocusPointerToAuthorityCone()
{
	const float HorizontalHalfRange =
		FMD_ScrapeFocusProjection::CalculatePointerNormalizedHalfRange(
			ScrapeFocusSession.ScrapeFocusHorizontalHalfTangent,
			ScrapeFocusSession.ScrapeFocusAuthorityHorizontalHalfTangent,
			ScrapeFocusPointerSafeMargin);
	const float VerticalHalfRange =
		FMD_ScrapeFocusProjection::CalculatePointerNormalizedHalfRange(
			ScrapeFocusSession.ScrapeFocusVerticalHalfTangent,
			ScrapeFocusSession.ScrapeFocusAuthorityVerticalHalfTangent,
			ScrapeFocusPointerSafeMargin);
	const double MinimumX = 0.5 - static_cast<double>(HorizontalHalfRange);
	const double MaximumX = 0.5 + static_cast<double>(HorizontalHalfRange);
	const double MinimumY = 0.5 - static_cast<double>(VerticalHalfRange);
	const double MaximumY = 0.5 + static_cast<double>(VerticalHalfRange);
	if (!bIsScraping || !ScrapeFocusSession.bHasScrapeFocusPointerStrokeAxis)
	{
		ScrapeFocusPointerNormalized.X = FMath::Clamp(
			ScrapeFocusPointerNormalized.X,
			MinimumX,
			MaximumX);
		ScrapeFocusPointerNormalized.Y = FMath::Clamp(
			ScrapeFocusPointerNormalized.Y,
			MinimumY,
			MaximumY);
		return;
	}

	// X/Y를 따로 clamp하면 대각선 끝점에서 잠긴 직선 밖으로 꺾인다.
	// 직선과 허용 사각형의 교차 구간을 구해 scalar travel만 제한한다.
	double MinimumTravel = -TNumericLimits<double>::Max();
	double MaximumTravel = TNumericLimits<double>::Max();
	const auto IntersectAxisInterval = [&MinimumTravel, &MaximumTravel](
		const double Anchor,
		const double Axis,
		const double Minimum,
		const double Maximum)
	{
		if (FMath::Abs(Axis) <= UE_DOUBLE_SMALL_NUMBER)
		{
			return Anchor >= Minimum && Anchor <= Maximum;
		}
		double First = (Minimum - Anchor) / Axis;
		double Second = (Maximum - Anchor) / Axis;
		if (First > Second)
		{
			Swap(First, Second);
		}
		MinimumTravel = FMath::Max(MinimumTravel, First);
		MaximumTravel = FMath::Min(MaximumTravel, Second);
		return MinimumTravel <= MaximumTravel;
	};
	if (!IntersectAxisInterval(
			ScrapeFocusSession.ScrapeFocusPointerStrokeAnchorNormalized.X,
			ScrapeFocusSession.ScrapeFocusPointerStrokeAxis.X,
			MinimumX,
			MaximumX)
		|| !IntersectAxisInterval(
			ScrapeFocusSession.ScrapeFocusPointerStrokeAnchorNormalized.Y,
			ScrapeFocusSession.ScrapeFocusPointerStrokeAxis.Y,
			MinimumY,
			MaximumY))
	{
		ScrapeFocusPointerNormalized = ScrapeFocusSession.ScrapeFocusPointerStrokeAnchorNormalized;
		return;
	}

	const double RequestedTravel = FVector2D::DotProduct(
		ScrapeFocusPointerNormalized - ScrapeFocusSession.ScrapeFocusPointerStrokeAnchorNormalized,
		ScrapeFocusSession.ScrapeFocusPointerStrokeAxis);
	const double ClampedTravel = FMath::Clamp(
		RequestedTravel,
		MinimumTravel,
		MaximumTravel);
	ScrapeFocusPointerNormalized = ScrapeFocusSession.ScrapeFocusPointerStrokeAnchorNormalized
		+ ScrapeFocusSession.ScrapeFocusPointerStrokeAxis * ClampedTravel;
}

void UMD_ScrapeToolComponent::ResetScrapeFocusPointerStrokeConstraint()
{
	ScrapeFocusSession.ScrapeFocusPointerStrokeAnchorNormalized = ScrapeFocusPointerNormalized;
	ScrapeFocusSession.ScrapeFocusPointerGestureDisplacement = FVector2D::ZeroVector;
	ScrapeFocusSession.ScrapeFocusPointerGestureAxis = FVector2D::ZeroVector;
	ScrapeFocusSession.ScrapeFocusPointerStrokeAxis = FVector2D::ZeroVector;
	ScrapeFocusSession.bHasScrapeFocusPointerStrokeAxis = false;
}

void UMD_ScrapeToolComponent::TryLatchScrapeFocusPointerStrokeAxis()
{
	if (!bIsScraping || ScrapeFocusSession.bHasScrapeFocusPointerStrokeAxis)
	{
		return;
	}

	const FVector2D Displacement = ScrapeFocusSession.ScrapeFocusPointerGestureDisplacement;
	const double Threshold = FMath::Max(
		static_cast<double>(
			ScrapeFocusPointerDirectionLockThresholdNormalized),
		static_cast<double>(UE_KINDA_SMALL_NUMBER));
	if (!FMath::IsFinite(Displacement.SizeSquared())
		|| Displacement.SizeSquared() < Threshold * Threshold)
	{
		return;
	}

	ScrapeFocusSession.ScrapeFocusPointerGestureAxis = Displacement.GetSafeNormal();
	const float AspectRatio = GetScrapeFocusPointerAspectRatio();
	ScrapeFocusSession.ScrapeFocusPointerStrokeAxis = FVector2D(
		ScrapeFocusSession.ScrapeFocusPointerGestureAxis.X,
		ScrapeFocusSession.ScrapeFocusPointerGestureAxis.Y * static_cast<double>(AspectRatio))
		.GetSafeNormal();
	ScrapeFocusSession.bHasScrapeFocusPointerStrokeAxis =
		!ScrapeFocusSession.ScrapeFocusPointerGestureAxis.IsNearlyZero()
		&& !ScrapeFocusSession.ScrapeFocusPointerStrokeAxis.IsNearlyZero();
}

float UMD_ScrapeToolComponent::GetScrapeFocusPointerAspectRatio() const
{
	if (ScrapeFocusViewportSize.X > 0 && ScrapeFocusViewportSize.Y > 0)
	{
		const float ViewportAspectRatio =
			static_cast<float>(ScrapeFocusViewportSize.X)
			/ static_cast<float>(ScrapeFocusViewportSize.Y);
		if (FMath::IsFinite(ViewportAspectRatio)
			&& ViewportAspectRatio > UE_SMALL_NUMBER)
		{
			return ViewportAspectRatio;
		}
	}
	return FMath::IsFinite(ScrapeFocusSession.ScrapeFocusAspectRatio)
		&& ScrapeFocusSession.ScrapeFocusAspectRatio > UE_SMALL_NUMBER
		? ScrapeFocusSession.ScrapeFocusAspectRatio
		: 1.0f;
}

bool UMD_ScrapeToolComponent::ApplyScrapeFocusCameraView(
	FMinimalViewInfo& InOutViewInfo) const
{
	if (!IsScrapeFocusActive() || !bHasScrapeFocusView)
	{
		return false;
	}

	InOutViewInfo.Rotation = ScrapeFocusViewRotation;
	InOutViewInfo.FOV = ScrapeFocusViewFOV;
	InOutViewInfo.DesiredFOV = ScrapeFocusViewFOV;
	return true;
}

FVector UMD_ScrapeToolComponent::GetScrapeFocusAimDirection() const
{
	if (!bHasScrapeFocusView
		|| ScrapeFocusViewportSize.X <= 0
		|| ScrapeFocusViewportSize.Y <= 0
		|| !FMath::IsFinite(ScrapeFocusSession.ScrapeFocusHorizontalHalfTangent)
		|| !FMath::IsFinite(ScrapeFocusSession.ScrapeFocusVerticalHalfTangent)
		|| ScrapeFocusSession.ScrapeFocusHorizontalHalfTangent <= UE_SMALL_NUMBER
		|| ScrapeFocusSession.ScrapeFocusVerticalHalfTangent <= UE_SMALL_NUMBER)
	{
		return FVector::ZeroVector;
	}

	// 방향 판정 구간에는 reticle만 움직이고 실제 판정 ray는 LMB 시작점에 둔다.
	const FVector2D AimPointerNormalized =
		bIsScraping && !ScrapeFocusSession.bHasScrapeFocusPointerStrokeAxis
		? ScrapeFocusSession.ScrapeFocusPointerStrokeAnchorNormalized
		: ScrapeFocusPointerNormalized;
	const double ScreenX = (AimPointerNormalized.X - 0.5) * 2.0;
	const double ScreenY = (0.5 - AimPointerNormalized.Y) * 2.0;
	const FRotationMatrix ViewAxes(ScrapeFocusViewRotation);
	return (ViewAxes.GetScaledAxis(EAxis::X)
		+ ViewAxes.GetScaledAxis(EAxis::Y) * ScreenX
			* static_cast<double>(ScrapeFocusSession.ScrapeFocusHorizontalHalfTangent)
		+ ViewAxes.GetScaledAxis(EAxis::Z) * ScreenY
			* static_cast<double>(ScrapeFocusSession.ScrapeFocusVerticalHalfTangent)).GetSafeNormal();
}

bool UMD_ScrapeToolComponent::IsCurrentScrapeFocusViewportAspectCompatible() const
{
	const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled())
	{
		// 원격 authority session에는 로컬 viewport가 없다. 해당 session은
		// Begin에서 합의한 aspect와 전달받은 camera-relative ray만 검증한다.
		return true;
	}

	const APlayerController* const PlayerController = Cast<APlayerController>(
		OwnerPawn->GetController());
	if (!PlayerController)
	{
		return false;
	}

	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	PlayerController->GetViewportSize(ViewportWidth, ViewportHeight);
	if (!FMD_ScrapeFocusProjection::IsViewportAspectCompatible(
		ScrapeFocusSession.ScrapeFocusAspectRatio,
		FIntPoint(ViewportWidth, ViewportHeight),
		ScrapeFocusAspectRatioAbsoluteTolerance))
	{
		return false;
	}

	const APlayerCameraManager* const PlayerCameraManager =
		PlayerController->PlayerCameraManager;
	const ULocalPlayer* const LocalPlayer = PlayerController->GetLocalPlayer();
	if (!PlayerCameraManager || !LocalPlayer)
	{
		return false;
	}
	FMinimalViewInfo ProjectionView =
		PlayerCameraManager->GetCameraCacheView();
	ProjectionView.FOV = ScrapeFocusViewFOV;
	float CurrentHorizontalHalfTangent = 0.0f;
	float CurrentVerticalHalfTangent = 0.0f;
	const float CurrentAspectRatio = static_cast<float>(ViewportWidth)
		/ static_cast<float>(ViewportHeight);
	return FMD_ScrapeFocusProjection::TryCalculateHalfTangents(
			ProjectionView,
			static_cast<EAspectRatioAxisConstraint>(
				LocalPlayer->AspectRatioAxisConstraint.GetValue()),
			CurrentAspectRatio,
			IsLegacyMaintainYFOVProjectionEnabled(),
			CurrentHorizontalHalfTangent,
			CurrentVerticalHalfTangent)
		&& FMath::Abs(CurrentHorizontalHalfTangent
				- ScrapeFocusSession.ScrapeFocusHorizontalHalfTangent)
			<= ProjectionHalfTangentTolerance
		&& FMath::Abs(CurrentVerticalHalfTangent
				- ScrapeFocusSession.ScrapeFocusVerticalHalfTangent)
			<= ProjectionHalfTangentTolerance;
}

bool UMD_ScrapeToolComponent::IsLegacyMaintainYFOVProjectionEnabled()
{
	const IConsoleVariable* const LegacyMaintainYFOV =
		IConsoleManager::Get().FindConsoleVariable(
			TEXT("r.UseLegacyMaintainYFOVViewMatrix"));
	return LegacyMaintainYFOV && LegacyMaintainYFOV->GetBool();
}
