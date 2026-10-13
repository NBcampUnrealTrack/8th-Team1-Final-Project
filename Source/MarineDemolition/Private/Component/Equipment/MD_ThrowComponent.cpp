#include "Component/Equipment/MD_ThrowComponent.h"
#include "Audio/MD_GameplayAudioFeedback.h"
#include "Audio/MD_AudioRelayComponent.h"

#include "AbilitySystemComponent.h"
#include "Character/MD_Character.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Component/Interaction/MD_InteractionComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Data/Equipment/MD_ThrowSettings.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/Trash/MD_TrashSortable.h"
#include "World/Equipment/MD_EquipmentPickup.h"

UMD_ThrowComponent::UMD_ThrowComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

AMD_Character* UMD_ThrowComponent::GetCharacter() const
{
	return Cast<AMD_Character>(GetOwner());
}

void UMD_ThrowComponent::Configure(UMD_ThrowSettings* InSettings)
{
	CancelThrow();
	Settings = InSettings;
}

bool UMD_ThrowComponent::CanThrow() const
{
	const AMD_Character* Character = GetCharacter();
	if (!IsValid(Character) || Character->IsActorBeingDestroyed() || !GetWorld() || GetWorld()->IsPaused()
		|| !IsValid(Settings) || !Settings->IsValidForThrow()) return false;
	const UMD_InteractionComponent* Interaction = Character->GetInteractionComponent();
	const AController* Controller = Character->GetController();
	return IsValid(Interaction) && Interaction->CanOwnerInteract() && IsValid(Controller)
		&& !Controller->IsMoveInputIgnored() && !Controller->IsLookInputIgnored();
}

bool UMD_ThrowComponent::ResolveTarget(AActor*& OutHeldActor, UMD_ToolDefinition*& OutTool, int32& OutSlot) const
{
	OutHeldActor = nullptr;
	OutTool = nullptr;
	OutSlot = INDEX_NONE;
	if (!CanThrow()) return false;
	const AMD_Character* Character = GetCharacter();
	AActor* HeldActor = Character->GetInteractionComponent()->GetHeldActor();
	if (IsValid(HeldActor))
	{
		// Carts and containers keep their ordinary carry behavior. An occupied hand never drops a hidden tool.
		if (!HeldActor->Implements<UMD_TrashSortable>()) return false;
		OutHeldActor = HeldActor;
		return true;
	}
	const UMD_EquipHotbarComponent* Hotbar = Character->GetHotbarComponent();
	if (!IsValid(Hotbar) || !Settings->EquipmentPickupClass) return false;
	OutTool = Hotbar->GetCurrentTool();
	if (!IsValid(OutTool) || !IsValid(OutTool->ToolMesh)) return false;
	OutSlot = Hotbar->GetCurrentSlotIndex();
	return Hotbar->GetItemCountAtSlot(OutSlot) > 0;
}

bool UMD_ThrowComponent::MatchesTarget(AActor* HeldActor, UMD_ToolDefinition* Tool, int32 Slot) const
{
	AActor* CurrentHeld = nullptr;
	UMD_ToolDefinition* CurrentTool = nullptr;
	int32 CurrentSlot = INDEX_NONE;
	return ResolveTarget(CurrentHeld, CurrentTool, CurrentSlot)
		&& CurrentHeld == HeldActor && CurrentTool == Tool && CurrentSlot == Slot;
}

void UMD_ThrowComponent::StopToolUse()
{
	AMD_Character* Character = GetCharacter();
	if (!IsValid(Character)) return;
	Character->CancelScrapeInteraction();
	UMD_EquipHotbarComponent* Hotbar = Character->GetHotbarComponent();
	UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
	if (IsValid(Hotbar) && IsValid(ASC))
	{
		const FGameplayAbilitySpecHandle Handle = Hotbar->FindItemAbility(Hotbar->GetCurrentItem());
		if (Handle.IsValid()) ASC->CancelAbilityHandle(Handle);
	}
}

void UMD_ThrowComponent::BeginThrow()
{
	if (bLocalCharging) return;
	AActor* Held = nullptr;
	UMD_ToolDefinition* Tool = nullptr;
	int32 Slot = INDEX_NONE;
	if (!ResolveTarget(Held, Tool, Slot)) return;
	StopToolUse();
	if (!MatchesTarget(Held, Tool, Slot)) return;
	LocalHeldActor = Held;
	LocalTool = Tool;
	LocalSlot = Slot;
	LocalStartedAt = GetWorld()->GetTimeSeconds();
	if (++LocalRequestId == 0) ++LocalRequestId;
	bLocalCharging = true;
	ChargeAudio = MDAudioFeedback::LocalPawn(GetCharacter(), TEXT("SFX.Carry.Hand.ThrowCharge"));
	RefreshTickEnabled();
	ServerBeginThrow(LocalRequestId, Held, Tool, Slot);
}

void UMD_ThrowComponent::ReleaseThrow()
{
	if (!bLocalCharging) return;
	if (!MatchesTarget(LocalHeldActor.Get(), LocalTool.Get(), LocalSlot))
	{
		CancelThrow();
		return;
	}
	const uint32 RequestId = LocalRequestId;
	ResetLocalCharge();
	ServerReleaseThrow(RequestId);
}

void UMD_ThrowComponent::CancelThrow()
{
	const bool bHadLocalCharge = bLocalCharging;
	ResetLocalCharge();
	if (GetOwner() && GetOwner()->HasAuthority()) EndServerCharge();
	else if (bHadLocalCharge) ServerCancelThrow(LocalRequestId);
}

float UMD_ThrowComponent::GetChargeFraction() const
{
	if (!bLocalCharging || !GetWorld() || !IsValid(Settings) || !Settings->IsValidForThrow()) return 0.0f;
	return static_cast<float>(FMath::Clamp((GetWorld()->GetTimeSeconds() - LocalStartedAt) / Settings->MaxChargeSeconds, 0.0, 1.0));
}

void UMD_ThrowComponent::ResetLocalCharge()
{
	MDAudioFeedback::Stop(this, ChargeAudio);
	bLocalCharging = false;
	LocalHeldActor.Reset();
	LocalTool.Reset();
	LocalSlot = INDEX_NONE;
	RefreshTickEnabled();
}

void UMD_ThrowComponent::EndServerCharge()
{
	const bool bWasCharging = bServerCharging;
	const uint32 RequestId = ServerRequestId;
	bServerCharging = false;
	ServerHeldActor.Reset();
	ServerTool.Reset();
	ServerSlot = INDEX_NONE;
	RefreshTickEnabled();
	if (bWasCharging) ClientEndThrow(RequestId);
}

void UMD_ThrowComponent::RefreshTickEnabled()
{
	SetComponentTickEnabled(bLocalCharging || bServerCharging);
}

void UMD_ThrowComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if ((bLocalCharging && !MatchesTarget(LocalHeldActor.Get(), LocalTool.Get(), LocalSlot))
		|| (bServerCharging && !MatchesTarget(ServerHeldActor.Get(), ServerTool.Get(), ServerSlot))) CancelThrow();
}

void UMD_ThrowComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelThrow();
	Super::EndPlay(EndPlayReason);
}

void UMD_ThrowComponent::ServerBeginThrow_Implementation(uint32 RequestId, AActor* ExpectedHeldActor,
	UMD_ToolDefinition* ExpectedTool, int32 ExpectedSlot)
{
	if (bServerCharging || RequestId == 0 || !MatchesTarget(ExpectedHeldActor, ExpectedTool, ExpectedSlot)
		|| GetWorld()->GetTimeSeconds() < NextServerThrowTime)
	{
		ClientEndThrow(RequestId);
		return;
	}
	StopToolUse();
	if (!MatchesTarget(ExpectedHeldActor, ExpectedTool, ExpectedSlot))
	{
		ClientEndThrow(RequestId);
		return;
	}
	ServerRequestId = RequestId;
	ServerHeldActor = ExpectedHeldActor;
	ServerTool = ExpectedTool;
	ServerSlot = ExpectedSlot;
	ServerStartedAt = GetWorld()->GetTimeSeconds();
	bServerCharging = true;
	RefreshTickEnabled();
}

void UMD_ThrowComponent::ServerCancelThrow_Implementation(uint32 RequestId)
{
	if (bServerCharging && ServerRequestId == RequestId) EndServerCharge();
}

void UMD_ThrowComponent::ClientEndThrow_Implementation(uint32 RequestId)
{
	if (LocalRequestId == RequestId) ResetLocalCharge();
}

void UMD_ThrowComponent::ServerReleaseThrow_Implementation(uint32 RequestId)
{
	if (!bServerCharging || ServerRequestId != RequestId) return;
	AActor* HeldActor = ServerHeldActor.Get();
	UMD_ToolDefinition* Tool = ServerTool.Get();
	const int32 Slot = ServerSlot;
	const bool bValidTarget = MatchesTarget(HeldActor, Tool, Slot);
	const double Elapsed = GetWorld()->GetTimeSeconds() - ServerStartedAt;
	// Release/Hotbar delegates can re-enter cancellation. End ownership of the charge first.
	EndServerCharge();
	if (!bValidTarget || !FMath::IsFinite(Elapsed) || Elapsed < 0.0) return;
	AMD_Character* Character = GetCharacter();
	const double Alpha = FMath::Clamp(Elapsed / Settings->MaxChargeSeconds, 0.0, 1.0);
	const FVector2D Speeds = HeldActor ? Settings->TrashLaunchSpeed : Settings->EquipmentLaunchSpeed;
	FVector Direction = Character->GetBaseAimRotation().Vector();
	Direction.Z += Settings->UpwardAimBias;
	Direction = Direction.GetSafeNormal();
	const FVector Velocity = Direction * FMath::Lerp(Speeds.X, Speeds.Y, Alpha)
		+ Character->GetVelocity() * Settings->InheritedVelocityScale;
	if (Direction.IsNearlyZero() || Velocity.ContainsNaN()) return;
	const bool bThrown = HeldActor ? ThrowHeldActor(HeldActor, Velocity) : ThrowEquipment(Tool, Slot, Direction, Velocity);
	if (bThrown)
	{
		NextServerThrowTime = GetWorld()->GetTimeSeconds() + Settings->ThrowCooldownSeconds;
		if (auto* Relay = Character->GetAudioRelayComponent())
			Relay->PlayConfirmedAtOwnerLocationForAll(FGameplayTag::RequestGameplayTag(TEXT("SFX.Carry.Hand.Throw"), false));
	}
}

bool UMD_ThrowComponent::ThrowHeldActor(AActor* HeldActor, const FVector& Velocity)
{
	UMD_InteractionComponent* Interaction = GetCharacter()->GetInteractionComponent();
	UPrimitiveComponent* Primitive = Interaction->GetHeldPrimitive();
	if (Interaction->GetHeldActor() != HeldActor || !IsValid(Primitive) || !Primitive->IsSimulatingPhysics()) return false;
	Interaction->ReleaseHeldActor();
	if (!IsValid(HeldActor) || !IsValid(Primitive)) return false;
	Primitive->SetUseCCD(true);
	Primitive->SetPhysicsLinearVelocity(Velocity);
	Primitive->WakeAllRigidBodies();
	HeldActor->ForceNetUpdate();
	return true;
}

bool UMD_ThrowComponent::ThrowEquipment(UMD_ToolDefinition* Tool, int32 Slot, const FVector& Direction, const FVector& Velocity)
{
	AMD_Character* Character = GetCharacter();
	UMD_EquipHotbarComponent* Hotbar = Character->GetHotbarComponent();
	FMD_HotbarToolEntry Transfer;
	if (!IsValid(Tool) || !IsValid(Hotbar) || Hotbar->GetCurrentSlotIndex() != Slot
		|| !Hotbar->GetItemTransferAtSlot(Slot, Transfer) || Transfer.ToolDefinition != Tool) return false;
	FVector EyeLocation;
	FRotator EyeRotation;
	Character->GetActorEyesViewPoint(EyeLocation, EyeRotation);
	FTransform SpawnTransform(FQuat::Identity, EyeLocation + Direction * Settings->EquipmentReleaseDistance);
	AMD_EquipmentPickup* Pickup = GetWorld()->SpawnActorDeferred<AMD_EquipmentPickup>(Settings->EquipmentPickupClass,
		SpawnTransform, nullptr, Character, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!IsValid(Pickup)) return false;
	if (!Pickup->InitializeItem(Transfer))
	{
		Pickup->Destroy();
		return false;
	}
	Pickup->FinishSpawning(SpawnTransform);
	UPrimitiveComponent* Primitive = Pickup->GetThrowPrimitive();
	if (!IsValid(Primitive) || !Primitive->IsSimulatingPhysics())
	{
		Pickup->Destroy();
		return false;
	}
	const FVector Extent = Primitive->Bounds.BoxExtent + FVector(Settings->CollisionClearance);
	const FVector ReleaseLocation = EyeLocation + Direction * (Settings->EquipmentReleaseDistance + Extent.Size());
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(EquipmentThrow), false, Character);
	QueryParams.AddIgnoredActor(Pickup);
	FHitResult Hit;
	const bool bBlocked = Extent.ContainsNaN() || GetWorld()->SweepSingleByChannel(Hit, EyeLocation, ReleaseLocation,
		FQuat::Identity, ECC_PhysicsBody, FCollisionShape::MakeBox(Extent), QueryParams,
		FCollisionResponseParams(Primitive->GetCollisionResponseToChannels()));
	if (bBlocked || !Pickup->SetActorLocation(ReleaseLocation, false, nullptr, ETeleportType::TeleportPhysics)
		|| !Hotbar->TryRemoveTransferredItem(Slot, Transfer))
	{
		Pickup->Destroy();
		return false;
	}
	Pickup->ActivatePickup();
	Primitive->SetPhysicsLinearVelocity(Velocity);
	Primitive->WakeAllRigidBodies();
	Pickup->ForceNetUpdate();
	return true;
}
