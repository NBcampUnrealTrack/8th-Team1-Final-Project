#pragma once

#include "World/Trash/MD_TrashSpawnVolume.h"
#include "MD_TrashFloorGenerator.generated.h"

/** Place once per map to generate trash from imported floor metadata, without drawing volumes. */
UCLASS(Blueprintable, HideCategories = ("MD|Trash Spawn|Shape"))
class MARINEDEMOLITION_API AMD_TrashFloorGenerator : public AMD_TrashSpawnVolume
{
	GENERATED_BODY()
public:
	AMD_TrashFloorGenerator();
};
