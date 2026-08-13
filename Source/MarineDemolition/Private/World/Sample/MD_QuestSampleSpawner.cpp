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

/**
 * PostInitializeComponents와 BeginPlay 양쪽에서 초기화를 시도한다.
 *
 * 레벨에 미리 배치된 스포너는 PostInitializeComponents 시점에 GameInstance가
 * 이미 살아 있어 그때 구독을 걸 수 있지만, 런타임에 스폰되거나 트래블 직후
 * 복원되는 경우에는 그 시점에 GameState·Subsystem이 아직 없다. 두 경로 모두
 * 커버하고 실제 중복 실행은 bSpawnerInitialized가 막는다.
 */
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
		// 레벨 트래블·종료 시 World가 이미 사라졌을 수 있어 한 번만 잡아두고 재사용한다.
		if (UWorld* World = GetWorld())
		{
			if (UGameInstance* GameInstance = World->GetGameInstance())
			{
				if (UMD_MerchantSubsystem* MerchantSubsystem = GameInstance->GetSubsystem<UMD_MerchantSubsystem>())
				{
					MerchantSubsystem->OnMerchantStateChanged.Remove(SubsystemChangedHandle);
				}
			}

			if (AMD_GameStateBase* GameState = World->GetGameState<AMD_GameStateBase>())
			{
				if (UMD_MerchantComponent* MerchantComponent = GameState->GetMerchantComponent())
				{
					MerchantComponent->OnMerchantStateChanged.RemoveDynamic(
						this, &AMD_QuestSampleSpawner::HandleMerchantStateChanged);
				}
			}
		}

		SubsystemChangedHandle.Reset();
		bSpawnerInitialized = false;
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

void AMD_QuestSampleSpawner::TrySpawnForQuestState()
{
	if (!HasAuthority())
	{
		return;
	}

	if (IsValid(SampleClass) == false)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[QuestSampleSpawner] %s: SampleClass가 비어 있다 — BP_QuestSampleSpawner 기본값에 샘플 BP(BP_SampleBase 등)를 지정하세요"),
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

	TrySpawnForMainQuestState(MerchantSubsystem);
	TrySpawnForRepeatQuestStates(MerchantSubsystem);
}

void AMD_QuestSampleSpawner::TrySpawnForMainQuestState(const UMD_MerchantSubsystem* MerchantSubsystem)
{
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

void AMD_QuestSampleSpawner::TrySpawnForRepeatQuestStates(const UMD_MerchantSubsystem* MerchantSubsystem)
{
	if (IsValid(MerchantSubsystem) == false || IsPrimaryRepeatQuestSpawner() == false)
	{
		return;
	}

	// 완료·실패 뒤 같은 퀘스트가 다시 제시될 때 새 수락 주기를 처리하기 위한 기록 정리.
	TSet<FName> AcceptedQuestIds;
	for (const FMD_RepeatQuestState& QuestState : MerchantSubsystem->GetRepeatQuests())
	{
		if (QuestState.Status == EMD_RepeatQuestStatus::Accepted && MatchesRepeatQuest(QuestState) == true)
		{
			AcceptedQuestIds.Add(QuestState.Definition.QuestId);
		}
	}

	// 이전에 필요한 샘플 생성 처리가 완료된 퀘스트 아이디 목록화
	TArray<FName> HandledQuestIdsToRemove;
	for (const FName& HandledQuestId : HandledRepeatQuestIds)
	{
		if (AcceptedQuestIds.Contains(HandledQuestId) == false)
		{
			HandledQuestIdsToRemove.Add(HandledQuestId);
		}
	}

	// 이전에 필요한 샘플 생성 처리가 완료된 퀘스트 아이디 제외
	for (const FName& HandledQuestId : HandledQuestIdsToRemove)
	{
		HandledRepeatQuestIds.Remove(HandledQuestId);
	}

	// 메인 퀘스트와 반복 퀘스트가 같은 등급 샘플을 함께 인정하는 현재 납품 규칙에 맞춘 부족분 계산.
	for (const FMD_RepeatQuestState& QuestState : MerchantSubsystem->GetRepeatQuests())
	{
		const FName QuestId = QuestState.Definition.QuestId;
		if (QuestState.Status != EMD_RepeatQuestStatus::Accepted || MatchesRepeatQuest(QuestState) == false || HandledRepeatQuestIds.Contains(QuestId) == true)
		{
			continue;
		}

		const int32 RemainingCount = FMath::Max(0, QuestState.Definition.RequiredCount - QuestState.CurrentProgress);
		const int32 MissingCount = FMath::Max(0, RemainingCount - CountAvailableSamplesForGrade());
		const int32 SpawnedCount = MissingCount > 0 ? SpawnRepeatQuestSamples(QuestState, MissingCount) : 0;
		if (MissingCount == 0 || SpawnedCount == MissingCount)
		{
			HandledRepeatQuestIds.Add(QuestId);
		}
	}
}

bool AMD_QuestSampleSpawner::IsPrimaryRepeatQuestSpawner() const
{
	const UWorld* World = GetWorld();
	bool bIsPrimarySpawner = IsValid(World);
	if (bIsPrimarySpawner == false)
	{
		return bIsPrimarySpawner;
	}

	const FString SpawnerPath = GetPathName();
	for (TActorIterator<AMD_QuestSampleSpawner> It(World); It; ++It)
	{
		const AMD_QuestSampleSpawner* Candidate = *It;
		if (IsValid(Candidate) == false || Candidate == this || Candidate->SampleGrade != SampleGrade || IsValid(Candidate->SampleClass.Get()) == false)
		{
			continue;
		}

		if (Candidate->GetPathName() < SpawnerPath)
		{
			bIsPrimarySpawner = false;
			break;
		}
	}

	return bIsPrimarySpawner;
}

int32 AMD_QuestSampleSpawner::CountAvailableSamplesForGrade() const
{
	const UWorld* World = GetWorld();
	int32 AvailableSampleCount = 0;
	if (IsValid(World) == false)
	{
		return AvailableSampleCount;
	}

	for (TActorIterator<AMD_SampleItem> It(World); It; ++It)
	{
		const AMD_SampleItem* Sample = *It;
		if (IsValid(Sample) == true && Sample->Grade == SampleGrade)
		{
			++AvailableSampleCount;
		}
	}

	return AvailableSampleCount;
}

bool AMD_QuestSampleSpawner::MatchesRepeatQuest(const FMD_RepeatQuestState& QuestState) const
{
	const FMD_QuestDefinitionData& Definition = QuestState.Definition;
	if (Definition.IsValidDefinition() == false)
	{
		return false;
	}

	const bool bRecoveryObjective = Definition.ObjectiveType == EMD_QuestObjectiveType::RecoverIntact || Definition.ObjectiveType == EMD_QuestObjectiveType::RecoverDamaged;
	if (bRecoveryObjective == false)
	{
		return false;
	}

	const FGameplayTag ExpectedGradeTag = UMD_MerchantSubsystem::GradeTagFromSampleGrade(SampleGrade);
	if (ExpectedGradeTag.IsValid() == false)
	{
		return false;
	}

	const bool bMatchesRepeatQuest = Definition.Kind == EMD_QuestKind::Repeat && Definition.TargetGradeTag.MatchesTagExact(ExpectedGradeTag) == true;
	return bMatchesRepeatQuest;
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

	// 기획 09 §1.5: 스폰 개수는 퀘스트 목표 개수와 1:1이고 여분 샘플은 두지 않는다.
	// 여분이 있으면 그 하나가 깨져도 납품은 가능한데 퀘스트는 파괴로 실패해버려
	// (§1.5 "실패 — 파괴, 재시도 불가") 회복 불가 상태가 된다. 배치 실수를 여기서 잡는다.
	if (Definition.RequiredCount > 0 && SamplesToSpawn != Definition.RequiredCount)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[QuestSampleSpawner] %s: [%s] SamplesToSpawn(%d)이 퀘스트 RequiredCount(%d)와 다르다 — 기획 09 §1.5는 1:1을 요구한다"),
			*GetName(),
			*Definition.QuestName.ToString(),
			SamplesToSpawn,
			Definition.RequiredCount);
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

		// 스폰 포인트에 스케일이 잡혀 있을 때만 덮어쓴다. 비어 있으면 샘플 BP의
		// 기본 스케일을 그대로 살린다 (여기서 기본값을 복제하면 양쪽이 갈라진다).
		const FVector SpawnScale = SpawnTransform.GetScale3D();
		if (!SpawnScale.IsNearlyZero())
		{
			Sample->SetActorScale3D(SpawnScale);
		}
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

int32 AMD_QuestSampleSpawner::SpawnRepeatQuestSamples(const FMD_RepeatQuestState& QuestState, int32 SpawnCount)
{
	const FMD_QuestDefinitionData& Definition = QuestState.Definition;
	int32 SpawnedCount = 0;
	if (Definition.IsValidDefinition() == false || SpawnCount <= 0)
	{
		return SpawnedCount;
	}

	const TArray<FTransform> SpawnTransforms = CollectSpawnTransforms();
	if (SpawnTransforms.Num() == 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[QuestSampleSpawner] %s: [%s] 반복 퀘스트 스폰 포인트 없음"),
			*GetName(),
			*Definition.QuestName.ToString());
		return SpawnedCount;
	}

	UWorld* World = GetWorld();
	if (IsValid(World) == false)
	{
		return SpawnedCount;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (int32 Index = 0; Index < SpawnCount; ++Index)
	{
		const FTransform& SpawnTransform = SpawnTransforms[Index % SpawnTransforms.Num()];
		AMD_SampleItem* Sample = World->SpawnActor<AMD_SampleItem>(SampleClass, SpawnTransform.GetLocation(), SpawnTransform.Rotator(), SpawnParams);
		if (IsValid(Sample) == false)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[QuestSampleSpawner] %s: [%s] 반복 퀘스트 샘플 스폰 실패 (index %d) — BP_SampleBase 부모=MD_SampleItem 확인"),
				*GetName(),
				*Definition.QuestName.ToString(),
				Index);
			continue;
		}

		// 스폰 포인트의 명시적인 크기만 반영하고 샘플 BP 기본 크기를 보존하기 위한 처리.
		const FVector SpawnScale = SpawnTransform.GetScale3D();
		if (SpawnScale.IsNearlyZero() == false)
		{
			Sample->SetActorScale3D(SpawnScale);
		}

		Sample->Grade = SampleGrade;
		Sample->SpawnProtectionDuration = SampleSpawnProtectionDuration;
		++SpawnedCount;

		UE_LOG(LogTemp, Log,
			TEXT("[QuestSampleSpawner] %s: [%s] 반복 퀘스트 %s급 샘플 스폰 @ %s"),
			*GetName(),
			*Definition.QuestName.ToString(),
			*StaticEnum<EMD_SampleGrade>()->GetNameStringByValue(static_cast<int64>(SampleGrade)),
			*Sample->GetActorLocation().ToCompactString());
	}

	return SpawnedCount;
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
