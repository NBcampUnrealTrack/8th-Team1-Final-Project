#include "Component/Support/MD_SupportPlacementComponent.h"

#include "Ability/Support/MD_GA_PlaceSupport.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/Equipment/MD_ItemDefinition.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

UMD_SupportPlacementComponent::UMD_SupportPlacementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	// Follow the current rendered camera every frame; a 30 Hz transform visibly steps while moving.
	PrimaryComponentTick.TickInterval = 0.f;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UMD_SupportPlacementComponent::BeginPlay()
{
	Super::BeginPlay();
	// Existing character Blueprints may have serialized the old 30 Hz component default.
	SetComponentTickInterval(0.f);
	if (UMD_EquipHotbarComponent* Hotbar = GetOwner()->FindComponentByClass<UMD_EquipHotbarComponent>())
		Hotbar->OnHotbarChanged.AddDynamic(this, &ThisClass::RefreshEquippedState);
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
		Pawn->ReceiveControllerChangedDelegate.AddDynamic(this, &ThisClass::HandleControllerChanged);
	RefreshEquippedState();
}

void UMD_SupportPlacementComponent::HandleControllerChanged(APawn* Pawn, AController* PreviousController, AController* NewController)
{
	RefreshEquippedState();
}

void UMD_SupportPlacementComponent::RefreshEquippedState()
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	UMD_EquipHotbarComponent* Hotbar = GetOwner()->FindComponentByClass<UMD_EquipHotbarComponent>();
	UMD_ItemDefinition* Item = Hotbar ? Hotbar->GetCurrentItem() : nullptr;
	const bool bEquipped = Pawn && Pawn->IsLocallyControlled() && GetNetMode() != NM_DedicatedServer
		&& Item && Item->GetUseAbilityTag().MatchesTagExact(MDGameplayTags::Ability_PlaceSupport);
	const bool bHasPreviewAssets = IsValid(HologramMaterial) && IsValid(RangeMaterial) && IsValid(RangeMesh);
	if (bEquipped && !bHasPreviewAssets)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Support preview [%s]: assign Hologram Material, Range Material and Range Mesh on SupportPlacementComponent in the character Blueprint."),
			*GetNameSafe(GetOwner()));
	}
	SetComponentTickEnabled(bEquipped && bHasPreviewAssets);
	if (bEquipped && bHasPreviewAssets) UpdatePreview();
	else DestroyPreview();
}

void UMD_SupportPlacementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdatePreview();
}

void UMD_SupportPlacementComponent::UpdatePreview()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_SupportPlacement_Preview);
	APawn* Pawn = Cast<APawn>(GetOwner());
	UMD_EquipHotbarComponent* Hotbar = GetOwner()->FindComponentByClass<UMD_EquipHotbarComponent>();
	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
	if (!Pawn || !Pawn->IsLocallyControlled() || !Hotbar || !ASC) { DestroyPreview(); return; }
	FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(Hotbar->FindItemAbility(Hotbar->GetCurrentItem()));
	const UMD_GA_PlaceSupport* Ability = Spec ? Cast<UMD_GA_PlaceSupport>(Spec->Ability) : nullptr;
	const UStaticMeshComponent* Template = Ability ? Ability->GetBraceMeshTemplate() : nullptr;
	if (!Template || !Template->GetStaticMesh() || !HologramMaterial || !RangeMaterial || !RangeMesh
		|| ASC->HasMatchingGameplayTag(MDGameplayTags::State_Dead))
	{
		DestroyPreview();
		return;
	}
	FMD_SupportPlacement Placement;
	Ability->EvaluatePlacement(Pawn, Placement);
	const bool bCanActivate = Ability->CanActivateAbility(Spec->Handle, ASC->AbilityActorInfo.Get());
	Placement.bCanPlace &= bCanActivate;
	if (!Ghost)
	{
		auto CreatePreviewMesh = [this](FName Name)
		{
			UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(GetOwner(), Name, RF_Transient);
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->SetGenerateOverlapEvents(false);
			Component->SetCanEverAffectNavigation(false);
			Component->SetCastShadow(false);
			Component->SetReceivesDecals(false);
			Component->SetOnlyOwnerSee(true);
			Component->SetMobility(EComponentMobility::Movable);
			Component->RegisterComponent();
			return Component;
		};
		Ghost = CreatePreviewMesh(TEXT("SupportPlacementGhost"));
		Range = CreatePreviewMesh(TEXT("SupportPlacementRange"));
		GhostMID = UMaterialInstanceDynamic::Create(HologramMaterial, this);
		RangeMID = UMaterialInstanceDynamic::Create(RangeMaterial, this);
		Range->SetStaticMesh(RangeMesh);
		Range->SetMaterial(0, RangeMID);
		bLastValid = !Placement.bCanPlace;
	}
	if (Ghost->GetStaticMesh() != Template->GetStaticMesh())
	{
		Ghost->SetStaticMesh(Template->GetStaticMesh());
		for (int32 Slot = 0; Slot < Ghost->GetNumMaterials(); ++Slot) Ghost->SetMaterial(Slot, GhostMID);
	}
	if (bLastValid != Placement.bCanPlace)
	{
		const FLinearColor Color = Placement.bCanPlace
			? FLinearColor(0.025f, 0.32f, 0.8f) : FLinearColor(0.9f, 0.025f, 0.015f);
		GhostMID->SetVectorParameterValue(TEXT("Tint"), Color);
		RangeMID->SetVectorParameterValue(TEXT("Tint"), Color);
		bLastValid = Placement.bCanPlace;
	}
	Ghost->SetWorldTransform(Placement.Transform);
	Range->SetWorldTransform(FTransform(FRotationMatrix::MakeFromZ(Placement.FloorNormal).ToQuat(),
		Placement.FloorLocation + Placement.FloorNormal * 2.f,
		FVector(Ability->GetSupportRadius() / 50.f, Ability->GetSupportRadius() / 50.f, 1.f)));
	Range->SetVisibility(Placement.bHasFloor);
}

void UMD_SupportPlacementComponent::DestroyPreview()
{
	if (Ghost) Ghost->DestroyComponent();
	if (Range) Range->DestroyComponent();
	Ghost = nullptr;
	Range = nullptr;
	GhostMID = nullptr;
	RangeMID = nullptr;
}

void UMD_SupportPlacementComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UMD_EquipHotbarComponent* Hotbar = GetOwner()->FindComponentByClass<UMD_EquipHotbarComponent>())
		Hotbar->OnHotbarChanged.RemoveDynamic(this, &ThisClass::RefreshEquippedState);
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
		Pawn->ReceiveControllerChangedDelegate.RemoveDynamic(this, &ThisClass::HandleControllerChanged);
	DestroyPreview();
	Super::EndPlay(EndPlayReason);
}
