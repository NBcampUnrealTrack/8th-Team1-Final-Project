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
	DOREPLIFETIME(AMD_TrashItem, TrashVariantId);
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
	if (TrashKindId != NewTrashKindId)
	{
		SetTrashAppearance(NewTrashKindId, NAME_None);
	}
}

bool AMD_TrashItem::SetTrashAppearance(const FName NewTrashKindId, const FName NewVariantId)
{
	const FName ResolvedVariantId = NewVariantId.IsNone()
		? UMD_TrashLibrary::GetDefaultTrashVariantId(NewTrashKindId) : NewVariantId;
	TSoftObjectPtr<UStaticMesh> SelectedMesh;
	if (!HasAuthority() || ResolvedVariantId.IsNone()
		|| !UMD_TrashLibrary::ResolveTrashKindMesh(NewTrashKindId, ResolvedVariantId, SelectedMesh))
	{
		return false;
	}
	// 잘못된 에셋 경로는 기존 선택을 바꾸기 전에 거절한다. 배치 도구가 로드한 메시는 재사용한다.
	if (!SelectedMesh.IsNull() && !SelectedMesh.Get() && !SelectedMesh.LoadSynchronous())
	{
		return false;
	}
	TrashKindId = NewTrashKindId;
	TrashVariantId = ResolvedVariantId;
	ApplyTrashKindRow();
	ForceNetUpdate();
	return true;
}

bool AMD_TrashItem::SetTrashVariantId(const FName NewVariantId)
{
	return SetTrashAppearance(TrashKindId, NewVariantId);
}

#if WITH_EDITOR
void AMD_TrashItem::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(AMD_TrashItem, TrashKindId))
	{
		TrashVariantId = NAME_None;
	}
	bHasAppliedTrashKind = false;
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void AMD_TrashItem::PostEditUndo()
{
	Super::PostEditUndo();
	bHasAppliedTrashKind = false;
	ApplyTrashKindRow();
}
#endif

void AMD_TrashItem::BeginPlay()
{
	Super::BeginPlay();
	ApplyTrashKindRow();
}

void AMD_TrashItem::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 테이블 편집 후 재구성에도 최신 메시/질량을 적용한다. 이미 로드된 메시는 재사용한다.
	bHasAppliedTrashKind = false;
	ApplyTrashKindRow();
}

TArray<FName> AMD_TrashItem::GetTrashKindIdOptions() const
{
	return UMD_TrashLibrary::GetTrashKindIds();
}

TArray<FName> AMD_TrashItem::GetTrashVariantIdOptions() const
{
	TArray<FName> Ids = UMD_TrashLibrary::GetTrashVariantIds(TrashKindId);
	return Ids;
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

	if (!bHasAppliedTrashKind || AppliedTrashKindId != TrashKindId || AppliedTrashVariantId != TrashVariantId)
	{
		// 행이 없으면 기본값 행이 그대로 적용된다. 무효 카테고리는 아래에서 누락 신호 색이 된다
		FMD_TrashKindRow KindRow;
		UMD_TrashLibrary::FindTrashKindRow(TrashKindId, KindRow);
		// 이전 맵의 None 선택도 이름을 저장하도록 정규화한다. 클라이언트는 서버 값을 받는다.
		if (HasAuthority() && TrashVariantId.IsNone()) TrashVariantId = KindRow.GetDefaultVariantId();

		TSoftObjectPtr<UStaticMesh> MeshReference;
		const bool bValidVariant = KindRow.ResolveMesh(TrashVariantId, MeshReference);
		UStaticMesh* KindMesh = nullptr;
		if (bValidVariant && !MeshReference.IsNull())
		{
			KindMesh = MeshReference.Get();
			if (!KindMesh) KindMesh = MeshReference.LoadSynchronous();
		}
		// 명시적 Variant가 삭제되거나 로드에 실패하면 다른 변형을 대신 표시하지 않는다.
		if (Mesh->GetStaticMesh() != KindMesh) Mesh->SetStaticMesh(KindMesh);
		Mesh->SetMassOverrideInKg(NAME_None, FMath::Max(KindRow.MassKg, 0.01f), true);

		AppliedTrashKindId = TrashKindId;
		AppliedTrashVariantId = TrashVariantId;
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
