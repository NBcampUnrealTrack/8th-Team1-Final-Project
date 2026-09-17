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
};
