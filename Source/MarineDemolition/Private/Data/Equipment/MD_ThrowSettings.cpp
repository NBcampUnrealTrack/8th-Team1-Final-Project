#include "Data/Equipment/MD_ThrowSettings.h"

bool UMD_ThrowSettings::IsValidForThrow() const
{
	const auto ValidSpeeds = [](const FVector2D& Speeds)
	{
		return FMath::IsFinite(Speeds.X) && FMath::IsFinite(Speeds.Y)
			&& Speeds.X >= 0.0 && Speeds.Y >= Speeds.X;
	};
	return FMath::IsFinite(MaxChargeSeconds) && MaxChargeSeconds > 0.0f
		&& ValidSpeeds(TrashLaunchSpeed) && ValidSpeeds(EquipmentLaunchSpeed)
		&& FMath::IsFinite(UpwardAimBias) && UpwardAimBias >= 0.0f && UpwardAimBias <= 1.0f
		&& FMath::IsFinite(InheritedVelocityScale) && InheritedVelocityScale >= 0.0f && InheritedVelocityScale <= 1.0f
		&& FMath::IsFinite(EquipmentReleaseDistance) && EquipmentReleaseDistance > 0.0f
		&& FMath::IsFinite(CollisionClearance) && CollisionClearance >= 0.0f
		&& FMath::IsFinite(ThrowCooldownSeconds) && ThrowCooldownSeconds >= 0.0f;
}
