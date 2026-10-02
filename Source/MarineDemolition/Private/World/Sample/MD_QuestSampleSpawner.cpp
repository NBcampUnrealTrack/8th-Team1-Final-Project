#include "World/Sample/MD_QuestSampleSpawner.h"
#include "Data/SaveGame/MD_SaveSlotManager.h"

#include "Components/ArrowComponent.h"
#include "Data/Merchant/MD_QuestDefinition.h"
#include "EngineUtils.h"
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
	// 월드 오브젝트 등록 중복과 클라이언트 등록 차단
	if (bSpawnerInitialized == true || HasAuthority() == false)
	{
		return;
	}
	// 저장 복원과 배치 오브젝트 관리를 위한 매니저 조회
	UWorld* World = GetWorld();
	// 유효한 월드에서만 등록 처리
	if (IsValid(World) == true)
	{
		// 물리 스포너의 저장 및 복원 등록 대상 조회
		UMD_QuestSampleSpawnerManager* Manager = World->GetSubsystem<UMD_QuestSampleSpawnerManager>();
		// 준비된 매니저에 등록하고 중복 초기화 방지
		if (IsValid(Manager) == true)
		{
			// 기존 배치 및 저장 복원 식별 유지
			Manager->RegisterSpawner(this);
			// 현재 월드의 등록 완료 기록
			bSpawnerInitialized = true;
		}
	}
}

void AMD_QuestSampleSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 파괴된 배치 오브젝트의 등록 해제 대상 조회
	UWorld* World = GetWorld();
	// 종료 중에도 유효한 월드의 등록만 해제
	if (IsValid(World) == true)
	{
		// 저장 복원용 스포너 매니저 조회
		UMD_QuestSampleSpawnerManager* Manager = World->GetSubsystem<UMD_QuestSampleSpawnerManager>();
		// 매니저가 보관한 종료 객체 참조 제거
		if (IsValid(Manager) == true)
		{
			// 종료 이후 생성 요청 대상에서 제외
			Manager->UnregisterSpawner(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}


void AMD_QuestSampleSpawner::TrySpawnForQuestState()
{
	// 랙과 캐비닛 등 파생 월드 오브젝트의 개별 생성 조건용 확장 지점
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

bool AMD_QuestSampleSpawner::DebugSpawnQuestSample(const UMD_QuestDefinition& Definition, int32 PhaseIndex)
{
#if !UE_BUILD_SHIPPING
	if (!HasAuthority() || !Definition.HasPhase(PhaseIndex) || !MatchesSpawnZone(Definition, PhaseIndex)) return false;
	const TSubclassOf<AMD_QuestItem> ItemClass = ResolveSpawnItemClass(Definition, PhaseIndex);
	if (!ItemClass || CollectSpawnTransforms().IsEmpty()) return false;
	const bool bRequiresGrade = ItemClass->IsChildOf(AMD_SampleItem::StaticClass())
		&& Definition.GetAcceptedGradeTags(PhaseIndex).Num() > 0;
	EMD_SampleGrade Grade = EMD_SampleGrade::C;
	if (bRequiresGrade && !ResolveSpawnGrade(Definition, PhaseIndex, Grade)) return false;
	const int32 RequiredCount = FMath::Max(1, Definition.GetRequiredCount(PhaseIndex));
	BindOccupyingQuestItemsToMainQuest(Definition.GetPrimaryAssetId(), ItemClass.Get(), bRequiresGrade, Grade, RequiredCount);
	const int32 MissingCount = RequiredCount - CountAvailableQuestItems(Definition.GetPrimaryAssetId(), ItemClass.Get(), bRequiresGrade, Grade);
	if (MissingCount > 0)
	{
		// 퀘스트 상태 생성 없이 지정된 배치 구성으로 부족 물품 생성
		SpawnConfiguredItems(PhaseIndex, Definition, ItemClass, bRequiresGrade, Grade, MissingCount);
	}
	return CountAvailableQuestItems(Definition.GetPrimaryAssetId(), ItemClass.Get(), bRequiresGrade, Grade) >= RequiredCount;
#else
	return false;
#endif
}

void AMD_QuestSampleSpawner::SpawnConfiguredItems(int32 PhaseIndex, const UMD_QuestDefinition& Definition, TSubclassOf<AMD_QuestItem> SpawnItemClass, bool bRequiresSampleGrade, EMD_SampleGrade SpawnGrade, int32 SpawnCount)
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
	// 지정된 배치 구성의 요구 수량 조회
	int32 RequiredCount = Definition.GetRequiredCount(PhaseIndex);
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
			// 생성 실패한 배치 구성과 위치 순번 기록
			UE_LOG(MarineDemolition, Warning, TEXT("%s [QuestSampleSpawner] %s: 퀘스트 아이템 스폰 실패 (Phase%d, index %d) — 요구 클래스 부모 확인"), *UE_CALL_LOCATIONINFO, *GetName(), PhaseIndex, Index);
			continue;
		}

		// 스폰 포인트의 명시적인 크기만 반영하여 아이템 Blueprint 기본 크기를 보존하는 처리
		FVector SpawnScale = SpawnTransform.GetScale3D();
		bool bHasSpawnScale = SpawnScale.IsNearlyZero() == false;
		if (bHasSpawnScale == true)
		{
			QuestItem->SetActorScale3D(SpawnScale);
		}

		// 저장 복원 및 동일 물품 중복 방지에 사용하는 구성 식별자 유지
		QuestItem->QuestDefinitionId = Definition.GetPrimaryAssetId();
		AMD_SampleItem* Sample = Cast<AMD_SampleItem>(QuestItem);
		bool bIsDestructibleSample = IsValid(Sample);
		if (bIsDestructibleSample == true)
		{
			// 파괴 가능한 샘플에 생성 직후 충격 보호를 추가하는 설정
			Sample->SpawnProtectionDuration = SampleSpawnProtectionDuration;
			if (WaterFloat.bEnabled) Sample->WaterFloat = WaterFloat;
			Sample->SuppressImpactDamage(2.0f);
			if (bRequiresSampleGrade == true)
			{
				// 등급 조건이 있는 반복 샘플에만 실제 생성 등급을 적용하는 설정
				Sample->Grade = SpawnGrade;
			}
		}

		FString ItemTypeLabel = bRequiresSampleGrade == true ? FString::Printf(TEXT("%s급 샘플"), *StaticEnum<EMD_SampleGrade>()->GetNameStringByValue(static_cast<int64>(SpawnGrade))) : (bIsDestructibleSample == true ? TEXT("파괴 가능한 퀘스트 샘플") : TEXT("파괴 불가 퀘스트 아이템"));
		// 실제 생성한 물품 구성과 위치 기록
		UE_LOG(MarineDemolition, Log, TEXT("%s [QuestSampleSpawner] %s: [%s] Phase%d %s 스폰 @ %s"), *UE_CALL_LOCATIONINFO, *GetName(), *Definition.QuestName.ToString(), PhaseIndex, *ItemTypeLabel, *QuestItem->GetActorLocation().ToCompactString());
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
