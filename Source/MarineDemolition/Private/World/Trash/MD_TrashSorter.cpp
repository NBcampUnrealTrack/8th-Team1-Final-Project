#include "World/Trash/MD_TrashSorter.h"

#include "Audio/MD_AudioSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Component/Interaction/MD_InteractionComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
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
#include "World/Sample/MD_QuestItem.h"
#include "World/Sample/MD_SampleItem.h"

AMD_TrashSorter::AMD_TrashSorter()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);

	InsertSoundTag = FGameplayTag::RequestGameplayTag(
		TEXT("SFX.Carry.Bin.Insert"), /*ErrorIfNotFound*/ false);

	// 고정 설치물. 밀려나면 안 된다
	Mesh->SetSimulatePhysics(false);

	IntakeVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("IntakeVolume"));
	IntakeVolume->SetupAttachment(Mesh);
	IntakeVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	IntakeVolume->SetCollisionObjectType(ECC_WorldDynamic);
	IntakeVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	IntakeVolume->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
	IntakeVolume->SetGenerateOverlapEvents(false);
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
}

void AMD_TrashSorter::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
	if (!IsValid(Mesh))
	{
		return;
	}

	Mesh->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue);
	Mesh->SetRenderCustomDepth(bHighlighted);
}

FText AMD_TrashSorter::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	return IsValid(GetHeldSortableForInteractor(Interactor))
		? NSLOCTEXT("MDInteraction", "TrashSorterInsertPrompt", "[E] 넣기")
		: FText::GetEmpty();
}

bool AMD_TrashSorter::CanInteract_Implementation(AActor* Interactor) const
{
	return IsValid(GetHeldSortableForInteractor(Interactor));
}

AActor* AMD_TrashSorter::GetHeldSortableForInteractor(AActor* Interactor) const
{
	const APawn* Pawn = Cast<APawn>(Interactor);
	const UMD_InteractionComponent* Interaction = IsValid(Pawn)
		? Pawn->FindComponentByClass<UMD_InteractionComponent>()
		: nullptr;
	AActor* HeldActor = IsValid(Interaction)
		? Interaction->GetHeldActor()
		: nullptr;
	if (!IsValid(HeldActor) || HeldActor->IsActorBeingDestroyed())
	{
		return nullptr;
	}

	return HeldActor->Implements<UMD_TrashContainer>()
		|| HeldActor->Implements<UMD_TrashSortable>()
		? HeldActor
		: nullptr;
}

void AMD_TrashSorter::Interact_Implementation(AActor* Interactor)
{
	if (!HasAuthority())
	{
		return;
	}

	APawn* Pawn = Cast<APawn>(Interactor);
	UMD_InteractionComponent* Interaction = IsValid(Pawn)
		? Pawn->FindComponentByClass<UMD_InteractionComponent>()
		: nullptr;
	AActor* HeldActor = GetHeldSortableForInteractor(Interactor);
	if (!IsValid(Interaction) || !IsValid(HeldActor))
	{
		return;
	}

	// 물리 핸들이 파괴될 액터를 계속 붙잡지 않도록 먼저 정상 해제한다.
	Interaction->ReleaseHeldActor();

	if (HeldActor->Implements<UMD_TrashContainer>())
	{
		JudgeAndSwallowContainer(HeldActor);
		return;
	}

	JudgeAndSwallow(HeldActor);
}

void AMD_TrashSorter::JudgeAndSwallow(AActor* Item)
{
	if (!IsValid(Item) || ActorsBeingProcessed.Contains(TWeakObjectPtr<AActor>(Item)))
	{
		return;
	}

	AMD_QuestItem* QuestItem = Cast<AMD_QuestItem>(Item);
	if (IsValid(QuestItem) == true && QuestItem->CanBeDestroyedByGameplay() == false)
	{
		// 파괴 불가 퀘스트 아이템을 단일 투입 경로에서도 소비하지 않는 보호 처리
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

	FVector SwallowLocation = Item->GetActorLocation();
	TWeakObjectPtr<AActor> ItemPtr(Item);
	ActorsBeingProcessed.Add(ItemPtr);

	int32 RefundCredits = 0;
	bool bConsumed = ConsumeSortable(Item, MDGameState, Result, RefundCredits);
	ActorsBeingProcessed.Remove(ItemPtr);
	if (!bConsumed)
	{
		return;
	}

	if (RefundCredits > 0)
	{
		// 정분류 환급을 일일 보고서의 분류 금액 행에 기록
		Merchant->AddCredits(EMD_DailyReportCategory::Categorizing, RefundCredits);
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

	// Drain이 최신 overlap을 반영한 최종 소비 목록을 반환한다.
	FVector SwallowLocation = ContainerActor->GetActorLocation();
	TWeakObjectPtr<AActor> ContainerPtr(ContainerActor);
	ActorsBeingProcessed.Add(ContainerPtr);

	TArray<AActor*> DrainedContents;
	if (!IMD_TrashContainer::Execute_TryDrainContentsForSorting(
		ContainerActor, DrainedContents))
	{
		ActorsBeingProcessed.Remove(ContainerPtr);
		return;
	}

	// 정상적인 서버 TrashBin은 반드시 파괴된다. 실패는 복구 분기 대신 개발 중 invariant 위반으로 알린다.
	bool bContainerDestroyed = ContainerActor->Destroy();
	ensureMsgf(bContainerDestroyed,
		TEXT("분류기에 들어온 컨테이너 '%s' 파괴에 실패했습니다."),
		*GetNameSafe(ContainerActor));
	ActorsBeingProcessed.Remove(ContainerPtr);

	int32 CorrectCount = 0;
	int32 MisSortCount = 0;
	int32 DiscardedCount = 0;
	int32 TotalRefundCredits = 0;

	for (AActor* Content : DrainedContents)
	{
		if (!IsValid(Content)) continue;

		TWeakObjectPtr<AActor> ContentPtr(Content);
		if (ActorsBeingProcessed.Contains(ContentPtr)) continue;
		ActorsBeingProcessed.Add(ContentPtr);

		AMD_QuestItem* QuestItem = Cast<AMD_QuestItem>(Content);
		if (IsValid(QuestItem) == true && QuestItem->CanBeDestroyedByGameplay() == false)
		{
			// 컨테이너에서 배출된 파괴 불가 퀘스트 아이템을 월드에 남기는 보호 처리
			ActorsBeingProcessed.Remove(ContentPtr);
			continue;
		}

		if (!Content->Implements<UMD_TrashSortable>())
		{
			// 샘플처럼 분류 계약이 없는 내용물은 점수/환급 없이 소비한다.
			AMD_SampleItem* Sample = Cast<AMD_SampleItem>(Content);
			TOptional<EMD_SampleGrade> DestroyedSampleGrade = Sample
				? TOptional<EMD_SampleGrade>(Sample->Grade)
				: TOptional<EMD_SampleGrade>();
			FPrimaryAssetId DestroyedSampleQuestDefinitionId = Sample != nullptr ? Sample->QuestDefinitionId : FPrimaryAssetId();
			if (Content->Destroy())
			{
				if (DestroyedSampleGrade.IsSet())
				{
					Merchant->NotifySampleDestroyed(DestroyedSampleQuestDefinitionId, DestroyedSampleGrade.GetValue());
				}
				++DiscardedCount;
			}
			ActorsBeingProcessed.Remove(ContentPtr);
			continue;
		}

		FMD_TrashSortResult Result;
		if (!ResolveSortResult(Content, Result))
		{
			UE_LOG(MarineDemolition, Error,
				TEXT("'%s'의 분류 정책이 유효하지 않아 점수 없이 폐기합니다."),
				*GetNameSafe(Content));
			if (Content->Destroy())
			{
				++DiscardedCount;
			}
			ActorsBeingProcessed.Remove(ContentPtr);
			continue;
		}

		int32 ItemRefundCredits = 0;
		if (ConsumeSortable(Content, MDGameState, Result, ItemRefundCredits))
		{
			Result.bCorrectSort ? ++CorrectCount : ++MisSortCount;
			TotalRefundCredits += ItemRefundCredits;
		}
		ActorsBeingProcessed.Remove(ContentPtr);
	}

	// 내용물 수와 무관하게 Merchant delegate/복제 갱신은 한 번만 일으킨다.
	if (TotalRefundCredits > 0)
	{
		// 일괄 정분류 환급을 일일 보고서의 분류 금액 행에 기록
		Merchant->AddCredits(EMD_DailyReportCategory::Categorizing, TotalRefundCredits);
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

	// Destroy 뒤에는 Item을 읽을 수 없으므로 방송에 실을 값을 미리 잡아 둔다.
	const TSubclassOf<AActor> ItemClass = Item->GetClass();

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
	OnTrashSorted.Broadcast(ItemClass, Result.bCorrectSort, AcceptedCategory);
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

	// 통째 투입은 품목 수와 무관하게 한 번만 낸다. 품목마다 내면 겹쳐서 뭉갠다.
	PlayInsertSound(Location);
}

void AMD_TrashSorter::MulticastItemSwallowed_Implementation(const bool bCorrectSort, const FVector Location)
{
	OnItemSwallowed(bCorrectSort, Location);
	PlayInsertSound(Location);
}

void AMD_TrashSorter::PlayInsertSound(const FVector& Location) const
{
	if (!InsertSoundTag.IsValid())
	{
		return;
	}

	const UWorld* const World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	if (UMD_AudioSubsystem* const Audio = World->GetSubsystem<UMD_AudioSubsystem>())
	{
		Audio->PlayEventAtLocation(InsertSoundTag, Location);
	}
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
