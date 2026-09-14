#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Contamination/MD_SurfaceStainSize.h"
#include "MD_SurfaceStainRecord.generated.h"

/** Stable level identity; zero is retained to prevent reward replay after load. */
USTRUCT()
struct MARINEDEMOLITION_API FMD_SurfaceStainRecord
{
	GENERATED_BODY()
	UPROPERTY() FSoftObjectPath ActorPath;
	UPROPERTY() int32 RemainingSize = INDEX_NONE;
	/** Read-only compatibility with schema 12 saves. New saves use RemainingSize. */
	UPROPERTY() uint8 RemainingStage = 3;

	int32 ResolveSize() const
	{
		return RemainingSize == INDEX_NONE ? FMD_SurfaceStainSize::FromLegacyStage(RemainingStage) : RemainingSize;
	}
};
