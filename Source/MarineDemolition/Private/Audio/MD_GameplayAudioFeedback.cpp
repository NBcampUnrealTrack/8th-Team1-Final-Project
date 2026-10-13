#include "Audio/MD_GameplayAudioFeedback.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

namespace
{
UMD_AudioSubsystem* Resolve(const UObject* Context)
{
	UWorld* W = IsValid(Context) ? Context->GetWorld() : nullptr;
	return W && W->IsGameWorld() && W->GetNetMode() != NM_DedicatedServer
		? W->GetSubsystem<UMD_AudioSubsystem>() : nullptr;
}
FGameplayTag Tag(const TCHAR* Event) { return FGameplayTag::RequestGameplayTag(FName(Event), false); }
}
FMD_AudioHandle MDAudioFeedback::At(const UObject* Context, const TCHAR* Event, const FVector& Location)
{
	if (auto* Audio = Resolve(Context)) return Audio->PlayEventAtLocation(Tag(Event), Location);
	return {};
}
FMD_AudioHandle MDAudioFeedback::World(AActor* Source, const TCHAR* Event)
{
	return IsValid(Source) ? At(Source, Event, Source->GetActorLocation()) : FMD_AudioHandle();
}
FMD_AudioHandle MDAudioFeedback::Attached(USceneComponent* Source, const TCHAR* Event)
{
	if (auto* Audio = Resolve(Source)) return Audio->PlayEventAttached(Tag(Event), Source);
	return {};
}
FMD_AudioHandle MDAudioFeedback::UI(const UObject* Context, const TCHAR* Event)
{
	if (auto* Audio = Resolve(Context)) return Audio->PlayEvent2D(Tag(Event));
	return {};
}
FMD_AudioHandle MDAudioFeedback::LocalPawn(const APawn* Pawn, const TCHAR* Event)
{
	return IsValid(Pawn) && Pawn->IsLocallyControlled() ? UI(Pawn, Event) : FMD_AudioHandle();
}
void MDAudioFeedback::Stop(const UObject* Context, FMD_AudioHandle& Handle, float Fade)
{
	if (Handle.IsValid()) if (auto* Audio = Resolve(Context)) Audio->StopHandle(Handle, Fade);
	Handle = {};
}
