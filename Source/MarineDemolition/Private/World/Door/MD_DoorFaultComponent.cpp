#include "World/Door/MD_DoorFaultComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"
#include "Components/PrimitiveComponent.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Components/BoxComponent.h"
#include "Engine/TriggerBox.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/SpectatorPawn.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "World/Facility/MD_RepairInstruction.h"

const FString UMD_DoorFaultComponent::RepairRoomTagPrefix(TEXT("MD.DoorFault.Room."));

bool UMD_DoorFaultComponent::TryRepairWithCrowbar(APawn* Pawn, int32 ExpectedFaultId)
{
	AActor* Door = GetOwner();
	if (!IsValid(Door) || !Door->HasAuthority() || !bOwnsLock || bPreviousLocked
		|| !CanReachForRepair(Pawn) || GetSnapshot().FaultId != ExpectedFaultId) return false;
	const UMD_EquipHotbarComponent* Hotbar = Pawn->FindComponentByClass<UMD_EquipHotbarComponent>();
	const UMD_ToolDefinition* Tool = Hotbar ? Hotbar->GetCurrentTool() : nullptr;
	if (!IsValid(Tool) || !Tool->ToolAbilityTag.MatchesTagExact(MDGameplayTags::Ability_UtilityRepair_Crowbar)) return false;
	FVector ViewOrigin; FRotator ViewRotation;
	Pawn->GetController()->GetPlayerViewPoint(ViewOrigin, ViewRotation);
	FHitResult ReachHit;
	FCollisionQueryParams ReachQuery(SCENE_QUERY_STAT(CrowbarServerReach), true, Pawn);
	if (!GetWorld()->LineTraceSingleByChannel(ReachHit, ViewOrigin, ViewOrigin + ViewRotation.Vector() * RepairReach, ECC_Visibility, ReachQuery)
		|| ReachHit.GetActor() != Door
		|| FVector::DistSquared(Pawn->GetActorLocation(), ReachHit.ImpactPoint) > FMath::Square(RepairReach + Pawn->GetSimpleCollisionHalfHeight())) return false;
	// A puzzle-owned lock is not an ordinary fault and cannot be bypassed.
	for (TActorIterator<AMD_RepairInstructionController> It(GetWorld()); It; ++It)
		if (It->InstructionDoor == Door && It->State.bDoorLocked) return false;
	FBoolProperty* Open = FindFProperty<FBoolProperty>(Door->GetClass(), TEXT("bDoorOpen"));
	UFunction* ApplyOpen = Door->FindFunction(TEXT("OnRep_bDoorOpen"));
	if (!Open || !ApplyOpen || ApplyOpen->NumParms != 0) return false;
	// Completion releases the fault-owned lock, alarms and manual repair occupancy first.
	if (!CompleteRepairFromValidatedSource(ExpectedFaultId)) return false;
	Open->SetPropertyValue_InContainer(Door, true);
	Door->ProcessEvent(ApplyOpen, nullptr);
	Door->FlushNetDormancy();
	Door->ForceNetUpdate();
	PlayFeedbackForAll(TEXT("SFX.Tool.Crowbar.Pry"), Door->GetActorLocation());
	PlayFeedbackForAll(TEXT("SFX.Tool.Crowbar.Release"), Door->GetActorLocation());
	return true;
}

bool UMD_DoorFaultComponent::HasRepairRoomOccupant() const
{
	const AActor* Door = GetOwner();
	const UWorld* World = GetWorld();
	if (!IsValid(Door) || !World || !World->IsGameWorld() || !Door->HasAuthority()
		|| World->GetNetMode() == NM_Client) return false;
	FName RoomTag = NAME_None;
	for (const FName Tag : Door->Tags)
	{
		if (!Tag.ToString().StartsWith(RepairRoomTagPrefix)) continue;
		// An ambiguous or absent assignment must never trap an empty room.
		if (!RoomTag.IsNone() && RoomTag != Tag) return false;
		RoomTag = Tag;
	}
	if (RoomTag.IsNone()) return false;
	for (TActorIterator<ATriggerBox> It(World); It; ++It)
	{
		if (It->IsActorBeingDestroyed() || !It->ActorHasTag(RoomTag)) continue;
		const UBoxComponent* Box = Cast<UBoxComponent>(It->GetCollisionComponent());
		if (!Box || Box->GetUnscaledBoxExtent().GetMin() <= 0.f
			|| Box->GetComponentScale().GetAbs().GetMin() <= SMALL_NUMBER) continue;
		// Poll actual server positions: no dependency on overlap callbacks, collision,
		// reconnect history, or whether the actor is visible in game.
		for (FConstPlayerControllerIterator PCIt = World->GetPlayerControllerIterator(); PCIt; ++PCIt)
		{
			const APlayerController* PC = PCIt->Get();
			const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
			if (!IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || Pawn->GetWorld() != World
				|| Pawn->IsA<ASpectatorPawn>() || (PC->PlayerState && PC->PlayerState->IsSpectator())) continue;
			const FVector Local = Box->GetComponentTransform().InverseTransformPosition(Pawn->GetActorLocation());
			const FVector Extent = Box->GetUnscaledBoxExtent();
			// Adjacent boxes form one room: their shared boundary is inside it too.
			if (!Local.ContainsNaN() && FMath::Abs(Local.X) <= Extent.X + KINDA_SMALL_NUMBER
				&& FMath::Abs(Local.Y) <= Extent.Y + KINDA_SMALL_NUMBER
				&& FMath::Abs(Local.Z) <= Extent.Z + KINDA_SMALL_NUMBER) return true;
		}
	}
	return false;
}

UMD_DoorFaultComponent::UMD_DoorFaultComponent()
{
	Kind = EMD_FacilityKind::Door;
	bRandomFaultEligible = true;
	bAllowManualRepair = true;
	bAllowInsulationTape = true;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.05f;
}

bool UMD_DoorFaultComponent::CanReceiveRandomFault() const
{
	const AActor* Door = GetOwner();
	if (!IsValid(Door)) return false;
	const FBoolProperty* Open = FindFProperty<FBoolProperty>(Door->GetClass(), TEXT("bDoorOpen"));
	const UFunction* ApplyOpen = Door->FindFunction(TEXT("OnRep_bDoorOpen"));
	const FBoolProperty* Locked = FindFProperty<FBoolProperty>(Door->GetClass(), TEXT("bLocked"));
	return Open && ApplyOpen && ApplyOpen->NumParms == 0
		&& (!Locked || !Locked->GetPropertyValue_InContainer(Door)) && Super::CanReceiveRandomFault()
		&& HasRepairRoomOccupant();
}

void UMD_DoorFaultComponent::BeginPlay()
{
	// Existing door meshes can inherit InteractTrace=Ignore. A locked door must
	// remain aimable from either side, including on replicated client components.
	for (UPrimitiveComponent* Surface : TInlineComponentArray<UPrimitiveComponent*>(GetOwner()))
		if (Surface->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block)
			Surface->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);
	OnStateChanged.AddUniqueDynamic(this, &ThisClass::ApplyFault);
	Super::BeginPlay();
	ApplyFault(this, FMD_FacilitySnapshot(), GetSnapshot());
}

void UMD_DoorFaultComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopFaultAudio();
	SetComponentTickEnabled(false);
	// A removed adapter must not leave a surviving Black door's sensor disabled.
	if (IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed()) RestoreOverlapEvents();
	SuppressedOverlapComponents.Reset();
	Super::EndPlay(EndPlayReason);
}

void UMD_DoorFaultComponent::StopFaultAudio()
{
	if (UWorld* World = GetWorld())
		if (UMD_AudioSubsystem* Audio = World->GetSubsystem<UMD_AudioSubsystem>())
			Audio->StopHandle(FaultAudioHandle, 0.f);
	FaultAudioHandle = FMD_AudioHandle();
}


void UMD_DoorFaultComponent::CloseDoor(bool bForceAnimation)
{
	AActor* Door = GetOwner();
	if (!IsValid(Door)) return;
	FBoolProperty* Open = FindFProperty<FBoolProperty>(Door->GetClass(), TEXT("bDoorOpen"));
	UFunction* ApplyOpen = Door->FindFunction(TEXT("OnRep_bDoorOpen"));
	if (!Open || !ApplyOpen || ApplyOpen->NumParms != 0) return;
	if (bForceAnimation || Open->GetPropertyValue_InContainer(Door))
	{
		Open->SetPropertyValue_InContainer(Door, false);
		Door->ProcessEvent(ApplyOpen, nullptr);
		if (Door->HasAuthority())
		{
			Door->FlushNetDormancy();
			Door->ForceNetUpdate();
		}
	}
}

void UMD_DoorFaultComponent::RestoreOverlapEvents()
{
	// Clear the bookkeeping before enabling sensors: overlap callbacks may reenter.
	TArray<TWeakObjectPtr<UPrimitiveComponent>> ToRestore = MoveTemp(SuppressedOverlapComponents);
	for (const TWeakObjectPtr<UPrimitiveComponent>& Component : ToRestore)
		if (Component.IsValid()) Component->SetGenerateOverlapEvents(true);
}

void UMD_DoorFaultComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// Black has no bLocked branch. Guard against an already queued open callback
	// or a late replicated open value in addition to suppressing proximity input.
	if (bOwnsLock && IsFaultActive()) CloseDoor(false);
}

void UMD_DoorFaultComponent::ApplyFault(UMD_FacilityFaultComponent* Facility,
	const FMD_FacilitySnapshot& Previous, const FMD_FacilitySnapshot& Current)
{
	AActor* Door = GetOwner();
	if (!IsValid(Door)) return;
	FBoolProperty* Locked = FindFProperty<FBoolProperty>(Door->GetClass(), TEXT("bLocked"));
	FBoolProperty* Open = FindFProperty<FBoolProperty>(Door->GetClass(), TEXT("bDoorOpen"));
	UFunction* ApplyOpen = Door->FindFunction(TEXT("OnRep_bDoorOpen"));
	if (!Open || !ApplyOpen || ApplyOpen->NumParms != 0) return;
	// Each peer hears its own spatial sound from replicated state, without a second multicast.
	if (!Current.IsFaultActive())
	{
		StopFaultAudio();
	}
	else if (!FaultAudioHandle.IsValid() && GetWorld()->GetNetMode() != NM_DedicatedServer)
	{
		if (UMD_AudioSubsystem* Audio = GetWorld()->GetSubsystem<UMD_AudioSubsystem>())
		{
			const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("SFX.World.Door.FaultAlarm"), false);
			if (Tag.IsValid()) FaultAudioHandle = Audio->PlayEventAttached(Tag, Door->GetRootComponent());
		}
	}
	if (!Locked)
	{
		// BP_Door_Black is a sibling BP with proximity events but no lock property.
		// Apply the replicated incident on BOTH peers so local overlaps cannot open it.
		if (Current.IsFaultActive())
		{
			const bool bNewLock = !bOwnsLock;
			bOwnsLock = true;
			if (bNewLock)
			{
				for (UPrimitiveComponent* Component : TInlineComponentArray<UPrimitiveComponent*>(Door))
					if (Component->GetGenerateOverlapEvents()) SuppressedOverlapComponents.Add(Component);
				for (const TWeakObjectPtr<UPrimitiveComponent>& Component : SuppressedOverlapComponents)
					if (Component.IsValid()) Component->SetGenerateOverlapEvents(false);
			}
			SetComponentTickEnabled(true);
			CloseDoor(bNewLock);
		}
		else if (bOwnsLock)
		{
			bOwnsLock = false;
			SetComponentTickEnabled(false);
			RestoreOverlapEvents();
		}
		return;
	}
	if (!Door->HasAuthority()) return;
	if (Current.IsFaultActive() && !bOwnsLock)
	{
		bPreviousLocked = Locked->GetPropertyValue_InContainer(Door);
		bOwnsLock = true;
		Locked->SetPropertyValue_InContainer(Door, true);
		Open->SetPropertyValue_InContainer(Door, false);
		Door->ProcessEvent(ApplyOpen, nullptr);
	}
	else if (!Current.IsFaultActive() && bOwnsLock)
	{
		Locked->SetPropertyValue_InContainer(Door, bPreviousLocked);
		bOwnsLock = false;
	}
	Door->FlushNetDormancy();
	Door->ForceNetUpdate();
}
