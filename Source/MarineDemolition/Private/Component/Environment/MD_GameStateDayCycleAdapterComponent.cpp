#include "Component/Environment/MD_GameStateDayCycleAdapterComponent.h"

#include "Kismet/GameplayStatics.h"
#include "Game/MD_GameStateBase.h"

UMD_GameStateDayCycleAdapterComponent::UMD_GameStateDayCycleAdapterComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

AMD_GameStateBase* UMD_GameStateDayCycleAdapterComponent::ResolveGameState() const
{
	if (const UWorld* World = GetWorld())
	{
		return Cast<AMD_GameStateBase>(UGameplayStatics::GetGameState(World));
	}
	return nullptr;
}

bool UMD_GameStateDayCycleAdapterComponent::IsDayCycleActive_Implementation() const
{
	if (!bDriveFromGameState)
	{
		return false;
	}

	return ResolveGameState() != nullptr;
}

float UMD_GameStateDayCycleAdapterComponent::GetTimeOfDayHours_Implementation() const
{
	if (const AMD_GameStateBase* GS = ResolveGameState())
	{
		// GameState is wall-clock (8 = 08:00). Sky controller 12 = midnight.
		return FMath::Fmod(GS->GetTimeOfDayHours() + 12.f, 24.f);
	}
	return 20.f; // 08:00
}

float UMD_GameStateDayCycleAdapterComponent::GetDayCycleProgress_Implementation() const
{
	if (const AMD_GameStateBase* GS = ResolveGameState())
	{
		return FMath::Frac((GS->GetTimeOfDayHours() + 12.f) / 24.f);
	}
	return 0.f;
}
