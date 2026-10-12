#include "World/Door/MD_DoorWiringPanel.h"
#include "World/Door/MD_DoorFaultComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

UMD_DoorPanelRepairComponent::UMD_DoorPanelRepairComponent()
{
	Kind = EMD_FacilityKind::WiringPanel;
	bRandomFaultEligible = false;
	bAllowManualRepair = true;
	bAllowInsulationTape = true;
}

UMD_FacilityFaultComponent* UMD_DoorPanelRepairComponent::ResolveRepairTarget() const
{
	const AMD_DoorWiringPanel* Panel = Cast<AMD_DoorWiringPanel>(GetOwner());
	AActor* Door = Panel ? Panel->LinkedDoor.Get() : nullptr;
	// Never forward to a sorter, another panel, or a different world.
	return IsValid(Door) && Door->GetWorld() == GetWorld()
		? Door->FindComponentByClass<UMD_DoorFaultComponent>() : nullptr;
}

AMD_DoorWiringPanel::AMD_DoorWiringPanel()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	InteractionBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionBounds"));
	SetRootComponent(InteractionBounds);
	InteractionBounds->SetBoxExtent(FVector(12.f, 45.f, 65.f));
	InteractionBounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionBounds->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionBounds->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	InteractionBounds->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);
	PanelMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PanelMesh"));
	PanelMesh->SetupAttachment(RootComponent);
	PanelMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Panel(TEXT("/Game/Assets/X7/Interior/Objects/futuristic+panel+3d+model/StaticMeshes/futuristic+panel+3d+model.futuristic+panel+3d+model"));
	if (Panel.Succeeded()) PanelMesh->SetStaticMesh(Panel.Object);
	FacilityRepair = CreateDefaultSubobject<UMD_DoorPanelRepairComponent>(TEXT("FacilityRepair"));
	FaultSparks = CreateDefaultSubobject<UNiagaraComponent>(TEXT("FaultSparks"));
	FaultSparks->SetupAttachment(RootComponent);
	FaultSparks->SetRelativeLocation(FVector(15.f, 0.f, 10.f));
	FaultSparks->SetAutoActivate(false);
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> Sparks(TEXT("/Game/VFX/05_Wire/NS_Wire_Cut.NS_Wire_Cut"));
	if (Sparks.Succeeded()) FaultSparks->SetAsset(Sparks.Object);
}

void AMD_DoorWiringPanel::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMD_DoorWiringPanel, LinkedDoor);
}

void AMD_DoorWiringPanel::BeginPlay()
{
	Super::BeginPlay();
	RefreshLinkedDoor();
	// The dynamic door component or linked actor can arrive later on a client.
	GetWorldTimerManager().SetTimer(LinkTimer, this, &ThisClass::RefreshLinkedDoor, 0.2f, true);
}

void AMD_DoorWiringPanel::OnRep_LinkedDoor() { RefreshLinkedDoor(); }

void AMD_DoorWiringPanel::RefreshLinkedDoor()
{
	UMD_FacilityFaultComponent* Fault = IsValid(LinkedDoor) && LinkedDoor->GetWorld() == GetWorld()
		? LinkedDoor->FindComponentByClass<UMD_DoorFaultComponent>() : nullptr;
	if (BoundDoorFault.Get() != Fault)
	{
		if (BoundDoorFault.IsValid()) BoundDoorFault->OnStateChanged.RemoveDynamic(this, &ThisClass::HandleDoorState);
		BoundDoorFault = Fault;
		if (Fault) Fault->OnStateChanged.AddUniqueDynamic(this, &ThisClass::HandleDoorState);
	}
	HandleDoorState(Fault, FMD_FacilitySnapshot(), Fault ? Fault->GetSnapshot() : FMD_FacilitySnapshot());
}

void AMD_DoorWiringPanel::HandleDoorState(UMD_FacilityFaultComponent*, const FMD_FacilitySnapshot&, const FMD_FacilitySnapshot& Current)
{
	const bool bBroken = Current.IsFaultActive();
	if (bShowingFault == bBroken) return;
	bShowingFault = bBroken;
	if (GetNetMode() == NM_DedicatedServer) return;
	if (bBroken)
	{
		EmitSparks();
		if (UMD_AudioSubsystem* Audio = GetWorld()->GetSubsystem<UMD_AudioSubsystem>())
		{
			const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("SFX.Hazard.Cable.SparkLoop"), false);
			if (Tag.IsValid()) SparkAudioHandle = Audio->PlayEventAttached(Tag, FaultSparks);
		}
		GetWorldTimerManager().SetTimer(SparkTimer, this, &ThisClass::EmitSparks, FMath::Max(0.2f, SparkRepeatSeconds), true);
	}
	else
	{
		GetWorldTimerManager().ClearTimer(SparkTimer);
		FaultSparks->DeactivateImmediate();
		StopSparkAudio();
	}
}

void AMD_DoorWiringPanel::StopSparkAudio()
{
	if (UWorld* World = GetWorld())
		if (UMD_AudioSubsystem* Audio = World->GetSubsystem<UMD_AudioSubsystem>())
			Audio->StopHandle(SparkAudioHandle, 0.f);
	SparkAudioHandle = FMD_AudioHandle();
}

void AMD_DoorWiringPanel::EmitSparks()
{
	if (FacilityRepair->HasRepairFault()) FaultSparks->Activate(true);
}

void AMD_DoorWiringPanel::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
	PanelMesh->SetCustomDepthStencilValue(HighlightStencilValue);
	PanelMesh->SetRenderCustomDepth(bHighlighted);
}

FText AMD_DoorWiringPanel::GetInteractionDisplayName_Implementation() const
{
	return NSLOCTEXT("MDInteraction", "DoorWiringPanelName", "문 배선반");
}

FText AMD_DoorWiringPanel::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	// 배선반을 조회한 요청자를 연결된 문의 수리 안내에 전달
	return FacilityRepair->GetRepairPrompt(Cast<APawn>(Interactor));
}
bool AMD_DoorWiringPanel::CanInteract_Implementation(AActor*) const { return FacilityRepair->HasRepairFault(); }
void AMD_DoorWiringPanel::Interact_Implementation(AActor* Interactor) { FacilityRepair->BeginManualRepair(Cast<APawn>(Interactor)); }
void AMD_DoorWiringPanel::InteractReleased_Implementation(AActor* Interactor) { FacilityRepair->StopManualRepair(Cast<APawn>(Interactor)); }

void AMD_DoorWiringPanel::EndPlay(const EEndPlayReason::Type Reason)
{
	StopSparkAudio();
	GetWorldTimerManager().ClearTimer(LinkTimer);
	GetWorldTimerManager().ClearTimer(SparkTimer);
	if (BoundDoorFault.IsValid()) BoundDoorFault->OnStateChanged.RemoveDynamic(this, &ThisClass::HandleDoorState);
	FaultSparks->DeactivateImmediate();
	Super::EndPlay(Reason);
}
