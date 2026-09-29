#include "World/Sample/MD_SampleProtectionZone.h"
#include "Components/BoxComponent.h"
#include "EngineUtils.h"

AMD_SampleProtectionZone::AMD_SampleProtectionZone()
{
	PrimaryActorTick.bCanEverTick = false;
	ProtectionBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("ProtectionBounds"));
	SetRootComponent(ProtectionBounds);
	ProtectionBounds->SetBoxExtent(FVector(300, 200, 400));
	ProtectionBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ProtectionBounds->SetGenerateOverlapEvents(false);
	ProtectionBounds->SetHiddenInGame(true);
	ProtectionBounds->ShapeColor = FColor::Green;
}

bool AMD_SampleProtectionZone::ContainsPosition(FVector Position) const
{
	if (!bProtectionEnabled || !IsValid(ProtectionBounds)) return false;
	const FVector Local = ProtectionBounds->GetComponentTransform().InverseTransformPosition(Position);
	const FVector Extent = ProtectionBounds->GetUnscaledBoxExtent();
	return FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y && FMath::Abs(Local.Z) <= Extent.Z;
}

bool AMD_SampleProtectionZone::ProtectsActor(const AActor* Actor)
{
	if (!IsValid(Actor) || !Actor->GetWorld()) return false;
	for (TActorIterator<AMD_SampleProtectionZone> It(Actor->GetWorld()); It; ++It)
		if (IsValid(*It) && !It->IsActorBeingDestroyed() && It->ContainsPosition(Actor->GetActorLocation())) return true;
	return false;
}
