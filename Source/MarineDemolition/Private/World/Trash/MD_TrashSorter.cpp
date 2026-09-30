#include "World/Trash/MD_TrashSorter.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/StateTreeComponent.h"
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
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "NativeGameplayTags.h"
#include "Util/MD_LogUtility.h"
#include "Util/MD_MaterialTintUtility.h"
#include "World/Sample/MD_QuestItem.h"
#include "World/Trash/MD_TrashItem.h"
#include "World/Trash/MD_TrashSpawnVolume.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_TrashSorter_Request, "Event.TrashSorter.Request");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_TrashSorter_Rejected, "Event.TrashSorter.Rejected");
// Legacy tag registration only, so existing assets can be opened during migration.
// These events are no longer sent. Delay Task completion now drives the return to Ready.
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_TrashSorter_Finished, "Event.TrashSorter.Finished");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_TrashSorter_ErrorFinished, "Event.TrashSorter.ErrorFinished");

AMD_TrashSorter::AMD_TrashSorter()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SorterStateTree = CreateDefaultSubobject<UStateTreeComponent>(TEXT("SorterStateTree"));
	SorterStateTree->SetStartLogicAutomatically(false);
	SortingAnimation = CreateDefaultSubobject<UMD_TrashSorterAnimationComponent>(TEXT("SortingAnimation"));
	SortActionText = NSLOCTEXT("MDInteraction", "TrashSorterSortAction", "분류");

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);

	// 고정 설치물. 밀려나면 안 된다
	Mesh->SetSimulatePhysics(false);

	IntakeVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("IntakeVolume"));
	IntakeVolume->SetupAttachment(Mesh);
	// 기존 BP의 컴포넌트 참조만 보존한다. 분류는 InteractTrace로만 선택한다.
	IntakeVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	IntakeVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	IntakeVolume->SetGenerateOverlapEvents(false);
	IntakeVolume->SetCanEverAffectNavigation(false);
}

void AMD_TrashSorter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMD_TrashSorter, SortedCount);
	DOREPLIFETIME(AMD_TrashSorter, RuntimeState);
}

void AMD_TrashSorter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	ApplyCategoryTint();
}

void AMD_TrashSorter::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// Enforce server-only startup even when an existing BP overrides the component default.
	SorterStateTree->SetStartLogicAutomatically(false);
}

void AMD_TrashSorter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SorterStateTree->StopLogic(TEXT("TrashSorter EndPlay"));
	EndSortingState();
	SortingAnimation->StopSorting();
	Super::EndPlay(EndPlayReason);
}

void AMD_TrashSorter::BeginPlay()
{
	Super::BeginPlay();

	// 기존 BP의 충돌 프로파일이 프로젝트 InteractTrace 기본 Ignore를 상속해도 조준된다.
	Mesh->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);

	// 기존 BP에 저장된 충돌/Overlap 설정도 끈다. 영역 크기와 위치는 분류에 영향이 없다.
	IntakeVolume->SetGenerateOverlapEvents(false);
	IntakeVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 드롭다운에 Bonus.* 도 뜨므로 칸이 아닌 값을 걸러낸다
	if (!UMD_TrashLibrary::IsTrashCategory(AcceptedCategory))
	{
		UE_LOG(MarineDemolition, Warning,
			TEXT("'%s'의 Accepted Category '%s'는 분류 칸이 아니다. 이 분류기는 아무것도 삼키지 않는다."),
			*GetName(), *AcceptedCategory.ToString());
		return;
	}

	if (HasAuthority())
	{
		SorterStateTree->StartLogic();
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
	if (!CanInteract_Implementation(Interactor) || SortActionText.IsEmpty())
	{
		return FText::GetEmpty();
	}

	return FText::Format(
		NSLOCTEXT("MDInteraction", "TrashSorterPromptFormat", "[E] {0}"),
		SortActionText);
}

bool AMD_TrashSorter::CanInteract_Implementation(AActor* Interactor) const
{
	// 거리와 조준은 InteractionComponent의 공통 Trace 및 서버 재검증이 담당한다.
	return RuntimeState.bReady && !RuntimeState.bSorting && !bHasPendingSortRequest
		&& (!HasAuthority() || (SorterStateTree->IsRunning() && !SorterStateTree->IsPaused()))
		&& IsValid(Interactor) && Interactor->IsA<APawn>();
}

bool AMD_TrashSorter::CanAcceptHeldItem(AActor* Interactor) const
{
	if (!UMD_TrashLibrary::IsTrashCategory(AcceptedCategory)) return false;
	AActor* Item = GetHeldSortableForInteractor(Interactor);
	if (!IsValid(Item)) return false;
	const AMD_QuestItem* QuestItem = Cast<AMD_QuestItem>(Item);
	return !IsValid(QuestItem) || QuestItem->CanBeDestroyedByGameplay();
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

	return !HeldActor->Implements<UMD_TrashContainer>()
		&& HeldActor->Implements<UMD_TrashSortable>()
		? HeldActor
		: nullptr;
}

void AMD_TrashSorter::Interact_Implementation(AActor* Interactor)
{
	if (!HasAuthority() || !CanInteract_Implementation(Interactor)) return;

	// Invalid held items still reach Interact so the tree can play its Error feedback.
	const bool bCanAcceptItem = CanAcceptHeldItem(Interactor);
	PendingInteractor = Interactor;
	PendingItem = bCanAcceptItem ? GetHeldSortableForInteractor(Interactor) : nullptr;
	bHasPendingSortRequest = true;
	// Reserve both accepted and rejected requests until their event has been processed.
	RuntimeState.bReady = false;
	RuntimeState.bHasResult = false;
	ForceNetUpdate();
	SorterStateTree->SendStateTreeEvent(bCanAcceptItem ? TAG_TrashSorter_Request : TAG_TrashSorter_Rejected);
}

bool AMD_TrashSorter::EnterReadyState()
{
	if (!HasAuthority() || RuntimeState.bSorting) return false;
	ClearPendingSortRequest();
	RuntimeState.bReady = true;
	ForceNetUpdate();
	return true;
}

void AMD_TrashSorter::ExitReadyState()
{
	if (!HasAuthority()) return;
	RuntimeState.bReady = false;
	ForceNetUpdate();
}

void AMD_TrashSorter::ClearPendingSortRequest()
{
	PendingInteractor.Reset();
	PendingItem.Reset();
	bHasPendingSortRequest = false;
}

bool AMD_TrashSorter::BeginPendingSort()
{
	if (!HasAuthority() || RuntimeState.bSorting || !bHasPendingSortRequest) return false;

	AActor* Interactor = PendingInteractor.Get();
	AActor* Item = PendingItem.Get();
	// Consume the request once, including failures. Never substitute a newly held item.
	ClearPendingSortRequest();
	if (!IsValid(Item) || !CanAcceptHeldItem(Interactor)
		|| GetHeldSortableForInteractor(Interactor) != Item)
	{
		return false;
	}

	AMD_GameStateBase* MDGameState = nullptr;
	UMD_MerchantSubsystem* Merchant = nullptr;
	FMD_TrashSortResult Result;
	if (!GetServerLedgers(MDGameState, Merchant) || !ResolveSortResult(Item, Result))
	{
		// Reject before releasing the item when data or accounting services are unavailable.
		return false;
	}

	UMD_InteractionComponent* Interaction = Interactor->FindComponentByClass<UMD_InteractionComponent>();
	if (!IsValid(Interaction)) return false;

	const FVector SwallowLocation = Item->GetActorLocation();
	const FMD_SorterItemVisual ItemVisual = UMD_TrashSorterAnimationComponent::CaptureItem(Item);
	RuntimeState.bReady = false;
	RuntimeState.bSorting = true;
	Interaction->ReleaseHeldActor();

	int32 RefundCredits = 0;
	if (!ConsumeSortable(Item, MDGameState, Result, RefundCredits))
	{
		RuntimeState.bSorting = false;
		ForceNetUpdate();
		return false;
	}

	RuntimeState.bHasResult = true;
	RuntimeState.bCorrectSort = Result.bCorrectSort;
	RuntimeState.StartServerTime = MDGameState->GetServerWorldTimeSeconds();
	RuntimeState.Duration = FMath::IsFinite(SortingDuration) ? FMath::Max(0.0f, SortingDuration) : 0.0f;
	RuntimeState.ItemVisual = ItemVisual;
	if (RefundCredits > 0)
	{
		Merchant->AddCredits(EMD_DailyReportCategory::Categorizing, RefundCredits);
	}

	ForceNetUpdate();
	MulticastItemSwallowed(Result.bCorrectSort, SwallowLocation);
	RefreshSortingPresentation();
	return true;
}

float AMD_TrashSorter::GetSortingRemainingTime() const
{
	if (!RuntimeState.bSorting || !GetWorld()) return 0.0f;
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	if (!GameState) return RuntimeState.Duration;
	return static_cast<float>(FMath::Clamp(
		RuntimeState.StartServerTime + RuntimeState.Duration - GameState->GetServerWorldTimeSeconds(),
		0.0, static_cast<double>(RuntimeState.Duration)));
}

void AMD_TrashSorter::EndSortingState()
{
	if (!HasAuthority()) return;
	ClearPendingSortRequest();
	RuntimeState.bReady = false;
	if (RuntimeState.bSorting)
	{
		RuntimeState.bSorting = false;
		RuntimeState.ItemVisual = FMD_SorterItemVisual();
		RefreshSortingPresentation();
	}
	ForceNetUpdate();
}

void AMD_TrashSorter::OnRep_RuntimeState(const FMD_TrashSorterRuntimeState& PreviousState)
{
	if (PreviousState.bSorting != RuntimeState.bSorting
		|| (RuntimeState.bSorting && (PreviousState.StartServerTime != RuntimeState.StartServerTime
			|| PreviousState.ItemVisual.Mesh != RuntimeState.ItemVisual.Mesh)))
	{
		RefreshSortingPresentation();
	}
}

void AMD_TrashSorter::RefreshSortingPresentation()
{
	const float Remaining = GetSortingRemainingTime();
	if (RuntimeState.bSorting)
		SortingAnimation->StartSorting(RuntimeState.ItemVisual, RuntimeState.Duration, RuntimeState.Duration - Remaining);
	else
		SortingAnimation->StopSorting();
	OnSortingStateChanged(RuntimeState.bSorting, RuntimeState.bCorrectSort, Remaining);
}

bool AMD_TrashSorter::BeginErrorState()
{
	if (!HasAuthority()) return false;
	ClearPendingSortRequest();
	MulticastErrorFeedback();
	return true;
}

void AMD_TrashSorter::MulticastErrorFeedback_Implementation()
{
	OnSortErrorFeedback();
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

	// Destroy 뒤에는 Item을 읽을 수 없으므로 방송과 원본 생성기에 실을 값을 미리 잡아 둔다.
	const TSubclassOf<AActor> ItemClass = Item->GetClass();
	AMD_TrashItem* SpawnedTrash = Cast<AMD_TrashItem>(Item);
	TWeakObjectPtr<AMD_TrashSpawnVolume> OriginGenerator(
		SpawnedTrash ? SpawnedTrash->GetOriginGenerator() : nullptr);
	const FName TrashKindId = SpawnedTrash ? SpawnedTrash->GetOriginKindId() : NAME_None;
	FIntVector OriginCell;
	const bool bHasOriginCell = SpawnedTrash && SpawnedTrash->GetOriginFloorCell(OriginCell);
	const FIntVector* Cell = bHasOriginCell ? &OriginCell : nullptr;
	if (AMD_TrashSpawnVolume* Generator = OriginGenerator.Get(); Generator && !Generator->CanConsumeTrash(TrashKindId, Cell))
	{
		return false;
	}

	// Actor 소비가 실패하면 점수와 환급을 주지 않아 같은 물건의 중복 지급을 막는다.
	if (!Item->Destroy())
	{
		return false;
	}
	if (AMD_TrashSpawnVolume* Generator = OriginGenerator.Get())
	{
		Generator->NotifyTrashConsumed(TrashKindId, Cell);
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
