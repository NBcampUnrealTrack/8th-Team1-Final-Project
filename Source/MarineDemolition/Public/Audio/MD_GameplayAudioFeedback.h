#pragma once
#include "CoreMinimal.h"
#include "Audio/MD_AudioSubsystem.h"
class APawn;

/** Presentation only. Call on each peer after replicated state or a validated multicast. */
namespace MDAudioFeedback
{
	MARINEDEMOLITION_API FMD_AudioHandle World(AActor* Source, const TCHAR* Event);
	MARINEDEMOLITION_API FMD_AudioHandle At(const UObject* Context, const TCHAR* Event, const FVector& Location);
	MARINEDEMOLITION_API FMD_AudioHandle Attached(USceneComponent* Source, const TCHAR* Event);
	MARINEDEMOLITION_API FMD_AudioHandle UI(const UObject* Context, const TCHAR* Event);
	MARINEDEMOLITION_API FMD_AudioHandle LocalPawn(const APawn* Pawn, const TCHAR* Event);
	MARINEDEMOLITION_API void Stop(const UObject* Context, FMD_AudioHandle& Handle, float Fade = .06f);
}
