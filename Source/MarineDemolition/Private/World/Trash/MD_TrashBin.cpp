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

	// 질량을 메시 볼륨에서 자동 계산하도록 두면, 임시 메시를 최종 메시로 교체하는 순간
	// 충돌 반응과 내용물과의 힘겨루기가 아무 코드 변경 없이 조용히 바뀐다. 기획 수치로 고정한다.
	//
	// 이 값은 운반 추종 감각을 바꾸지 않는다. PhysicsHandle의 스프링은 Chaos에서
	// EJointForceMode::Acceleration 기본값으로 동작해 유효 질량으로 정규화되기 때문이다.
	// 실제로 달라지는 것은 벽·캐릭터·내용물과 부딪힐 때 주고받는 운동량이다.
	// 넣은 내용물이 통을 밀고 다니면 올리고, 통이 아무것도 밀어내지 못하면 내린다.
	Mesh->SetMassOverrideInKg(NAME_None, 40.f, true);

	// 손에서 놓은 뒤의 잔여 운동을 정리하는 값이다. 운반 중에는 PhysicsHandle 드라이브가
	// 지배적이라 영향이 작지만, AngularDamping은 잡은 상태의 회전 흔들림도 조금 더 눌러준다.
	// 엔진 기본 AngularDamping 0은 던진 통이 무언가에 부딪힐 때까지 계속 도는 상태를 뜻한다.
	Mesh->SetLinearDamping(0.5f);
	Mesh->SetAngularDamping(1.f);

	// InertiaTensorScale은 기본 (1, 1, 1)을 유지한다. 회전 관성을 직접 배율로 조절하는 값이라
	// 올리면 같은 충격에도 통이 덜 기울어 내용물이 덜 쏟아지고, 내리면 쉽게 뒤집힌다.
	// 작은 충돌만으로 내용물이 쏟아지면 HoldProfile의 Angular 값보다 이 값을 먼저 올려본다.

	GrabPoint = CreateDefaultSubobject<USceneComponent>(TEXT("GrabPoint"));
	GrabPoint->SetupAttachment(Mesh);
	// 임시 메시 기준 상단 중앙. Mesh 로컬 좌표이므로 컴포넌트 스케일의 영향을 받는다.
	// BP_MD_TrashBin은 이 값을 덮어써서 통을 캐릭터 앞으로 밀어내는 용도로도 쓰고 있다.
	// 잡는 점을 본체 밖으로 빼면 지렛대가 길어지므로, 순수한 배치 이동은 HoldOffset을 쓴다.
	GrabPoint->SetRelativeLocation(FVector(0.f, 0.f, 80.f));

	// 위치 추종과 보간은 캐릭터 기본값(750/200, 보간 50)을 그대로 두고 회전만 완화한다.
	// 캐릭터 기본값 1500/500은 물체를 손에 단단히 고정하므로 큰 통을 든 무게감이 나지 않는다.
	// Stiffness를 낮춰 회전이 손보다 늦게 따라오게 하고, Damping도 함께 낮춰
	// 회전을 멈춘 뒤 한두 번 흔들리다 안정되는 여지를 남긴다.
	// 조정 시에는 Stiffness 500 -> 400 -> 300, Damping 200 -> 250 -> 300 순으로 한 값씩 움직인다.
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
