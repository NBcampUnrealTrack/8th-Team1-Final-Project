#include "World/Sample/MD_QuestSampleSpawner.h"

#include "Components/ArrowComponent.h"
#include "Components/SceneComponent.h"
#include "Component/Merchant/MD_MerchantComponent.h"
#include "Data/Merchant/MD_QuestDefinition.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/MD_GameStateBase.h"
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "Util/MD_LogUtility.h"
#include "World/Sample/MD_SampleItem.h"
#include "World/Sample/MD_QuestSampleSpawnerManager.h"

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

	// 상태 변경 구독이 가능한 MerchantSubsystem을 초기화 완료의 필수 조건으로 사용한다.
	UMD_MerchantSubsystem* MerchantSubsystem = GameInstance->GetSubsystem<UMD_MerchantSubsystem>();
	bool bIsMerchantSubsystemValid = IsValid(MerchantSubsystem);
	if (bIsMerchantSubsystemValid == false)
	{
		return;
	}

	// 중복 등록을 피하면서 GameInstance 수명의 상태 변경 알림을 연결한다.
	bool bHasSubsystemChangedHandle = SubsystemChangedHandle.IsValid();
	if (bHasSubsystemChangedHandle == false)
	{
		SubsystemChangedHandle = MerchantSubsystem->OnMerchantStateChanged.AddUObject(
			this, &AMD_QuestSampleSpawner::HandleSubsystemStateChanged);
	}

	// 델리게이트 핸들이 생성되지 않았다면 BeginPlay에서 초기화를 다시 시도한다.
	bool bIsSubsystemSubscriptionValid = SubsystemChangedHandle.IsValid();
	if (bIsSubsystemSubscriptionValid == false)
	{
		return;
	}

	// 생성 준비가 끝난 객체를 월드 매니저의 무작위 선택 후보로 등록한다.
	UMD_QuestSampleSpawnerManager* SpawnerManager = World->GetSubsystem<UMD_QuestSampleSpawnerManager>();
	bool bIsSpawnerManagerValid = IsValid(SpawnerManager);
	if (bIsSpawnerManagerValid == false)
	{
		return;
	}

	// 매니저가 월드를 재탐색하지 않도록 현재 스포너를 직접 전달한다.
	SpawnerManager->RegisterSpawner(this);

	// 필수 구독이 준비된 뒤에만 BeginPlay의 재시도를 막는다.
	bSpawnerInitialized = true;

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
			// 종료되는 객체가 이후 무작위 선택 후보로 남지 않도록 매니저에서 해제한다.
			UMD_QuestSampleSpawnerManager* SpawnerManager = World->GetSubsystem<UMD_QuestSampleSpawnerManager>();
			bool bIsSpawnerManagerValid = IsValid(SpawnerManager);
			if (bIsSpawnerManagerValid == true)
			{
				SpawnerManager->UnregisterSpawner(this);
			}

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
	// 등록 완료 전 생성 요청은 매니저의 비동기 대기열에서 처리하도록 전달한다.
	UMD_QuestSampleSpawnerManager* SpawnerManager = IsValid(World) == true ? World->GetSubsystem<UMD_QuestSampleSpawnerManager>() : nullptr;
	bool bIsSpawnerManagerValid = IsValid(SpawnerManager);
	if (bIsSpawnerManagerValid == false)
	{
		return;
	}

	// 등록이 끝난 요청만 현재 호출에서 즉시 생성 검사를 계속한다.
	bool bCanEvaluateSpawnImmediately = SpawnerManager->RequestSpawnEvaluation(this);
	if (bCanEvaluateSpawnImmediately == false)
	{
		return;
	}

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

	// 다른 퀘스트가 같은 Phase 인덱스를 사용해도 생성 요청이 차단되지 않도록 Definition을 먼저 비교한다.
	bool bMatchesLastMainQuest = LastSpawnedMainQuestDefinitionId == QuestState.DefinitionId;
	if (bMatchesLastMainQuest == true)
	{
		// 동일한 퀘스트의 동일 Phase에서 반복된 상태 알림만 이미 처리한 요청으로 판단한다.
		bool bMatchesLastMainPhase = LastSpawnedMainPhaseIndex == QuestState.CurrentPhaseIndex;
		if (bMatchesLastMainPhase == true)
		{
			return;
		}
	}

	UMD_QuestDefinition* Definition = UMD_QuestDefinition::LoadDefinition(QuestState.DefinitionId);
	// 원본 포인터부터 현재 단계까지 순서대로 검증하기 위한 결과
	bool bIsDefinitionValid = IsValid(Definition);
	if (bIsDefinitionValid == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s [QuestSampleSpawner] %s: Day%d 퀘스트 Definition 없음"), *UE_CALL_LOCATIONINFO, *GetName(), QuestState.UnlockDay);
		return;
	}

	bool bIsDefinitionDataValid = Definition->IsValidDefinition();
	if (bIsDefinitionDataValid == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s [QuestSampleSpawner] %s: Day%d 퀘스트 Definition 없음"), *UE_CALL_LOCATIONINFO, *GetName(), QuestState.UnlockDay);
		return;
	}

	bool bHasCurrentPhase = Definition->HasPhase(QuestState.CurrentPhaseIndex);
	if (bHasCurrentPhase == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s [QuestSampleSpawner] %s: Day%d 퀘스트 Definition 없음"), *UE_CALL_LOCATIONINFO, *GetName(), QuestState.UnlockDay);
		return;
	}

	// 지역이 지정된 퀘스트는 같은 지역으로 표시된 스포너에서만 아이템을 생성한다.
	bool bMatchesSpawnZone = MatchesSpawnZone(*Definition, QuestState.CurrentPhaseIndex);
	if (bMatchesSpawnZone == false)
	{
		return;
	}

	// 같은 Zone의 후보 중 월드 매니저가 무작위로 선택한 스포너만 생성을 진행한다.
	UWorld* World = GetWorld();
	UMD_QuestSampleSpawnerManager* SpawnerManager = IsValid(World) == true ? World->GetSubsystem<UMD_QuestSampleSpawnerManager>() : nullptr;
	bool bIsSpawnerManagerValid = IsValid(SpawnerManager);
	if (bIsSpawnerManagerValid == false)
	{
		return;
	}

	// 퀘스트와 Phase별로 저장된 대표 스포너인지 확인한다.
	bool bIsSelectedSpawner = SpawnerManager->IsSelectedSpawner(this, *Definition, QuestState.CurrentPhaseIndex);
	if (bIsSelectedSpawner == false)
	{
		return;
	}

	// 공통 Definition의 청소·마감 페이즈는 각 전용 진행 시스템이 처리하므로 샘플을 만들지 않는 분기
	EMD_QuestObjectiveType ObjectiveType = Definition->GetObjectiveType(QuestState.CurrentPhaseIndex);
	bool bIsRecoveryObjective = ObjectiveType == EMD_QuestObjectiveType::RecoverIntact || ObjectiveType == EMD_QuestObjectiveType::RecoverDamaged;
	if (bIsRecoveryObjective == false)
	{
		// 생성 대상이 아닌 Phase도 같은 상태 알림에서 반복 검사하지 않도록 Definition을 기록한다.
		LastSpawnedMainQuestDefinitionId = QuestState.DefinitionId;
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
		UE_LOG(MarineDemolition, Warning, TEXT("%s [QuestSampleSpawner] %s: [%s] 요구 아이템 클래스와 기본 SampleClass가 모두 비어 있다"), *UE_CALL_LOCATIONINFO, *GetName(), *Definition->QuestName.ToString());
		return;
	}

	UClass* RequiredItemClassObject = IsValid(RequiredItemClass.Get()) == true ? RequiredItemClass.Get() : SpawnItemClass.Get();
	bool bIsSampleItemClass = SpawnItemClass.Get()->IsChildOf(AMD_SampleItem::StaticClass());
	bool bRequiresSampleGrade = bIsSampleItemClass == true && Definition->GetAcceptedGradeTags(QuestState.CurrentPhaseIndex).Num() > 0;
	// 스포너 설정과 무관하게 퀘스트가 요구하는 등급을 생성하기 위한 기본 결과를 준비한다.
	EMD_SampleGrade ResolvedSpawnGrade = EMD_SampleGrade::C;
	bool bHasResolvedSpawnGrade = bRequiresSampleGrade == false || ResolveSpawnGrade(*Definition, QuestState.CurrentPhaseIndex, ResolvedSpawnGrade) == true;
	if (bHasResolvedSpawnGrade == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s [QuestSampleSpawner] %s: [%s] 허용 등급을 샘플 등급으로 변환할 수 없다"), *UE_CALL_LOCATIONINFO, *GetName(), *Definition->QuestName.ToString());
		return;
	}

	BindOccupyingQuestItemsToMainQuest(QuestState.DefinitionId, RequiredItemClassObject, bRequiresSampleGrade, ResolvedSpawnGrade, RemainingCount);
	int32 MissingCount = FMath::Max(0, RemainingCount - CountAvailableQuestItems(QuestState.DefinitionId, RequiredItemClassObject, bRequiresSampleGrade, ResolvedSpawnGrade));
	if (MissingCount > 0)
	{
		SpawnMainQuestItems(QuestState, *Definition, SpawnItemClass, bRequiresSampleGrade, ResolvedSpawnGrade, MissingCount);
	}

	// 생성 및 부족분 검사가 끝난 퀘스트와 Phase 조합을 함께 기록한다.
	LastSpawnedMainQuestDefinitionId = QuestState.DefinitionId;
	LastSpawnedMainPhaseIndex = QuestState.CurrentPhaseIndex;
}

void AMD_QuestSampleSpawner::TrySpawnForRepeatQuestStates(const UMD_MerchantSubsystem* MerchantSubsystem)
{
	if (IsValid(MerchantSubsystem) == false)
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

		// 같은 Zone의 후보 중 월드 매니저가 무작위로 선택한 스포너만 부족분을 생성한다.
		UWorld* World = GetWorld();
		UMD_QuestSampleSpawnerManager* SpawnerManager = IsValid(World) == true ? World->GetSubsystem<UMD_QuestSampleSpawnerManager>() : nullptr;
		bool bIsSpawnerManagerValid = IsValid(SpawnerManager);
		if (bIsSpawnerManagerValid == false)
		{
			continue;
		}

		// 퀘스트와 Phase별 대표 스포너가 현재 스포너인지 확인한다.
		bool bIsSelectedSpawner = SpawnerManager->IsSelectedSpawner(this, *Definition, QuestState.CurrentPhaseIndex);
		if (bIsSelectedSpawner == false)
		{
			continue;
		}

		int32 RemainingCount = FMath::Max(0, Definition->GetRequiredCount(QuestState.CurrentPhaseIndex) - QuestState.CurrentProgress);
		TSubclassOf<AMD_QuestItem> RequiredItemClass = Definition->GetRequiredItemClass(QuestState.CurrentPhaseIndex);
		TSubclassOf<AMD_QuestItem> SpawnItemClass = ResolveSpawnItemClass(*Definition, QuestState.CurrentPhaseIndex);
		bool bIsSpawnItemClassValid = IsValid(SpawnItemClass.Get());
		if (bIsSpawnItemClassValid == false)
		{
			UE_LOG(MarineDemolition, Warning, TEXT("%s [QuestSampleSpawner] %s: [%s] 요구 아이템 클래스와 기본 SampleClass가 모두 비어 있다"), *UE_CALL_LOCATIONINFO, *GetName(), *Definition->QuestName.ToString());
			continue;
		}

		UClass* RequiredItemClassObject = IsValid(RequiredItemClass.Get()) == true ? RequiredItemClass.Get() : SpawnItemClass.Get();
		bool bIsSampleItemClass = SpawnItemClass.Get()->IsChildOf(AMD_SampleItem::StaticClass());
		bool bRequiresSampleGrade = bIsSampleItemClass == true && Definition->GetAcceptedGradeTags(QuestState.CurrentPhaseIndex).Num() > 0;
		// 반복 퀘스트가 요구하는 등급을 스포너 설정 없이 직접 해석한다.
		EMD_SampleGrade ResolvedSpawnGrade = EMD_SampleGrade::C;
		bool bHasResolvedSpawnGrade = bRequiresSampleGrade == false || ResolveSpawnGrade(*Definition, QuestState.CurrentPhaseIndex, ResolvedSpawnGrade) == true;
		if (bHasResolvedSpawnGrade == false)
		{
			UE_LOG(MarineDemolition, Warning, TEXT("%s [QuestSampleSpawner] %s: [%s] 허용 등급을 샘플 등급으로 변환할 수 없다"), *UE_CALL_LOCATIONINFO, *GetName(), *Definition->QuestName.ToString());
			continue;
		}

		int32 MissingCount = FMath::Max(0, RemainingCount - CountAvailableQuestItems(QuestState.DefinitionId, RequiredItemClassObject, bRequiresSampleGrade, ResolvedSpawnGrade));
		if (MissingCount > 0)
		{
			SpawnRepeatQuestItems(QuestState, *Definition, SpawnItemClass, bRequiresSampleGrade, ResolvedSpawnGrade, MissingCount);
		}
	}
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
	// 지역이 지정된 목표가 다른 지역의 스포너에서 생성되지 않도록 먼저 제한한다.
	bool bMatchesSpawnZone = MatchesSpawnZone(Definition, PhaseIndex);
	if (bMatchesSpawnZone == false)
	{
		return false;
	}

	EMD_QuestObjectiveType ObjectiveType = Definition.GetObjectiveType(PhaseIndex);
	bool bIsRecoveryObjective = ObjectiveType == EMD_QuestObjectiveType::RecoverIntact || ObjectiveType == EMD_QuestObjectiveType::RecoverDamaged;
	if (bIsRecoveryObjective == false)
	{
		return false;
	}

	// 등급은 스포너 선택 조건에서 제외하고 퀘스트 데이터를 통해 생성 시점에 결정한다.
	bool bMatchesRepeatQuest = true;
	return bMatchesRepeatQuest;
}

bool AMD_QuestSampleSpawner::MatchesSpawnZone(const UMD_QuestDefinition& Definition, int32 PhaseIndex) const
{
	// 퀘스트에서 지역을 비워 둔 경우 기존 스포너 배치가 모두 계속 동작하도록 허용한다.
	FGameplayTag RequiredSpawnZoneTag = Definition.GetSpawnZoneTag(PhaseIndex);
	bool bHasZoneRestriction = RequiredSpawnZoneTag.IsValid() == true;
	bool bMatchesSpawnZone = true;
	if (bHasZoneRestriction == true)
	{
		// 지역 제한이 있으면 스포너에 지정한 지역과 정확히 일치해야 한다.
		bMatchesSpawnZone = SpawnZoneTag.MatchesTagExact(RequiredSpawnZoneTag);
	}
	return bMatchesSpawnZone;
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
	// 퀘스트에 등록된 첫 번째 지원 등급을 실제 생성 등급으로 결정한다.
	bool bOutResolved = false;
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
				// 퀘스트 허용 태그에 대응하는 등급을 생성 결과로 저장한다.
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

	return bOutResolved;
}

bool AMD_QuestSampleSpawner::FindLinkedMainQuest(
	const UMD_MerchantSubsystem* MerchantSubsystem,
	FMD_MainQuestState& OutQuestState) const
{
	for (const FMD_MainQuestState& QuestState : MerchantSubsystem->GetMainQuests())
	{
		// 현재 활성화된 메인 퀘스트만 스포너 연결 후보로 사용한다.
		if (QuestState.Status != EMD_MainQuestStatus::Active)
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

		// 날짜 대신 현재 Phase의 Zone과 일치하는 스포너를 연결한다.
		bool bMatchesSpawnZone = MatchesSpawnZone(*Definition, QuestState.CurrentPhaseIndex);
		if (bMatchesSpawnZone == true)
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
		UE_LOG(MarineDemolition, Warning,
			TEXT("%s [QuestSampleSpawner] %s: 스폰 포인트 없음"),
			*UE_CALL_LOCATIONINFO,
			*GetName());
		return;
	}

	// 기획 09 §1.5: 스폰 개수는 퀘스트 목표 개수와 1:1이고 여분 샘플은 두지 않는다.
	// 여분이 있으면 그 하나가 깨져도 납품은 가능한데 퀘스트는 파괴로 실패해버려
	// (§1.5 "실패 — 파괴, 재시도 불가") 회복 불가 상태가 된다. 배치 실수를 여기서 잡는다.
	int32 RequiredCount = Definition.GetRequiredCount(QuestState.CurrentPhaseIndex);
	if (RequiredCount > 0 && SamplesToSpawn != RequiredCount)
	{
		UE_LOG(MarineDemolition, Warning,
			TEXT("%s [QuestSampleSpawner] %s: [%s] SamplesToSpawn(%d)이 퀘스트 RequiredCount(%d)와 다르다 — 기획 09 §1.5는 1:1을 요구한다"),
			*UE_CALL_LOCATIONINFO,
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
			UE_LOG(MarineDemolition, Warning, TEXT("%s [QuestSampleSpawner] %s: 퀘스트 아이템 스폰 실패 (Day%d, index %d) — 요구 클래스 부모 확인"), *UE_CALL_LOCATIONINFO, *GetName(), QuestState.UnlockDay, Index);
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
		UE_LOG(MarineDemolition, Log, TEXT("%s [QuestSampleSpawner] %s: [%s] Day%d %s 스폰 @ %s"), *UE_CALL_LOCATIONINFO, *GetName(), *Definition.QuestName.ToString(), QuestState.UnlockDay, *ItemTypeLabel, *QuestItem->GetActorLocation().ToCompactString());
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
		UE_LOG(MarineDemolition, Warning,
			TEXT("%s [QuestSampleSpawner] %s: [%s] 반복 퀘스트 스폰 포인트 없음"),
			*UE_CALL_LOCATIONINFO,
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
			UE_LOG(MarineDemolition, Warning, TEXT("%s [QuestSampleSpawner] %s: [%s] 반복 퀘스트 아이템 스폰 실패 (index %d) — 요구 클래스 부모 확인"), *UE_CALL_LOCATIONINFO, *GetName(), *Definition.QuestName.ToString(), Index);
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
		UE_LOG(MarineDemolition, Log, TEXT("%s [QuestSampleSpawner] %s: [%s] 반복 퀘스트 %s 스폰 @ %s"), *UE_CALL_LOCATIONINFO, *GetName(), *Definition.QuestName.ToString(), *ItemTypeLabel, *QuestItem->GetActorLocation().ToCompactString());
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
