#include "World/Sample/MD_QuestSampleSpawner.h"

#include "Components/ArrowComponent.h"
#include "Components/SceneComponent.h"
#include "Component/Merchant/MD_MerchantComponent.h"
#include "Data/Merchant/MD_SiteDefinition.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/MD_GameStateBase.h"
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "UObject/SoftObjectPath.h"
#include "World/Sample/MD_SampleItem.h"

AMD_QuestSampleSpawner::AMD_QuestSampleSpawner()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	DefaultSpawnPoint = CreateDefaultSubobject<UArrowComponent>(TEXT("DefaultSpawnPoint"));
	DefaultSpawnPoint->SetupAttachment(Root);
	DefaultSpawnPoint->SetArrowColor(FColor::Yellow);
	DefaultSpawnPoint->ArrowSize = 1.25f;
	DefaultSpawnPoint->bHiddenInGame = true;

	QuestCatalogSite = TSoftObjectPtr<UMD_SiteDefinition>(
		FSoftObjectPath(TEXT("/Game/Data/Site/DA_Site_X7")));

	RefreshLinkedQuestPreview();
}

#if WITH_EDITOR
void AMD_QuestSampleSpawner::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(AMD_QuestSampleSpawner, MainQuestIndex)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(AMD_QuestSampleSpawner, QuestCatalogSite)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(AMD_QuestSampleSpawner, UnlockDay))
	{
		if (PropertyName == GET_MEMBER_NAME_CHECKED(AMD_QuestSampleSpawner, UnlockDay))
		{
			MainQuestIndex = FMath::Max(0, UnlockDay - 1);
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(AMD_QuestSampleSpawner, MainQuestIndex))
		{
			UnlockDay = MainQuestIndex + 1;
		}

		RefreshLinkedQuestPreview();
	}
}
#endif

void AMD_QuestSampleSpawner::RefreshLinkedQuestPreview()
{
	if (const UMD_SiteDefinition* Site = QuestCatalogSite.LoadSynchronous())
	{
		if (Site->MainQuestLine.IsValidIndex(MainQuestIndex))
		{
			LinkedQuestName = Site->MainQuestLine[MainQuestIndex].QuestName;
			return;
		}
	}

	LinkedQuestName = FText::GetEmpty();
}

bool AMD_QuestSampleSpawner::MatchesLinkedQuest(const FMD_MainQuestState& QuestState) const
{
	if (QuestState.SourceIndex != INDEX_NONE)
	{
		return QuestState.SourceIndex == MainQuestIndex;
	}

	return QuestState.UnlockDay == UnlockDay;
}

void AMD_QuestSampleSpawner::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (HasAuthority())
	{
		InitializeSpawner();
	}
}

void AMD_QuestSampleSpawner::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		InitializeSpawner();
	}
}

void AMD_QuestSampleSpawner::InitializeSpawner()
{
	if (bSpawnerInitialized || !HasAuthority())
	{
		return;
	}

	UWorld* World = GetWorld();
	UGameInstance* GameInstance = IsValid(World) ? World->GetGameInstance() : nullptr;
	if (!IsValid(GameInstance))
	{
		return;
	}

	bSpawnerInitialized = true;
	ResolveDefaultSampleClass();

	if (UMD_MerchantSubsystem* MerchantSubsystem = GameInstance->GetSubsystem<UMD_MerchantSubsystem>())
	{
		if (!SubsystemChangedHandle.IsValid())
		{
			SubsystemChangedHandle = MerchantSubsystem->OnMerchantStateChanged.AddUObject(
				this, &AMD_QuestSampleSpawner::HandleSubsystemStateChanged);
		}
	}

	if (AMD_GameStateBase* GameState = World->GetGameState<AMD_GameStateBase>())
	{
		if (UMD_MerchantComponent* MerchantComponent = GameState->GetMerchantComponent())
		{
			MerchantComponent->OnMerchantStateChanged.AddDynamic(
				this, &AMD_QuestSampleSpawner::HandleMerchantStateChanged);
		}
	}

	// GameMode의 Merchant 동기화가 끝난 뒤 한 번 더 검사한다.
	World->GetTimerManager().SetTimerForNextTick(this, &AMD_QuestSampleSpawner::TrySpawnForQuestState);
}

void AMD_QuestSampleSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		if (UWorld* World = GetWorld())
		{
			if (UGameInstance* GameInstance = World->GetGameInstance())
			{
				if (UMD_MerchantSubsystem* MerchantSubsystem = GameInstance->GetSubsystem<UMD_MerchantSubsystem>())
				{
					MerchantSubsystem->OnMerchantStateChanged.Remove(SubsystemChangedHandle);
				}
			}
		}

		SubsystemChangedHandle.Reset();
		bSpawnerInitialized = false;

		if (AMD_GameStateBase* GameState = GetWorld()->GetGameState<AMD_GameStateBase>())
		{
			if (UMD_MerchantComponent* MerchantComponent = GameState->GetMerchantComponent())
			{
				MerchantComponent->OnMerchantStateChanged.RemoveDynamic(
					this, &AMD_QuestSampleSpawner::HandleMerchantStateChanged);
			}
		}
	}

	Super::EndPlay(EndPlayReason);
}

void AMD_QuestSampleSpawner::HandleMerchantStateChanged()
{
	TrySpawnForQuestState();
}

void AMD_QuestSampleSpawner::HandleSubsystemStateChanged()
{
	TrySpawnForQuestState();
}

void AMD_QuestSampleSpawner::ResolveDefaultSampleClass()
{
	if (SampleClass)
	{
		return;
	}

	static const FSoftClassPath DefaultSampleClassPath(
		TEXT("/Game/Blueprint/Sample/BP_SampleBase.BP_SampleBase_C"));

	if (UClass* LoadedClass = DefaultSampleClassPath.TryLoadClass<AMD_SampleItem>())
	{
		SampleClass = LoadedClass;
	}
}

void AMD_QuestSampleSpawner::TrySpawnForQuestState()
{
	if (!HasAuthority())
	{
		return;
	}

	ResolveDefaultSampleClass();

	if (!SampleClass)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[QuestSampleSpawner] %s: SampleClass 없음 — BP_SampleBase가 MD_SampleItem을 상속하는지 확인"),
			*GetName());
		return;
	}

	UWorld* World = GetWorld();
	UGameInstance* GameInstance = IsValid(World) ? World->GetGameInstance() : nullptr;
	const UMD_MerchantSubsystem* MerchantSubsystem =
		IsValid(GameInstance) ? GameInstance->GetSubsystem<UMD_MerchantSubsystem>() : nullptr;
	if (!IsValid(MerchantSubsystem))
	{
		return;
	}

	if (!MerchantSubsystem->HasActiveContract())
	{
		return;
	}

	FMD_MainQuestState QuestState;
	if (!FindLinkedMainQuest(MerchantSubsystem, QuestState))
	{
		return;
	}

	if (QuestState.Status == EMD_MainQuestStatus::Locked)
	{
		bHasSpawnedForActiveQuest = false;
		return;
	}

	if (QuestState.Status != EMD_MainQuestStatus::Active)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[QuestSampleSpawner] %s: Day%d 퀘스트 상태=%d — Active일 때만 스폰"),
			*GetName(), UnlockDay, static_cast<int32>(QuestState.Status));
		return;
	}

	if (bHasSpawnedForActiveQuest || HasOccupyingSamples())
	{
		bHasSpawnedForActiveQuest = true;
		return;
	}

	SpawnSamples(QuestState);
	bHasSpawnedForActiveQuest = true;
}

bool AMD_QuestSampleSpawner::FindLinkedMainQuest(
	const UMD_MerchantSubsystem* MerchantSubsystem,
	FMD_MainQuestState& OutQuestState) const
{
	for (const FMD_MainQuestState& QuestState : MerchantSubsystem->GetMainQuests())
	{
		if (MatchesLinkedQuest(QuestState))
		{
			OutQuestState = QuestState;
			return true;
		}
	}

	return false;
}

bool AMD_QuestSampleSpawner::HasOccupyingSamples() const
{
	const UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return false;
	}

	const TArray<FTransform> SpawnTransforms = CollectSpawnTransforms();
	if (SpawnTransforms.Num() == 0)
	{
		return false;
	}

	const float RadiusSq = FMath::Square(OccupiedCheckRadius);
	TSet<const AMD_SampleItem*> CountedSamples;

	for (const FTransform& SpawnTransform : SpawnTransforms)
	{
		const FVector Location = SpawnTransform.GetLocation();

		for (TActorIterator<AMD_SampleItem> It(World); It; ++It)
		{
			const AMD_SampleItem* Sample = *It;
			if (!IsValid(Sample) || Sample->Grade != SampleGrade || CountedSamples.Contains(Sample))
			{
				continue;
			}

			if (FVector::DistSquared(Sample->GetActorLocation(), Location) <= RadiusSq)
			{
				CountedSamples.Add(Sample);
			}
		}
	}

	return CountedSamples.Num() >= SamplesToSpawn;
}

void AMD_QuestSampleSpawner::SpawnSamples(const FMD_MainQuestState& QuestState)
{
	const FMD_QuestDefinitionData& Definition = QuestState.Definition;
	if (!Definition.IsValidDefinition())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[QuestSampleSpawner] %s: Day%d 퀘스트 Definition 없음"),
			*GetName(), UnlockDay);
		return;
	}

	const FGameplayTag ExpectedGradeTag = UMD_MerchantSubsystem::GradeTagFromSampleGrade(SampleGrade);
	if (ExpectedGradeTag.IsValid()
		&& !Definition.TargetGradeTag.MatchesTagExact(ExpectedGradeTag))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[QuestSampleSpawner] %s: [%s] SampleGrade와 퀘스트 등급 태그가 다르다"),
			*GetName(),
			*Definition.QuestName.ToString());
	}

	const TArray<FTransform> SpawnTransforms = CollectSpawnTransforms();
	if (SpawnTransforms.Num() == 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[QuestSampleSpawner] %s: 스폰 포인트 없음"),
			*GetName());
		return;
	}

	const int32 SpawnCount = FMath::Max(1, SamplesToSpawn);
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (int32 Index = 0; Index < SpawnCount; ++Index)
	{
		const FTransform& SpawnTransform = SpawnTransforms[Index % SpawnTransforms.Num()];

		AMD_SampleItem* Sample = GetWorld()->SpawnActor<AMD_SampleItem>(
			SampleClass,
			SpawnTransform.GetLocation(),
			SpawnTransform.Rotator(),
			SpawnParams);

		if (!IsValid(Sample))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[QuestSampleSpawner] %s: 샘플 스폰 실패 (Day%d, index %d) — BP_SampleBase 부모=MD_SampleItem 확인"),
				*GetName(), UnlockDay, Index);
			continue;
		}

		Sample->SetActorScale3D(SpawnTransform.GetScale3D().IsNearlyZero()
			? FVector(0.4f)
			: SpawnTransform.GetScale3D());
		Sample->Grade = SampleGrade;
		Sample->SpawnProtectionDuration = SampleSpawnProtectionDuration;

		UE_LOG(LogTemp, Log,
			TEXT("[QuestSampleSpawner] %s: [%s] Day%d %s급 샘플 스폰 @ %s"),
			*GetName(),
			*Definition.QuestName.ToString(),
			QuestState.UnlockDay,
			*StaticEnum<EMD_SampleGrade>()->GetNameStringByValue(static_cast<int64>(SampleGrade)),
			*Sample->GetActorLocation().ToCompactString());
	}
}

TArray<FTransform> AMD_QuestSampleSpawner::CollectSpawnTransforms() const
{
	TArray<FTransform> Transforms;

	TArray<UArrowComponent*> ArrowPoints;
	GetComponents<UArrowComponent>(ArrowPoints);

	if (ArrowPoints.Num() == 0 && IsValid(DefaultSpawnPoint))
	{
		ArrowPoints.Add(DefaultSpawnPoint);
	}

	Transforms.Reserve(ArrowPoints.Num());
	for (const UArrowComponent* Arrow : ArrowPoints)
	{
		if (IsValid(Arrow))
		{
			Transforms.Add(Arrow->GetComponentTransform());
		}
	}

	if (Transforms.Num() == 0 && IsValid(Root))
	{
		Transforms.Add(Root->GetComponentTransform());
	}

	return Transforms;
}
