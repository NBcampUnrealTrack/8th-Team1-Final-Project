#include "World/Trash/MD_TrashItem.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
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

FMD_TrashSortDescriptor AMD_TrashItem::GetTrashSortDescriptor_Implementation() const
{
	FMD_TrashSortDescriptor Descriptor;
	UMD_TrashLibrary::GetTrashKindSortDescriptor(TrashKindId, Descriptor);
	return Descriptor;
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

	bool bNeedsKindData = !bHasAppliedTrashKind || AppliedTrashKindId != TrashKindId;
#if WITH_EDITOR
	// 에디터 배치에서는 DataTable 행 수정도 즉시 반영하되 PIE/런타임 캐시는 유지한다.
	if (const UWorld* World = GetWorld(); World && World->WorldType == EWorldType::Editor)
	{
		bNeedsKindData = true;
	}
#endif

	if (bNeedsKindData)
	{
		FMD_TrashKindRow KindRow;
		if (!UMD_TrashLibrary::FindTrashKindRow(TrashKindId, KindRow))
		{
			return;
		}

		// 메시 볼륨 자동 계산에 맡기면 메시 교체만으로 무게감이 조용히 달라진다
		Mesh->SetMassOverrideInKg(NAME_None, FMath::Max(KindRow.MassKg, 0.01f), true);

		// 이미 메모리에 있으면 Get()으로 즉시 사용하고, 종류가 실제로 바뀐 경우에만 동기 로드한다.
		UStaticMesh* KindMesh = KindRow.Mesh.Get();
		if (!KindMesh && !KindRow.Mesh.IsNull())
		{
			KindMesh = KindRow.Mesh.LoadSynchronous();
		}
		if (KindMesh && Mesh->GetStaticMesh() != KindMesh)
		{
			// 이전 메시의 override MID가 새 메시로 새어 가지 않게 먼저 해제한다.
			if (TrashMID)
			{
				Mesh->SetMaterial(0, nullptr);
				TrashMID = nullptr;
			}
			Mesh->SetStaticMesh(KindMesh);
		}

		AppliedTrashKindId = TrashKindId;
		AppliedCategory = KindRow.Category;
		bHasAppliedTrashKind = true;
	}

	// 전용 서버에는 색/MID가 필요 없다. 메시·질량은 서버 물리 때문에 위에서 계속 적용한다.
	if (GetNetMode() == NM_DedicatedServer
		|| !bTintPlaceholderMeshByCategory
		|| TintParameterName.IsNone()
		|| Mesh->GetNumMaterials() <= 0)
	{
		return;
	}

	if (!TrashMID)
	{
		TrashMID = Mesh->CreateAndSetMaterialInstanceDynamic(0);
	}
	if (!TrashMID)
	{
		return;
	}

	MDMaterialTint::SetVectorParameterChecked(
		TrashMID,
		TintParameterName,
		UMD_TrashLibrary::GetCategoryColor(AppliedCategory));
}
