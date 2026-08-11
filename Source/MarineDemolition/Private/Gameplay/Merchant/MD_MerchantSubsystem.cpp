// Fill out your copyright notice in the Description page of Project Settings.


#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "GameplayTagContainer.h"
#include "Data/Merchant/MD_SiteDefinition.h"
#include "Data/Shop/MD_ShopCatalog.h"
#include "Types/Score/MD_ScoreTypes.h"
#include "Types/SaveGame/MD_SaveDataTypes.h"


int32 UMD_MerchantSubsystem::GetActiveQuestReward() const
{
	int32 Total = 0;

	for (const FMD_MainQuestState& QuestState : MainQuests)
	{
		if (QuestState.IsActive() && QuestState.Definition.IsValidDefinition())
		{
			Total += QuestState.Definition.RewardCredits;
		}
	}

	for (const FMD_RepeatQuestState& QuestState : RepeatQuests)
	{
		if (QuestState.IsActive() && QuestState.Definition.IsValidDefinition())
		{
			Total += QuestState.Definition.RewardCredits;
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

EMD_ShopPurchaseResult UMD_MerchantSubsystem::TryPurchaseShopItem(const FMD_ShopItem& ShopItem)
{
	// 클라이언트가 로컬 원장을 변경하지 못하도록 막는 서버 권위 검사
	if (IsValid(GetWorld()) == false || GetWorld()->GetNetMode() == ENetMode::NM_Client)
	{
		return EMD_ShopPurchaseResult::InvalidRequest;
	}

	// 카탈로그 식별자와 구매 가격이 없는 항목의 직접 구매를 막는 데이터 검사
	if (ShopItem.ItemId.IsNone() == true || ShopItem.Price < 0)
	{
		return EMD_ShopPurchaseResult::InvalidItem;
	}

	const TArray<FName> OwnedItemIds = GetOwnedItemIds();

	// UI 표시와 동일한 조건을 서버 원장 값으로 다시 계산하는 해금 검사
	if (ShopItem.IsUnlocked(Affinity, QuestCompletions, OwnedItemIds) == false)
	{
		return EMD_ShopPurchaseResult::Locked;
	}

	// 개당 판매 품목이 아닌 장비의 중복 결제를 막는 보유 검사
	if (ShopItem.bPricePerUnit == false && GetPurchasedItemCount(ShopItem.ItemId) > 0)
	{
		return EMD_ShopPurchaseResult::FullyStocked;
	}

	// 부족한 크레딧으로 원장이 음수가 되는 상황을 막는 결제 검사
	if (Credits < ShopItem.Price)
	{
		return EMD_ShopPurchaseResult::InsufficientCredits;
	}

	// 결제와 구매 원장을 같은 프레임에 반영하여 부분 성공을 막는 원자적 처리
	Credits -= ShopItem.Price;
	int32& PurchasedCount = PurchasedItemCounts.FindOrAdd(ShopItem.ItemId);
	PurchasedCount += 1;

	UE_LOG(LogTemp, Log, TEXT("[Merchant] 상점 구매 완료: %s (-%d, 잔액 %d, 누적 %d개)"), *ShopItem.ItemId.ToString(), ShopItem.Price, Credits, PurchasedCount);

	// GameState 복제 미러와 열려 있는 상점 UI를 함께 갱신하기 위한 변경 알림
	OnMerchantStateChanged.Broadcast();

	return EMD_ShopPurchaseResult::Success;
}

TArray<FName> UMD_MerchantSubsystem::GetOwnedItemIds() const
{
	TArray<FName> OwnedItemIds;
	PurchasedItemCounts.GenerateKeyArray(OwnedItemIds);

	// TMap 순회 순서 차이로 복제 배열이 불필요하게 변경되는 상황을 막기 위한 정렬
	OwnedItemIds.Sort(FNameLexicalLess());
	return OwnedItemIds;
}

int32 UMD_MerchantSubsystem::GetPurchasedItemCount(FName ItemId) const
{
	const int32* PurchasedCount = PurchasedItemCounts.Find(ItemId);
	return PurchasedCount != nullptr ? *PurchasedCount : 0;
}

void UMD_MerchantSubsystem::AddAffinity(float Amount)
{
	// 의미 없는 변경 알림과 부동소수점 누적 오차를 줄이기 위한 조기 종료
	if (FMath::IsNearlyZero(Amount) == true)
	{
		return;
	}

	// 기획에서 정의한 호감도 범위를 벗어나 상점 해금 판정이 왜곡되지 않도록 하는 제한
	Affinity = FMath::Clamp(Affinity + Amount, -4.0f, 5.0f);
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
		const FMD_QuestDefinitionData& Definition = Site->MainQuestLine[Index];
		if (!Definition.IsValidDefinition())
		{
			UE_LOG(LogTemp, Warning, TEXT("[Merchant] MainQuestLine[%d]가 비어 있다"), Index);
			continue;
		}

		FMD_MainQuestState NewMain;
		NewMain.Definition = Definition;
		NewMain.SourceIndex = Index;
		NewMain.UnlockDay = Index + 1;
		NewMain.Status = EMD_MainQuestStatus::Locked;

		MainQuests.Add(NewMain);
	}

	// 새 계약이므로 경고 모디파이어 초기화
	NextDepositMultiplier = 1.f;
	bConsumableDiscountActive = true;

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

	// Day에 도달하면 Locked에서 Active가 된다. 실패·완료 건은 그대로 둔다.
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

		if (QuestState.CurrentProgress >= QuestState.Definition.RequiredCount)
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

		if (QuestState.CurrentProgress >= QuestState.Definition.RequiredCount)
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

void UMD_MerchantSubsystem::NotifySampleDestroyed(EMD_SampleGrade Grade)
{
	const FGameplayTag GradeTag = GradeTagFromSampleGrade(Grade);
	if (!GradeTag.IsValid())
	{
		return;
	}

	bool bChanged = false;

	for (FMD_MainQuestState& QuestState : MainQuests)
	{
		if (!QuestState.IsActive() || !DoesQuestTargetGrade(QuestState.Definition, GradeTag))
		{
			continue;
		}

		FailMainQuest(QuestState, EMD_QuestFailureReason::Destroyed);
		bChanged = true;
	}

	for (FMD_RepeatQuestState& QuestState : RepeatQuests)
	{
		if (!QuestState.IsActive() || !DoesQuestTargetGrade(QuestState.Definition, GradeTag))
		{
			continue;
		}

		QuestState.Status = EMD_RepeatQuestStatus::Failed;
		bChanged = true;
	}

	if (bChanged)
	{
		OnMerchantStateChanged.Broadcast();
	}
}

/**
 * Day 마감 시 미완료 활성 퀘스트를 실패로 확정한다.
 *
 * 메인 퀘스트는 Day별 고정 라인이라 **다음날 재시도가 없다.** 마감 시점에 완료되지
 * 않았으면 그대로 실패다. 기획 09 §1.5의 "미회수 … 다음날 재시도 가능"은 파괴되지
 * 않은 **샘플 액터가 맵에 남는다**는 뜻이지(같은 표 "미회수 샘플" 행), 그 Day의 메인
 * 퀘스트가 다음날로 이월된다는 뜻이 아니다. 남은 샘플은 이후 Day의 퀘스트 조건을
 * 만족시킬 때 쓰인다.
 */
void UMD_MerchantSubsystem::ResolveActiveQuestsAtDayEnd(int32 CompletedDay)
{
	if (CompletedDay <= 0)
	{
		return;
	}

	bool bChanged = false;

	for (FMD_MainQuestState& QuestState : MainQuests)
	{
		if (QuestState.Status != EMD_MainQuestStatus::Active)
		{
			continue;
		}

		if (QuestState.UnlockDay > CompletedDay)
		{
			continue;
		}

		FailMainQuest(QuestState, EMD_QuestFailureReason::NotDelivered);
		bChanged = true;
	}

	for (FMD_RepeatQuestState& QuestState : RepeatQuests)
	{
		if (!QuestState.IsActive())
		{
			continue;
		}

		QuestState.Status = EMD_RepeatQuestStatus::Failed;
		bChanged = true;
	}

	if (bChanged)
	{
		UE_LOG(LogTemp, Log, TEXT("[Merchant] Day %d 마감 — 미완료 퀘스트 실패 처리"), CompletedDay);
		OnMerchantStateChanged.Broadcast();
	}
}

void UMD_MerchantSubsystem::FinalizeContract()
{
	bool bChanged = false;

	// 미완료 반복만 실패로 정리한다. 메인은 Day 마감 시 ResolveActiveQuestsAtDayEnd에서 처리한다.
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

FMD_MerchantData UMD_MerchantSubsystem::GetMerchantSaveData() const
{
	// 하나의 수집 시점에 속한 상인 원장만 슬롯에 기록하기 위한 값 복사
	FMD_MerchantData SaveData;
	SaveData.bConsumableDiscountActive = bConsumableDiscountActive;
	SaveData.Credits = Credits;
	SaveData.Affinity = Affinity;
	SaveData.CurrentDay = CurrentDay;
	SaveData.NextDepositMultiplier = NextDepositMultiplier;
	SaveData.QuestCompletions = QuestCompletions;
	SaveData.PurchasedItemCounts = PurchasedItemCounts;
	SaveData.CurrentSite = TSoftObjectPtr<UMD_SiteDefinition>(CurrentSite.Get());
	SaveData.MainQuests = MainQuests;
	SaveData.RepeatQuests = RepeatQuests;

	// UObject 강참조 집합을 파일에 안전한 소프트 경로 목록으로 변환하기 위한 처리
	for (const TObjectPtr<UMD_SiteDefinition>& ClearedSite : ClearedSites)
	{
		if (IsValid(ClearedSite) == true)
		{
			SaveData.ClearedSites.Add(TSoftObjectPtr<UMD_SiteDefinition>(ClearedSite.Get()));
		}
	}

	return SaveData;
}

bool UMD_MerchantSubsystem::ApplyMerchantSaveData(const FMD_MerchantData& SaveData)
{
	// 클라이언트 적용과 손상된 경제·퀘스트 상태의 부분 적용을 막기 위한 검사
	if (IsValid(GetWorld()) == false || GetWorld()->GetNetMode() == ENetMode::NM_Client || SaveData.IsStructurallyValid() == false)
	{
		return false;
	}

	// 현재 현장 애셋을 먼저 검증하여 실패 시 기존 서버 원장을 유지하기 위한 선행 로드
	UMD_SiteDefinition* LoadedCurrentSite = SaveData.CurrentSite.IsNull() == true ? nullptr : SaveData.CurrentSite.LoadSynchronous();
	if (SaveData.CurrentSite.IsNull() == false && IsValid(LoadedCurrentSite) == false)
	{
		return false;
	}

	// 클리어 현장 경로를 임시 집합에 모두 구성한 뒤 한 번에 교체하기 위한 처리
	TSet<TObjectPtr<UMD_SiteDefinition>> LoadedClearedSites;
	for (const TSoftObjectPtr<UMD_SiteDefinition>& ClearedSitePath : SaveData.ClearedSites)
	{
		UMD_SiteDefinition* ClearedSite = ClearedSitePath.IsNull() == true ? nullptr : ClearedSitePath.LoadSynchronous();
		if (IsValid(ClearedSite) == false)
		{
			return false;
		}

		LoadedClearedSites.Add(ClearedSite);
	}

	// 모든 참조 검증 이후 경제·호감도·퀘스트 원장을 한 번에 교체하는 처리
	bConsumableDiscountActive = SaveData.bConsumableDiscountActive;
	Credits = SaveData.Credits;
	Affinity = SaveData.Affinity;
	CurrentDay = SaveData.CurrentDay;
	NextDepositMultiplier = SaveData.NextDepositMultiplier;
	QuestCompletions = SaveData.QuestCompletions;
	PurchasedItemCounts = SaveData.PurchasedItemCounts;
	CurrentSite = LoadedCurrentSite;
	MainQuests = SaveData.MainQuests;
	RepeatQuests = SaveData.RepeatQuests;
	ClearedSites = MoveTemp(LoadedClearedSites);
	OnMerchantStateChanged.Broadcast();

	return true;
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
	TArray<FMD_QuestDefinitionData> Pool = CurrentSite->RepeatQuestPool;
	Pool.RemoveAll([this](const FMD_QuestDefinitionData& Entry)
		{
			if (!Entry.IsValidDefinition())
			{
				return true;
			}

			return RepeatQuests.ContainsByPredicate(
				[&Entry](const FMD_RepeatQuestState& Existing)
				{
					return MD_QuestDefinitionsEqual(Existing.Definition, Entry);
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

bool UMD_MerchantSubsystem::DoesDeliveryMatch(
	const FMD_QuestDefinitionData& Definition,
	const FGameplayTag& GradeTag,
	bool bIntact) const
{
	if (!Definition.IsValidDefinition())
	{
		return false;
	}

	const bool bIsRecovery =
		Definition.ObjectiveType == EMD_QuestObjectiveType::RecoverIntact
		|| Definition.ObjectiveType == EMD_QuestObjectiveType::RecoverDamaged;

	if (!bIsRecovery)
	{
		return false;
	}

	if (Definition.ObjectiveType == EMD_QuestObjectiveType::RecoverIntact && !bIntact)
	{
		return false;
	}

	return Definition.TargetGradeTag.MatchesTagExact(GradeTag);
}

void UMD_MerchantSubsystem::FailMainQuest(
	FMD_MainQuestState& QuestState,
	EMD_QuestFailureReason Reason)
{
	if (QuestState.Status != EMD_MainQuestStatus::Active)
	{
		return;
	}

	QuestState.Status = EMD_MainQuestStatus::Failed;
	QuestState.FailureReason = Reason;

	if (QuestState.Definition.IsValidDefinition())
	{
		UE_LOG(LogTemp, Log, TEXT("[Merchant] 메인 퀘스트 실패: %s (%s)"),
			*QuestState.Definition.QuestName.ToString(),
			*StaticEnum<EMD_QuestFailureReason>()->GetNameStringByValue(static_cast<int64>(Reason)));
	}

	// TODO(김동연): `09` §1.5 실패 점수·호감도 감점을 여기서 적용한다.
}

FGameplayTag UMD_MerchantSubsystem::GradeTagFromSampleGrade(EMD_SampleGrade Grade)
{
	const UEnum* GradeEnum = StaticEnum<EMD_SampleGrade>();
	if (!GradeEnum)
	{
		return FGameplayTag();
	}

	const FString ShortName = GradeEnum->GetNameStringByValue(static_cast<int64>(Grade));
	return FGameplayTag::RequestGameplayTag(
		FName(*FString::Printf(TEXT("Resource.Sample.Grade.%s"), *ShortName)),
		/*ErrorIfNotFound=*/false);
}

bool UMD_MerchantSubsystem::DoesQuestTargetGrade(
	const FMD_QuestDefinitionData& Definition,
	const FGameplayTag& GradeTag) const
{
	if (!Definition.IsValidDefinition() || !GradeTag.IsValid())
	{
		return false;
	}

	const bool bIsRecovery =
		Definition.ObjectiveType == EMD_QuestObjectiveType::RecoverIntact
		|| Definition.ObjectiveType == EMD_QuestObjectiveType::RecoverDamaged;

	return bIsRecovery && Definition.TargetGradeTag.MatchesTagExact(GradeTag);
}

void UMD_MerchantSubsystem::ApplyQuestReward(const FMD_QuestDefinitionData& Definition)
{
	if (!Definition.IsValidDefinition())
	{
		return;
	}

	const int32 Payout = (Definition.ObjectiveType == EMD_QuestObjectiveType::RecoverDamaged)
		? Definition.RewardCredits / 2
		: Definition.RewardCredits;

	AddCreditsInternal(Payout);
	++QuestCompletions;

	UE_LOG(LogTemp, Log, TEXT("[Merchant] 퀘스트 완료: %s (+%d, 누적 성공 %d)"),
		*Definition.QuestName.ToString(), Payout, QuestCompletions);
}

void UMD_MerchantSubsystem::ResetContract()
{
	Credits = 0;
	Affinity = 0.0f;
	CurrentSite = nullptr;
	CurrentDay = 0;
	MainQuests.Reset();
	RepeatQuests.Reset();
	ClearedSites.Reset();
	NextDepositMultiplier = 1.f;
	bConsumableDiscountActive = true;
	QuestCompletions = 0;
	PurchasedItemCounts.Reset();
	OnMerchantStateChanged.Broadcast();
}
