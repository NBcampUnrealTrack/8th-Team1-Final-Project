#include "World/Trash/MD_TrashItem.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/StaticMesh.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Gameplay/Trash/MD_TrashLibrary.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Net/UnrealNetwork.h"

AMD_TrashItem::AMD_TrashItem()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	Mesh->SetCollisionObjectType(ECC_PhysicsBody);
	// 캐릭터와의 물리 충돌은 유지하되 작은 쓰레기를 발판이나 계단으로 판정하지 않는다
	Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Mesh->CanCharacterStepUpOn = ECB_No;
	Mesh->SetWalkableSlopeOverride(FWalkableSlopeOverride(WalkableSlope_Unwalkable, 0.f));
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

FText AMD_TrashItem::GetInteractionDisplayName_Implementation() const
{
	FMD_TrashKindRow TrashKind;
	return UMD_TrashLibrary::FindTrashKindRow(TrashKindId, TrashKind)
		? TrashKind.DisplayName
		: FText::GetEmpty();
}

FText AMD_TrashItem::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	static_cast<void>(Interactor);
	return NSLOCTEXT("MDInteraction", "TrashItemPickupPrompt", "[E] 줍기");
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

void AMD_TrashItem::SetTrashKindId(const FName NewTrashKindId)
{
	if (TrashKindId == NewTrashKindId)
	{
		return;
	}

	TrashKindId = NewTrashKindId;
	ApplyTrashKindRow();
	ForceNetUpdate();
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

	if (!bHasAppliedTrashKind || AppliedTrashKindId != TrashKindId)
	{
		// 행이 없으면 기본값 행이 그대로 적용된다. 무효 카테고리는 아래에서 누락 신호 색이 된다
		FMD_TrashKindRow KindRow;
		UMD_TrashLibrary::FindTrashKindRow(TrashKindId, KindRow);

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
			Mesh->SetStaticMesh(KindMesh);
		}

		AppliedTrashKindId = TrashKindId;
		AppliedCategory = KindRow.Category;
		bHasAppliedTrashKind = true;
	}

	// 전용 서버에는 색이 필요 없다. 메시·질량은 서버 물리 때문에 위에서 계속 적용한다.
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// MID를 쓰면 개체마다 고유 머티리얼이 생겨 같은 메시끼리도 인스턴싱이 깨진다.
	// Custom Primitive Data는 머티리얼을 공유한 채 값만 개체별로 넘긴다.
	// 틴트를 끄면 데이터가 0(검정)으로 남으므로 흰색으로 중립화한다.
	const FLinearColor TintColor = bTintPlaceholderMeshByCategory
		? UMD_TrashLibrary::GetCategoryColor(AppliedCategory)
		: FLinearColor::White;

	Mesh->SetCustomPrimitiveDataVector4(TintCustomDataIndex, FVector4(TintColor));
}
