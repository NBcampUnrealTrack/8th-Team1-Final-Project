#include "World/Sample/MD_QuestRackSpawner.h"
#include "Types/Persistence/MD_WorldActorSnapshotTypes.h"

#include "World/Sample/MD_QuestItem.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"

void AMD_QuestRackSpawner::CaptureRackState(FMD_QuestRackRecord& OutRecord) const
{
	OutRecord.Mode = static_cast<uint8>(RackMode);
	OutRecord.bCartridgeReleased = bCartridgeReleased;
	OutRecord.bCartridgeCollected = bCartridgeCollected;
	OutRecord.bRackDisabled = bRackDisabled;
	OutRecord.CartridgeActorName = IsValid(ReleasedCartridge) ? ReleasedCartridge->GetFName() : NAME_None;
	OutRecord.CartridgeLevelPackage = IsValid(ReleasedCartridge)
		? FName(UWorld::RemovePIEPrefix(ReleasedCartridge->GetLevel()->GetOutermost()->GetName())) : NAME_None;
	OutRecord.bPadlockCut = bPadlockCut;
	OutRecord.CutSeconds = PadlockCutSeconds;
	OutRecord.BarnacleRemovalRatios.Reset();
}

bool AMD_QuestRackSpawner::ValidateRackState(const FMD_QuestRackRecord& Record)
{
	if ((Record.bCartridgeCollected && (!Record.bCartridgeReleased || Record.bRackDisabled))
		|| Record.CartridgeActorName.IsNone() != Record.CartridgeLevelPackage.IsNone()
		|| (!Record.bCartridgeReleased && !Record.CartridgeActorName.IsNone())) return false;
	if (Record.Mode > static_cast<uint8>(EMD_QuestRackMode::Open)
		|| !FMath::IsFinite(Record.CutSeconds) || Record.CutSeconds < 0.f) return false;
	if (Record.Mode != static_cast<uint8>(EMD_QuestRackMode::BarnaclesAndPadlock))
		return !Record.bPadlockCut && Record.CutSeconds == 0.f && Record.BarnacleRemovalRatios.IsEmpty();
	// Accept historical barnacle data without recreating the retired gate.
	if (Record.BarnacleRemovalRatios.Num() > 256) return false;
	for (const float Ratio : Record.BarnacleRemovalRatios)
	{
		if (!FMath::IsFinite(Ratio) || Ratio < 0.f || Ratio > 1.f) return false;
	}
	if (Record.bCartridgeReleased && !Record.bPadlockCut) return false;
	return true;
}

bool AMD_QuestRackSpawner::CanRestoreRackState(const FMD_QuestRackRecord& Record) const
{
	if (!ValidateRackState(Record) || Record.Mode != static_cast<uint8>(RackMode)) return false;
	if (RackMode != EMD_QuestRackMode::BarnaclesAndPadlock) return true;
	if (!FMath::IsFinite(RequiredPadlockCutSeconds) || RequiredPadlockCutSeconds <= 0.f) return false;
	const float Required = FMath::Max(RequiredPadlockCutSeconds, 0.1f);
	return Record.CutSeconds <= Required && (!Record.bPadlockCut || Record.CutSeconds >= Required);
}

bool AMD_QuestRackSpawner::RestoreRackState(const FMD_QuestRackRecord& Record)
{
	if (!HasAuthority() || !CanRestoreRackState(Record)) return false;
	bCartridgeReleased = Record.bCartridgeReleased;
	bCartridgeCollected = Record.bCartridgeCollected;
	bRackDisabled = Record.bRackDisabled;
	ReleasedCartridge = nullptr;
	if (!Record.CartridgeActorName.IsNone())
	{
		for (TActorIterator<AMD_QuestItem> It(GetWorld()); It; ++It)
		{
			if (It->GetFName() == Record.CartridgeActorName
				&& FName(UWorld::RemovePIEPrefix(It->GetLevel()->GetOutermost()->GetName())) == Record.CartridgeLevelPackage
				&& It->QuestDefinitionId == RackQuestDefinitionId)
			{
				ReleasedCartridge = *It;
				It->SetOwner(this);
				break;
			}
		}
		if (!ReleasedCartridge && !bCartridgeCollected) return false;
	}
	bPadlockCut = Record.bPadlockCut;
	PadlockCutSeconds = Record.CutSeconds;
	if (RackMode == EMD_QuestRackMode::Contamination && bCartridgeReleased)
	{
		if (!RackScrapeSurface->CompleteCleaningWithoutDrops()) return false;
	}
	OnRep_RackState();
	ForceNetUpdate();
	return true;
}
