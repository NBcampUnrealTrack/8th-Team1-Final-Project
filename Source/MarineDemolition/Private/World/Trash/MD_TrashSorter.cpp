#include "World/Trash/MD_TrashSorter.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/MD_GameStateBase.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "Gameplay/Trash/MD_TrashContainer.h"
#include "Gameplay/Trash/MD_TrashLibrary.h"
#include "Gameplay/Trash/MD_TrashSortable.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Util/MD_LogUtility.h"
#include "Util/MD_MaterialTintUtility.h"
#include "World/Sample/MD_SampleItem.h"

AMD_TrashSorter::AMD_TrashSorter()
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

void AMD_TrashSorter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMD_TrashSorter, SortedCount);
}

void AMD_TrashSorter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	ApplyCategoryTint();
}

void AMD_TrashSorter::BeginPlay()
{
	Super::BeginPlay();

	// 드롭다운에 Bonus.* 도 뜨므로 칸이 아닌 값을 걸러낸다
	if (!UMD_TrashLibrary::IsTrashCategory(AcceptedCategory))
	{
		UE_LOG(MarineDemolition, Warning,
			TEXT("'%s'의 Accepted Category '%s'는 분류 칸이 아니다. 이 분류기는 아무것도 삼키지 않는다."),
			*GetName(), *AcceptedCategory.ToString());
		return;
	}

	if (!HasAuthority())
	{
		IntakeVolume->SetGenerateOverlapEvents(false);
		return;
	}

	IntakeVolume->OnComponentBeginOverlap.AddDynamic(this, &AMD_TrashSorter::HandleBeginOverlap);
}

void AMD_TrashSorter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsValid(IntakeVolume))
	{
		IntakeVolume->OnComponentBeginOverlap.RemoveDynamic(this, &AMD_TrashSorter::HandleBeginOverlap);
	}

	Super::EndPlay(EndPlayReason);
}

void AMD_TrashSorter::HandleBeginOverlap(
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

	if (OtherActor->Implements<UMD_TrashContainer>())
	{
		JudgeAndSwallowContainer(OtherActor);
		return;
	}

	// 분리 대상이 아닌 것(떨어뜨린 도구 등)은 통과
	if (OtherActor->Implements<UMD_TrashSortable>())
	{
		JudgeAndSwallow(OtherActor);
	}
}

void AMD_TrashSorter::JudgeAndSwallow(AActor* Item)
{
	if (!IsValid(Item) || ActorsBeingProcessed.Contains(TWeakObjectPtr<AActor>(Item)))
	{
		return;
	}

	AMD_GameStateBase* MDGameState = nullptr;
	UMD_MerchantSubsystem* Merchant = nullptr;
	FMD_TrashSortResult Result;
	if (!GetServerLedgers(MDGameState, Merchant) || !ResolveSortResult(Item, Result))
	{
		// 데이터 누락 시 원인 액터를 없애지 않는다.
		return;
	}

	const FVector SwallowLocation = Item->GetActorLocation();
	const TWeakObjectPtr<AActor> ItemPtr(Item);
	ActorsBeingProcessed.Add(ItemPtr);

	int32 RefundCredits = 0;
	const bool bConsumed = ConsumeSortable(Item, MDGameState, Result, RefundCredits);
	ActorsBeingProcessed.Remove(ItemPtr);
	if (!bConsumed)
	{
		return;
	}

	if (RefundCredits > 0)
	{
		Merchant->AddCredits(RefundCredits);
	}

	ForceNetUpdate();
	MulticastItemSwallowed(Result.bCorrectSort, SwallowLocation);
}

void AMD_TrashSorter::JudgeAndSwallowContainer(AActor* ContainerActor)
{
	if (!IsValid(ContainerActor)
		|| ActorsBeingProcessed.Contains(TWeakObjectPtr<AActor>(ContainerActor)))
	{
		return;
	}

	AMD_GameStateBase* MDGameState = nullptr;
	UMD_MerchantSubsystem* Merchant = nullptr;
	if (!GetServerLedgers(MDGameState, Merchant))
	{
		return;
	}

	// Drain 전에 모든 sortable descriptor를 검증한다. 잘못된 테이블 때문에 통 전체를 잃지 않는다.
	const TArray<AActor*> PreviewContents =
		IMD_TrashContainer::Execute_GetContainedActors(ContainerActor);
	for (AActor* Content : PreviewContents)
	{
		if (!IsValid(Content) || !Content->Implements<UMD_TrashSortable>()) continue;

		FMD_TrashSortResult PreviewResult;
		if (!ResolveSortResult(Content, PreviewResult))
		{
			return;
		}
	}

	const FVector SwallowLocation = ContainerActor->GetActorLocation();
	const TWeakObjectPtr<AActor> ContainerPtr(ContainerActor);
	ActorsBeingProcessed.Add(ContainerPtr);

	TArray<AActor*> DrainedContents;
	if (!IMD_TrashContainer::Execute_TryDrainContentsForSorting(
		ContainerActor, DrainedContents))
	{
		ActorsBeingProcessed.Remove(ContainerPtr);
		return;
	}

	// Drain 구현이 preview 이후 최신 overlap을 포함할 수 있으므로 실제 인수받은 집합도 파괴 전에 재검증한다.
	TMap<TWeakObjectPtr<AActor>, FMD_TrashSortResult> ResolvedResults;
	for (AActor* Content : DrainedContents)
	{
		if (!IsValid(Content) || !Content->Implements<UMD_TrashSortable>()) continue;

		FMD_TrashSortResult Result;
		if (!ResolveSortResult(Content, Result))
		{
			const bool bRestored =
				IMD_TrashContainer::Execute_TryRestoreDrainedContentsAfterFailedSort(
					ContainerActor, DrainedContents);
			ActorsBeingProcessed.Remove(ContainerPtr);
			UE_LOG(MarineDemolition, Error,
				TEXT("'%s' 컨테이너의 '%s' 분류 정책 검증에 실패했다. Drain rollback=%s"),
				*GetNameSafe(ContainerActor),
				*GetNameSafe(Content),
				bRestored ? TEXT("성공") : TEXT("실패"));
			return;
		}

		ResolvedResults.Add(TWeakObjectPtr<AActor>(Content), Result);
	}

	// 통 자체는 점수/환급 대상이 아니며, 먼저 파괴에 성공해야 내용물 원장을 변경한다.
	if (!ContainerActor->Destroy())
	{
		// Blueprint 알림 재진입이 이미 통을 파괴했다면 drain 소유권은 Sorter가 그대로 마무리한다.
		if (!ContainerActor->IsActorBeingDestroyed())
		{
			const bool bRestored =
				IMD_TrashContainer::Execute_TryRestoreDrainedContentsAfterFailedSort(
					ContainerActor, DrainedContents);
			ActorsBeingProcessed.Remove(ContainerPtr);
			UE_LOG(MarineDemolition, Warning,
				TEXT("'%s' 컨테이너 drain 후 파괴에 실패했다. rollback=%s"),
				*GetNameSafe(ContainerActor),
				bRestored ? TEXT("성공") : TEXT("실패"));
			return;
		}
	}
	ActorsBeingProcessed.Remove(ContainerPtr);

	int32 CorrectCount = 0;
	int32 MisSortCount = 0;
	int32 DiscardedCount = 0;
	int32 TotalRefundCredits = 0;

	for (AActor* Content : DrainedContents)
	{
		if (!IsValid(Content)) continue;

		const TWeakObjectPtr<AActor> ContentPtr(Content);
		if (ActorsBeingProcessed.Contains(ContentPtr)) continue;
		ActorsBeingProcessed.Add(ContentPtr);

		if (!Content->Implements<UMD_TrashSortable>())
		{
			// 샘플처럼 분류 계약이 없는 내용물은 점수/환급 없이 소비한다.
			const AMD_SampleItem* Sample = Cast<AMD_SampleItem>(Content);
			const TOptional<EMD_SampleGrade> DestroyedSampleGrade = Sample
				? TOptional<EMD_SampleGrade>(Sample->Grade)
				: TOptional<EMD_SampleGrade>();
			if (Content->Destroy())
			{
				if (DestroyedSampleGrade.IsSet())
				{
					Merchant->NotifySampleDestroyed(DestroyedSampleGrade.GetValue());
				}
				++DiscardedCount;
			}
			ActorsBeingProcessed.Remove(ContentPtr);
			continue;
		}

		const FMD_TrashSortResult* Result = ResolvedResults.Find(ContentPtr);
		int32 ItemRefundCredits = 0;
		if (Result
			&& ConsumeSortable(Content, MDGameState, *Result, ItemRefundCredits))
		{
			Result->bCorrectSort ? ++CorrectCount : ++MisSortCount;
			TotalRefundCredits += ItemRefundCredits;
		}
		ActorsBeingProcessed.Remove(ContentPtr);
	}

	// 최대 12개가 들어와도 Merchant delegate/복제 갱신은 한 번만 일으킨다.
	if (TotalRefundCredits > 0)
	{
		Merchant->AddCredits(TotalRefundCredits);
	}

	ForceNetUpdate();
	MulticastContainerSwallowed(
		CorrectCount,
		MisSortCount,
		DiscardedCount,
		TotalRefundCredits,
		SwallowLocation);
}

bool AMD_TrashSorter::ResolveSortResult(
	AActor* Item,
	FMD_TrashSortResult& OutResult) const
{
	FMD_TrashSortDescriptor Descriptor;
	if (!UMD_TrashLibrary::ResolveSortDescriptor(Item, Descriptor))
	{
		OutResult = FMD_TrashSortResult();
		return false;
	}

	OutResult = UMD_TrashLibrary::EvaluateSort(Descriptor, AcceptedCategory);
	return OutResult.bCanSort;
}

bool AMD_TrashSorter::ConsumeSortable(
	AActor* Item,
	AMD_GameStateBase* MDGameState,
	const FMD_TrashSortResult& Result,
	int32& OutRefundCredits)
{
	OutRefundCredits = 0;
	if (!HasAuthority() || !IsValid(Item) || !IsValid(MDGameState) || !Result.bCanSort)
	{
		return false;
	}

	// Actor 소비가 실패하면 점수와 환급을 주지 않아 같은 물건의 중복 지급을 막는다.
	if (!Item->Destroy())
	{
		return false;
	}

	MDGameState->AddScoreEvent(Result.PrimaryScoreTag);
	if (Result.bCorrectSort && Result.BonusScoreTag.IsValid())
	{
		MDGameState->AddScoreEvent(Result.BonusScoreTag);
	}

	++SortedCount;
	OutRefundCredits = Result.bCorrectSort ? FMath::Max(0, Result.RefundCredits) : 0;
	return true;
}

bool AMD_TrashSorter::GetServerLedgers(
	AMD_GameStateBase*& OutGameState,
	UMD_MerchantSubsystem*& OutMerchant) const
{
	OutGameState = nullptr;
	OutMerchant = nullptr;
	if (!HasAuthority() || !GetWorld())
	{
		return false;
	}

	OutGameState = GetWorld()->GetGameState<AMD_GameStateBase>();
	UGameInstance* GameInstance = GetWorld()->GetGameInstance();
	OutMerchant = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UMD_MerchantSubsystem>()
		: nullptr;
	return IsValid(OutGameState) && IsValid(OutMerchant);
}

void AMD_TrashSorter::MulticastContainerSwallowed_Implementation(
	const int32 CorrectCount,
	const int32 MisSortCount,
	const int32 DiscardedCount,
	const int32 RefundCredits,
	const FVector Location)
{
	OnContainerSwallowed(
		CorrectCount,
		MisSortCount,
		DiscardedCount,
		RefundCredits,
		Location);
}

void AMD_TrashSorter::MulticastItemSwallowed_Implementation(const bool bCorrectSort, const FVector Location)
{
	OnItemSwallowed(bCorrectSort, Location);
}

void AMD_TrashSorter::ApplyCategoryTint()
{
	if (!IsValid(Mesh)
		|| GetNetMode() == NM_DedicatedServer
		|| !bTintPlaceholderMeshByCategory
		|| TintParameterName.IsNone()
		|| Mesh->GetNumMaterials() <= 0)
	{
		return;
	}

	if (!SorterMID)
	{
		SorterMID = Mesh->CreateAndSetMaterialInstanceDynamic(0);
	}
	if (!SorterMID)
	{
		return;
	}

	MDMaterialTint::SetVectorParameterChecked(
		SorterMID,
		TintParameterName,
		UMD_TrashLibrary::GetCategoryColor(AcceptedCategory));
}
