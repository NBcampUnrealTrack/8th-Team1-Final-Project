#include "World/Trash/MD_TrashBin.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Net/UnrealNetwork.h"

AMD_TrashBin::AMD_TrashBin()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	Mesh->SetCollisionObjectType(ECC_PhysicsBody);
	Mesh->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);
	Mesh->SetSimulatePhysics(true);

	GrabPoint = CreateDefaultSubobject<USceneComponent>(TEXT("GrabPoint"));
	GrabPoint->SetupAttachment(Mesh);
	// 현재 약 160 cm 높이인 임시 메시의 상단 중앙. 에셋 교체 시 Blueprint에서 조정한다.
	GrabPoint->SetRelativeLocation(FVector(0.f, 0.f, 80.f));

	// 위치 추종은 기존 감각을 유지하고, 회전만 부드럽게 따라오도록 시작한다.
	HoldProfile.AngularStiffness = 500.f;
	HoldProfile.AngularDamping = 200.f;

	ContentsVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("ContentsVolume"));
	ContentsVolume->SetupAttachment(Mesh);
	ContentsVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ContentsVolume->SetCollisionObjectType(ECC_WorldDynamic);
	ContentsVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	ContentsVolume->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
	ContentsVolume->SetGenerateOverlapEvents(true);
	ContentsVolume->SetCanEverAffectNavigation(false);
}

void AMD_TrashBin::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
	{
		ContentsVolume->SetGenerateOverlapEvents(false);
		return;
	}

	ContentsVolume->OnComponentBeginOverlap.AddDynamic(this, &AMD_TrashBin::HandleBeginOverlap);
	ContentsVolume->OnComponentEndOverlap.AddDynamic(this, &AMD_TrashBin::HandleEndOverlap);

	// BeginPlay 이전부터 겹쳐 있던 액터도 최초 상태에 포함한다.
	RefreshContents();
}

void AMD_TrashBin::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsValid(ContentsVolume))
	{
		ContentsVolume->OnComponentBeginOverlap.RemoveDynamic(
			this, &AMD_TrashBin::HandleBeginOverlap);
		ContentsVolume->OnComponentEndOverlap.RemoveDynamic(
			this, &AMD_TrashBin::HandleEndOverlap);
	}

	for (const TWeakObjectPtr<AActor>& ContentPtr : Contents)
	{
		if (AActor* Content = ContentPtr.Get())
		{
			Content->OnDestroyed.RemoveDynamic(this, &AMD_TrashBin::HandleContentDestroyed);
		}
	}
	Contents.Reset();

	Super::EndPlay(EndPlayReason);
}

void AMD_TrashBin::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMD_TrashBin, ContentCount);
}

void AMD_TrashBin::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
	TagContainer.Reset();

	if (PickupMethodTag.IsValid()
		&& PickupMethodTag.MatchesTag(MDGameplayTags::Interaction_Pickup_Physics))
	{
		TagContainer.AddTag(PickupMethodTag);
	}
}

UPrimitiveComponent* AMD_TrashBin::GetGrabComponent_Implementation() const
{
	return Mesh;
}

USceneComponent* AMD_TrashBin::GetGrabPoint_Implementation() const
{
	return GrabPoint;
}

FVector AMD_TrashBin::GetHoldOffset_Implementation() const
{
	return HoldOffset;
}

FMD_HoldProfile AMD_TrashBin::GetHoldProfile_Implementation() const
{
	return HoldProfile;
}

void AMD_TrashBin::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
	if (IsValid(Mesh))
	{
		Mesh->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue);
		Mesh->SetRenderCustomDepth(bHighlighted);
	}
}

void AMD_TrashBin::HandleBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	if (!HasAuthority()) return;

	RefreshContents();
}

void AMD_TrashBin::HandleEndOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	int32 OtherBodyIndex)
{
	if (!HasAuthority()) return;

	RefreshContents();
}

void AMD_TrashBin::HandleContentDestroyed(AActor* DestroyedActor)
{
	if (!DestroyedActor) return;

	if (Contents.Remove(TWeakObjectPtr<AActor>(DestroyedActor)) == 0) return;

	SyncContentCount();
}

bool AMD_TrashBin::IsAcceptedContent(const AActor* Actor) const
{
	if (!IsValid(Actor) || AcceptedContentTags.IsEmpty())
	{
		return false;
	}

	const IGameplayTagAssetInterface* TagAsset = Cast<IGameplayTagAssetInterface>(Actor);
	if (!TagAsset)
	{
		return false;
	}

	FGameplayTagContainer ActorTags;
	TagAsset->GetOwnedGameplayTags(ActorTags);

	for (const FGameplayTag& AcceptedTag : AcceptedContentTags)
	{
		if (AcceptedTag.MatchesTag(MDGameplayTags::Resource_Storage_Content)
			&& ActorTags.HasTag(AcceptedTag))
		{
			return true;
		}
	}

	return false;
}

void AMD_TrashBin::RefreshContents()
{
	if (!HasAuthority() || !IsValid(ContentsVolume)) return;

	TSet<AActor*> OverlappingActors;
	ContentsVolume->GetOverlappingActors(OverlappingActors);

	TSet<TWeakObjectPtr<AActor>> ActualContents;
	ActualContents.Reserve(OverlappingActors.Num());
	for (AActor* OverlappingActor : OverlappingActors)
	{
		if (IsAcceptedContent(OverlappingActor))
		{
			ActualContents.Add(TWeakObjectPtr<AActor>(OverlappingActor));
		}
	}

	for (auto ContentIt = Contents.CreateIterator(); ContentIt; ++ContentIt)
	{
		AActor* Content = ContentIt->Get();
		if (IsValid(Content) && ActualContents.Contains(*ContentIt))
		{
			continue;
		}

		if (IsValid(Content))
		{
			Content->OnDestroyed.RemoveDynamic(this, &AMD_TrashBin::HandleContentDestroyed);
		}
		ContentIt.RemoveCurrent();
	}

	for (const TWeakObjectPtr<AActor>& ContentPtr : ActualContents)
	{
		if (Contents.Contains(ContentPtr))
		{
			continue;
		}

		if (AActor* Content = ContentPtr.Get())
		{
			Contents.Add(ContentPtr);
			Content->OnDestroyed.AddUniqueDynamic(this, &AMD_TrashBin::HandleContentDestroyed);
		}
	}

	SyncContentCount();
}

void AMD_TrashBin::SyncContentCount()
{
	const int32 NewCount = Contents.Num();
	if (ContentCount == NewCount) return;

	ContentCount = NewCount;
	ForceNetUpdate();
}
