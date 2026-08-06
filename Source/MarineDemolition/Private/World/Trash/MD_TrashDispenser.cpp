#include "World/Trash/MD_TrashDispenser.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Game/MD_GameStateBase.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Gameplay/Trash/MD_TrashLibrary.h"
#include "Gameplay/Trash/MD_TrashSortable.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Util/MD_LogUtility.h"
#include "Util/MD_MaterialTintUtility.h"

AMD_TrashDispenser::AMD_TrashDispenser()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);

	// 고정 설치물. 밀려나면 안 된다
	Mesh->SetSimulatePhysics(false);

	IntakeVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("IntakeVolume"));
	IntakeVolume->SetupAttachment(Mesh);
	IntakeVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	IntakeVolume->SetCollisionObjectType(ECC_WorldDynamic);
	IntakeVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	IntakeVolume->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
	IntakeVolume->SetGenerateOverlapEvents(true);
	IntakeVolume->SetCanEverAffectNavigation(false);
}

void AMD_TrashDispenser::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMD_TrashDispenser, SortedCount);
}

void AMD_TrashDispenser::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	ApplyCategoryTint();
}

void AMD_TrashDispenser::BeginPlay()
{
	Super::BeginPlay();

	// 드롭다운에 Bonus.* 도 뜨므로 칸이 아닌 값을 걸러낸다
	if (!UMD_TrashLibrary::IsTrashCategory(AcceptedCategory))
	{
		UE_LOG(MarineDemolition, Warning,
			TEXT("'%s'의 Accepted Category '%s'는 분류 칸이 아니다. 이 컨테이너는 아무것도 삼키지 않는다."),
			*GetName(), *AcceptedCategory.ToString());
		return;
	}

	if (!HasAuthority())
	{
		IntakeVolume->SetGenerateOverlapEvents(false);
		return;
	}

	IntakeVolume->OnComponentBeginOverlap.AddDynamic(this, &AMD_TrashDispenser::HandleBeginOverlap);
}

void AMD_TrashDispenser::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsValid(IntakeVolume))
	{
		IntakeVolume->OnComponentBeginOverlap.RemoveDynamic(this, &AMD_TrashDispenser::HandleBeginOverlap);
	}

	Super::EndPlay(EndPlayReason);
}

void AMD_TrashDispenser::HandleBeginOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/,
	AActor* OtherActor,
	UPrimitiveComponent* /*OtherComponent*/,
	int32 /*OtherBodyIndex*/,
	bool /*bFromSweep*/,
	const FHitResult& /*SweepResult*/)
{
	if (!HasAuthority() || !IsValid(OtherActor)) return;

	// 컴포넌트가 여러 개면 콜백이 연달아 온다. 첫 번째에서 이미 삼켰다
	if (OtherActor->IsActorBeingDestroyed()) return;

	// 분리 대상이 아닌 것(떨어뜨린 도구 등)은 통과
	if (!OtherActor->Implements<UMD_TrashSortable>()) return;

	JudgeAndSwallow(OtherActor);
}

void AMD_TrashDispenser::JudgeAndSwallow(AActor* Item)
{
	AMD_GameStateBase* MDGameState = GetWorld() ? GetWorld()->GetGameState<AMD_GameStateBase>() : nullptr;
	if (!IsValid(MDGameState)) return;

	const FGameplayTag CorrectCategory = IMD_TrashSortable::Execute_GetTrashCategory(Item);
	// 데이터 누락. 삼켜 없애면 원인 추적이 안 되므로 그대로 둔다 (경고는 조회 계층이 남김)
	if (!CorrectCategory.IsValid()) return;

	const bool bCorrectSort = CorrectCategory.MatchesTagExact(AcceptedCategory);

	if (bCorrectSort)
	{
		// 칸 태그가 곧 점수 이벤트 태그. 단가는 DA_ScoreTable에 있다
		MDGameState->AddScoreEvent(CorrectCategory);

		// 기획 05 §1.1 S+06~09
		const FGameplayTag BonusTag = IMD_TrashSortable::Execute_GetSortScoreBonusTag(Item);
		if (BonusTag.IsValid())
		{
			MDGameState->AddScoreEvent(BonusTag);
		}
	}
	else
	{
		// 기획 05 S−01/S−02. 넣은 칸이 아니라 물건의 정답 칸으로 감점이 갈린다
		MDGameState->AddScoreEvent(UMD_TrashLibrary::GetMisplacedSortScoreTag(CorrectCategory));
	}

	++SortedCount;

	const FVector SwallowLocation = Item->GetActorLocation();
	MulticastItemSwallowed(bCorrectSort, SwallowLocation);

	// 들고 있는 채로 밀어 넣어도 된다. 캐릭터 Tick이 운반 상태를 알아서 정리한다
	Item->Destroy();
}

void AMD_TrashDispenser::MulticastItemSwallowed_Implementation(const bool bCorrectSort, const FVector Location)
{
	OnItemSwallowed(bCorrectSort, Location);
}

void AMD_TrashDispenser::ApplyCategoryTint()
{
	if (!IsValid(Mesh) || !bTintPlaceholderMeshByCategory || TintParameterName.IsNone())
	{
		return;
	}

	UMaterialInstanceDynamic* DispenserMID = Mesh->CreateAndSetMaterialInstanceDynamic(0);
	if (!DispenserMID)
	{
		return;
	}

	MDMaterialTint::SetVectorParameterChecked(
		DispenserMID,
		TintParameterName,
		UMD_TrashLibrary::GetCategoryColor(AcceptedCategory));
}
