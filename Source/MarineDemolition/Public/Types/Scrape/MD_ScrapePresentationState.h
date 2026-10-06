#pragma once

#include "CoreMinimal.h"

/** Animation-only contact snapshot. It never authorizes gameplay or replicates. */
struct FMD_ScrapeVisualContactPose
{
	FVector ContactWorldLocation = FVector::ZeroVector;
	FVector ContactWorldNormal = FVector::ZeroVector;
	FVector StrokeWorldTangent = FVector::ZeroVector;
	uint64 FocusSessionId = 0;
	uint32 LmbStrokeGeneration = 0;
	uint64 LastUpdatedFrame = MAX_uint64;
	uint32 UpdateGeneration = 0;
	bool bValid = false;
	bool bTangentValid = false;
	bool bDirectionLocked = false;
	bool bTeleportOrSnap = false;
};

/** Preserved wall-focus display state. No active input or ordinary equip path enables it. */
struct FMD_ScrapePresentationState
{
	bool bCenterViewActive = false;
	bool bSurfaceStrokeActive = false;
	FVector2D PointerNormalized = FVector2D(0.5, 0.5);
	FMD_ScrapeVisualContactPose VisualContactPose;
	// Retain the archived independent-viewmodel arm policy without enabling it.
	bool bHideFirstPersonArmsForLegacyViewModel = false;

	bool IsViewActive() const { return bCenterViewActive; }
	bool IsSurfaceScrapeVisualStrokeActive() const { return bSurfaceStrokeActive; }
	FVector2D GetPointerNormalized() const { return PointerNormalized; }
	bool GetScrapeVisualContactPose(FMD_ScrapeVisualContactPose& OutPose) const
	{
		OutPose = VisualContactPose;
		return OutPose.bValid;
	}
};
