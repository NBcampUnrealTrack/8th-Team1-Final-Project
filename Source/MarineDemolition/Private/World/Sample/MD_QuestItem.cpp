#include "World/Sample/MD_QuestItem.h"

#include "Components/StaticMeshComponent.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "GameplayTags/MD_GameplayTags.h"

AMD_QuestItem::AMD_QuestItem()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);

	// 일반 피해 이벤트가 파괴 기능을 가진 파생 클래스 밖에서는 처리되지 않게 하는 기본 정책
	SetCanBeDamaged(false);

	// 모든 퀘스트 아이템이 공유하는 물리 운반 구성
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetRelativeScale3D(FVector(0.4f));
	Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	Mesh->SetCollisionObjectType(ECC_PhysicsBody);
	Mesh->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);
	Mesh->SetGenerateOverlapEvents(true);
	Mesh->SetSimulatePhysics(true);
}

void AMD_QuestItem::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
	TagContainer.Reset();

	// 물리 운반 상호작용에 필요한 태그 제공
	if (PickupMethodTag.IsValid() == true && PickupMethodTag.MatchesTag(MDGameplayTags::Interaction_Pickup_Physics) == true)
	{
		TagContainer.AddTag(PickupMethodTag);
	}

	// 보관함 내용물 분류에 필요한 태그 제공
	if (StorageContentTag.IsValid() == true && StorageContentTag.MatchesTag(MDGameplayTags::Resource_Storage_Content) == true)
	{
		TagContainer.AddTag(StorageContentTag);
	}
}

void AMD_QuestItem::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
	if (IsValid(Mesh) == false)
	{
		return;
	}

	// 상호작용 가능 대상을 구분하기 위한 외곽선 표시
	Mesh->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue);
	Mesh->SetRenderCustomDepth(bHighlighted);
}

FGameplayTag AMD_QuestItem::GetQuestGradeTag() const
{
	FGameplayTag OutGradeTag;
	return OutGradeTag;
}

bool AMD_QuestItem::IsQuestItemIntact() const
{
	bool bOutIntact = true;
	return bOutIntact;
}

bool AMD_QuestItem::CanBeDestroyedByGameplay() const
{
	bool bOutCanBeDestroyed = false;
	return bOutCanBeDestroyed;
}

void AMD_QuestItem::NotifyGrabbed()
{
}

void AMD_QuestItem::SuppressImpactDamage(float Duration)
{
	// 파괴 기능이 없는 부모 클래스에서 호출 인터페이스만 유지하기 위한 미사용 처리
	static_cast<void>(Duration);
}
