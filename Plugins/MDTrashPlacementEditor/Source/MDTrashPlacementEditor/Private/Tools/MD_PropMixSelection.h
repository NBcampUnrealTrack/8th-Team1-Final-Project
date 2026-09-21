#pragma once

#include "CoreMinimal.h"

/** Equal base weights with bounded, per-entry recency bias. Reading a preview never advances history. */
class FMD_PropMixSelection
{
public:
	void Reset();
	void SetCandidates(TArray<FGuid> InIds);
	int32 PickIndex(int32 Seed, uint32 CandidateIndex) const;
	void Consume(const FGuid& SelectedId);

private:
	double GetWeight(const FGuid& Id) const;
	TArray<FGuid> Ids;
	TMap<FGuid, uint32> Misses;
};
