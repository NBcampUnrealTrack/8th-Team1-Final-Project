#include "Data/Merchant/MD_QuestDefinition.h"

#include "Engine/AssetManager.h"
#include "World/Sample/MD_QuestItem.h"
#include "World/Sample/MD_SampleItem.h"

bool FMD_QuestPhaseData::AcceptsQuestItem(AMD_QuestItem* SubmittedItem) const
{
	bool bOutAccepted = false;
	if (IsValid(SubmittedItem) == false)
	{
		return bOutAccepted;
	}

	// 파괴 가능한 샘플을 제출할 때만 설정된 등급 조건을 적용하는 판정
	UClass* SubmittedClass = SubmittedItem->GetClass();
	bool bCheckGradeTag = IsValid(SubmittedClass) == true && SubmittedClass->IsChildOf(AMD_SampleItem::StaticClass()) == true;
	bool bHasGradeRequirement = AcceptedGradeTags.Num() > 0;
	bool bGradeMatches = true;
	if (bCheckGradeTag == true)
	{
		FGameplayTag GradeTag = SubmittedItem->GetQuestGradeTag();
		bGradeMatches = bHasGradeRequirement == false || (GradeTag.IsValid() == true && AcceptedGradeTags.HasTagExact(GradeTag) == true);
	}

	bool bIntact = SubmittedItem->IsQuestItemIntact();
	bool bIsRecovery = ObjectiveType == EMD_QuestObjectiveType::RecoverIntact || ObjectiveType == EMD_QuestObjectiveType::RecoverDamaged;
	bool bConditionMatches = ObjectiveType == EMD_QuestObjectiveType::RecoverDamaged || bIntact == true;
	bOutAccepted = bIsRecovery == true && bGradeMatches == true && bConditionMatches == true;
	return bOutAccepted;
}

bool FMD_QuestPhaseData::TargetsQuestItem(const FGameplayTag& GradeTag) const
{
	// 등급 조건이 없는 아이템도 회수 목표로 판정하기 위한 대상 판정
	bool bIsRecovery = ObjectiveType == EMD_QuestObjectiveType::RecoverIntact || ObjectiveType == EMD_QuestObjectiveType::RecoverDamaged;
	bool bHasGradeRequirement = AcceptedGradeTags.Num() > 0;
	bool bGradeMatches = bHasGradeRequirement == false || (GradeTag.IsValid() == true && AcceptedGradeTags.HasTagExact(GradeTag) == true);
	bool bOutTargetsQuestItem = bIsRecovery == true && bGradeMatches == true;
	return bOutTargetsQuestItem;
}

const FPrimaryAssetType UMD_QuestDefinition::QuestPrimaryAssetType(TEXT("Quest"));

FPrimaryAssetId UMD_QuestDefinition::GetPrimaryAssetId() const
{
	// 별도 식별자 없이 자산 이름을 저장과 조회의 단일 기준으로 사용하는 처리
	FPrimaryAssetId OutPrimaryAssetId(QuestPrimaryAssetType, GetFName());
	return OutPrimaryAssetId;
}

UMD_QuestDefinition* UMD_QuestDefinition::LoadDefinition(const FPrimaryAssetId& DefinitionId)
{
	UMD_QuestDefinition* OutValidatedDefinition = nullptr;
	// 잘못된 식별자에서는 Asset Manager 조회를 시작하지 않기 위한 사전 판정
	bool bIsDefinitionIdValid = DefinitionId.IsValid();
	if (bIsDefinitionIdValid == true)
	{
		// 이미 로드된 원본을 우선 사용하고 필요한 경우에만 패키지를 동기 로드하는 처리
		UAssetManager& AssetManager = UAssetManager::Get();
		UMD_QuestDefinition* Definition = Cast<UMD_QuestDefinition>(AssetManager.GetPrimaryAssetObject(DefinitionId));
		bool bIsDefinitionLoaded = IsValid(Definition);
		if (bIsDefinitionLoaded == false)
		{
			FSoftObjectPath DefinitionPath = AssetManager.GetPrimaryAssetPath(DefinitionId);
			Definition = Cast<UMD_QuestDefinition>(DefinitionPath.TryLoad());
		}

		// 다른 타입 또는 다른 ID로 등록된 에셋이 상태 데이터에 연결되지 않도록 하는 검증
		OutValidatedDefinition = IsValid(Definition) == true && Definition->GetPrimaryAssetId() == DefinitionId ? Definition : nullptr;
	}
	return OutValidatedDefinition;
}

bool UMD_QuestDefinition::IsValidDefinition() const
{
	bool bOutValid = GetPrimaryAssetId().IsValid() == true && Phases.Num() > 0;
	if (bOutValid == true)
	{
		for (const FMD_QuestPhaseData& Phase : Phases)
		{
			if (Phase.RequiredCount <= 0)
			{
				bOutValid = false;
				break;
			}
		}
	}
	return bOutValid;
}

bool UMD_QuestDefinition::HasPhase(int32 PhaseIndex) const
{
	return Phases.IsValidIndex(PhaseIndex);
}

const FMD_QuestPhaseData* UMD_QuestDefinition::GetPhase(int32 PhaseIndex) const
{
	const FMD_QuestPhaseData* OutPhase = Phases.IsValidIndex(PhaseIndex) == true ? &Phases[PhaseIndex] : nullptr;
	return OutPhase;
}

int32 UMD_QuestDefinition::GetPhaseCount() const
{
	return Phases.Num();
}

EMD_QuestObjectiveType UMD_QuestDefinition::GetObjectiveType(int32 PhaseIndex) const
{
	const FMD_QuestPhaseData* Phase = GetPhase(PhaseIndex);
	return Phase != nullptr ? Phase->ObjectiveType : EMD_QuestObjectiveType::RecoverIntact;
}

int32 UMD_QuestDefinition::GetRequiredCount(int32 PhaseIndex) const
{
	const FMD_QuestPhaseData* Phase = GetPhase(PhaseIndex);
	int32 OutRequiredCount = Phase != nullptr ? Phase->RequiredCount : 0;
	OutRequiredCount = FMath::Max(OutRequiredCount, 0);
	return OutRequiredCount;
}

FGameplayTag UMD_QuestDefinition::GetSpawnZoneTag(int32 PhaseIndex) const
{
	// 유효하지 않은 페이즈는 지역 제한이 없는 값으로 처리한다.
	const FMD_QuestPhaseData* Phase = GetPhase(PhaseIndex);
	FGameplayTag OutSpawnZoneTag = Phase != nullptr ? Phase->SpawnZoneTag : FGameplayTag();
	return OutSpawnZoneTag;
}

FText UMD_QuestDefinition::GetHUDDescription(int32 PhaseIndex) const
{
	const FMD_QuestPhaseData* Phase = GetPhase(PhaseIndex);
	FText OutDescription = Phase != nullptr ? Phase->HUDDescription : FText::GetEmpty();
	return OutDescription;
}

FText UMD_QuestDefinition::GetCompletionDialogue(int32 PhaseIndex) const
{
	const FMD_QuestPhaseData* Phase = GetPhase(PhaseIndex);
	FText OutDialogue = Phase != nullptr ? Phase->CompletionDialogue : FText::GetEmpty();
	return OutDialogue;
}

const FMD_QuestRewardData& UMD_QuestDefinition::GetPhaseCompletionReward(int32 PhaseIndex) const
{
	const FMD_QuestPhaseData* Phase = GetPhase(PhaseIndex);
	if (Phase != nullptr)
	{
		return Phase->PhaseCompletionReward;
	}
	static const FMD_QuestRewardData EmptyReward{};
	return EmptyReward;
}

FGameplayTagContainer UMD_QuestDefinition::GetAcceptedGradeTags(int32 PhaseIndex) const
{
	const FMD_QuestPhaseData* Phase = GetPhase(PhaseIndex);
	FGameplayTagContainer OutGradeTags;
	if (Phase != nullptr)
	{
		OutGradeTags = Phase->AcceptedGradeTags;
	}
	return OutGradeTags;
}

TSubclassOf<AMD_QuestItem> UMD_QuestDefinition::GetRequiredItemClass(int32 PhaseIndex) const
{
	const FMD_QuestPhaseData* Phase = GetPhase(PhaseIndex);
	TSubclassOf<AMD_QuestItem> OutRequiredItemClass;
	if (Phase != nullptr)
	{
		OutRequiredItemClass = Phase->RequiredItemClass;
	}
	return OutRequiredItemClass;
}

bool UMD_QuestDefinition::AllowsDamagedTarget(int32 PhaseIndex) const
{
	return GetObjectiveType(PhaseIndex) == EMD_QuestObjectiveType::RecoverDamaged;
}

bool UMD_QuestDefinition::AcceptsQuestItem(int32 PhaseIndex, AMD_QuestItem* SubmittedItem) const
{
	const FMD_QuestPhaseData* Phase = GetPhase(PhaseIndex);
	bool bOutAcceptsQuestItem = Phase != nullptr && Phase->AcceptsQuestItem(SubmittedItem) == true;
	return bOutAcceptsQuestItem;
}

bool UMD_QuestDefinition::TargetsQuestItem(int32 PhaseIndex, const FGameplayTag& GradeTag) const
{
	const FMD_QuestPhaseData* Phase = GetPhase(PhaseIndex);
	bool bOutTargetsQuestItem = Phase != nullptr && Phase->TargetsQuestItem(GradeTag) == true;
	return bOutTargetsQuestItem;
}

bool UMD_QuestDefinition::IsObjectiveComplete(int32 PhaseIndex, int32 CurrentProgress) const
{
	bool bOutComplete = HasPhase(PhaseIndex) == true && PhaseIndex == GetPhaseCount() - 1 && CurrentProgress >= FMath::Max(GetRequiredCount(PhaseIndex), 1);
	return bOutComplete;
}
