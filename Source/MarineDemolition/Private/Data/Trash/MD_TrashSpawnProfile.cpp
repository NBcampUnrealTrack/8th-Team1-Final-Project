#include "Data/Trash/MD_TrashSpawnProfile.h"

UMD_TrashSpawnProfile::UMD_TrashSpawnProfile()
{
	PlayerCountMultipliers = { 1.0f, 1.7f, 2.3f, 2.8f };
	DifficultyMultipliers.Add(EMD_Difficulty::Safe, 0.8f);
	DifficultyMultipliers.Add(EMD_Difficulty::Standard, 1.0f);
	DifficultyMultipliers.Add(EMD_Difficulty::Dangerous, 1.2f);
}

FPrimaryAssetId UMD_TrashSpawnProfile::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("TrashSpawnProfile"), GetFName());
}

float UMD_TrashSpawnProfile::GetPlayerCountMultiplier(const int32 PlayerCount) const
{
	if (PlayerCountMultipliers.IsEmpty())
	{
		return 1.0f;
	}
	const int32 Index = FMath::Clamp(PlayerCount - 1, 0, PlayerCountMultipliers.Num() - 1);
	const float Multiplier = PlayerCountMultipliers[Index];
	return FMath::IsFinite(Multiplier) && Multiplier > 0.0f ? Multiplier : 1.0f;
}

float UMD_TrashSpawnProfile::GetDifficultyMultiplier(const EMD_Difficulty Difficulty) const
{
	const float* Multiplier = DifficultyMultipliers.Find(Difficulty);
	return Multiplier && FMath::IsFinite(*Multiplier) && *Multiplier > 0.0f ? *Multiplier : 1.0f;
}

int32 UMD_TrashSpawnProfile::ComputeTotalTrashCount(const int32 PlayerCount, const EMD_Difficulty Difficulty) const
{
	return FMath::Max(0, FMath::RoundToInt32(BaseTrashCount * GetPlayerCountMultiplier(FMath::Max(1, PlayerCount)) * GetDifficultyMultiplier(Difficulty)));
}
