#include "World/Environment/MD_CoolingHazardZone.h"

#include "Components/BoxComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "World/Wall/MD_CoolingPump.h"

AMD_CoolingHazardZone::AMD_CoolingHazardZone()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	DamageZone = CreateDefaultSubobject<UBoxComponent>(TEXT("DamageZone"));
	SetRootComponent(DamageZone);
	DamageZone->InitBoxExtent(FVector(400.f, 400.f, 200.f));
	DamageZone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	DamageZone->SetCollisionObjectType(ECC_WorldDynamic);
	DamageZone->SetCollisionResponseToAllChannels(ECR_Ignore);
	DamageZone->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	DamageZone->SetGenerateOverlapEvents(true);
	DamageZone->SetCanEverAffectNavigation(false);
}

bool AMD_CoolingHazardZone::IsHazardActive() const
{
	if (!HasAuthority())
	{
		return bHazardActive;
	}
	const AMD_CoolingPump* Pump = CoolingPump.Get();
	// A missing/unloaded reference must not silently make the room safe.
	return !IsValid(Pump) || !Pump->IsCoolingRestored();
}

void AMD_CoolingHazardZone::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMD_CoolingHazardZone, bHazardActive);
}

void AMD_CoolingHazardZone::BeginPlay()
{
	Super::BeginPlay();
	DamageInterval = FMath::IsFinite(DamageInterval) ? FMath::Max(0.1f, DamageInterval) : 1.f;
	DamagePerSecond = FMath::IsFinite(DamagePerSecond) ? FMath::Max(0.f, DamagePerSecond) : 8.f;
	if (HasAuthority())
	{
		DamageZone->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::HandleBeginOverlap);
		if (CoolingPump.IsNull())
		{
			UE_LOG(LogTemp, Warning, TEXT("[Cooling] %s: assign CoolingPump; hazard stays active until linked."), *GetName());
		}
		// Defer until every placed actor has initialized its start-broken state.
		GetWorldTimerManager().SetTimer(UpdateTimer, this, &ThisClass::UpdateHazard, 0.1f, true);
	}
	OnRep_HazardActive();
}

void AMD_CoolingHazardZone::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(UpdateTimer);
	DamageZone->OnComponentBeginOverlap.RemoveDynamic(this, &ThisClass::HandleBeginOverlap);
	NextDamageTime.Empty();
	Super::EndPlay(EndPlayReason);
}

void AMD_CoolingHazardZone::OnRep_HazardActive()
{
	OnHazardStateChanged(bHazardActive);
}

void AMD_CoolingHazardZone::HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	if (HasAuthority() && IsHazardActive())
	{
		DamagePawn(OtherActor);
	}
}

void AMD_CoolingHazardZone::DamagePawn(AActor* Actor)
{
	if (!HasAuthority() || !IsHazardActive() || !IsValid(Actor) || !Actor->IsA<APawn>())
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	const TWeakObjectPtr<AActor> Key(Actor);
	if (const double* NextTime = NextDamageTime.Find(Key); NextTime && Now < *NextTime)
	{
		return;
	}
	// Entry damage and timer damage share a per-pawn cooldown (multiple body overlaps cannot multiply it).
	NextDamageTime.Add(Key, Now + DamageInterval);
	UGameplayStatics::ApplyDamage(Actor, DamagePerSecond * DamageInterval, nullptr, this, HazardDamageType);
}

void AMD_CoolingHazardZone::UpdateHazard()
{
	if (!HasAuthority())
	{
		return;
	}
	const bool bNewActive = IsHazardActive();
	if (bHazardActive != bNewActive)
	{
		bHazardActive = bNewActive;
		OnRep_HazardActive();
		ForceNetUpdate();
	}
	if (!bNewActive)
	{
		NextDamageTime.Empty();
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	for (auto It = NextDamageTime.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || It.Value() <= Now)
		{
			It.RemoveCurrent();
		}
	}
	TArray<AActor*> Pawns;
	DamageZone->GetOverlappingActors(Pawns, APawn::StaticClass());
	for (AActor* Pawn : Pawns)
	{
		DamagePawn(Pawn);
	}
}
