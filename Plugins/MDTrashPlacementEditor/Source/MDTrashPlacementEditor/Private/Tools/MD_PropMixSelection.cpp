#include "Tools/MD_PropMixSelection.h"

#include "Math/RandomStream.h"
#include "Templates/TypeHash.h"

void FMD_PropMixSelection::Reset()
{
	Ids.Reset();
	Misses.Reset();
}

void FMD_PropMixSelection::SetCandidates(TArray<FGuid> InIds)
{
	TMap<FGuid, uint32> Updated;
	Updated.Reserve(InIds.Num());
	for (const FGuid& Id : InIds)
	{
		Updated.Add(Id, Misses.FindRef(Id));
	}
	Ids = MoveTemp(InIds);
	Misses = MoveTemp(Updated);
}

double FMD_PropMixSelection::GetWeight(const FGuid& Id) const
{
	// Every N-1 misses add 25% to the base weight, capped at twice the base.
	return 1.0 + FMath::Min(1.0, 0.25 * Misses.FindRef(Id) / FMath::Max(1, Ids.Num() - 1));
}

int32 FMD_PropMixSelection::PickIndex(int32 Seed, uint32 CandidateIndex) const
{
	if (Ids.IsEmpty())
	{
		return INDEX_NONE;
	}
	double Total = 0.0;
	for (const FGuid& Id : Ids)
	{
		Total += GetWeight(Id);
	}
	const FRandomStream Random(static_cast<int32>(HashCombineFast(GetTypeHash(Seed), CandidateIndex)));
	double Draw = Random.GetFraction() * Total;
	for (int32 Index = 0; Index < Ids.Num(); ++Index)
	{
		Draw -= GetWeight(Ids[Index]);
		if (Draw < 0.0)
		{
			return Index;
		}
	}
	return Ids.Num() - 1;
}

void FMD_PropMixSelection::Consume(const FGuid& SelectedId)
{
	if (!Misses.Contains(SelectedId))
	{
		return;
	}
	for (TPair<FGuid, uint32>& Item : Misses)
	{
		Item.Value = Item.Key == SelectedId ? 0 : FMath::Min(Item.Value + 1, 1000000u);
	}
}
