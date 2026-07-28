#include "Environment/MD_GameStateDayCycleAdapterComponent.h"

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

	if (const AMD_GameStateBase* GS = ResolveGameState())
	{
		return GS->GetElapsedRealSeconds() > 0.0 || GS->GetElapsedGameSeconds() > 0.0;
	}
	return false;
}

float UMD_GameStateDayCycleAdapterComponent::GetTimeOfDayHours_Implementation() const
{
	if (const AMD_GameStateBase* GS = ResolveGameState())
	{
		return GS->GetTimeOfDayHours();
	}
	return 0.f;
}

float UMD_GameStateDayCycleAdapterComponent::GetDayCycleProgress_Implementation() const
{
	if (const AMD_GameStateBase* GS = ResolveGameState())
	{
		return FMath::Frac(GS->GetTimeOfDayHours() / 24.f);
	}
	return 0.f;
}
