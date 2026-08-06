#include "World/Trash/MD_TrashItem.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Gameplay/Trash/MD_TrashLibrary.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Util/MD_MaterialTintUtility.h"

AMD_TrashItem::AMD_TrashItem()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	Mesh->SetCollisionObjectType(ECC_PhysicsBody);
	Mesh->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);
	// 쓰레기통이 오버랩으로 내용물을 세므로 꺼서는 안 된다
	Mesh->SetGenerateOverlapEvents(true);
	Mesh->SetSimulatePhysics(true);

	// 감쇠가 없으면 가벼운 쓰레기가 계속 굴러가 동선 밖으로 사라진다
	Mesh->SetLinearDamping(0.5f);
	Mesh->SetAngularDamping(1.f);

	// 산재 쓰레기는 종류와 무관하게 태그가 같으므로 코드에서 기본값을 준다
	PickupMethodTag = MDGameplayTags::Interaction_Pickup_Physics;
	StorageContentTag = MDGameplayTags::Resource_Storage_Content_Trash;
}

void AMD_TrashItem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMD_TrashItem, TrashKindId);
}

void AMD_TrashItem::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
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

void AMD_TrashItem::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
	if (IsValid(Mesh))
	{
		Mesh->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue);
		Mesh->SetRenderCustomDepth(bHighlighted);
	}
}

void AMD_TrashItem::SetTrashKindId(const FName InTrashKindId)
{
	TrashKindId = InTrashKindId;
	ApplyTrashKindRow();
}

FGameplayTag AMD_TrashItem::GetTrashCategory_Implementation() const
{
	return UMD_TrashLibrary::GetTrashKindCategory(TrashKindId);
}

FGameplayTag AMD_TrashItem::GetSortScoreBonusTag_Implementation() const
{
	return UMD_TrashLibrary::GetTrashKindScoreBonusTag(TrashKindId);
}

void AMD_TrashItem::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	ApplyTrashKindRow();
}

TArray<FName> AMD_TrashItem::GetTrashKindIdOptions() const
{
	return UMD_TrashLibrary::GetTrashKindIds();
}

void AMD_TrashItem::OnRep_TrashKindId()
{
	ApplyTrashKindRow();
}

void AMD_TrashItem::ApplyTrashKindRow()
{
	if (!IsValid(Mesh))
	{
		return;
	}

	// 행이 없으면 기본값이 들어온다. 경고는 조회 계층이 한 번만 남긴다
	FMD_TrashKindRow KindRow;
	UMD_TrashLibrary::FindTrashKindRow(TrashKindId, KindRow);

	// 메시 볼륨 자동 계산에 맡기면 메시 교체만으로 무게감이 조용히 달라진다
	Mesh->SetMassOverrideInKg(NAME_None, FMath::Max(KindRow.MassKg, 0.01f), true);

	// 행에 메시가 없으면 Blueprint에 설정된 메시를 유지한다
	if (UStaticMesh* KindMesh = KindRow.Mesh.LoadSynchronous())
	{
		Mesh->SetStaticMesh(KindMesh);
	}

	if (!bTintPlaceholderMeshByCategory || TintParameterName.IsNone())
	{
		return;
	}

	UMaterialInstanceDynamic* TrashMID = Mesh->CreateAndSetMaterialInstanceDynamic(0);
	if (!TrashMID)
	{
		return;
	}

	MDMaterialTint::SetVectorParameterChecked(
		TrashMID,
		TintParameterName,
		UMD_TrashLibrary::GetCategoryColor(KindRow.Category));
}
