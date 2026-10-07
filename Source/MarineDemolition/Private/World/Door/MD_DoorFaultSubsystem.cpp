#include "World/Door/MD_DoorFaultSubsystem.h"
#include "EngineUtils.h"
#include "World/Door/MD_DoorFaultComponent.h"
#if !UE_BUILD_SHIPPING
#include "HAL/IConsoleManager.h"
#include "GameFramework/PlayerController.h"
#include "World/Door/MD_DoorWiringPanel.h"

namespace
{
void MDBreakDoorCommand(const TArray<FString>& Args, UWorld* World)
{
	if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_Client)
	{
		UE_LOG(LogTemp, Display, TEXT("MD.BreakDoor: run in the host/server game console."));
		return;
	}
	if (Args.Num() > 1)
	{
		UE_LOG(LogTemp, Display, TEXT("MD.BreakDoor [ActorName]: omit the name to target the door or wiring panel in view."));
		return;
	}
	AActor* Target = nullptr;
	if (!Args.IsEmpty())
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->IsActorBeingDestroyed()) continue;
			bool bMatches = It->GetName().Equals(Args[0], ESearchCase::IgnoreCase);
#if WITH_EDITOR
			bMatches |= It->GetActorLabel().Equals(Args[0], ESearchCase::IgnoreCase);
#endif
			if (!bMatches) continue;
			if (Target)
			{
				UE_LOG(LogTemp, Display, TEXT("MD.BreakDoor: ambiguous name '%s'; use the exact actor name."), *Args[0]);
				return;
			}
			Target = *It;
		}
	}
	else
	{
		APlayerController* LocalPC = nullptr;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			if (It->IsValid() && It->Get()->IsLocalController()) { LocalPC = It->Get(); break; }
		if (LocalPC)
		{
			FVector Start; FRotator Rotation; LocalPC->GetPlayerViewPoint(Start, Rotation);
			FCollisionQueryParams Query(SCENE_QUERY_STAT(MDBreakDoor), true, LocalPC->GetPawn());
			FHitResult Hit;
			if (World->LineTraceSingleByChannel(Hit, Start, Start + Rotation.Vector() * 2000.f, ECC_Visibility, Query))
				Target = Hit.GetActor();
		}
	}
	if (const AMD_DoorWiringPanel* Panel = Cast<AMD_DoorWiringPanel>(Target)) Target = Panel->LinkedDoor;
	UMD_DoorFaultComponent* Fault = IsValid(Target) && Target->GetWorld() == World
		? Target->FindComponentByClass<UMD_DoorFaultComponent>() : nullptr;
	if (!Fault)
	{
		UE_LOG(LogTemp, Display, TEXT("MD.BreakDoor: no fault-enabled door found. Aim at a door/panel within 20m or specify its exact name. Exempt doors stay exempt."));
		return;
	}
	const bool bBroken = Fault->DebugForceBreak();
	UE_LOG(LogTemp, Display, TEXT("MD.BreakDoor: %s %s (incident %d)."), *Target->GetName(),
		bBroken ? TEXT("broken; repair at its wiring panel") : TEXT("unchanged (already faulty or exempt)"), Fault->GetSnapshot().FaultId);
}

FAutoConsoleCommandWithWorldAndArgs GMDForceDoorFault(
	TEXT("MD.BreakDoor"),
	TEXT("Force one door fault for testing. MD.BreakDoor [ActorName]; no argument targets the visible door/panel within 20m. Host/server only; bypasses occupancy, preserves fault exemptions."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MDBreakDoorCommand), ECVF_Cheat);
}
#endif

void UMD_DoorFaultSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!InWorld.IsGameWorld() || InWorld.GetNetMode() == NM_Client) return;
	DoorClass = LoadClass<AActor>(nullptr, TEXT("/Game/Assets/X7/Interior/Objects/Door/BP_Door.BP_Door_C"));
	BlackDoorClass = LoadClass<AActor>(nullptr, TEXT("/Game/Assets/X7/Interior/Objects/Door/BP_Door_Black.BP_Door_Black_C"));
	if (!DoorClass && !BlackDoorClass) return;
	for (TActorIterator<AActor> It(&InWorld); It; ++It) AttachFaultAdapter(*It);
	SpawnHandle = InWorld.AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &ThisClass::AttachFaultAdapter));
}

void UMD_DoorFaultSubsystem::AttachFaultAdapter(AActor* Actor)
{
	if (!IsValid(Actor) || !((DoorClass && Actor->IsA(DoorClass)) || (BlackDoorClass && Actor->IsA(BlackDoorClass))) || !Actor->HasAuthority()
		|| Actor->ActorHasTag(UMD_FacilityFaultComponent::FaultExemptActorTag)
		|| Actor->FindComponentByClass<UMD_FacilityFaultComponent>()) return;
	Actor->SetReplicates(true);
	UMD_DoorFaultComponent* Adapter = NewObject<UMD_DoorFaultComponent>(Actor, TEXT("FacilityDoorFaultRuntime"));
	Actor->AddInstanceComponent(Adapter);
	Adapter->RegisterComponent();
}

void UMD_DoorFaultSubsystem::Deinitialize()
{
	if (GetWorld() && SpawnHandle.IsValid()) GetWorld()->RemoveOnActorSpawnedHandler(SpawnHandle);
	SpawnHandle.Reset();
	DoorClass = nullptr;
	BlackDoorClass = nullptr;
	Super::Deinitialize();
}
