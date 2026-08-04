#include "World/Sample/MD_SampleItem.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Net/UnrealNetwork.h"

AMD_SampleItem::AMD_SampleItem()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);
	BreakImpactSpeed = 800.0f;
	DamageImpactSpeed = 400.0f;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetRelativeScale3D(FVector(0.4f));
	Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	Mesh->SetCollisionObjectType(ECC_PhysicsBody);
	Mesh->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);
	Mesh->SetGenerateOverlapEvents(true);
	Mesh->SetNotifyRigidBodyCollision(true);
	Mesh->SetSimulatePhysics(true);
}

void AMD_SampleItem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	DOREPLIFETIME(AMD_SampleItem, bDamaged);
}

void AMD_SampleItem::OnRep_Damaged()
{
	if (bDamaged)
	{
		ApplyDamagedVisual();
	}
}

void AMD_SampleItem::ApplyDamagedVisual()
{
	if (UMaterialInstanceDynamic* MID = Mesh->CreateAndSetMaterialInstanceDynamic(0))
	{
		MID->SetVectorParameterValue(TEXT("Color"), FLinearColor::Red);
	}
}

void AMD_SampleItem::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
	TagContainer.Reset();

	if (PickupMethodTag.IsValid()
		&& PickupMethodTag.MatchesTag(MDGameplayTags::Interaction_Pickup_Physics))
	{
		TagContainer.AddTag(PickupMethodTag);
	}

	if (StorageContentTag.IsValid()
		&& StorageContentTag.MatchesTag(MDGameplayTags::Resource_Storage_Content))
	{
		TagContainer.AddTag(StorageContentTag);
	}
}

void AMD_SampleItem::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
	if (IsValid(Mesh))
	{
		Mesh->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue);
		Mesh->SetRenderCustomDepth(bHighlighted);
	}
}

void AMD_SampleItem::SetDamaged()
{
	if (bDamaged)
	{
		return;
	}
	bDamaged = true;

	ApplyDamagedVisual();
}

void AMD_SampleItem::MulticastSpawnBreakDebris_Implementation()
{
	if (!BreakDebrisClass)
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	GetWorld()->SpawnActor<AActor>(BreakDebrisClass, GetActorTransform(), SpawnParams);
}

void AMD_SampleItem::BeginPlay()
{
	Super::BeginPlay();

	OnTakeAnyDamage.AddDynamic(this, &AMD_SampleItem::OnAnyDamage);
	if (HasAuthority())
	{
		Mesh->OnComponentHit.AddDynamic(this, &AMD_SampleItem::HandleMeshHit);
	}
}

void AMD_SampleItem::OnAnyDamage(AActor* DamagedActor, float Damage, const UDamageType* DamageType,
	AController* InstigatedBy, AActor* DamageCauser)
{
	if (bDamaged)
	{
		return;
	}
	SetDamaged();

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Orange,
				FString::Printf(TEXT("샘플 손상: %s"), *GetName()));
	}
}

void AMD_SampleItem::HandleMeshHit(
	UPrimitiveComponent* HitComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	FVector NormalImpulse,
	const FHitResult& Hit)
{
	if (!HasAuthority() || IsActorBeingDestroyed())
	{
		return;
	}

	const float SampleMass = Mesh->GetMass();
	if (!FMath::IsFinite(SampleMass) || SampleMass <= UE_SMALL_NUMBER)
	{
		return;
	}

	const float ImpactSpeed = NormalImpulse.Size() / SampleMass;
	if (!FMath::IsFinite(ImpactSpeed))
	{
		return;
	}

	// 파괴 임계 이상이면 즉시 파괴, 손상 임계 이상이면 손상만 (기획 02 §4 손 운반 리스크)
	if (ImpactSpeed >= BreakImpactSpeed)
	{
		MulticastSpawnBreakDebris();
		Destroy();
		return;
	}

	if (ImpactSpeed >= DamageImpactSpeed)
	{
		SetDamaged();
	}
}
