#pragma once
#include "CoreMinimal.h"
class AActor;
class UWorld;
struct FHitResult;

/** Shared visible-sticker picking for editor erase and authoritative scraper strokes. */
class MARINEDEMOLITION_API FMD_StainTargeting final
{
public:
	static bool Trace(UWorld& World, const FVector& Origin, const FVector& Direction, double Reach,
		FHitResult& OutHit, const AActor* IgnoredActor = nullptr);
};
