#include "World/Support/MD_SupportBrace.h"

#include "Component/Support/MD_SupplyComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "World/Wall/MD_CuttableWall.h"

AMD_SupportBrace::AMD_SupportBrace()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetRelativeScale3D(FVector(0.3f, 0.3f, 2.2f));
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);
}

void AMD_SupportBrace::BeginPlay()
{
	Super::BeginPlay();
	// Also correct existing BP collision defaults so the shared interaction trace can find us.
	Mesh->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);
}

void AMD_SupportBrace::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
	if (!IsValid(Mesh)) return;
	Mesh->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue);
	Mesh->SetRenderCustomDepth(bHighlighted);
}

FText AMD_SupportBrace::GetInteractionDisplayName_Implementation() const
{
	return NSLOCTEXT("MDInteraction", "SupportBraceName", "지지대");
}

FText AMD_SupportBrace::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	return CanInteract_Implementation(Interactor)
		? NSLOCTEXT("MDInteraction", "DismantleSupportBrace", "[E] 철거") : FText::GetEmpty();
}

bool AMD_SupportBrace::CanInteract_Implementation(AActor* Interactor) const
{
	const APawn* Pawn = Cast<APawn>(Interactor);
	const UMD_SupplyComponent* Supply = IsValid(Pawn)
		? Pawn->FindComponentByClass<UMD_SupplyComponent>() : nullptr;
	// Aim, distance, obstructions and player state are validated by InteractionComponent.
	return !bDismantling && !IsActorBeingDestroyed() && IsValid(Supply)
		&& Supply->GetSupportCount() >= 0 && Supply->GetSupportCount() < MAX_int32;
}

void AMD_SupportBrace::Interact_Implementation(AActor* Interactor)
{
	if (!HasAuthority() || !CanInteract_Implementation(Interactor)) return;
	UMD_SupplyComponent* Supply = Interactor->FindComponentByClass<UMD_SupplyComponent>();
	bDismantling = true;
	Dismantler = Interactor;
	// Destroy invokes EndPlay to release only this brace's support, preserving overlapping braces.
	if (!Destroy())
	{
		bDismantling = false;
		Dismantler.Reset();
		return;
	}
	// Exactly one recovery on the server; repeated/concurrent requests cannot refund this actor again.
	if (IsValid(Supply) && IsValid(Interactor))
	{
		Supply->AddSupport(1);
		Interactor->ForceNetUpdate();
	}
}

void AMD_SupportBrace::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMD_SupportBrace, bWeakened);
}

void AMD_SupportBrace::SetSupportedWall(AMD_CuttableWall* Wall)
{
	TArray<TWeakObjectPtr<AMD_CuttableWall>> Walls;
	if (IsValid(Wall)) Walls.Add(Wall);
	SetSupportedWalls(Walls);
}

void AMD_SupportBrace::SetSupportedWalls(const TArray<TWeakObjectPtr<AMD_CuttableWall>>& Walls)
{
	if (!HasAuthority() || bWeakened) return;
	TArray<TWeakObjectPtr<AMD_CuttableWall>> PreviousWalls = MoveTemp(SupportedWalls);
	SupportedWalls.Reset();
	if (IsValid(SupportedWall)) PreviousWalls.AddUnique(SupportedWall.Get());
	for (const TWeakObjectPtr<AMD_CuttableWall>& Wall : Walls)
	{
		if (Wall.IsValid() && (Wall->CanCut() || Wall->IsAwaitingSupportCollapse()))
		{
			SupportedWalls.AddUnique(Wall);
			Wall->SetSupported(true);
			Wall->ForceNetUpdate();
		}
	}
	SupportedWall = SupportedWalls.IsEmpty() ? nullptr : SupportedWalls[0].Get();
	// Rebinding the same load must not briefly remove its last support.
	PreviousWalls.RemoveAll([this](const TWeakObjectPtr<AMD_CuttableWall>& Wall)
		{ return SupportedWalls.Contains(Wall); });
	UpdateReleasedWalls(PreviousWalls, nullptr);
}

void AMD_SupportBrace::ReleaseSupport(AActor* ResponsibleActor)
{
	if (!HasAuthority() || !GetWorld()) return;
	TArray<TWeakObjectPtr<AMD_CuttableWall>> PreviousWalls = MoveTemp(SupportedWalls);
	SupportedWalls.Reset();
	if (IsValid(SupportedWall)) PreviousWalls.AddUnique(SupportedWall.Get());
	SupportedWall = nullptr;
	UpdateReleasedWalls(PreviousWalls, ResponsibleActor);
}

void AMD_SupportBrace::UpdateReleasedWalls(
	const TArray<TWeakObjectPtr<AMD_CuttableWall>>& Walls, AActor* ResponsibleActor)
{
	if (!HasAuthority() || !GetWorld()) return;
	// Only placement/removal/overnight events scan braces. No installed brace ticks.
	for (const TWeakObjectPtr<AMD_CuttableWall>& Wall : Walls)
	{
		if (!Wall.IsValid()) continue;
		bool bStillSupported = false;
		for (TActorIterator<AMD_SupportBrace> It(GetWorld()); It; ++It)
		{
			if (*It != this && !It->IsActorBeingDestroyed() && !It->bWeakened
				&& (It->SupportedWalls.Contains(Wall) || It->SupportedWall == Wall.Get()))
			{
				bStillSupported = true;
				break;
			}
		}
		Wall->UpdateSupportState(bStillSupported, ResponsibleActor);
		Wall->ForceNetUpdate();
	}
}

void AMD_SupportBrace::RestoreWeakenedState(bool bWasWeakened)
{
	if (!HasAuthority()) return;
	if (bWasWeakened) ReleaseSupport();
	bWeakened = bWasWeakened;
}

void AMD_SupportBrace::Weaken()
{
	if (!HasAuthority() || bWeakened) return;
	bWeakened = true;
	ReleaseSupport();
	AddActorLocalRotation(FRotator(0.f, 0.f, FMath::FRandRange(8.f, 15.f)));
	ForceNetUpdate();
}

void AMD_SupportBrace::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// World teardown must not clear other walls as they are being saved/travelled.
	if (EndPlayReason == EEndPlayReason::Destroyed) ReleaseSupport(Dismantler.Get());
	Super::EndPlay(EndPlayReason);
}
