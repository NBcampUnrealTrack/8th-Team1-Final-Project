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

	// 메시 교체로 충돌 반응이 바뀌지 않도록 자동 계산 대신 기획 수치로 고정한다.
	// Native CDO construction happens before GEngine is ready. Store the override
	// directly in the body template so spawned physics bodies inherit it safely.
	Mesh->BodyInstance.SetMassOverride(40.f, true);

	// 손에서 놓은 뒤의 잔여 운동을 정리한다. 엔진 기본 AngularDamping 0은 계속 회전한다.
	Mesh->SetLinearDamping(0.5f);
	Mesh->SetAngularDamping(1.f);

	GrabPoint = CreateDefaultSubobject<USceneComponent>(TEXT("GrabPoint"));
	GrabPoint->SetupAttachment(Mesh);
	// 임시 메시 기준 상단 중앙. 에셋 교체 시 Blueprint에서 조정한다.
	GrabPoint->SetRelativeLocation(FVector(0.f, 0.f, 80.f));

	// 회전만 캐릭터 기본값(1500/500)보다 완화해 큰 통을 든 무게감을 준다.
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
	RefreshContentsFromOverlaps();
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

	// 운반 중에 통이 사라지면 내용물이 물리가 꺼진 채로 남는다
	ReleaseContentsAfterCarry();

	for (const TWeakObjectPtr<AActor>& ContentPtr : Contents)
	{
		if (AActor* Content = ContentPtr.Get())
		{
			Content->OnDestroyed.RemoveDynamic(this, &AMD_TrashBin::HandleContentDestroyed);
		}
	}
	Contents.Reset();
	ContentOverlapCounts.Reset();
	ContentAdmissionOrder.Reset();

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

TArray<AActor*> AMD_TrashBin::GetContainedActors_Implementation() const
{
	TArray<AActor*> Snapshot;
	GetContentsSnapshot(Snapshot);
	return Snapshot;
}

bool AMD_TrashBin::TryDrainContentsForSorting_Implementation(
	TArray<AActor*>& OutContents)
{
	OutContents.Reset();
	if (!HasAuthority() || bSortingTransactionActive || IsActorBeingDestroyed())
	{
		return false;
	}

	// 같은 game-thread transaction 안에서 최신 overlap을 확정한 뒤 재진입을 먼저 막는다.
	RefreshContentsFromOverlapsPreservingAdmissions();
	bSortingTransactionActive = true;

	if (IsValid(ContentsVolume))
	{
		ContentsVolume->SetGenerateOverlapEvents(false);
		ContentsVolume->OnComponentBeginOverlap.RemoveDynamic(
			this, &AMD_TrashBin::HandleBeginOverlap);
		ContentsVolume->OnComponentEndOverlap.RemoveDynamic(
			this, &AMD_TrashBin::HandleEndOverlap);
	}

	GetContentsSnapshot(OutContents);
	for (AActor* Content : OutContents)
	{
		if (IsValid(Content))
		{
			// Sorter가 곧 파괴한다. 파괴 callback이 빈 컨테이너를 다시 채우지 않게 연결을 먼저 끊는다.
			Content->OnDestroyed.RemoveDynamic(this, &AMD_TrashBin::HandleContentDestroyed);
		}
	}

	Contents.Reset();
	ContentOverlapCounts.Reset();
	ContentAdmissionOrder.Reset();
	SyncContentCount();
	return true;
}

bool AMD_TrashBin::TryRestoreDrainedContentsAfterFailedSort_Implementation(
	const TArray<AActor*>& DrainedContents)
{
	if (!HasAuthority() || !bSortingTransactionActive || IsActorBeingDestroyed()
		|| !IsValid(ContentsVolume))
	{
		return false;
	}

	bSortingTransactionActive = false;
	ContentsVolume->OnComponentBeginOverlap.AddUniqueDynamic(
		this, &AMD_TrashBin::HandleBeginOverlap);
	ContentsVolume->OnComponentEndOverlap.AddUniqueDynamic(
		this, &AMD_TrashBin::HandleEndOverlap);
	ContentsVolume->SetGenerateOverlapEvents(true);
	ContentsVolume->UpdateOverlaps();

	// 실패한 transaction이 넘긴 기존 소유물을 먼저 복구하고, 새 overlap은 이름순 후순위로 둔다.
	ContentAdmissionOrder.Reset();
	for (AActor* Content : DrainedContents)
	{
		if (IsAcceptedContent(Content))
		{
			ContentAdmissionOrder.AddUnique(TWeakObjectPtr<AActor>(Content));
		}
	}
	RebuildContentsFromOverlaps(/*bPreserveAdmissionOrder=*/true);
	return true;
}

void AMD_TrashBin::GetContentsSnapshot(TArray<AActor*>& OutContents) const
{
	OutContents.Reset();
	OutContents.Reserve(Contents.Num());
	for (const TWeakObjectPtr<AActor>& ContentPtr : Contents)
	{
		if (AActor* Content = ContentPtr.Get(); IsValid(Content))
		{
			OutContents.Add(Content);
		}
	}
}

void AMD_TrashBin::RefreshContentsFromOverlaps()
{
	if (!HasAuthority() || bSortingTransactionActive || !IsValid(ContentsVolume))
	{
		return;
	}

	// 이벤트 순서와 무관하게 현재 위치 기준 overlap cache를 먼저 갱신한다.
	ContentsVolume->UpdateOverlaps();
	// 새로 발견한 후보는 BeginOverlap 발생 순서와 무관하게 ActorName 순으로 정한다.
	RebuildContentsFromOverlaps(/*bPreserveAdmissionOrder=*/false);
}

void AMD_TrashBin::RefreshContentsFromOverlapsPreservingAdmissions()
{
	if (!HasAuthority() || bSortingTransactionActive || !IsValid(ContentsVolume))
	{
		return;
	}

	ContentsVolume->UpdateOverlaps();
	RebuildContentsFromOverlaps(/*bPreserveAdmissionOrder=*/true);
}

void AMD_TrashBin::OnCarryStarted_Implementation()
{
	FreezeContentsForCarry();
}

void AMD_TrashBin::OnCarrySettled_Implementation()
{
	ReleaseContentsAfterCarry();
}

void AMD_TrashBin::OnCarryEnded_Implementation()
{
	ReleaseContentsAfterCarry();
}

void AMD_TrashBin::FreezeContentsForCarry()
{
	if (!HasAuthority() || bSortingTransactionActive || !IsValid(Mesh)) return;

	// 중복 lifecycle 통지가 와도 같은 액터를 두 번 attach하지 않는다.
	if (!CarryFrozenContents.IsEmpty()) return;

	for (const TWeakObjectPtr<AActor>& ContentPtr : Contents)
	{
		AActor* Content = ContentPtr.Get();
		if (!IsValid(Content)) continue;

		UPrimitiveComponent* ContentRoot = Cast<UPrimitiveComponent>(Content->GetRootComponent());
		if (!IsValid(ContentRoot) || !ContentRoot->IsSimulatingPhysics()) continue;

		FMD_CarryFrozenContent& Frozen = CarryFrozenContents.AddDefaulted_GetRef();
		Frozen.Actor = ContentPtr;
		Frozen.PreviousCollisionEnabled = ContentRoot->GetCollisionEnabled();

		// bRepPhysics가 false로 바뀌면 클라이언트도 SyncReplicatedPhysicsSimulation으로 따라 끈다
		ContentRoot->SetSimulatePhysics(false);

		// 키네마틱은 질량이 무한대라 통 벽에 닿으면 통을 밀거나 가둔다.
		// 쿼리는 남겨 ContentsVolume 오버랩 판정이 계속 돌게 한다
		ContentRoot->SetCollisionEnabled(ECollisionEnabled::QueryOnly);

		Content->AttachToComponent(Mesh, FAttachmentTransformRules::KeepWorldTransform);
	}
}

void AMD_TrashBin::ReleaseContentsAfterCarry()
{
	if (CarryFrozenContents.IsEmpty()) return;

	for (const FMD_CarryFrozenContent& Frozen : CarryFrozenContents)
	{
		AActor* Content = Frozen.Actor.Get();
		if (!IsValid(Content)) continue;

		Content->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

		if (UPrimitiveComponent* ContentRoot = Cast<UPrimitiveComponent>(Content->GetRootComponent()))
		{
			// 충돌이 먼저다. 물리가 먼저 켜지면 그 프레임에 통 바닥을 뚫는다
			ContentRoot->SetCollisionEnabled(Frozen.PreviousCollisionEnabled);
			ContentRoot->SetSimulatePhysics(true);
		}
	}

	CarryFrozenContents.Reset();

	// 붙어 있는 동안 오버랩이 흔들렸을 수 있다. drain 중이면 새 입고를 재구성하지 않는다.
	if (!bSortingTransactionActive)
	{
		RefreshContentsFromOverlapsPreservingAdmissions();
	}
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
	UPrimitiveComponent* /*OverlappedComponent*/,
	AActor* OtherActor,
	UPrimitiveComponent* /*OtherComponent*/,
	int32 /*OtherBodyIndex*/,
	bool /*bFromSweep*/,
	const FHitResult& /*SweepResult*/)
{
	if (!HasAuthority() || bSortingTransactionActive || !IsAcceptedContent(OtherActor))
	{
		return;
	}

	const TWeakObjectPtr<AActor> ContentPtr(OtherActor);
	int32& OverlapCount = ContentOverlapCounts.FindOrAdd(ContentPtr);
	if (OverlapCount == 0)
	{
		ContentAdmissionOrder.Add(ContentPtr);
	}
	++OverlapCount;

	if (TryAdmitContent(OtherActor))
	{
		SyncContentCount();
	}
}

void AMD_TrashBin::HandleEndOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/,
	AActor* OtherActor,
	UPrimitiveComponent* /*OtherComponent*/,
	int32 /*OtherBodyIndex*/)
{
	if (!HasAuthority() || bSortingTransactionActive || !OtherActor)
	{
		return;
	}

	const TWeakObjectPtr<AActor> ContentPtr(OtherActor);
	int32* OverlapCount = ContentOverlapCounts.Find(ContentPtr);
	if (!OverlapCount)
	{
		return;
	}

	--(*OverlapCount);
	if (*OverlapCount > 0)
	{
		return;
	}

	ContentOverlapCounts.Remove(ContentPtr);
	ContentAdmissionOrder.Remove(ContentPtr);
	const bool bRemoved = RemoveAdmittedContent(OtherActor);
	AdmitWaitingContents();
	if (bRemoved)
	{
		SyncContentCount();
	}
}

void AMD_TrashBin::HandleContentDestroyed(AActor* DestroyedActor)
{
	if (!DestroyedActor) return;

	const TWeakObjectPtr<AActor> ContentPtr(DestroyedActor);
	ContentOverlapCounts.Remove(ContentPtr);
	ContentAdmissionOrder.Remove(ContentPtr);
	if (Contents.Remove(ContentPtr) > 0)
	{
		AdmitWaitingContents();
		SyncContentCount();
	}
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

bool AMD_TrashBin::TryAdmitContent(AActor* Actor)
{
	if (!IsAcceptedContent(Actor)
		|| Contents.Contains(TWeakObjectPtr<AActor>(Actor))
		|| Contents.Num() >= GetMaxCapacity())
	{
		return false;
	}

	const TWeakObjectPtr<AActor> ContentPtr(Actor);
	Contents.Add(ContentPtr);
	Actor->OnDestroyed.AddUniqueDynamic(this, &AMD_TrashBin::HandleContentDestroyed);
	return true;
}

bool AMD_TrashBin::RemoveAdmittedContent(AActor* Actor)
{
	if (!Actor || Contents.Remove(TWeakObjectPtr<AActor>(Actor)) == 0)
	{
		return false;
	}

	Actor->OnDestroyed.RemoveDynamic(this, &AMD_TrashBin::HandleContentDestroyed);
	return true;
}

void AMD_TrashBin::AdmitWaitingContents()
{
	for (int32 Index = 0;
		Index < ContentAdmissionOrder.Num() && Contents.Num() < GetMaxCapacity();)
	{
		const TWeakObjectPtr<AActor> ContentPtr = ContentAdmissionOrder[Index];
		AActor* Content = ContentPtr.Get();
		const int32* OverlapCount = ContentOverlapCounts.Find(ContentPtr);
		if (!IsValid(Content) || !OverlapCount || *OverlapCount <= 0)
		{
			ContentOverlapCounts.Remove(ContentPtr);
			ContentAdmissionOrder.RemoveAt(Index);
			continue;
		}

		if (!Contents.Contains(ContentPtr) && !IsAcceptedContent(Content))
		{
			ContentOverlapCounts.Remove(ContentPtr);
			ContentAdmissionOrder.RemoveAt(Index);
			continue;
		}

		TryAdmitContent(Content);
		++Index;
	}
}

void AMD_TrashBin::RebuildContentsFromOverlaps(const bool bPreserveAdmissionOrder)
{
	if (!HasAuthority() || bSortingTransactionActive || !IsValid(ContentsVolume))
	{
		return;
	}

	TArray<UPrimitiveComponent*> OverlappingComponents;
	ContentsVolume->GetOverlappingComponents(OverlappingComponents);

	TMap<TWeakObjectPtr<AActor>, int32> NewOverlapCounts;
	for (UPrimitiveComponent* OverlappingComponent : OverlappingComponents)
	{
		AActor* OverlappingActor = IsValid(OverlappingComponent)
			? OverlappingComponent->GetOwner()
			: nullptr;
		if (OverlappingActor != this && IsAcceptedContent(OverlappingActor))
		{
			++NewOverlapCounts.FindOrAdd(TWeakObjectPtr<AActor>(OverlappingActor));
		}
	}

	// 기존 입고 순서를 보존하고 새로 발견한 액터만 뒤에 붙여 capacity 승격을 예측 가능하게 한다.
	TArray<TWeakObjectPtr<AActor>> NewAdmissionOrder;
	NewAdmissionOrder.Reserve(NewOverlapCounts.Num());
	if (bPreserveAdmissionOrder)
	{
		for (const TWeakObjectPtr<AActor>& ContentPtr : ContentAdmissionOrder)
		{
			if (ContentPtr.IsValid() && NewOverlapCounts.Contains(ContentPtr))
			{
				NewAdmissionOrder.AddUnique(ContentPtr);
			}
		}
	}
	TArray<TWeakObjectPtr<AActor>> NewAdmissionCandidates;
	for (const TPair<TWeakObjectPtr<AActor>, int32>& Pair : NewOverlapCounts)
	{
		if (Pair.Key.IsValid() && !NewAdmissionOrder.Contains(Pair.Key))
		{
			NewAdmissionCandidates.Add(Pair.Key);
		}
	}
	NewAdmissionCandidates.Sort(
		[](const TWeakObjectPtr<AActor>& Left, const TWeakObjectPtr<AActor>& Right)
		{
			return GetNameSafe(Left.Get()) < GetNameSafe(Right.Get());
		});
	NewAdmissionOrder.Append(NewAdmissionCandidates);

	TSet<TWeakObjectPtr<AActor>> DesiredContents;
	DesiredContents.Reserve(FMath::Min(NewAdmissionOrder.Num(), GetMaxCapacity()));
	for (const TWeakObjectPtr<AActor>& ContentPtr : NewAdmissionOrder)
	{
		if (DesiredContents.Num() >= GetMaxCapacity())
		{
			break;
		}
		DesiredContents.Add(ContentPtr);
	}

	for (auto ContentIt = Contents.CreateIterator(); ContentIt; ++ContentIt)
	{
		if (DesiredContents.Contains(*ContentIt))
		{
			continue;
		}
		if (AActor* Content = ContentIt->Get())
		{
			Content->OnDestroyed.RemoveDynamic(this, &AMD_TrashBin::HandleContentDestroyed);
		}
		ContentIt.RemoveCurrent();
	}

	for (const TWeakObjectPtr<AActor>& ContentPtr : DesiredContents)
	{
		if (Contents.Contains(ContentPtr)) continue;
		if (AActor* Content = ContentPtr.Get())
		{
			Contents.Add(ContentPtr);
			Content->OnDestroyed.AddUniqueDynamic(this, &AMD_TrashBin::HandleContentDestroyed);
		}
	}

	ContentOverlapCounts = MoveTemp(NewOverlapCounts);
	ContentAdmissionOrder = MoveTemp(NewAdmissionOrder);
	SyncContentCount();
}

void AMD_TrashBin::SyncContentCount()
{
	const int32 NewCount = Contents.Num();
	if (ContentCount == NewCount) return;

	ContentCount = NewCount;
	OnContentsChanged.Broadcast(ContentCount, GetMaxCapacity());
	ForceNetUpdate();
}

void AMD_TrashBin::OnRep_ContentCount()
{
	OnContentsChanged.Broadcast(ContentCount, GetMaxCapacity());
}
