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
	if (const UMD_SiteDefinition* Site = QuestCatalogSite.LoadSynchronous())
	{
		if (Site->MainQuestLine.IsValidIndex(MainQuestIndex))
		{
			const UMD_QuestDefinition* Definition = UMD_QuestDefinition::LoadDefinition(Site->MainQuestLine[MainQuestIndex]);
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

	const UMD_QuestDefinition* Definition = UMD_QuestDefinition::LoadDefinition(QuestState.DefinitionId);
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
	const EMD_QuestObjectiveType ObjectiveType = Definition->GetObjectiveType(QuestState.CurrentPhaseIndex);
	const bool bIsRecoveryObjective = ObjectiveType == EMD_QuestObjectiveType::RecoverIntact || ObjectiveType == EMD_QuestObjectiveType::RecoverDamaged;
	if (bIsRecoveryObjective == false)
	{
		LastSpawnedMainPhaseIndex = QuestState.CurrentPhaseIndex;
		return;
	}

	// 기존 점유 샘플을 목표에 연결한 뒤 제출 완료 수량과 현재 월드 잔여 수량을 제외한 부족분 계산
	const int32 RequiredCount = FMath::Max(1, Definition->GetRequiredCount(QuestState.CurrentPhaseIndex));
	const int32 RemainingCount = FMath::Max(0, RequiredCount - QuestState.CurrentProgress);
	TSubclassOf<AMD_SampleItem> RequiredItemClass = Definition->GetRequiredItemClass(QuestState.CurrentPhaseIndex);
	UClass* RequiredItemClassObject = RequiredItemClass.Get();
	BindOccupyingSamplesToMainQuest(QuestState.DefinitionId, RequiredItemClassObject, RemainingCount);
	const int32 MissingCount = FMath::Max(0, RemainingCount - CountAvailableSamplesForQuest(QuestState.DefinitionId, RequiredItemClassObject));
	if (MissingCount > 0)
	{
		TSubclassOf<AMD_SampleItem> SpawnItemClass = ResolveSpawnItemClass(*Definition, QuestState.CurrentPhaseIndex);
		bool bIsSpawnItemClassValid = IsValid(SpawnItemClass.Get());
		if (bIsSpawnItemClassValid == true)
		{
			SpawnSamples(QuestState, *Definition, SpawnItemClass, MissingCount);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[QuestSampleSpawner] %s: [%s] 요구 아이템 클래스와 기본 SampleClass가 모두 비어 있다"), *GetName(), *Definition->QuestName.ToString());
		}
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

		const UMD_QuestDefinition* Definition = UMD_QuestDefinition::LoadDefinition(QuestState.DefinitionId);
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
		TSubclassOf<AMD_SampleItem> RequiredItemClass = Definition->GetRequiredItemClass(QuestState.CurrentPhaseIndex);
		UClass* RequiredItemClassObject = RequiredItemClass.Get();
		int32 MissingCount = FMath::Max(0, RemainingCount - CountAvailableSamplesForQuest(QuestState.DefinitionId, RequiredItemClassObject));
		if (MissingCount > 0)
		{
			TSubclassOf<AMD_SampleItem> SpawnItemClass = ResolveSpawnItemClass(*Definition, QuestState.CurrentPhaseIndex);
			bool bIsSpawnItemClassValid = IsValid(SpawnItemClass.Get());
			if (bIsSpawnItemClassValid == false)
			{
				UE_LOG(LogTemp, Warning, TEXT("[QuestSampleSpawner] %s: [%s] 요구 아이템 클래스와 기본 SampleClass가 모두 비어 있다"), *GetName(), *Definition->QuestName.ToString());
				continue;
			}

			SpawnRepeatQuestSamples(QuestState, *Definition, SpawnItemClass, MissingCount);
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

int32 AMD_QuestSampleSpawner::CountAvailableSamplesForQuest(const FPrimaryAssetId& QuestDefinitionId, const UClass* RequiredItemClass) const
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
		bool bIsSampleValid = IsValid(Sample);
		if (bIsSampleValid == false)
		{
			continue;
		}

		bool bIsSampleBeingDestroyed = Sample->IsActorBeingDestroyed();
		bool bMatchesRequiredClass = RequiredItemClass == nullptr || Sample->IsA(RequiredItemClass) == true;
		if (bIsSampleBeingDestroyed == false && bMatchesRequiredClass == true && Sample->Grade == SampleGrade && Sample->QuestDefinitionId == QuestDefinitionId)
		{
			++AvailableSampleCount;
		}
	}

	return AvailableSampleCount;
}

bool AMD_QuestSampleSpawner::MatchesRepeatQuest(const UMD_QuestDefinition& Definition, int32 PhaseIndex) const
{
	const EMD_QuestObjectiveType ObjectiveType = Definition.GetObjectiveType(PhaseIndex);
	const bool bIsRecoveryObjective = ObjectiveType == EMD_QuestObjectiveType::RecoverIntact || ObjectiveType == EMD_QuestObjectiveType::RecoverDamaged;
	if (bIsRecoveryObjective == false)
	{
		return false;
	}

	const FGameplayTag ExpectedGradeTag = UMD_MerchantSubsystem::GradeTagFromSampleGrade(SampleGrade);
	// 복수 허용 등급마다 별도 스포너가 목표 수량을 중복 생성하지 않도록 첫 등급을 대표 생성 등급으로 사용하는 판정
	const FGameplayTagContainer AcceptedGrades = Definition.GetAcceptedGradeTags(PhaseIndex);
	const TArray<FGameplayTag>& AcceptedGradeTags = AcceptedGrades.GetGameplayTagArray();
	bool bMatchesRepeatQuest = ExpectedGradeTag.IsValid() == true && (AcceptedGradeTags.Num() > 0 ? AcceptedGradeTags[0].MatchesTagExact(ExpectedGradeTag) == true : SampleGrade == EMD_SampleGrade::C);
	return bMatchesRepeatQuest;
}

TSubclassOf<AMD_SampleItem> AMD_QuestSampleSpawner::ResolveSpawnItemClass(const UMD_QuestDefinition& Definition, int32 PhaseIndex) const
{
	// 에셋이 지정한 요구 클래스를 우선하여 퀘스트별 액터 구성을 스포너 설정과 분리하는 선택
	TSubclassOf<AMD_SampleItem> OutSpawnItemClass = Definition.GetRequiredItemClass(PhaseIndex);
	bool bHasRequiredItemClass = IsValid(OutSpawnItemClass.Get());
	if (bHasRequiredItemClass == false)
	{
		OutSpawnItemClass = SampleClass;
	}
	return OutSpawnItemClass;
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

int32 AMD_QuestSampleSpawner::BindOccupyingSamplesToMainQuest(const FPrimaryAssetId& QuestDefinitionId, const UClass* RequiredItemClass, int32 MaxAssignments) const
{
	const UWorld* World = GetWorld();
	int32 AssignedSampleCount = 0;
	if (IsValid(World) == false || QuestDefinitionId.IsValid() == false || MaxAssignments <= 0)
	{
		return AssignedSampleCount;
	}

	const TArray<FTransform> SpawnTransforms = CollectSpawnTransforms();
	if (SpawnTransforms.Num() == 0)
	{
		return AssignedSampleCount;
	}

	const float RadiusSq = FMath::Square(OccupiedCheckRadius);
	TSet<AMD_SampleItem*> CountedSamples;

	for (const FTransform& SpawnTransform : SpawnTransforms)
	{
		if (AssignedSampleCount >= MaxAssignments)
		{
			break;
		}

		const FVector Location = SpawnTransform.GetLocation();

		for (TActorIterator<AMD_SampleItem> It(World); It; ++It)
		{
			AMD_SampleItem* Sample = *It;
			bool bIsSampleValid = IsValid(Sample);
			if (bIsSampleValid == false)
			{
				continue;
			}

			bool bIsSampleBeingDestroyed = Sample->IsActorBeingDestroyed();
			bool bHasQuestDefinition = Sample->QuestDefinitionId.IsValid();
			bool bWasSampleCounted = CountedSamples.Contains(Sample);
			bool bMatchesRequiredClass = RequiredItemClass == nullptr || Sample->IsA(RequiredItemClass) == true;
			if (bIsSampleBeingDestroyed == true || Sample->Grade != SampleGrade || bHasQuestDefinition == true)
			{
				continue;
			}

			if (bWasSampleCounted == true || bMatchesRequiredClass == false)
			{
				continue;
			}

			if (FVector::DistSquared(Sample->GetActorLocation(), Location) <= RadiusSq)
			{
				// 일반 샘플을 현재 메인 퀘스트 전용 목표로 전환하여 제출 이벤트가 정확한 ID를 전달하도록 하는 연결
				Sample->QuestDefinitionId = QuestDefinitionId;
				CountedSamples.Add(Sample);
				++AssignedSampleCount;
				break;
			}
		}
	}

	return AssignedSampleCount;
}

void AMD_QuestSampleSpawner::SpawnSamples(const FMD_MainQuestState& QuestState, const UMD_QuestDefinition& Definition, TSubclassOf<AMD_SampleItem> SpawnItemClass, int32 SpawnCount)
{
	UWorld* World = GetWorld();
	bool bIsWorldValid = IsValid(World);
	if (bIsWorldValid == false)
	{
		return;
	}

	const FGameplayTag ExpectedGradeTag = UMD_MerchantSubsystem::GradeTagFromSampleGrade(SampleGrade);
	// 유효한 스포너 등급과 퀘스트 단계의 목표 등급 일치 여부
	bool bIsExpectedGradeValid = ExpectedGradeTag.IsValid();
	UClass* SpawnItemClassObject = SpawnItemClass.Get();
	bool bTargetsExpectedGrade = bIsExpectedGradeValid == true && Definition.TargetsSample(QuestState.CurrentPhaseIndex, SpawnItemClassObject, ExpectedGradeTag) == true;
	if (bIsExpectedGradeValid == true && bTargetsExpectedGrade == false)
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
	const int32 RequiredCount = Definition.GetRequiredCount(QuestState.CurrentPhaseIndex);
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
		const FTransform& SpawnTransform = SpawnTransforms[Index % SpawnTransforms.Num()];

		AMD_SampleItem* Sample = World->SpawnActor<AMD_SampleItem>(
			SpawnItemClass,
			SpawnTransform.GetLocation(),
			SpawnTransform.Rotator(),
			SpawnParams);

		bool bIsSampleValid = IsValid(Sample);
		if (bIsSampleValid == false)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[QuestSampleSpawner] %s: 샘플 스폰 실패 (Day%d, index %d) — BP_SampleBase 부모=MD_SampleItem 확인"),
				*GetName(), UnlockDay, Index);
			continue;
		}

		// 스폰 포인트에 스케일이 잡혀 있을 때만 덮어쓴다. 비어 있으면 샘플 BP의
		// 기본 스케일을 그대로 살린다 (여기서 기본값을 복제하면 양쪽이 갈라진다).
		const FVector SpawnScale = SpawnTransform.GetScale3D();
		bool bHasSpawnScale = SpawnScale.IsNearlyZero() == false;
		if (bHasSpawnScale == true)
		{
			Sample->SetActorScale3D(SpawnScale);
		}
		Sample->Grade = SampleGrade;
		Sample->QuestDefinitionId = QuestState.DefinitionId;
		Sample->SpawnProtectionDuration = SampleSpawnProtectionDuration;

		// 생성 직후 샘플 파괴 방지를 위하여 2초동안 무적 처리
		Sample->SuppressImpactDamage(2);

		UE_LOG(LogTemp, Log,
			TEXT("[QuestSampleSpawner] %s: [%s] Day%d %s급 샘플 스폰 @ %s"),
			*GetName(),
			*Definition.QuestName.ToString(),
			QuestState.UnlockDay,
			*StaticEnum<EMD_SampleGrade>()->GetNameStringByValue(static_cast<int64>(SampleGrade)),
			*Sample->GetActorLocation().ToCompactString());
	}
}

void AMD_QuestSampleSpawner::SpawnRepeatQuestSamples(const FMD_RepeatQuestState& QuestState, const UMD_QuestDefinition& Definition, TSubclassOf<AMD_SampleItem> SpawnItemClass, int32 SpawnCount)
{
	if (SpawnCount <= 0)
	{
		return;
	}

	const TArray<FTransform> SpawnTransforms = CollectSpawnTransforms();
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
		const FTransform& SpawnTransform = SpawnTransforms[Index % SpawnTransforms.Num()];
		AMD_SampleItem* Sample = World->SpawnActor<AMD_SampleItem>(SpawnItemClass, SpawnTransform.GetLocation(), SpawnTransform.Rotator(), SpawnParams);
		bool bIsSampleValid = IsValid(Sample);
		if (bIsSampleValid == false)
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
		bool bHasSpawnScale = SpawnScale.IsNearlyZero() == false;
		if (bHasSpawnScale == true)
		{
			Sample->SetActorScale3D(SpawnScale);
		}

		Sample->Grade = SampleGrade;
		// 목표 샘플이 별도 물품 ID 없이 원본 반복 퀘스트를 직접 식별하도록 하는 연결
		Sample->QuestDefinitionId = QuestState.DefinitionId;
		Sample->SpawnProtectionDuration = SampleSpawnProtectionDuration;

		Sample->SuppressImpactDamage(2);

		UE_LOG(LogTemp, Log,
			TEXT("[QuestSampleSpawner] %s: [%s] 반복 퀘스트 %s급 샘플 스폰 @ %s"),
			*GetName(),
			*Definition.QuestName.ToString(),
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
