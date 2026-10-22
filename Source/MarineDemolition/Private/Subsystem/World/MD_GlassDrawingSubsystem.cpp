#include "Subsystem/World/MD_GlassDrawingSubsystem.h"

#include "Components/StaticMeshComponent.h"
#include "Data/Drawing/MD_GlassDrawingProfile.h"
#include "Data/Drawing/MD_GlassDrawingSettings.h"
#include "Engine/World.h"
#include "Util/MD_LogUtility.h"
#include "World/Interaction/MD_GlassDrawingBoard.h"
#include "World/Interaction/MD_GlassDrawingDesk.h"

void UMD_GlassDrawingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// Load while the world initializes so the first stroke does not stall on a synchronous load.
	EnsureProfile();
}

bool UMD_GlassDrawingSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UMD_GlassDrawingSubsystem::Deinitialize()
{
	Boards.Empty();
	DeskReservations.Empty();
	Super::Deinitialize();
}

bool UMD_GlassDrawingSubsystem::ResolveCoordinates(const FHitResult& Hit, FVector2D& OutUV) const
{
	OutUV = FVector2D::ZeroVector;
	const UMD_GlassDrawingProfile* CurrentProfile = GetProfile();
	return IsValid(CurrentProfile) && CurrentProfile->ResolveHit(Hit, OutUV);
}

UMD_GlassDrawingProfile* UMD_GlassDrawingSubsystem::GetProfile() const
{
	const_cast<UMD_GlassDrawingSubsystem*>(this)->EnsureProfile();
	return DefaultProfile.Get();
}

void UMD_GlassDrawingSubsystem::EnsureProfile()
{
	if (IsValid(DefaultProfile) || bProfileLookupAttempted) return;
	bProfileLookupAttempted = true;
	DefaultProfile = UMD_GlassDrawingSettings::Get()->LoadDefaultProfile();
	if (!IsValid(DefaultProfile))
	{
		UE_LOG(MarineDemolition, Warning,
			TEXT("Glass drawing is disabled: set Project Settings > Game > MD Glass Drawing > Default Profile."));
	}
}

AMD_GlassDrawingBoard* UMD_GlassDrawingSubsystem::ResolveSurfaceHit(const FHitResult& Hit, FVector2D& OutUV, bool bCreate)
{
	OutUV = FVector2D::ZeroVector;
	UWorld* World = GetWorld();
	UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Hit.GetComponent());
	if (!World || !ResolveCoordinates(Hit, OutUV) || !IsValid(Mesh) || Mesh->GetWorld() != World) return nullptr;
	const TWeakObjectPtr<UStaticMeshComponent> Key(Mesh);
	if (const TWeakObjectPtr<AMD_GlassDrawingBoard>* Existing = Boards.Find(Key); Existing && Existing->IsValid())
	{
		return Existing->Get();
	}
	if (!bCreate || World->GetNetMode() == NM_Client) return nullptr;
	for (auto It = Boards.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || !It.Value().IsValid()) It.RemoveCurrent();
	}
	if (Boards.Num() >= DefaultProfile->MaxUsedSurfaces) return nullptr;

	AMD_GlassDrawingBoard* Board = World->SpawnActorDeferred<AMD_GlassDrawingBoard>(
		AMD_GlassDrawingBoard::StaticClass(), Mesh->GetComponentTransform(), Mesh->GetOwner(), nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!IsValid(Board)) return nullptr;
	Board->InitializeSurface(Mesh, DefaultProfile);
	Board->FinishSpawning(Mesh->GetComponentTransform());
	RegisterBoard(Board);
	return Board;
}

void UMD_GlassDrawingSubsystem::RegisterBoard(AMD_GlassDrawingBoard* Board)
{
	if (IsValid(Board) && IsValid(Board->GetSourceMesh()) && Board->GetWorld() == GetWorld())
	{
		Boards.Add(Board->GetSourceMesh(), Board);
	}
}

void UMD_GlassDrawingSubsystem::UnregisterBoard(AMD_GlassDrawingBoard* Board)
{
	if (!Board) return;
	const TWeakObjectPtr<UStaticMeshComponent> Key(Board->GetSourceMesh());
	if (const TWeakObjectPtr<AMD_GlassDrawingBoard>* Existing = Boards.Find(Key); Existing && Existing->Get() == Board)
	{
		Boards.Remove(Key);
	}
}

void UMD_GlassDrawingSubsystem::RemoveInvalidDeskReservations()
{
	for (auto It = DeskReservations.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || !It.Value().IsValid()
			|| It.Value()->IsActorBeingDestroyed()) It.RemoveCurrent();
	}
}

bool UMD_GlassDrawingSubsystem::TryReserveDeskSurface(UStaticMeshComponent* Surface, AMD_GlassDrawingDesk* Desk)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client) return false;
	RemoveInvalidDeskReservations();
	if (!IsValid(Surface) || !Surface->IsRegistered() || Surface->GetWorld() != World
		|| !IsValid(Desk) || !Desk->HasAuthority() || Desk->IsActorBeingDestroyed()
		|| Desk->GetWorld() != World || Desk->GetTargetGlassMesh() != Surface) return false;
	const TWeakObjectPtr<UStaticMeshComponent> Key(Surface);
	const TWeakObjectPtr<AMD_GlassDrawingDesk> DeskKey(Desk);
	if (const TWeakObjectPtr<AMD_GlassDrawingDesk>* Existing = DeskReservations.Find(Key))
	{
		return *Existing == DeskKey;
	}
	// Retargeting an occupied desk cannot acquire a second pane before its original exit finishes.
	for (const auto& Reservation : DeskReservations)
	{
		if (Reservation.Value == DeskKey) return false;
	}
	DeskReservations.Add(Key, DeskKey);
	return true;
}

void UMD_GlassDrawingSubsystem::ReleaseDeskSurface(AMD_GlassDrawingDesk* Desk)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client) return;
	RemoveInvalidDeskReservations();
	if (!Desk) return;
	const TWeakObjectPtr<AMD_GlassDrawingDesk> DeskKey(Desk);
	for (auto It = DeskReservations.CreateIterator(); It; ++It)
	{
		// Remove by reserving desk, not its editable current target: retarget/teardown may change it.
		if (It.Value() == DeskKey) It.RemoveCurrent();
	}
}

AMD_Character* UMD_GlassDrawingSubsystem::GetSurfaceOccupant(const UStaticMeshComponent* Surface) const
{
	if (!IsValid(Surface) || Surface->GetWorld() != GetWorld()) return nullptr;
	const TWeakObjectPtr<UStaticMeshComponent> Key(const_cast<UStaticMeshComponent*>(Surface));
	const TWeakObjectPtr<AMD_GlassDrawingDesk>* Existing = DeskReservations.Find(Key);
	const AMD_GlassDrawingDesk* Desk = Existing ? Existing->Get() : nullptr;
	return IsValid(Desk) && !Desk->IsActorBeingDestroyed() ? Desk->GetOccupant() : nullptr;
}
