#include "World/Sample/MD_QuestRackSpawner.h"

#include "Character/MD_Character.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Component/Interaction/MD_InteractionComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/DecalComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/Equipment/MD_ItemDefinition.h"
#include "Data/Merchant/MD_QuestDefinition.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "Gameplay/Scrape/MD_ScrapeMeshUVData.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "World/Sample/MD_QuestItem.h"
#include "World/Sample/MD_QuestSampleSpawnerManager.h"

UMD_QuestRackScrapeSurface::UMD_QuestRackScrapeSurface()
{
	bContaminationEnabled = false;
	bForceFullContamination = false;
	ContaminationItemsPerFace = 0;
	ContaminationSurfaceMapping.Mode = EMD_ScrapeSurfaceMappingMode::MeshUV;
}

void UMD_QuestRackScrapeSurface::ConfigureRackBeforePlay(UStaticMeshComponent* InMesh,
	UDecalComponent* Front, UDecalComponent* Back, bool bEnabled)
{
	if (!HasBegunPlay())
	{
		bContaminationEnabled = bEnabled;
		ConfigureExternalComponents(InMesh, Front, Back);
	}
}

bool UMD_QuestRackScrapeSurface::CompleteCleaningWithoutDrops()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !IsSurfaceInitialized()) return false;
	if (IsContaminationFullyRemoved()) return true;
	TArray<int32> Indices;
	TArray<float> Initial;
	TArray<float> Zero;
	TArray<float> Covered;
	const FIntPoint Grid = GetContaminationGridDimensions();
	for (int32 Index = 0; Index < Grid.X * Grid.Y * 2; ++Index)
	{
		FMD_ScrapeSurfaceCell Cell;
		if (!GetContaminationCellState(Index, Cell)) continue;
		Indices.Add(Index);
		Initial.Add(Cell.InitialContamination);
		Zero.Add(0.f);
		Covered.Add(1.f);
	}
	// Reuse the shared wall state/visual/replication path. Keep the surface
	// enabled so saved clean cells and pre-completion saves remain restorable.
	// Restoring absolute cells produces no extra loot or tool-load rewards.
	RestoreContaminationCells(Indices, Initial, Zero, Zero, Zero, Covered);
	return IsContaminationFullyRemoved();
}

AMD_QuestRackSpawner::AMD_QuestRackSpawner()
{
	bReplicates = true;
	RackQuestDefinitionId = FPrimaryAssetId(FPrimaryAssetType(TEXT("Quest")), FName(TEXT("X7-M01")));
	RackMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RackMesh"));
	RackMesh->SetupAttachment(Root);
	RackMesh->SetCollisionProfileName(TEXT("BlockAll"));
	RackMesh->SetRelativeScale3D(FVector(2.f));
	RackWorkSurface = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RackWorkSurface"));
	RackWorkSurface->SetupAttachment(RackMesh);
	RackWorkSurface->SetRelativeLocation(FVector(0.f, 24.f, 50.f));
	RackWorkSurface->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	RackWorkSurface->SetCollisionResponseToAllChannels(ECR_Ignore);
	RackWorkSurface->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	RackWorkSurface->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);
	PadlockMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PadlockMesh"));
	PadlockMesh->SetupAttachment(RackMesh);
	PadlockMesh->SetRelativeLocation(FVector(-2.f, 26.f, 44.f));
	PadlockMesh->SetRelativeScale3D(FVector(0.5f));
	PadlockMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PadlockMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	PadlockMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	PadlockMesh->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);
	FrontContaminationDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("FrontContaminationDecal"));
	BackContaminationDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("BackContaminationDecal"));
	for (UDecalComponent* Decal : {FrontContaminationDecal.Get(), BackContaminationDecal.Get()})
	{
		Decal->SetupAttachment(RackWorkSurface);
		Decal->SetVisibility(false);
		Decal->SetHiddenInGame(true);
	}
	RackScrapeSurface = CreateDefaultSubobject<UMD_QuestRackScrapeSurface>(TEXT("RackScrapeSurface"));
	RackScrapeSurface->ConfigureRackBeforePlay(RackWorkSurface, FrontContaminationDecal, BackContaminationDecal, false);
	DefaultSpawnPoint->SetRelativeLocation(FVector(0.f, 85.f, 105.f));
	DefaultSpawnPoint->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));
}

void AMD_QuestRackSpawner::ConfigureRackComponents()
{
	// BlockAll inherits Ignore for the project's custom interaction channel.
	// The body remains the E target after the work panel/lock disappears.
	RackMesh->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);
	RackScrapeSurface->ConfigureRackBeforePlay(RackWorkSurface, FrontContaminationDecal, BackContaminationDecal,
		RackMode == EMD_QuestRackMode::Contamination);
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
	// Component BeginPlay runs while its actor is still beginning play, so the
	// shared wall surface deliberately defers initialization to its owner.
	if (RackScrapeSurface->IsContaminationEnabled() && !RackScrapeSurface->IsSurfaceInitialized())
	{
		RackScrapeSurface->InitializeSurface();
	}
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

bool AMD_QuestRackSpawner::FindActiveRackQuest(FMD_MainQuestState& OutState) const
{
	const UGameInstance* GI = GetGameInstance();
	const UMD_MerchantSubsystem* Merchant = GI ? GI->GetSubsystem<UMD_MerchantSubsystem>() : nullptr;
	if (!Merchant || !Merchant->HasActiveContract()) return false;
	for (const FMD_MainQuestState& State : Merchant->GetMainQuests())
	{
		if (State.IsActive() && ClaimsQuestPhase(State.DefinitionId, State.CurrentPhaseIndex))
		{
			OutState = State;
			return true;
		}
	}
	return false;
}

float AMD_QuestRackSpawner::GetContaminationRemovedRatio() const
{
	if (RackMode == EMD_QuestRackMode::Contamination && bCartridgeReleased) return 1.f;
	if (!IsValid(RackScrapeSurface) || !RackScrapeSurface->IsContaminationEnabled()
		|| !RackScrapeSurface->IsSurfaceInitialized()) return 0.f;
	const UMD_ScrapeMeshUVData* Data = RackScrapeSurface->GetContaminationMeshUVData();
	// No baked surface must not count as an already clean rack.
	if (!Data || Data->ValidCellCount <= 0) return 0.f;
	return RackScrapeSurface->GetContaminationCleanRatio();
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
		return FMath::IsFinite(RequiredCleanRatio) && GetContaminationRemovedRatio() >= FMath::Clamp(RequiredCleanRatio, 0.f, 1.f);
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
	RefreshRackGroup();
	FMD_MainQuestState State;
	const bool bActive = FindActiveRackQuest(State);
	if (bQuestActive != bActive)
	{
		bQuestActive = bActive;
		OnRep_RackState();
		ForceNetUpdate();
	}
	if (!bActive || bRackDisabled || bCartridgeReleased || !IsAccessRequirementMet()) return;
	if (RackMode == EMD_QuestRackMode::Contamination)
	{
		if (!RackScrapeSurface->CompleteCleaningWithoutDrops()) return;
	}
	bCartridgeReleased = true;
	OnRep_RackState();
	ForceNetUpdate();
}

AMD_QuestItem* AMD_QuestRackSpawner::SpawnCartridge(const FMD_MainQuestState& State)
{
	UMD_QuestDefinition* Definition = UMD_QuestDefinition::LoadDefinition(State.DefinitionId);
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
	Item->QuestDefinitionId = State.DefinitionId;
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
	// The successful second server grab disables the other rack in the same call.
	RefreshRackGroup();
}

void AMD_QuestRackSpawner::RefreshRackGroup()
{
	if (!HasAuthority()) return;
	TArray<AMD_QuestRackSpawner*> Group;
	int32 Collected = 0;
	for (TActorIterator<AMD_QuestRackSpawner> It(GetWorld()); It; ++It)
	{
		if (!It->ClaimsQuestPhase(RackQuestDefinitionId, RackPhaseIndex)) continue;
		Group.Add(*It);
		Collected += It->bCartridgeCollected ? 1 : 0;
	}
	if (Collected < 2) return;
	for (AMD_QuestRackSpawner* Rack : Group)
	{
		if (Rack->bCartridgeCollected || Rack->bRackDisabled) continue;
		Rack->bRackDisabled = true;
		Rack->OnRep_RackState();
		Rack->ForceNetUpdate();
	}
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
	FMD_MainQuestState State;
	if (!FindActiveRackQuest(State)) return;
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
	if (RackWorkSurface)
	{
		const bool bShowWorkSurface = RackMode == EMD_QuestRackMode::Contamination && !bCartridgeReleased;
		RackWorkSurface->SetVisibility(bShowWorkSurface);
		RackWorkSurface->SetHiddenInGame(!bShowWorkSurface);
		RackWorkSurface->SetCollisionEnabled(bShowWorkSurface && bCanWork ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	}
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
	if (RackMode == EMD_QuestRackMode::Contamination)
		return FText::Format(NSLOCTEXT("MDQuestRack", "Clean", "[RMB] 오염물 제거 {0}% / {1}%"),
			FMath::FloorToInt(GetContaminationRemovedRatio()*100.f), FMath::RoundToInt(RequiredCleanRatio*100.f));
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
	FMD_MainQuestState State;
	UMD_InteractionComponent* Interaction = Interactor->FindComponentByClass<UMD_InteractionComponent>();
	if (!bCartridgeReleased || bRackDisabled || !FindActiveRackQuest(State)
		|| !Interaction || !Interaction->CanOwnerInteract() || Interaction->IsHolding()) return;
	TGuardValue<bool> CollectionGuard(bCollectionInProgress, true);
	AMD_QuestItem* Item = IsValid(ReleasedCartridge) ? ReleasedCartridge.Get() : SpawnCartridge(State);
	if (!Item) return;
	if (!Interaction->TryReceiveQuestItem(Item, this))
	{
		// A failed handoff leaves the rack ready and never drops a duplicate item.
		ReleasedCartridge = nullptr;
		Item->Destroy();
		ForceNetUpdate();
	}
}
