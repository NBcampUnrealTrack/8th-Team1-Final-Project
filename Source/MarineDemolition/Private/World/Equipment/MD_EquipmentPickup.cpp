#include "World/Equipment/MD_EquipmentPickup.h"

#include "Component/Interaction/MD_InteractionPromptComponent.h"
#include "Gameplay/Interaction/MD_InteractionPresentationLibrary.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/Equipment/MD_ItemDefinition.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "Engine/StaticMesh.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Net/UnrealNetwork.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "UObject/ConstructorHelpers.h"

AMD_EquipmentPickup::AMD_EquipmentPickup()
{
	// 투척 도구와 아이템에 공통 적용할 유형 선택 컴포넌트 생성
	InteractionPrompt = CreateDefaultSubobject<UMD_InteractionPromptComponent>(TEXT("InteractionPrompt"));
	// 월드에 놓인 인벤토리 항목의 기본 줍기 안내 선택
	InteractionPrompt->PromptType = EMD_InteractionPromptType::Pickup;
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);

	PhysicsRoot = CreateDefaultSubobject<UBoxComponent>(TEXT("PhysicsRoot"));
	SetRootComponent(PhysicsRoot);
	PhysicsRoot->SetBoxExtent(FVector(MinimumCollisionExtent));
	PhysicsRoot->SetCollisionProfileName(TEXT("PhysicsActor"));
	PhysicsRoot->SetCollisionObjectType(ECC_PhysicsBody);
	PhysicsRoot->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Ignore);
	PhysicsRoot->SetGenerateOverlapEvents(false);
	PhysicsRoot->CanCharacterStepUpOn = ECB_No;
	PhysicsRoot->SetWalkableSlopeOverride(FWalkableSlopeOverride(WalkableSlope_Unwalkable, 0.0f));
	PhysicsRoot->SetSimulatePhysics(true);
	PhysicsRoot->SetUseCCD(true);
	PhysicsRoot->SetLinearDamping(0.05f);
	PhysicsRoot->SetAngularDamping(0.1f);
	static ConstructorHelpers::FObjectFinder<UPhysicalMaterial> PickupMaterial(
		TEXT("/Game/Physics/PM_MD_LoosePickup.PM_MD_LoosePickup"));
	if (PickupMaterial.Succeeded()) ContactMaterial = PickupMaterial.Object;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(PhysicsRoot);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetGenerateOverlapEvents(false);
	SetActorHiddenInGame(true);
}

void AMD_EquipmentPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMD_EquipmentPickup, ItemPayload);
	DOREPLIFETIME(AMD_EquipmentPickup, bPickupActive);
}

void AMD_EquipmentPickup::BeginPlay()
{
	Super::BeginPlay();
	ApplyItemVisual();
	RefreshPickupState();
}

bool AMD_EquipmentPickup::InitializeItem(const FMD_HotbarToolEntry& Entry)
{
	if (!HasAuthority() || bPickupActive || IsValid(ItemPayload.ToolDefinition)
		|| !IsValid(Entry.ToolDefinition) || !Entry.ToolDefinition->GetPrimaryAssetId().IsValid()
		|| Entry.Count != 1 || !FMath::IsFinite(Entry.Charge) || Entry.Charge < 0.0f
		|| !FMath::IsFinite(Entry.CostPerOnce) || Entry.CostPerOnce < 0.0f
		|| !IsValid(Entry.ToolDefinition->ToolMesh))
	{
		return false;
	}
	ItemPayload = Entry;
	if (!ApplyItemVisual())
	{
		ItemPayload = FMD_HotbarToolEntry();
		return false;
	}
	ForceNetUpdate();
	return true;
}

UPrimitiveComponent* AMD_EquipmentPickup::GetThrowPrimitive() const
{
	return PhysicsRoot;
}

void AMD_EquipmentPickup::ActivatePickup()
{
	if (!HasAuthority() || bPickupActive || !IsValid(ItemPayload.ToolDefinition)
		|| !IsValid(PhysicsRoot) || !PhysicsRoot->IsSimulatingPhysics())
	{
		return;
	}
	bPickupActive = true;
	RefreshPickupState();
	ForceNetUpdate();
}

bool AMD_EquipmentPickup::ApplyItemVisual()
{
	if (!IsValid(Mesh) || !IsValid(PhysicsRoot) || !IsValid(ItemPayload.ToolDefinition)
		|| !IsValid(ItemPayload.ToolDefinition->ToolMesh))
	{
		return false;
	}
	UStaticMesh* ItemMesh = ItemPayload.ToolDefinition->ToolMesh;
	const float DefinitionMass = ItemPayload.ToolDefinition->WorldMassKg;
	if (!FMath::IsFinite(DefinitionMass) || DefinitionMass < 0.0f) return false;
	const float ResolvedMassKg = DefinitionMass > 0.0f ? DefinitionMass : ItemMassKg;
	if (IsValid(ContactMaterial) && PhysicsRoot->BodyInstance.GetPhysMaterialOverride() != ContactMaterial)
		PhysicsRoot->SetPhysMaterialOverride(ContactMaterial);
	const FBoxSphereBounds Bounds = ItemMesh->GetBounds();
	if (Bounds.Origin.ContainsNaN() || Bounds.BoxExtent.ContainsNaN()
		|| !FMath::IsFinite(Bounds.SphereRadius) || Bounds.SphereRadius <= 0.0f
		|| !FMath::IsFinite(MinimumCollisionExtent) || MinimumCollisionExtent <= 0.0f
		|| !FMath::IsFinite(ResolvedMassKg) || ResolvedMassKg <= 0.0f)
	{
		return false;
	}
	Mesh->SetStaticMesh(ItemMesh);
	// The actor transform and throw sweep refer to the center of the body's bounds.
	Mesh->SetRelativeLocation(-Bounds.Origin);
	PhysicsRoot->SetBoxExtent(Bounds.BoxExtent.ComponentMax(FVector(MinimumCollisionExtent)));
	PhysicsRoot->SetMassOverrideInKg(NAME_None, ResolvedMassKg, true);
	return true;
}

void AMD_EquipmentPickup::RefreshPickupState()
{
	SetActorHiddenInGame(!bPickupActive);
	if (IsValid(PhysicsRoot))
	{
		PhysicsRoot->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace,
			bPickupActive ? ECR_Block : ECR_Ignore);
	}
}

void AMD_EquipmentPickup::OnRep_ItemPayload()
{
	ApplyItemVisual();
	RefreshPickupState();
}

void AMD_EquipmentPickup::OnRep_PickupActive()
{
	RefreshPickupState();
}

void AMD_EquipmentPickup::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
	if (IsValid(Mesh))
	{
		Mesh->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue);
		Mesh->SetRenderCustomDepth(bHighlighted && bPickupActive);
	}
}

FText AMD_EquipmentPickup::GetInteractionDisplayName_Implementation() const
{
	return IsValid(ItemPayload.ToolDefinition) ? ItemPayload.ToolDefinition->ItemName : FText::GetEmpty();
}

FText AMD_EquipmentPickup::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	// 선택한 유형과 현재 적용 입력 키를 사용하는 줍기 안내 반환
	return UMD_InteractionPresentationLibrary::FindActorActionPrompt(this, Interactor);
}

bool AMD_EquipmentPickup::CanInteract_Implementation(AActor* Interactor) const
{
	if (!bPickupActive || bTransferInProgress || IsActorBeingDestroyed()
		|| !IsValid(Interactor) || !IsValid(ItemPayload.ToolDefinition) || ItemPayload.Count != 1)
	{
		return false;
	}
	const UMD_EquipHotbarComponent* Hotbar = Interactor->FindComponentByClass<UMD_EquipHotbarComponent>();
	if (!IsValid(Hotbar))
	{
		return false;
	}
	// Ordinary focus failures must not enter CanAddItem's warning path every frame.
	const int32 OwnedCount = Hotbar->GetItemCount(ItemPayload.ToolDefinition);
	if (OwnedCount > 0)
	{
		return !ItemPayload.ToolDefinition->IsA<UMD_ToolDefinition>() && OwnedCount < MAX_int32
			&& Hotbar->CanAddItem(ItemPayload.ToolDefinition, 1);
	}
	for (int32 Slot = 0; Slot < Hotbar->GetSlotCount(); ++Slot)
	{
		if (!IsValid(Hotbar->GetItemAtSlot(Slot)))
		{
			return Hotbar->CanAddItem(ItemPayload.ToolDefinition, 1);
		}
	}
	return false;
}

void AMD_EquipmentPickup::Interact_Implementation(AActor* Interactor)
{
	if (!HasAuthority() || !IsValid(Interactor) || !Interactor->HasAuthority()
		|| !CanInteract_Implementation(Interactor))
	{
		return;
	}
	UMD_EquipHotbarComponent* Hotbar = Interactor->FindComponentByClass<UMD_EquipHotbarComponent>();
	// Adding an item broadcasts UI/gameplay delegates before returning.
	TGuardValue<bool> TransferGuard(bTransferInProgress, true);
	if (IsValid(Hotbar) && Hotbar->AddTransferredItem(ItemPayload))
	{
		bPickupActive = false;
		RefreshPickupState();
		Destroy();
	}
}
