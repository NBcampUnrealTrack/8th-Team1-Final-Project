// Copyright Epic Games, Inc. All Rights Reserved.

#include "Component/Scrape/MD_ScrapeToolComponent.h"

#include "Gameplay/Scrape/MD_ScrapeFocusProjection.h"
#include "Camera/CameraTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

namespace
{
	bool IsFiniteScrapeVisualVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y)
			&& FMath::IsFinite(Value.Z);
	}

	bool TryProjectScrapeVisualTangent(
		const FVector& Candidate,
		const FVector& SurfaceNormal,
		FVector& OutTangent)
	{
		if (!IsFiniteScrapeVisualVector(Candidate)
			|| !IsFiniteScrapeVisualVector(SurfaceNormal))
		{
			return false;
		}

		OutTangent = FVector::VectorPlaneProject(Candidate, SurfaceNormal);
		return IsFiniteScrapeVisualVector(OutTangent)
			&& OutTangent.Normalize();
	}

	void StabilizeScrapeVisualTangentSign(
		const FMD_ScrapeVisualContactPose& PreviousPose,
		FVector& InOutTangent)
	{
		if (PreviousPose.bTangentValid
			&& FVector::DotProduct(
				PreviousPose.StrokeWorldTangent,
				InOutTangent) < 0.0f)
		{
			InOutTangent *= -1.0f;
		}
	}
}

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
	PlayerController->SetMouseLocation(ViewportWidth / 2, ViewportHeight / 2);
	ScrapeFocusPointerNormalized = FVector2D(0.5, 0.5);
	bHasScrapeFocusView = true;
	ClampScrapeFocusPointerToAuthorityCone();
	ResetScrapeFocusPointerStrokeConstraint();
	return true;
}

bool UMD_ScrapeToolComponent::ConsumeScrapeFocusLookInput(
	const FVector2D& LookInput)
{
	if (!IsScrapeFocusViewActive())
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
	if (!IsScrapeFocusViewActive() || !bHasScrapeFocusView)
	{
		return false;
	}

	InOutViewInfo.Rotation = ScrapeFocusViewRotation;
	InOutViewInfo.FOV = ScrapeFocusViewFOV;
	InOutViewInfo.DesiredFOV = ScrapeFocusViewFOV;
	return true;
}

FVector UMD_ScrapeToolComponent::GetScrapeFocusAimDirectionForPointer(
	const FVector2D& PointerNormalized) const
{
	if (!bHasScrapeFocusView
		|| ScrapeFocusViewportSize.X <= 0
		|| ScrapeFocusViewportSize.Y <= 0
		|| PointerNormalized.ContainsNaN()
		|| !FMath::IsFinite(ScrapeFocusSession.ScrapeFocusHorizontalHalfTangent)
		|| !FMath::IsFinite(ScrapeFocusSession.ScrapeFocusVerticalHalfTangent)
		|| ScrapeFocusSession.ScrapeFocusHorizontalHalfTangent <= UE_SMALL_NUMBER
		|| ScrapeFocusSession.ScrapeFocusVerticalHalfTangent <= UE_SMALL_NUMBER)
	{
		return FVector::ZeroVector;
	}

	const double ScreenX = (PointerNormalized.X - 0.5) * 2.0;
	const double ScreenY = (0.5 - PointerNormalized.Y) * 2.0;
	const FRotationMatrix ViewAxes(ScrapeFocusViewRotation);
	return (ViewAxes.GetScaledAxis(EAxis::X)
		+ ViewAxes.GetScaledAxis(EAxis::Y) * ScreenX
			* static_cast<double>(ScrapeFocusSession.ScrapeFocusHorizontalHalfTangent)
		+ ViewAxes.GetScaledAxis(EAxis::Z) * ScreenY
			* static_cast<double>(ScrapeFocusSession.ScrapeFocusVerticalHalfTangent)).GetSafeNormal();
}

FVector UMD_ScrapeToolComponent::GetScrapeFocusAimDirection() const
{
	// 방향 판정 구간에는 reticle만 움직이고 실제 판정 ray는 LMB 시작점에 둔다.
	const FVector2D AimPointerNormalized =
		bIsScraping && !ScrapeFocusSession.bHasScrapeFocusPointerStrokeAxis
		? ScrapeFocusSession.ScrapeFocusPointerStrokeAnchorNormalized
		: ScrapeFocusPointerNormalized;
	return GetScrapeFocusAimDirectionForPointer(AimPointerNormalized);
}

FVector UMD_ScrapeToolComponent::GetScrapeFocusVisualAimDirection() const
{
	// Animation pose는 lock 판정 중에도 화면에 표시된 포인터를 그대로 따른다.
	return GetScrapeFocusAimDirectionForPointer(ScrapeFocusPointerNormalized);
}

void UMD_ScrapeToolComponent::ForceRefreshLocalScrapeVisualContactPose(
	const bool bTeleportOrSnap)
{
	// 승인 clamp처럼 같은 frame의 앞선 visual trace를 의도적으로 폐기하는 경계다.
	LastScrapeVisualContactTraceFrame = MAX_uint64;
	bForceNextScrapeVisualContactSnap |= bTeleportOrSnap;
	UpdateLocalScrapeVisualContactPose(0.0f);
}

void UMD_ScrapeToolComponent::MarkScrapeVisualContactPoseUpdated()
{
	++ScrapeVisualContactPose.UpdateGeneration;
	if (ScrapeVisualContactPose.UpdateGeneration == 0)
	{
		++ScrapeVisualContactPose.UpdateGeneration;
	}
	ScrapeVisualContactPose.LastUpdatedFrame = GFrameCounter;
}

void UMD_ScrapeToolComponent::PublishInvalidScrapeVisualContactPose(
	const bool bTeleportOrSnap)
{
	const uint32 PreviousUpdateGeneration =
		ScrapeVisualContactPose.UpdateGeneration;
	ScrapeVisualContactPose = FMD_ScrapeVisualContactPose();
	ScrapeVisualContactPose.FocusSessionId = IsScrapeFocusActive()
		? ScrapeFocusSession.ActiveScrapeFocusSessionId
		: 0;
	ScrapeVisualContactPose.LmbStrokeGeneration = LmbStrokeGeneration;
	ScrapeVisualContactPose.UpdateGeneration = PreviousUpdateGeneration;
	ScrapeVisualContactPose.bTeleportOrSnap = bTeleportOrSnap;
	MarkScrapeVisualContactPoseUpdated();
}

void UMD_ScrapeToolComponent::ResetScrapeVisualContactTracking(
	const bool bPublishInvalidPose)
{
	ScrapeVisualContactSurface.Reset();
	ScrapeVisualContactPrimitive.Reset();
	ScrapeVisualContactSurfaceFace = EMD_ScrapeSurfaceFace::Front;
	ScrapeVisualStrokeAnchorWorldLocation = FVector::ZeroVector;
	ScrapeVisualStrokeAnchorSurface.Reset();
	ScrapeVisualStrokeAnchorPrimitive.Reset();
	ScrapeVisualStrokeAnchorFocusSessionId = 0;
	ScrapeVisualStrokeAnchorLmbGeneration = 0;
	bHasScrapeVisualStrokeAnchor = false;
	ScrapeVisualLockedWorldTangent = FVector::ZeroVector;
	ScrapeVisualLockedFocusSessionId = 0;
	ScrapeVisualLockedLmbGeneration = 0;
	ScrapeVisualInvalidElapsedSeconds = 0.0;
	LastScrapeVisualContactTraceFrame = MAX_uint64;
	bForceNextScrapeVisualContactSnap = false;
	if (bPublishInvalidPose)
	{
		PublishInvalidScrapeVisualContactPose(true);
	}
}

void UMD_ScrapeToolComponent::BeginScrapeVisualStroke()
{
	ScrapeVisualLockedWorldTangent = FVector::ZeroVector;
	ScrapeVisualLockedFocusSessionId = 0;
	ScrapeVisualLockedLmbGeneration = 0;
	ScrapeVisualInvalidElapsedSeconds = 0.0;
	ScrapeVisualStrokeAnchorFocusSessionId =
		ScrapeFocusSession.ActiveScrapeFocusSessionId;
	ScrapeVisualStrokeAnchorLmbGeneration = LmbStrokeGeneration;
	bHasScrapeVisualStrokeAnchor = ScrapeVisualContactPose.bValid
		&& ScrapeVisualContactPose.FocusSessionId
			== ScrapeFocusSession.ActiveScrapeFocusSessionId;
	if (bHasScrapeVisualStrokeAnchor)
	{
		ScrapeVisualStrokeAnchorWorldLocation =
			ScrapeVisualContactPose.ContactWorldLocation;
		ScrapeVisualStrokeAnchorSurface = ScrapeVisualContactSurface;
		ScrapeVisualStrokeAnchorPrimitive = ScrapeVisualContactPrimitive;
		ScrapeVisualContactPose.LmbStrokeGeneration = LmbStrokeGeneration;
		ScrapeVisualContactPose.bDirectionLocked = false;
		ScrapeVisualContactPose.bTeleportOrSnap = false;
		MarkScrapeVisualContactPoseUpdated();
	}
}

void UMD_ScrapeToolComponent::EndScrapeVisualStroke()
{
	ScrapeVisualLockedWorldTangent = FVector::ZeroVector;
	ScrapeVisualLockedFocusSessionId = 0;
	ScrapeVisualLockedLmbGeneration = 0;
	ScrapeVisualStrokeAnchorFocusSessionId = 0;
	ScrapeVisualStrokeAnchorLmbGeneration = 0;
	bHasScrapeVisualStrokeAnchor = false;
	if (ScrapeVisualContactPose.bValid)
	{
		// Ready pose는 마지막 유효 180도 동치 축을 유지하되 lock만 해제한다.
		ScrapeVisualContactPose.bDirectionLocked = false;
		ScrapeVisualContactPose.bTeleportOrSnap = false;
		MarkScrapeVisualContactPoseUpdated();
	}
}

void UMD_ScrapeToolComponent::UpdateLocalScrapeVisualContactPose(
	const float DeltaTime)
{
	const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled())
	{
		return;
	}

	const uint64 FocusSessionId =
		ScrapeFocusSession.ActiveScrapeFocusSessionId;
	if (!IsScrapeFocusActive()
		|| !bHasScrapeFocusView
		|| FocusSessionId == 0
		|| !IsScrapeToolCurrentlyEligible())
	{
		if (ScrapeVisualContactPose.bValid
			|| ScrapeVisualContactPose.FocusSessionId != FocusSessionId
			|| ScrapeVisualContactSurface.IsValid()
			|| ScrapeVisualContactPrimitive.IsValid()
			|| bHasScrapeVisualStrokeAnchor
			|| !ScrapeVisualLockedWorldTangent.IsNearlyZero())
		{
			ResetScrapeVisualContactTracking(true);
			// eligibility/reset 뒤 같은 render frame에 새 visual trace를 열지 않는다.
			LastScrapeVisualContactTraceFrame = GFrameCounter;
		}
		return;
	}

	if (LastScrapeVisualContactTraceFrame == GFrameCounter)
	{
		return;
	}
	LastScrapeVisualContactTraceFrame = GFrameCounter;

	const FVector VisualAimDirection = GetScrapeFocusVisualAimDirection();
	const AController* const OwnerController = OwnerPawn->GetController();
	FVector VisualTraceOrigin = FVector::ZeroVector;
	FRotator IgnoredCurrentViewRotation = FRotator::ZeroRotator;
	if (OwnerController)
	{
		OwnerController->GetPlayerViewPoint(
			VisualTraceOrigin,
			IgnoredCurrentViewRotation);
	}
	const bool bHasCurrentViewOrigin = OwnerController
		&& IsFiniteScrapeVisualVector(VisualTraceOrigin);
	FMD_ScrapeAimQueryResult VisualAim;
	if (bHasCurrentViewOrigin && !VisualAimDirection.IsNearlyZero())
	{
		// 회전/FOV는 캡처 basis를 유지하고 원점만 판정 경로와 같은 live view다.
		VisualAim = QueryScrapeAim(VisualTraceOrigin, VisualAimDirection);
	}

	FVector ContactNormal = VisualAim.WorldContactNormal;
	if (VisualAim.Surface)
	{
		FVector CanonicalSurfaceNormal = FVector::ZeroVector;
		if (VisualAim.Surface->TryGetScrapeSurfaceWorldNormalAtPosition(
			VisualAim.SurfacePositionCm, VisualAim.SurfaceFace,
			CanonicalSurfaceNormal))
		{
			ContactNormal = CanonicalSurfaceNormal;
		}
	}
    // Only this local rendering copy is raised to the solid crust. Authority ray,
    // cell ownership, brush work and barnacle priority continue to use the original wall.
    if (VisualAim.Surface && !VisualAim.Barnacle)
    {
        FVector SolidContact;
        if (VisualAim.Surface->TryGetSolidVisualWorldContact(VisualAim.SurfacePositionCm,
            VisualAim.WorldContactLocation, ContactNormal, SolidContact))
        {
            VisualAim.WorldContactLocation = SolidContact;
        }
    }
	const bool bHasValidContact = VisualAim.IsActionable()
		&& IsFiniteScrapeVisualVector(VisualAim.WorldContactLocation)
		&& IsFiniteScrapeVisualVector(ContactNormal)
		&& ContactNormal.Normalize();
	if (!bHasValidContact)
	{
		const double SafeDeltaTime = FMath::IsFinite(DeltaTime)
			? FMath::Max(static_cast<double>(DeltaTime), 0.0)
			: 0.0;
		ScrapeVisualInvalidElapsedSeconds += SafeDeltaTime;
		const double GraceSeconds = static_cast<double>(
			FMath::Max(TransientMissGraceSeconds, 0.0f));
		if (!bForceNextScrapeVisualContactSnap
			&& ScrapeVisualContactPose.bValid
			&& ScrapeVisualContactPose.FocusSessionId == FocusSessionId
			&& ScrapeVisualInvalidElapsedSeconds <= GraceSeconds)
		{
			// 짧은 trace 유실은 마지막 pose를 그대로 lease한다.
			return;
		}

		if (ScrapeVisualContactPose.bValid
			|| ScrapeVisualContactPose.FocusSessionId != FocusSessionId
			|| ScrapeVisualContactSurface.IsValid()
			|| ScrapeVisualContactPrimitive.IsValid()
			|| bHasScrapeVisualStrokeAnchor
			|| !ScrapeVisualLockedWorldTangent.IsNearlyZero())
		{
			ResetScrapeVisualContactTracking(true);
			// persistent miss를 같은 render frame에 다시 조회하지 않는다.
			LastScrapeVisualContactTraceFrame = GFrameCounter;
		}
		return;
	}

	ScrapeVisualInvalidElapsedSeconds = 0.0;
	UPrimitiveComponent* const ContactPrimitive = VisualAim.Hit.GetComponent();
	const bool bHadValidPose = ScrapeVisualContactPose.bValid;
	const bool bNewFocus = !bHadValidPose
		|| ScrapeVisualContactPose.FocusSessionId != FocusSessionId;
	const bool bSurfaceChanged = bHadValidPose
		&& (ScrapeVisualContactSurface.Get() != VisualAim.Surface.Get()
			|| (VisualAim.Surface
				&& ScrapeVisualContactSurfaceFace != VisualAim.SurfaceFace)
			|| (!VisualAim.Surface
				&& ScrapeVisualContactPrimitive.Get() != ContactPrimitive));
	const double PositionSnapDistanceCm = static_cast<double>(
		FMath::Max(ReconnectCandidateMaxCm, 0.0f));
	const bool bLargePositionJump = bHadValidPose
		&& FVector::DistSquared(
			ScrapeVisualContactPose.ContactWorldLocation,
			VisualAim.WorldContactLocation)
			> PositionSnapDistanceCm * PositionSnapDistanceCm;
	const float NormalSnapCosine = FMath::Cos(FMath::DegreesToRadians(
		FMath::Clamp(LargeCornerSplitDegrees, 0.0f, 180.0f)));
	const bool bLargeNormalJump = bHadValidPose
		&& FVector::DotProduct(
			ScrapeVisualContactPose.ContactWorldNormal,
			ContactNormal) < NormalSnapCosine;
	const bool bTeleportOrSnap = bNewFocus
		|| bForceNextScrapeVisualContactSnap
		|| bSurfaceChanged
		|| bLargePositionJump
		|| bLargeNormalJump;

	const bool bStrokeIdentityMatches = bHasScrapeVisualStrokeAnchor
		&& ScrapeVisualStrokeAnchorFocusSessionId == FocusSessionId
		&& ScrapeVisualStrokeAnchorLmbGeneration == LmbStrokeGeneration;
	if (bIsScraping && !bStrokeIdentityMatches)
	{
		ScrapeVisualStrokeAnchorWorldLocation = VisualAim.WorldContactLocation;
		ScrapeVisualStrokeAnchorSurface = VisualAim.Surface.Get();
		ScrapeVisualStrokeAnchorPrimitive = ContactPrimitive;
		ScrapeVisualStrokeAnchorFocusSessionId = FocusSessionId;
		ScrapeVisualStrokeAnchorLmbGeneration = LmbStrokeGeneration;
		bHasScrapeVisualStrokeAnchor = true;
	}

	FVector StrokeTangent = FVector::ZeroVector;
	bool bTangentValid = false;
	bool bDirectionLocked = false;
	bool bReusedLockedTangent = false;
	const bool bShouldLockDirection = bIsScraping
		&& ScrapeFocusSession.bHasScrapeFocusPointerStrokeAxis;
	if (bShouldLockDirection)
	{
		const bool bLockedIdentityMatches =
			ScrapeVisualLockedFocusSessionId == FocusSessionId
			&& ScrapeVisualLockedLmbGeneration == LmbStrokeGeneration
			&& !ScrapeVisualLockedWorldTangent.IsNearlyZero();
		if (bLockedIdentityMatches)
		{
			bTangentValid = TryProjectScrapeVisualTangent(
				ScrapeVisualLockedWorldTangent,
				ContactNormal,
				StrokeTangent);
			bReusedLockedTangent = bTangentValid;
		}
		else
		{
			const bool bAnchorTargetMatches = bHasScrapeVisualStrokeAnchor
				&& ScrapeVisualStrokeAnchorSurface.Get() == VisualAim.Surface.Get()
				&& (VisualAim.Surface
					|| ScrapeVisualStrokeAnchorPrimitive.Get() == ContactPrimitive);
			if (bAnchorTargetMatches)
			{
				bTangentValid = TryProjectScrapeVisualTangent(
					VisualAim.WorldContactLocation
						- ScrapeVisualStrokeAnchorWorldLocation,
					ContactNormal,
					StrokeTangent);
			}
			if (!bTangentValid)
			{
				const FVector2D ScreenAxis =
					ScrapeFocusSession.ScrapeFocusPointerStrokeAxis;
				const FRotationMatrix ViewAxes(ScrapeFocusViewRotation);
				const FVector ScreenAxisWorld =
					ViewAxes.GetScaledAxis(EAxis::Y)
						* ScreenAxis.X
						* static_cast<double>(ScrapeFocusSession.ScrapeFocusHorizontalHalfTangent)
					- ViewAxes.GetScaledAxis(EAxis::Z)
						* ScreenAxis.Y
						* static_cast<double>(ScrapeFocusSession.ScrapeFocusVerticalHalfTangent);
				bTangentValid = TryProjectScrapeVisualTangent(
					ScreenAxisWorld,
					ContactNormal,
					StrokeTangent);
			}
		}
	}

	const FRotationMatrix ViewAxes(ScrapeFocusViewRotation);
	if (!bTangentValid && ScrapeVisualContactPose.bTangentValid)
	{
		bTangentValid = TryProjectScrapeVisualTangent(
			ScrapeVisualContactPose.StrokeWorldTangent,
			ContactNormal,
			StrokeTangent);
	}
	if (!bTangentValid)
	{
		bTangentValid = TryProjectScrapeVisualTangent(
			ViewAxes.GetScaledAxis(EAxis::Z),
			ContactNormal,
			StrokeTangent);
	}
	if (!bTangentValid)
	{
		bTangentValid = TryProjectScrapeVisualTangent(
			ViewAxes.GetScaledAxis(EAxis::Y),
			ContactNormal,
			StrokeTangent);
	}
	if (!bTangentValid)
	{
		bTangentValid = TryProjectScrapeVisualTangent(
			ViewAxes.GetScaledAxis(EAxis::X),
			ContactNormal,
			StrokeTangent);
	}
	if (bTangentValid)
	{
		StabilizeScrapeVisualTangentSign(
			ScrapeVisualContactPose,
			StrokeTangent);
		bDirectionLocked = bShouldLockDirection;
		if (bDirectionLocked && !bReusedLockedTangent)
		{
			ScrapeVisualLockedWorldTangent = StrokeTangent;
			ScrapeVisualLockedFocusSessionId = FocusSessionId;
			ScrapeVisualLockedLmbGeneration = LmbStrokeGeneration;
		}
	}

	ScrapeVisualContactPose.ContactWorldLocation =
		VisualAim.WorldContactLocation;
	ScrapeVisualContactPose.ContactWorldNormal = ContactNormal;
	ScrapeVisualContactPose.StrokeWorldTangent = bTangentValid
		? StrokeTangent
		: FVector::ZeroVector;
	ScrapeVisualContactPose.FocusSessionId = FocusSessionId;
	ScrapeVisualContactPose.LmbStrokeGeneration = LmbStrokeGeneration;
	ScrapeVisualContactPose.bValid = true;
	ScrapeVisualContactPose.bTangentValid = bTangentValid;
	ScrapeVisualContactPose.bDirectionLocked = bDirectionLocked;
	ScrapeVisualContactPose.bTeleportOrSnap = bTeleportOrSnap;
	MarkScrapeVisualContactPoseUpdated();
	bForceNextScrapeVisualContactSnap = false;

	ScrapeVisualContactSurface = VisualAim.Surface.Get();
	ScrapeVisualContactPrimitive = ContactPrimitive;
	ScrapeVisualContactSurfaceFace = VisualAim.SurfaceFace;
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
