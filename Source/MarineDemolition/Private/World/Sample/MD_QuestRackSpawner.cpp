#include "World/Sample/MD_QuestRackSpawner.h"

#include "Character/MD_Character.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Component/Interaction/MD_InteractionComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/Equipment/MD_ItemDefinition.h"
#include "Data/Merchant/MD_QuestDefinition.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "World/Sample/MD_QuestItem.h"
#include "World/Sample/MD_QuestSampleSpawnerManager.h"

AMD_QuestRackSpawner::AMD_QuestRackSpawner()
{
	bReplicates = true;
	RackQuestDefinitionId = FPrimaryAssetId(FPrimaryAssetType(TEXT("Quest")), FName(TEXT("X7-M01")));
	RackMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RackMesh"));
	RackMesh->SetupAttachment(Root);
	RackMesh->SetCollisionProfileName(TEXT("BlockAll"));
	RackMesh->SetRelativeScale3D(FVector(2.f));
	PadlockMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PadlockMesh"));
	PadlockMesh->SetupAttachment(RackMesh);
	PadlockMesh->SetRelativeLocation(FVector(-2.f, 26.f, 44.f));
	PadlockMesh->SetRelativeScale3D(FVector(0.5f));
	PadlockMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PadlockMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	PadlockMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	PadlockMesh->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);
	DefaultSpawnPoint->SetRelativeLocation(FVector(0.f, 85.f, 105.f));
	DefaultSpawnPoint->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));
}

void AMD_QuestRackSpawner::ConfigureRackComponents()
{
	// BlockAll inherits Ignore for the project's custom interaction channel.
	// The body remains the E target after the lock disappears.
	RackMesh->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);
	OnRep_RackState();
}

void AMD_QuestRackSpawner::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ConfigureRackComponents();
}

void AMD_QuestRackSpawner::PostInitializeComponents()
{
	ConfigureRackComponents();
	Super::PostInitializeComponents();
}

void AMD_QuestRackSpawner::BeginPlay()
{
	ConfigureRackComponents();
	Super::BeginPlay();
	if (HasAuthority())
	{
		OnRep_RackState();
		GetWorldTimerManager().SetTimer(GateEvaluationTimer, this,
			&ThisClass::TrySpawnForQuestState, 0.2f, true);
	}
}

void AMD_QuestRackSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bRackEndingPlay = true;
	GetWorldTimerManager().ClearTimer(GateEvaluationTimer);
	Super::EndPlay(EndPlayReason);
}

void AMD_QuestRackSpawner::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMD_QuestRackSpawner, RackMode);
	DOREPLIFETIME(AMD_QuestRackSpawner, bQuestActive);
	DOREPLIFETIME(AMD_QuestRackSpawner, bPadlockCut);
	DOREPLIFETIME(AMD_QuestRackSpawner, PadlockCutSeconds);
	DOREPLIFETIME(AMD_QuestRackSpawner, bCartridgeReleased);
	DOREPLIFETIME(AMD_QuestRackSpawner, bCartridgeCollected);
	DOREPLIFETIME(AMD_QuestRackSpawner, bRackDisabled);
	DOREPLIFETIME(AMD_QuestRackSpawner, ReleasedCartridge);
}

bool AMD_QuestRackSpawner::ClaimsQuestPhase(const FPrimaryAssetId& Id, int32 Phase) const
{
	return RackQuestDefinitionId.IsValid() && Id == RackQuestDefinitionId && Phase == RackPhaseIndex;
}


float AMD_QuestRackSpawner::GetPadlockCutRatio() const
{
	return bPadlockCut ? 1.f : FMath::Clamp(PadlockCutSeconds / FMath::Max(RequiredPadlockCutSeconds, 0.1f), 0.f, 1.f);
}

bool AMD_QuestRackSpawner::IsAccessRequirementMet() const
{
	switch (RackMode)
	{
	case EMD_QuestRackMode::Contamination:
		return true; // Retired cleaning mode behaves as an open rack.
	case EMD_QuestRackMode::BarnaclesAndPadlock:
		return bPadlockCut;
	case EMD_QuestRackMode::Open:
		return true;
	default:
		return false;
	}
}

void AMD_QuestRackSpawner::TrySpawnForQuestState()
{
	if (!HasAuthority() || !HasActorBegunPlay() || bRackEndingPlay) return;
	UMD_QuestSampleSpawnerManager* Manager = GetWorld()->GetSubsystem<UMD_QuestSampleSpawnerManager>();
	if (!Manager || !Manager->RequestSpawnEvaluation(this)) return;
	// 퀘스트 진행과 무관하게 랙의 물리 접근 조건 활성화
	if (bQuestActive == false || bRackDisabled == true)
	{
		// 모든 배치 랙의 청소 및 잠금 해제 허용
		bQuestActive = true;
		// 이전 퀘스트 수량 제한으로 잠긴 저장 상태 해제
		bRackDisabled = false;
		// 호스트의 물리 접근 상태 갱신
		OnRep_RackState();
		// 클라이언트에 복구된 랙 접근 상태 전달
		ForceNetUpdate();
	}
	if (bCartridgeReleased == true || IsAccessRequirementMet() == false) return;
	bCartridgeReleased = true;
	OnRep_RackState();
	ForceNetUpdate();
}

AMD_QuestItem* AMD_QuestRackSpawner::SpawnCartridge()
{
	// 배치된 랙의 기존 카트리지 구성 조회
	UMD_QuestDefinition* Definition = UMD_QuestDefinition::LoadDefinition(RackQuestDefinitionId);
	if (!Definition || !Definition->HasPhase(RackPhaseIndex) || Definition->UsesHotbarDelivery(RackPhaseIndex)) return nullptr;
	const EMD_QuestObjectiveType Objective = Definition->GetObjectiveType(RackPhaseIndex);
	if (Objective != EMD_QuestObjectiveType::RecoverIntact && Objective != EMD_QuestObjectiveType::RecoverDamaged) return nullptr;
	const TSubclassOf<AMD_QuestItem> RequiredClass = ResolveSpawnItemClass(*Definition, RackPhaseIndex);
	const TSubclassOf<AMD_QuestItem> ItemClass = RackCartridgeClass ? RackCartridgeClass : RequiredClass;
	if (!RequiredClass || !ItemClass || !ItemClass->IsChildOf(RequiredClass)) return nullptr;
	const FTransform Placement = DefaultSpawnPoint->GetComponentTransform();
	AMD_QuestItem* Item = GetWorld()->SpawnActorDeferred<AMD_QuestItem>(ItemClass, Placement,
		this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Item)
	{
		if (!bSpawnFailureLogged) UE_LOG(LogTemp, Error, TEXT("[QuestRack] %s failed to release cartridge"), *GetName());
		bSpawnFailureLogged = true;
		return nullptr;
	}
	// 저장 복원에서 같은 랙의 카트리지를 식별할 구성 키 유지
	Item->QuestDefinitionId = RackQuestDefinitionId;
	ReleasedCartridge = Item;
	UGameplayStatics::FinishSpawningActor(Item, Placement);
	Item->SuppressImpactDamage(SampleSpawnProtectionDuration);
	Item->SetActorHiddenInGame(true);
	Item->SetActorEnableCollision(false);
	if (UStaticMeshComponent* ItemMesh = Cast<UStaticMeshComponent>(Item->GetRootComponent()))
		ItemMesh->SetSimulatePhysics(false);
	ForceNetUpdate();
	return Item;
}

bool AMD_QuestRackSpawner::CanCollectCartridge(const AMD_QuestItem* Item) const
{
	// Collected items remain carryable for normal delivery, including after completion.
	return Item && Item == ReleasedCartridge && (bCartridgeCollected || (bQuestActive && bCartridgeReleased && !bRackDisabled));
}

void AMD_QuestRackSpawner::NotifyCartridgeGrabbed(AMD_QuestItem* Item)
{
	if (!HasAuthority() || !CanCollectCartridge(Item) || bCartridgeCollected) return;
	bCartridgeCollected = true;
	OnRep_RackState();
	ForceNetUpdate();
}


void AMD_QuestRackSpawner::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
	if (!bRackDisabled && RackMode == EMD_QuestRackMode::BarnaclesAndPadlock && !bPadlockCut)
		TagContainer.AddTag(MDGameplayTags::Interaction_Cuttable);
}

bool AMD_QuestRackSpawner::CanCutPadlockFromHit(const FHitResult& Hit) const
{
	return bQuestActive && !bRackDisabled && RackMode == EMD_QuestRackMode::BarnaclesAndPadlock
		&& !bPadlockCut && !bCartridgeReleased && Hit.GetActor() == this && Hit.GetComponent() == PadlockMesh;
}

void AMD_QuestRackSpawner::AddPadlockCutProgress(float WorkSeconds, AActor* Cutter, const FHitResult& Hit)
{
	if (!HasAuthority() || !CanCutPadlockFromHit(Hit) || !FMath::IsFinite(WorkSeconds) || WorkSeconds <= 0.f || WorkSeconds > 1.f) return;
	const AMD_Character* Character = Cast<AMD_Character>(Cutter);
	const UMD_EquipHotbarComponent* Hotbar = Character ? Character->GetHotbarComponent() : nullptr;
	UMD_ItemDefinition* Tool = Hotbar ? Hotbar->GetCurrentItem() : nullptr;
	const AController* Controller = Character ? Character->GetController() : nullptr;
	if (!Tool || !Controller || !Tool->GetUseAbilityTag().MatchesTagExact(MDGameplayTags::Ability_WallCut)) return;
	FVector Start;
	FRotator Rotation;
	Controller->GetPlayerViewPoint(Start, Rotation);
	FHitResult Verified;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MDRackPadlockCut), false, Cutter);
	if (!GetWorld()->LineTraceSingleByChannel(Verified, Start, Start + Rotation.Vector() * PadlockReachCm, ECC_Visibility, Params)
		|| !CanCutPadlockFromHit(Verified)) return;
	const float Required = FMath::IsFinite(RequiredPadlockCutSeconds) ? FMath::Max(RequiredPadlockCutSeconds, 0.1f) : 3.6f;
	PadlockCutSeconds = FMath::Min(PadlockCutSeconds + WorkSeconds, Required);
	if (PadlockCutSeconds >= Required)
	{
		bPadlockCut = true;
		OnRep_RackState();
		TrySpawnForQuestState();
	}
	ForceNetUpdate();
}

void AMD_QuestRackSpawner::OnRep_RackState()
{
	const bool bCanWork = !HasActorBegunPlay() || (bQuestActive && !bRackDisabled && !bCartridgeCollected);
	if (IsValid(ReleasedCartridge) && !bCartridgeCollected)
	{
		// Old saves may contain a released but uncollected floor item. Keep it
		// reserved inside the rack until the same E handoff succeeds.
		ReleasedCartridge->SetActorHiddenInGame(true);
		ReleasedCartridge->SetActorEnableCollision(false);
		if (UStaticMeshComponent* ItemMesh = Cast<UStaticMeshComponent>(ReleasedCartridge->GetRootComponent()))
			ItemMesh->SetSimulatePhysics(false);
	}
	else if (IsValid(ReleasedCartridge))
	{
		ReleasedCartridge->SetActorHiddenInGame(false);
		ReleasedCartridge->SetActorEnableCollision(true);
	}
	if (!PadlockMesh) return;
	const bool bVisible = RackMode == EMD_QuestRackMode::BarnaclesAndPadlock && !bPadlockCut;
	PadlockMesh->SetVisibility(bVisible, true);
	PadlockMesh->SetHiddenInGame(!bVisible, true);
	PadlockMesh->SetCollisionEnabled(bVisible && bCanWork ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
}

bool AMD_QuestRackSpawner::CanInteract_Implementation(AActor* Interactor) const
{
	return IsValid(Interactor) && bQuestActive && !bRackDisabled && !bCartridgeCollected;
}

FText AMD_QuestRackSpawner::GetInteractionDisplayName_Implementation() const
{
	return NSLOCTEXT("MDQuestRack", "Name", "서버랙");
}

FText AMD_QuestRackSpawner::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	if (!CanInteract_Implementation(Interactor)) return FText::GetEmpty();
	if (bCartridgeReleased)
		return NSLOCTEXT("MDQuestRack", "Collect", "[E] 카트리지 회수");
	if (RackMode == EMD_QuestRackMode::BarnaclesAndPadlock)
	{
		return FText::Format(NSLOCTEXT("MDQuestRack", "Lock", "[LMB] 자물쇠 절단 {0}%"), FMath::FloorToInt(GetPadlockCutRatio()*100.f));
	}
	return NSLOCTEXT("MDQuestRack", "Open", "카트리지 회수 가능");
}

void AMD_QuestRackSpawner::Interact_Implementation(AActor* Interactor)
{
	if (!HasAuthority() || !CanInteract_Implementation(Interactor) || bCollectionInProgress) return;
	TrySpawnForQuestState();
	UMD_InteractionComponent* Interaction = Interactor->FindComponentByClass<UMD_InteractionComponent>();
	// 랙 해제 상태와 실제 운반 가능 여부로 회수 요청 검증
	if (bCartridgeReleased == false || bRackDisabled == true || IsValid(Interaction) == false
		|| Interaction->CanOwnerInteract() == false || Interaction->IsHolding() == true) return;
	TGuardValue<bool> CollectionGuard(bCollectionInProgress, true);
	// 이미 생성한 카트리지를 재사용하고 미생성 시에만 배치 구성으로 생성
	AMD_QuestItem* Item = IsValid(ReleasedCartridge) == true ? ReleasedCartridge.Get() : SpawnCartridge();
	if (!Item) return;
	if (!Interaction->TryReceiveQuestItem(Item, this))
	{
		// A failed handoff leaves the rack ready and never drops a duplicate item.
		ReleasedCartridge = nullptr;
		Item->Destroy();
		ForceNetUpdate();
	}
}
