#include "Sample/MD_SampleItem.h"

#include "GameplayTags/MD_GameplayTags.h"
#include "Materials/MaterialInstanceDynamic.h"

AMD_SampleItem::AMD_SampleItem()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetRelativeScale3D(FVector(0.4f));
	Mesh->SetCollisionObjectType(ECC_WorldDynamic);
	Mesh->SetGenerateOverlapEvents(true);

	StaticGameplayTags.AddTag(MDGameplayTags::Interaction_Pickup_Inventory);
	StaticGameplayTags.AddTag(MDGameplayTags::Item_Type_Sample);
}

void AMD_SampleItem::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
	TagContainer = StaticGameplayTags;
}

void AMD_SampleItem::BeginPlay()
{
	Super::BeginPlay();

	OnTakeAnyDamage.AddDynamic(this, &AMD_SampleItem::OnAnyDamage);
}

void AMD_SampleItem::OnAnyDamage(AActor* DamagedActor, float Damage, const UDamageType* DamageType,
	AController* InstigatedBy, AActor* DamageCauser)
{
	if (bDamaged)
	{
		return;
	}
	bDamaged = true;

	if (UMaterialInstanceDynamic* MID = Mesh->CreateAndSetMaterialInstanceDynamic(0))
	{
		MID->SetVectorParameterValue(TEXT("Color"), FLinearColor::Red);
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Orange,
			FString::Printf(TEXT("샘플 손상: %s"), *GetName()));
	}
}
