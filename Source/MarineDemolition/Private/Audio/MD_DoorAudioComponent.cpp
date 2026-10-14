#include "Audio/MD_DoorAudioComponent.h"
#include "Audio/MD_GameplayAudioFeedback.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

void UMD_DoorAudioComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetWorld()->GetNetMode() == NM_DedicatedServer) return;
	Refresh();
	GetWorld()->GetTimerManager().SetTimer(Timer, this, &ThisClass::Refresh, .1f, true);
}
void UMD_DoorAudioComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(Timer);
	Super::EndPlay(Reason);
}
void UMD_DoorAudioComponent::Refresh()
{
	AActor* Door = GetOwner();
	if (!IsValid(Door)) return;
	const auto* Open = FindFProperty<FBoolProperty>(Door->GetClass(), TEXT("bDoorOpen"));
	if (!Open) return;
	const bool Value = Open->GetPropertyValue_InContainer(Door);
	if (PreviousOpen.IsSet() && PreviousOpen.GetValue() != Value)
		MDAudioFeedback::World(Door, Value ? TEXT("SFX.World.Door.Open") : TEXT("SFX.World.Door.Close"));
	PreviousOpen = Value;
}
