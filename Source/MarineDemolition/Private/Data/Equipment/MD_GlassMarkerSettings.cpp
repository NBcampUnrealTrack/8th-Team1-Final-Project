#include "Data/Equipment/MD_GlassMarkerSettings.h"

bool UMD_GlassMarkerSettings::IsValidForMarker() const
{
	return FMath::IsFinite(ReachCm) && ReachCm > 0.0f && ReachCm <= 250.0f
		&& FMath::IsFinite(SampleIntervalSeconds) && SampleIntervalSeconds >= 0.05f && SampleIntervalSeconds <= 0.25f
		&& FMath::IsFinite(MinimumPointSpacingCm) && MinimumPointSpacingCm > 0.0f
		&& FMath::IsFinite(MaximumJoinGapSeconds) && MaximumJoinGapSeconds >= SampleIntervalSeconds && MaximumJoinGapSeconds <= 1.0f
		&& FMath::IsFinite(MaximumSegmentLengthCm) && MaximumSegmentLengthCm >= MinimumPointSpacingCm && MaximumSegmentLengthCm <= 100.0f
		&& !EraserRotationOffset.ContainsNaN();
}
