// Fill out your copyright notice in the Description page of Project Settings.


#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "GameplayTagContainer.h"
#include "Data/Merchant/MD_QuestDefinition.h"
#include "Data/Merchant/MD_SiteDefinition.h"


int32 UMD_MerchantSubsystem::GetActiveQuestReward() const
{
	int32 Total = 0;

	for (const FMD_MainQuestState& QuestState : MainQuests)
	{
		if (QuestState.IsActive() && IsValid(QuestState.Definition))
		{
			Total += QuestState.Definition->RewardCredits;
		}
	}

	for (const FMD_RepeatQuestState& QuestState : RepeatQuests)
	{
		if (QuestState.IsActive() && IsValid(QuestState.Definition))
		{
			Total += QuestState.Definition->RewardCredits;
		}
	}

	return Total;
}

void UMD_MerchantSubsystem::AddCredits(int32 Amount)
{
	//크레딧은 최소 0
	if (Amount == 0)
	{
		return;
	}

	Credits = FMath::Max(0, Credits + Amount);

	OnMerchantStateChanged.Broadcast();
}

void UMD_MerchantSubsystem::BeginContract(UMD_SiteDefinition* Site, int32 AdvancePayment)
{
	if (!IsValid(Site))
	{
		return;
	}

	CurrentSite = Site;
	CurrentDay = 0;

	//메인퀘스트 반복 방지
	MainQuests.Reset();

	for (int32 Index = 0; Index < Site->MainQuestLine.Num(); ++Index)
	{
		UMD_QuestDefinition* Definition = Site->MainQuestLine[Index];
		if (!IsValid(Definition))
		{
			// 애셋 배열에 빈 슬롯이 있으면 건너뛴다. 다만 Day 번호가 밀리므로 경고를 남긴다.
			UE_LOG(LogTemp, Warning, TEXT("[Merchant] MainQuestLine[%d]가 비어 있다"), Index);
			continue;
		}

		FMD_MainQuestState NewMain;
		NewMain.Definition = Definition;
		NewMain.UnlockDay = Index + 1;   // 인덱스 0 → Day 1
		NewMain.Status = EMD_MainQuestStatus::Locked;

		MainQuests.Add(NewMain);

	}

	AddCreditsInternal(AdvancePayment);

	UE_LOG(LogTemp, Log, TEXT("[Merchant] 계약 시작: %s (메인 %d건, 착수금 %d)"), *Site->SiteName.ToString(), MainQuests.Num(), AdvancePayment);

	OnMerchantStateChanged.Broadcast();
}

bool UMD_MerchantSubsystem::HasClearedCurrentSite() const
{
	return IsValid(CurrentSite) && ClearedSites.Contains(CurrentSite);
}

void UMD_MerchantSubsystem::BeginDay(int32 NewDay)
{
	CurrentDay = NewDay;

	// 메인은 수락 절차가 없다. Day에 도달하면 Locked에서 곧바로 Active가 된다.
	// 전날 미완료 메인도 Active로 남으므로 병행 진행이 가능하다. (기획 10 §2)
	for (FMD_MainQuestState& MainQuest : MainQuests)
	{
		if (MainQuest.Status == EMD_MainQuestStatus::Locked && MainQuest.UnlockDay <= NewDay)
		{
			MainQuest.Status = EMD_MainQuestStatus::Active;
		}
	}
	RerollRepeatQuests();

	UE_LOG(LogTemp, Log, TEXT("[Merchant] Day %d 시작 (반복 %d건)"), NewDay, RepeatQuests.Num());

	OnMerchantStateChanged.Broadcast();
}

void UMD_MerchantSubsystem::MarkSiteCleared()
{
	if (!IsValid(CurrentSite))
	{
		return;
	}

	ClearedSites.Add(CurrentSite);

	UE_LOG(LogTemp, Log, TEXT("[Merchant] 현장 클리어 기록: %s"), *CurrentSite->SiteName.ToString());

	OnMerchantStateChanged.Broadcast();
}

bool UMD_MerchantSubsystem::TryAcceptRepeatQuest(int32 QuestIndex)
{
	if (!RepeatQuests.IsValidIndex(QuestIndex))
	{
		return false;
	}

	FMD_RepeatQuestState& QuestState = RepeatQuests[QuestIndex];

	// Offered에서만 수락할 수 있다. 이미 끝난 카드를 되살릴 수 없다.
	if (QuestState.Status != EMD_RepeatQuestStatus::Offered)
	{
		return false;
	}

	QuestState.Status = EMD_RepeatQuestStatus::Accepted;
	QuestState.CurrentProgress = 0;

	OnMerchantStateChanged.Broadcast();

	return true;
}

bool UMD_MerchantSubsystem::TryDeclineRepeatQuest(int32 QuestIndex)
{
	if (!RepeatQuests.IsValidIndex(QuestIndex))
	{
		return false;
	}

	FMD_RepeatQuestState& QuestState = RepeatQuests[QuestIndex];
	if (QuestState.Status != EMD_RepeatQuestStatus::Offered)
	{
		return false;
	}

	// 거절은 페널티가 없다. 다음 Day 재롤 때 이 카드가 사라진다.
	QuestState.Status = EMD_RepeatQuestStatus::Declined;

	OnMerchantStateChanged.Broadcast();

	return true;
}

void UMD_MerchantSubsystem::NotifySampleDelivered(const FGameplayTag& GradeTag, bool bIntact)
{
	if (!GradeTag.IsValid())
	{
		return;
	}

	bool bChanged = false;

	for (FMD_MainQuestState& QuestState : MainQuests)
	{
		if (!QuestState.IsActive() || !DoesDeliveryMatch(QuestState.Definition, GradeTag, bIntact))
		{
			continue;
		}

		++QuestState.CurrentProgress;
		bChanged = true;

		if (QuestState.CurrentProgress >= QuestState.Definition->RequiredCount)
		{
			QuestState.Status = EMD_MainQuestStatus::Completed;
			ApplyQuestReward(QuestState.Definition);
		}
	}

	for (FMD_RepeatQuestState& QuestState : RepeatQuests)
	{
		if (!QuestState.IsActive() || !DoesDeliveryMatch(QuestState.Definition, GradeTag, bIntact))
		{
			continue;
		}

		++QuestState.CurrentProgress;
		bChanged = true;

		if (QuestState.CurrentProgress >= QuestState.Definition->RequiredCount)
		{
			QuestState.Status = EMD_RepeatQuestStatus::Completed;
			ApplyQuestReward(QuestState.Definition);
		}
	}

	// 아무 퀘스트도 반응하지 않았으면 UI를 갱신할 이유가 없다.
	if (bChanged)
	{
		OnMerchantStateChanged.Broadcast();
	}

}

void UMD_MerchantSubsystem::FinalizeContract()
{
	bool bChanged = false;

	// 메인은 실패 상태가 없다. 미완료는 그대로 남긴다. (기획 10 §2)
	for (FMD_RepeatQuestState& QuestState : RepeatQuests)
	{
		if (QuestState.IsActive())
		{
			// 크레딧 차감 없음. 실패는 호감도만 하락한다. (07 M-02 삭제)
			QuestState.Status = EMD_RepeatQuestStatus::Failed;
			bChanged = true;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[Merchant] 계약 종료"));

	if (bChanged)
	{
		OnMerchantStateChanged.Broadcast();
	}
}

void UMD_MerchantSubsystem::RerollRepeatQuests()
{
	// 현장을 한 번 클리어해야 반복 퀘스트가 열린다. 첫 방문에는 메인만 보인다. (기획 09 §4)
	if (!IsValid(CurrentSite) || !HasClearedCurrentSite())
	{
		RepeatQuests.Reset();
		return;
	}

	// 수락해 진행 중인 퀘스트는 남기고, 미수락·거절 카드만 교체한다.
	RepeatQuests.RemoveAll([](const FMD_RepeatQuestState& Entry)
		{
			return Entry.Status == EMD_RepeatQuestStatus::Offered
				|| Entry.Status == EMD_RepeatQuestStatus::Declined;
		});

	// 이미 들고 있는 퀘스트와 겹치지 않게 후보에서 제외한다.
	TArray<TObjectPtr<UMD_QuestDefinition>> Pool = CurrentSite->RepeatQuestPool;
	Pool.RemoveAll([this](const TObjectPtr<UMD_QuestDefinition>& Entry)
		{
			if (!IsValid(Entry))
			{
				return true;
			}

			return RepeatQuests.ContainsByPredicate(
				[&Entry](const FMD_RepeatQuestState& Existing)
				{
					return Existing.Definition == Entry;
				});
		});

	// 애셋에 Min > Max로 잘못 입력돼도 크래시하지 않게 정렬한다.
	const int32 MinCount = FMath::Min(CurrentSite->MinRepeatCount, CurrentSite->MaxRepeatCount);
	const int32 MaxCount = FMath::Max(CurrentSite->MinRepeatCount, CurrentSite->MaxRepeatCount);

	const int32 TargetCount = FMath::Min(
		FMath::RandRange(MinCount, MaxCount),
		RepeatQuests.Num() + Pool.Num());

	while (RepeatQuests.Num() < TargetCount && Pool.Num() > 0)
	{
		const int32 PickIndex = FMath::RandRange(0, Pool.Num() - 1);

		FMD_RepeatQuestState NewQuest;
		NewQuest.Definition = Pool[PickIndex];
		NewQuest.Status = EMD_RepeatQuestStatus::Offered;
		RepeatQuests.Add(NewQuest);

		Pool.RemoveAtSwap(PickIndex);
	}
}

void UMD_MerchantSubsystem::AddCreditsInternal(int32 Amount)
{
	if (Amount == 0)
	{
		return;
	}

	// 크레딧은 0 밑으로 내려가지 않는다.
	Credits = FMath::Max(0, Credits + Amount);
}

bool UMD_MerchantSubsystem::DoesDeliveryMatch(const UMD_QuestDefinition* Definition, const FGameplayTag& GradeTag, bool bIntact) const
{
	if (!IsValid(Definition))
	{
		return false;
	}

	// 지금은 회수형만 판정한다. 청소·마감은 정분류 시스템이 붙은 뒤에 처리한다.
	const bool bIsRecovery =
		Definition->ObjectiveType == EMD_QuestObjectiveType::RecoverIntact
		|| Definition->ObjectiveType == EMD_QuestObjectiveType::RecoverDamaged;

	if (!bIsRecovery)
	{
		return false;
	}

	// Intact 전용 퀘스트는 손상 납품을 인정하지 않는다. (기획 02 §4)
	if (Definition->ObjectiveType == EMD_QuestObjectiveType::RecoverIntact && !bIntact)
	{
		return false;
	}

	// 등급은 계층 매칭이 아니라 정확히 일치해야 한다. A급 퀘스트는 S급 납품으로 안 채워진다.
	return Definition->TargetGradeTag.MatchesTagExact(GradeTag);
}

void UMD_MerchantSubsystem::ApplyQuestReward(const UMD_QuestDefinition* Definition)
{
	if (!IsValid(Definition))
	{
		return;
	}

	// 손상 허용 회수는 보수 50%. (기획 07 §3.2)
	const int32 Payout = (Definition->ObjectiveType == EMD_QuestObjectiveType::RecoverDamaged)
		? Definition->RewardCredits / 2
		: Definition->RewardCredits;

	AddCreditsInternal(Payout);

	// FMD_ShopItem::IsUnlocked의 QuestCompletions 인자로 쓰인다.
	++QuestCompletions;

	UE_LOG(LogTemp, Log, TEXT("[Merchant] 퀘스트 완료: %s (+%d, 누적 성공 %d)"),
		*Definition->QuestName.ToString(), Payout, QuestCompletions);

	// TODO(김동연): 호감도 단계에서 Affinity 상승을 추가한다.
}
