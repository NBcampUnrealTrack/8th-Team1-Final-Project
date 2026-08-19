#include "World/Sample/MD_QuestSampleSpawner.h"

#include "Components/ArrowComponent.h"
#include "Components/SceneComponent.h"
#include "Component/Merchant/MD_MerchantComponent.h"
#include "Data/Merchant/MD_QuestDefinition.h"
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
	if (UMD_SiteDefinition* Site = QuestCatalogSite.LoadSynchronous())
	{
		if (Site->MainQuestLine.IsValidIndex(MainQuestIndex))
		{
			UMD_QuestDefinition* Definition = UMD_QuestDefinition::LoadDefinition(Site->MainQuestLine[MainQuestIndex]);
			if (IsValid(Definition) == true)
			{
				LinkedQuestName = Definition->QuestName;
				return;
			}
		}
	}

	LinkedQuestName = FText::GetEmpty();
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

	UWorld* World = GetWorld();
	UGameInstance* GameInstance = IsValid(World) ? World->GetGameInstance() : nullptr;
	UMD_MerchantSubsystem* MerchantSubsystem =
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
		LastSpawnedMainPhaseIndex = INDEX_NONE;
		return;
	}

	if (QuestState.Status != EMD_MainQuestStatus::Active)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[QuestSampleSpawner] %s: Day%d 퀘스트 상태=%d — Active일 때만 스폰"),
			*GetName(), UnlockDay, static_cast<int32>(QuestState.Status));
		return;
	}

	if (LastSpawnedMainPhaseIndex == QuestState.CurrentPhaseIndex)
	{
		return;
	}

	UMD_QuestDefinition* Definition = UMD_QuestDefinition::LoadDefinition(QuestState.DefinitionId);
	// 원본 포인터부터 현재 단계까지 순서대로 검증하기 위한 결과
	bool bIsDefinitionValid = IsValid(Definition);
	if (bIsDefinitionValid == false)
	{
		UE_LOG(LogTemp, Warning, TEXT("[QuestSampleSpawner] %s: Day%d 퀘스트 Definition 없음"), *GetName(), UnlockDay);
		return;
	}

	bool bIsDefinitionDataValid = Definition->IsValidDefinition();
	if (bIsDefinitionDataValid == false)
	{
		UE_LOG(LogTemp, Warning, TEXT("[QuestSampleSpawner] %s: Day%d 퀘스트 Definition 없음"), *GetName(), UnlockDay);
		return;
	}

	bool bHasCurrentPhase = Definition->HasPhase(QuestState.CurrentPhaseIndex);
	if (bHasCurrentPhase == false)
	{
		UE_LOG(LogTemp, Warning, TEXT("[QuestSampleSpawner] %s: Day%d 퀘스트 Definition 없음"), *GetName(), UnlockDay);
		return;
	}

	// 공통 Definition의 청소·마감 페이즈는 각 전용 진행 시스템이 처리하므로 샘플을 만들지 않는 분기
	EMD_QuestObjectiveType ObjectiveType = Definition->GetObjectiveType(QuestState.CurrentPhaseIndex);
	bool bIsRecoveryObjective = ObjectiveType == EMD_QuestObjectiveType::RecoverIntact || ObjectiveType == EMD_QuestObjectiveType::RecoverDamaged;
	if (bIsRecoveryObjective == false)
	{
		LastSpawnedMainPhaseIndex = QuestState.CurrentPhaseIndex;
		return;
	}

	// 기존 점유 아이템을 목표에 연결한 뒤 제출 완료 수량과 현재 월드 잔여 수량을 제외한 부족분 계산
	int32 RequiredCount = FMath::Max(1, Definition->GetRequiredCount(QuestState.CurrentPhaseIndex));
	int32 RemainingCount = FMath::Max(0, RequiredCount - QuestState.CurrentProgress);
	TSubclassOf<AMD_QuestItem> RequiredItemClass = Definition->GetRequiredItemClass(QuestState.CurrentPhaseIndex);
	TSubclassOf<AMD_QuestItem> SpawnItemClass = ResolveSpawnItemClass(*Definition, QuestState.CurrentPhaseIndex);
	bool bIsSpawnItemClassValid = IsValid(SpawnItemClass.Get());
	if (bIsSpawnItemClassValid == false)
	{
		UE_LOG(LogTemp, Warning, TEXT("[QuestSampleSpawner] %s: [%s] 요구 아이템 클래스와 기본 SampleClass가 모두 비어 있다"), *GetName(), *Definition->QuestName.ToString());
		return;
	}

	UClass* RequiredItemClassObject = IsValid(RequiredItemClass.Get()) == true ? RequiredItemClass.Get() : SpawnItemClass.Get();
	bool bIsSampleItemClass = SpawnItemClass.Get()->IsChildOf(AMD_SampleItem::StaticClass());
	bool bRequiresSampleGrade = bIsSampleItemClass == true && Definition->GetAcceptedGradeTags(QuestState.CurrentPhaseIndex).Num() > 0;
	EMD_SampleGrade ResolvedSpawnGrade = SampleGrade;
	bool bHasResolvedSpawnGrade = bRequiresSampleGrade == false || ResolveSpawnGrade(*Definition, QuestState.CurrentPhaseIndex, ResolvedSpawnGrade) == true;
	if (bHasResolvedSpawnGrade == false)
	{
		UE_LOG(LogTemp, Warning, TEXT("[QuestSampleSpawner] %s: [%s] 허용 등급을 샘플 등급으로 변환할 수 없다"), *GetName(), *Definition->QuestName.ToString());
		return;
	}

	// 역할 교환으로 스포너 설정과 퀘스트 등급이 달라져도 원본 퀘스트 조건을 따르는 보정
	if (bRequiresSampleGrade == true && ResolvedSpawnGrade != SampleGrade)
	{
		UE_LOG(LogTemp, Warning, TEXT("[QuestSampleSpawner] %s: [%s] 스포너 등급을 %s에서 %s로 보정"), *GetName(), *Definition->QuestName.ToString(), *StaticEnum<EMD_SampleGrade>()->GetNameStringByValue(static_cast<int64>(SampleGrade)), *StaticEnum<EMD_SampleGrade>()->GetNameStringByValue(static_cast<int64>(ResolvedSpawnGrade)));
	}

	BindOccupyingQuestItemsToMainQuest(QuestState.DefinitionId, RequiredItemClassObject, bRequiresSampleGrade, ResolvedSpawnGrade, RemainingCount);
	int32 MissingCount = FMath::Max(0, RemainingCount - CountAvailableQuestItems(QuestState.DefinitionId, RequiredItemClassObject, bRequiresSampleGrade, ResolvedSpawnGrade));
	if (MissingCount > 0)
	{
		SpawnMainQuestItems(QuestState, *Definition, SpawnItemClass, bRequiresSampleGrade, ResolvedSpawnGrade, MissingCount);
	}

	LastSpawnedMainPhaseIndex = QuestState.CurrentPhaseIndex;
}

void AMD_QuestSampleSpawner::TrySpawnForRepeatQuestStates(const UMD_MerchantSubsystem* MerchantSubsystem)
{
	if (IsValid(MerchantSubsystem) == false || IsPrimaryRepeatQuestSpawner() == false)
	{
		return;
	}

	// 수락한 반복 퀘스트의 현재 페이즈에 연결된 샘플 부족분만 생성한다.
	for (const FMD_RepeatQuestState& QuestState : MerchantSubsystem->GetRepeatQuests())
	{
		if (QuestState.Status != EMD_RepeatQuestStatus::Accepted)
		{
			continue;
		}

		UMD_QuestDefinition* Definition = UMD_QuestDefinition::LoadDefinition(QuestState.DefinitionId);
		bool bIsDefinitionValid = IsValid(Definition);
		if (bIsDefinitionValid == false)
		{
			continue;
		}

		bool bHasCurrentPhase = Definition->HasPhase(QuestState.CurrentPhaseIndex);
		if (bHasCurrentPhase == false)
		{
			continue;
		}

		bool bIsObjectiveComplete = Definition->IsObjectiveComplete(QuestState.CurrentPhaseIndex, QuestState.CurrentProgress);
		if (bIsObjectiveComplete == true)
		{
			continue;
		}

		bool bMatchesRepeatQuest = MatchesRepeatQuest(*Definition, QuestState.CurrentPhaseIndex);
		if (bMatchesRepeatQuest == false)
		{
			continue;
		}

		int32 RemainingCount = FMath::Max(0, Definition->GetRequiredCount(QuestState.CurrentPhaseIndex) - QuestState.CurrentProgress);
		TSubclassOf<AMD_QuestItem> RequiredItemClass = Definition->GetRequiredItemClass(QuestState.CurrentPhaseIndex);
		TSubclassOf<AMD_QuestItem> SpawnItemClass = ResolveSpawnItemClass(*Definition, QuestState.CurrentPhaseIndex);
		bool bIsSpawnItemClassValid = IsValid(SpawnItemClass.Get());
		if (bIsSpawnItemClassValid == false)
		{
			UE_LOG(LogTemp, Warning, TEXT("[QuestSampleSpawner] %s: [%s] 요구 아이템 클래스와 기본 SampleClass가 모두 비어 있다"), *GetName(), *Definition->QuestName.ToString());
			continue;
		}

		UClass* RequiredItemClassObject = IsValid(RequiredItemClass.Get()) == true ? RequiredItemClass.Get() : SpawnItemClass.Get();
		bool bIsSampleItemClass = SpawnItemClass.Get()->IsChildOf(AMD_SampleItem::StaticClass());
		bool bRequiresSampleGrade = bIsSampleItemClass == true && Definition->GetAcceptedGradeTags(QuestState.CurrentPhaseIndex).Num() > 0;
		EMD_SampleGrade ResolvedSpawnGrade = SampleGrade;
		bool bHasResolvedSpawnGrade = bRequiresSampleGrade == false || ResolveSpawnGrade(*Definition, QuestState.CurrentPhaseIndex, ResolvedSpawnGrade) == true;
		if (bHasResolvedSpawnGrade == false)
		{
			UE_LOG(LogTemp, Warning, TEXT("[QuestSampleSpawner] %s: [%s] 허용 등급을 샘플 등급으로 변환할 수 없다"), *GetName(), *Definition->QuestName.ToString());
			continue;
		}

		int32 MissingCount = FMath::Max(0, RemainingCount - CountAvailableQuestItems(QuestState.DefinitionId, RequiredItemClassObject, bRequiresSampleGrade, ResolvedSpawnGrade));
		if (MissingCount > 0)
		{
			SpawnRepeatQuestItems(QuestState, *Definition, SpawnItemClass, bRequiresSampleGrade, ResolvedSpawnGrade, MissingCount);
		}
	}
}

bool AMD_QuestSampleSpawner::IsPrimaryRepeatQuestSpawner() const
{
	UWorld* World = GetWorld();
	bool bIsPrimarySpawner = IsValid(World);
	if (bIsPrimarySpawner == false)
	{
		return bIsPrimarySpawner;
	}

	FString SpawnerPath = GetPathName();
	for (TActorIterator<AMD_QuestSampleSpawner> It(World); It; ++It)
	{
		AMD_QuestSampleSpawner* Candidate = *It;
		bool bIsCandidateValid = IsValid(Candidate);
		if (bIsCandidateValid == false || Candidate == this || Candidate->SampleGrade != SampleGrade)
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

int32 AMD_QuestSampleSpawner::CountAvailableQuestItems(const FPrimaryAssetId& QuestDefinitionId, const UClass* RequiredItemClass, bool bRequiresSampleGrade, EMD_SampleGrade TargetGrade) const
{
	UWorld* World = GetWorld();
	int32 AvailableQuestItemCount = 0;
	if (IsValid(World) == false)
	{
		return AvailableQuestItemCount;
	}

	for (TActorIterator<AMD_QuestItem> It(World); It; ++It)
	{
		AMD_QuestItem* QuestItem = *It;
		if (IsValid(QuestItem) == false)
		{
			continue;
		}

		if (QuestItem->IsActorBeingDestroyed() == true || QuestItem->QuestDefinitionId != QuestDefinitionId)
		{
			continue;
		}

		bool bMatchesRequiredClass = RequiredItemClass == nullptr || QuestItem->IsA(RequiredItemClass) == true;
		if (bMatchesRequiredClass == false)
		{
			continue;
		}

		if (bRequiresSampleGrade == true)
		{
			AMD_SampleItem* Sample = Cast<AMD_SampleItem>(QuestItem);
			if (IsValid(Sample) == false || Sample->Grade != TargetGrade)
			{
				continue;
			}
		}

		++AvailableQuestItemCount;
	}

	return AvailableQuestItemCount;
}

bool AMD_QuestSampleSpawner::MatchesRepeatQuest(const UMD_QuestDefinition& Definition, int32 PhaseIndex) const
{
	EMD_QuestObjectiveType ObjectiveType = Definition.GetObjectiveType(PhaseIndex);
	bool bIsRecoveryObjective = ObjectiveType == EMD_QuestObjectiveType::RecoverIntact || ObjectiveType == EMD_QuestObjectiveType::RecoverDamaged;
	if (bIsRecoveryObjective == false)
	{
		return false;
	}

	FGameplayTag ExpectedGradeTag = UMD_MerchantSubsystem::GradeTagFromSampleGrade(SampleGrade);
	// 복수 허용 등급마다 별도 스포너가 목표 수량을 중복 생성하지 않도록 첫 등급을 대표 생성 등급으로 사용하는 판정
	FGameplayTagContainer AcceptedGrades = Definition.GetAcceptedGradeTags(PhaseIndex);
	const TArray<FGameplayTag>& AcceptedGradeTags = AcceptedGrades.GetGameplayTagArray();
	bool bMatchesRepeatQuest = ExpectedGradeTag.IsValid() == true && (AcceptedGradeTags.Num() > 0 ? AcceptedGradeTags[0].MatchesTagExact(ExpectedGradeTag) == true : SampleGrade == EMD_SampleGrade::C);
	return bMatchesRepeatQuest;
}

TSubclassOf<AMD_QuestItem> AMD_QuestSampleSpawner::ResolveSpawnItemClass(const UMD_QuestDefinition& Definition, int32 PhaseIndex) const
{
	// 에셋이 지정한 요구 클래스를 우선하여 퀘스트별 액터 구성을 스포너 설정과 분리하는 선택
	TSubclassOf<AMD_QuestItem> OutSpawnItemClass = Definition.GetRequiredItemClass(PhaseIndex);
	bool bHasRequiredItemClass = IsValid(OutSpawnItemClass.Get());
	if (bHasRequiredItemClass == false)
	{
		OutSpawnItemClass = SampleClass;
	}
	return OutSpawnItemClass;
}

bool AMD_QuestSampleSpawner::ResolveSpawnGrade(const UMD_QuestDefinition& Definition, int32 PhaseIndex, EMD_SampleGrade& OutSampleGrade) const
{
	// 스포너 우선 등급이 퀘스트에서 허용되면 기존 배치 의도를 보존하는 선택
	OutSampleGrade = SampleGrade;
	FGameplayTag PreferredGradeTag = UMD_MerchantSubsystem::GradeTagFromSampleGrade(SampleGrade);
	bool bOutResolved = Definition.TargetsQuestItem(PhaseIndex, PreferredGradeTag);

	// 역할 교환된 퀘스트의 첫 허용 등급을 실제 생성 등급으로 사용하는 대체 선택
	if (bOutResolved == false)
	{
		FGameplayTagContainer AcceptedGrades = Definition.GetAcceptedGradeTags(PhaseIndex);
		const TArray<FGameplayTag>& AcceptedGradeTags = AcceptedGrades.GetGameplayTagArray();
		EMD_SampleGrade SupportedGrades[] = { EMD_SampleGrade::C, EMD_SampleGrade::B, EMD_SampleGrade::A, EMD_SampleGrade::S };
		for (const FGameplayTag& AcceptedGradeTag : AcceptedGradeTags)
		{
			for (EMD_SampleGrade SupportedGrade : SupportedGrades)
			{
				FGameplayTag SupportedGradeTag = UMD_MerchantSubsystem::GradeTagFromSampleGrade(SupportedGrade);
				bool bMatchesAcceptedGrade = SupportedGradeTag.MatchesTagExact(AcceptedGradeTag);
				if (bMatchesAcceptedGrade == true)
				{
					OutSampleGrade = SupportedGrade;
					bOutResolved = true;
					break;
				}
			}

			if (bOutResolved == true)
			{
				break;
			}
		}
	}

	return bOutResolved;
}

bool AMD_QuestSampleSpawner::FindLinkedMainQuest(
	const UMD_MerchantSubsystem* MerchantSubsystem,
	FMD_MainQuestState& OutQuestState) const
{
	for (const FMD_MainQuestState& QuestState : MerchantSubsystem->GetMainQuests())
	{
		if (QuestState.UnlockDay == UnlockDay)
		{
			OutQuestState = QuestState;
			return true;
		}
	}

	return false;
}

int32 AMD_QuestSampleSpawner::BindOccupyingQuestItemsToMainQuest(const FPrimaryAssetId& QuestDefinitionId, const UClass* RequiredItemClass, bool bRequiresSampleGrade, EMD_SampleGrade TargetGrade, int32 MaxAssignments) const
{
	UWorld* World = GetWorld();
	int32 AssignedQuestItemCount = 0;
	if (IsValid(World) == false || QuestDefinitionId.IsValid() == false || MaxAssignments <= 0)
	{
		return AssignedQuestItemCount;
	}

	TArray<FTransform> SpawnTransforms = CollectSpawnTransforms();
	if (SpawnTransforms.Num() == 0)
	{
		return AssignedQuestItemCount;
	}

	float RadiusSq = FMath::Square(OccupiedCheckRadius);
	TSet<AMD_QuestItem*> CountedQuestItems;

	for (const FTransform& SpawnTransform : SpawnTransforms)
	{
		if (AssignedQuestItemCount >= MaxAssignments)
		{
			break;
		}

		FVector Location = SpawnTransform.GetLocation();

		for (TActorIterator<AMD_QuestItem> It(World); It; ++It)
		{
			AMD_QuestItem* QuestItem = *It;
			if (IsValid(QuestItem) == false)
			{
				continue;
			}

			if (QuestItem->IsActorBeingDestroyed() == true || QuestItem->QuestDefinitionId.IsValid() == true)
			{
				continue;
			}

			bool bWasQuestItemCounted = CountedQuestItems.Contains(QuestItem);
			bool bMatchesRequiredClass = RequiredItemClass == nullptr || QuestItem->IsA(RequiredItemClass) == true;
			if (bWasQuestItemCounted == true || bMatchesRequiredClass == false)
			{
				continue;
			}

			if (bRequiresSampleGrade == true)
			{
				AMD_SampleItem* Sample = Cast<AMD_SampleItem>(QuestItem);
				if (IsValid(Sample) == false || Sample->Grade != TargetGrade)
				{
					continue;
				}
			}

			if (FVector::DistSquared(QuestItem->GetActorLocation(), Location) <= RadiusSq)
			{
				// 배치 아이템을 현재 메인 퀘스트 전용 목표로 전환하여 제출 이벤트가 정확한 ID를 전달하도록 하는 연결
				QuestItem->QuestDefinitionId = QuestDefinitionId;
				CountedQuestItems.Add(QuestItem);
				++AssignedQuestItemCount;
				break;
			}
		}
	}

	return AssignedQuestItemCount;
}

void AMD_QuestSampleSpawner::SpawnMainQuestItems(const FMD_MainQuestState& QuestState, const UMD_QuestDefinition& Definition, TSubclassOf<AMD_QuestItem> SpawnItemClass, bool bRequiresSampleGrade, EMD_SampleGrade SpawnGrade, int32 SpawnCount)
{
	UWorld* World = GetWorld();
	bool bIsWorldValid = IsValid(World);
	if (bIsWorldValid == false)
	{
		return;
	}

	TArray<FTransform> SpawnTransforms = CollectSpawnTransforms();
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
	int32 RequiredCount = Definition.GetRequiredCount(QuestState.CurrentPhaseIndex);
	if (RequiredCount > 0 && SamplesToSpawn != RequiredCount)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[QuestSampleSpawner] %s: [%s] SamplesToSpawn(%d)이 퀘스트 RequiredCount(%d)와 다르다 — 기획 09 §1.5는 1:1을 요구한다"),
			*GetName(),
			*Definition.QuestName.ToString(),
			SamplesToSpawn,
			RequiredCount);
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (int32 Index = 0; Index < SpawnCount; ++Index)
	{
		FTransform& SpawnTransform = SpawnTransforms[Index % SpawnTransforms.Num()];

		AMD_QuestItem* QuestItem = World->SpawnActor<AMD_QuestItem>(SpawnItemClass, SpawnTransform.GetLocation(), SpawnTransform.Rotator(), SpawnParams);

		bool bIsQuestItemValid = IsValid(QuestItem);
		if (bIsQuestItemValid == false)
		{
			UE_LOG(LogTemp, Warning, TEXT("[QuestSampleSpawner] %s: 퀘스트 아이템 스폰 실패 (Day%d, index %d) — 요구 클래스 부모 확인"), *GetName(), UnlockDay, Index);
			continue;
		}

		// 스폰 포인트의 명시적인 크기만 반영하여 아이템 Blueprint 기본 크기를 보존하는 처리
		FVector SpawnScale = SpawnTransform.GetScale3D();
		bool bHasSpawnScale = SpawnScale.IsNearlyZero() == false;
		if (bHasSpawnScale == true)
		{
			QuestItem->SetActorScale3D(SpawnScale);
		}

		QuestItem->QuestDefinitionId = QuestState.DefinitionId;
		AMD_SampleItem* Sample = Cast<AMD_SampleItem>(QuestItem);
		bool bIsDestructibleSample = IsValid(Sample);
		if (bIsDestructibleSample == true)
		{
			// 파괴 가능한 샘플에 생성 직후 충격 보호를 추가하는 설정
			Sample->SpawnProtectionDuration = SampleSpawnProtectionDuration;
			Sample->SuppressImpactDamage(2.0f);
			if (bRequiresSampleGrade == true)
			{
				// 등급 조건이 있는 반복 샘플에만 실제 생성 등급을 적용하는 설정
				Sample->Grade = SpawnGrade;
			}
		}

		FString ItemTypeLabel = bRequiresSampleGrade == true ? FString::Printf(TEXT("%s급 샘플"), *StaticEnum<EMD_SampleGrade>()->GetNameStringByValue(static_cast<int64>(SpawnGrade))) : (bIsDestructibleSample == true ? TEXT("파괴 가능한 퀘스트 샘플") : TEXT("파괴 불가 퀘스트 아이템"));
		UE_LOG(LogTemp, Log, TEXT("[QuestSampleSpawner] %s: [%s] Day%d %s 스폰 @ %s"), *GetName(), *Definition.QuestName.ToString(), QuestState.UnlockDay, *ItemTypeLabel, *QuestItem->GetActorLocation().ToCompactString());
	}
}

void AMD_QuestSampleSpawner::SpawnRepeatQuestItems(const FMD_RepeatQuestState& QuestState, const UMD_QuestDefinition& Definition, TSubclassOf<AMD_QuestItem> SpawnItemClass, bool bRequiresSampleGrade, EMD_SampleGrade SpawnGrade, int32 SpawnCount)
{
	if (SpawnCount <= 0)
	{
		return;
	}

	TArray<FTransform> SpawnTransforms = CollectSpawnTransforms();
	if (SpawnTransforms.Num() == 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[QuestSampleSpawner] %s: [%s] 반복 퀘스트 스폰 포인트 없음"),
			*GetName(),
			*Definition.QuestName.ToString());
		return;
	}

	UWorld* World = GetWorld();
	bool bIsWorldValid = IsValid(World);
	if (bIsWorldValid == false)
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (int32 Index = 0; Index < SpawnCount; ++Index)
	{
		FTransform& SpawnTransform = SpawnTransforms[Index % SpawnTransforms.Num()];
		AMD_QuestItem* QuestItem = World->SpawnActor<AMD_QuestItem>(SpawnItemClass, SpawnTransform.GetLocation(), SpawnTransform.Rotator(), SpawnParams);
		bool bIsQuestItemValid = IsValid(QuestItem);
		if (bIsQuestItemValid == false)
		{
			UE_LOG(LogTemp, Warning, TEXT("[QuestSampleSpawner] %s: [%s] 반복 퀘스트 아이템 스폰 실패 (index %d) — 요구 클래스 부모 확인"), *GetName(), *Definition.QuestName.ToString(), Index);
			continue;
		}

		// 스폰 포인트의 명시적인 크기만 반영하고 샘플 BP 기본 크기를 보존하기 위한 처리.
		FVector SpawnScale = SpawnTransform.GetScale3D();
		bool bHasSpawnScale = SpawnScale.IsNearlyZero() == false;
		if (bHasSpawnScale == true)
		{
			QuestItem->SetActorScale3D(SpawnScale);
		}

		// 목표 아이템이 별도 물품 ID 없이 원본 반복 퀘스트를 직접 식별하도록 하는 연결
		QuestItem->QuestDefinitionId = QuestState.DefinitionId;
		AMD_SampleItem* Sample = Cast<AMD_SampleItem>(QuestItem);
		bool bIsDestructibleSample = IsValid(Sample);
		if (bIsDestructibleSample == true)
		{
			// 파괴 가능한 샘플에 생성 직후 충격 보호를 추가하는 설정
			Sample->SpawnProtectionDuration = SampleSpawnProtectionDuration;
			Sample->SuppressImpactDamage(2.0f);
			if (bRequiresSampleGrade == true)
			{
				// 등급 조건이 있는 반복 샘플에만 실제 생성 등급을 적용하는 설정
				Sample->Grade = SpawnGrade;
			}
		}

		FString ItemTypeLabel = bRequiresSampleGrade == true ? FString::Printf(TEXT("%s급 샘플"), *StaticEnum<EMD_SampleGrade>()->GetNameStringByValue(static_cast<int64>(SpawnGrade))) : (bIsDestructibleSample == true ? TEXT("파괴 가능한 퀘스트 샘플") : TEXT("파괴 불가 퀘스트 아이템"));
		UE_LOG(LogTemp, Log, TEXT("[QuestSampleSpawner] %s: [%s] 반복 퀘스트 %s 스폰 @ %s"), *GetName(), *Definition.QuestName.ToString(), *ItemTypeLabel, *QuestItem->GetActorLocation().ToCompactString());
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
