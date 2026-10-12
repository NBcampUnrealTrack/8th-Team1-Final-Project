#include "World/Door/MD_DoorStatusLightSubsystem.h"

#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "World/Door/MD_DoorStatusLightComponent.h"

void UMD_DoorStatusLightSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (InWorld.GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	DoorClass = LoadClass<AActor>(
		nullptr,
		TEXT("/Game/Assets/X7/Interior/Objects/Door/BP_Door.BP_Door_C"));
	BlackDoorClass = LoadClass<AActor>(nullptr, TEXT("/Game/Assets/X7/Interior/Objects/Door/BP_Door_Black.BP_Door_Black_C"));
	if (!IsValid(DoorClass) && !IsValid(BlackDoorClass))
	{
		UE_LOG(LogTemp, Warning, TEXT("[DoorStatusLight] BP_Door class not found"));
		return;
	}

	for (TActorIterator<AActor> It(&InWorld); It; ++It)
	{
		AttachStatusLight(*It);
	}

	ActorSpawnedHandle = InWorld.AddOnActorSpawnedHandler(
		FOnActorSpawned::FDelegate::CreateUObject(this, &ThisClass::HandleActorSpawned));
}

void UMD_DoorStatusLightSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld(); IsValid(World) && ActorSpawnedHandle.IsValid())
	{
		World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
		ActorSpawnedHandle.Reset();
	}

	DoorClass = nullptr;
	BlackDoorClass = nullptr;
	Super::Deinitialize();
}

void UMD_DoorStatusLightSubsystem::AttachStatusLight(AActor* Door)
{
	if (!IsValid(Door)
		|| !((IsValid(DoorClass) && Door->IsA(DoorClass)) || (IsValid(BlackDoorClass) && Door->IsA(BlackDoorClass)))
		|| IsValid(Door->FindComponentByClass<UMD_DoorStatusLightComponent>()))
	{
		return;
	}

	UMD_DoorStatusLightComponent* StatusLight =
		NewObject<UMD_DoorStatusLightComponent>(Door, TEXT("DoorStatusLightRuntime"));
	if (!IsValid(StatusLight))
	{
		return;
	}

	Door->AddInstanceComponent(StatusLight);
	StatusLight->RegisterComponent();
}

void UMD_DoorStatusLightSubsystem::HandleActorSpawned(AActor* Actor)
{
	AttachStatusLight(Actor);
}
