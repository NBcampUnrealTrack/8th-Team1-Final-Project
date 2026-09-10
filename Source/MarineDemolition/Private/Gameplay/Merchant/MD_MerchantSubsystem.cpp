// Fill out your copyright notice in the Description page of Project Settings.


#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "GameplayTagContainer.h"
#include "Data/Merchant/MD_QuestDefinition.h"
#include "Data/Merchant/MD_SiteDefinition.h"
#include "Data/Shop/MD_ShopCatalog.h"
#include "Data/Equipment/MD_ItemDefinition.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "Game/MD_GameStateBase.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Player/MD_PlayerState.h"
#include "Types/Score/MD_ScoreTypes.h"
#include "Types/SaveGame/MD_SaveDataTypes.h"
#include "World/Sample/MD_QuestItem.h"
#include "Util/MD_LogUtility.h"


int32 UMD_MerchantSubsystem::GetActiveQuestReward() const
{
	int32 Total = 0;

	for (const FMD_MainQuestState& QuestState : MainQuests)
	{
		if (QuestState.IsActive() == false)
		{
			continue;
		}

		UMD_QuestDefinition* Definition = UMD_QuestDefinition::LoadDefinition(QuestState.DefinitionId);
		if (IsValid(Definition) == true)
		{
			Total += Definition->SuccessReward.Credits;
		}
	}

	for (const FMD_RepeatQuestState& QuestState : RepeatQuests)
	{
		if (QuestState.IsActive() == false)
		{
			continue;
		}

		const UMD_QuestDefinition* Definition = UMD_QuestDefinition::LoadDefinition(QuestState.DefinitionId);
		if (IsValid(Definition) == true)
		{
			Total += Definition->SuccessReward.Credits;
		}
	}

	return Total;
}

TArray<FMD_DailyReportValue> UMD_MerchantSubsystem::GetDailyCreditTotals() const
{
	// 서버 Map을 RPC와 복제가 지원하는 항목 배열로 변환
	TArray<FMD_DailyReportValue> OutCreditTotals;
	OutCreditTotals.Reserve(DailyCreditTotals.Num());
	for (const TPair<EMD_DailyReportCategory, int64>& CreditEntry : DailyCreditTotals)
	{
		int32 CreditValue = static_cast<int32>(FMath::Clamp<int64>(CreditEntry.Value, MIN_int32, MAX_int32));
		OutCreditTotals.Add({CreditEntry.Key, CreditValue});
	}

	return OutCreditTotals;
}

void UMD_MerchantSubsystem::AddCredits(EMD_DailyReportCategory Category, int32 Amount)
{
	// 모든 외부 크레딧 변경을 단일 Enum 기반 기록 경로로 처리
	AddCreditsInternal(Category, Amount);
	OnMerchantStateChanged.Broadcast();
}

EMD_ShopPurchaseResult UMD_MerchantSubsystem::TryPurchaseShopItem(AMD_PlayerState* PurchasingPlayerState, const FMD_ShopItem& ShopItem, TFunctionRef<bool()> FulfillPurchase)
{
	// 클라이언트가 로컬 원장을 변경하지 못하도록 막는 서버 권위 검사
	if (IsValid(PurchasingPlayerState) == false || PurchasingPlayerState->HasAuthority() == false)
	{
		return EMD_ShopPurchaseResult::InvalidRequest;
	}

	// 검증과 구매 기록에서 함께 사용할 전체 식별자 조회
	FPrimaryAssetId ItemAssetId = ShopItem.GetItemAssetId();
	// 검증한 정의를 지급 성공 이후의 소스 조회에도 재사용
	UMD_ItemDefinition* Definition = ShopItem.GetDefinition();
	// 미등록 또는 미로드 정의와 잘못된 가격의 직접 결제 차단
	if (ItemAssetId.IsValid() == false || IsValid(Definition) == false || ShopItem.Price < 0)
	{
		return EMD_ShopPurchaseResult::InvalidItem;
	}

	// 해금과 가격 계산에 사용할 구매 이력을 한 번만 복사
	TArray<FPrimaryAssetId> PurchasedItemIds = PurchasingPlayerState->GetPurchasedShopItemIds();
	// 등록된 구매 금지를 먼저 적용하고 나머지 상점 진행 조건 검사
	if (PurchasingPlayerState->IsShopItemPurchaseBlocked(ItemAssetId) == true
		|| ShopItem.IsUnlocked(Affinity, QuestCompletions, PurchasedItemIds) == false)
	{
		return EMD_ShopPurchaseResult::Locked;
	}

	// 이번 성공으로 이력이 바뀌기 전에 서버 구매 이력으로 결제 금액 확정
	int32 PurchasePrice = ShopItem.GetPurchasePrice(PurchasedItemIds);
	// 무료 재구매를 포함한 실제 금액으로 결제 가능 여부 검사
	if (Credits < PurchasePrice)
	{
		return EMD_ShopPurchaseResult::InsufficientCredits;
	}

	// 결제와 구매 원장을 같은 프레임에 반영하여 부분 성공을 막는 원자적 처리
	if (FulfillPurchase() == false)
	{
		return EMD_ShopPurchaseResult::InvalidRequest;
	}

	// 실제 지급에 성공한 도구의 업그레이드 소스 설정 조회
	UMD_ToolDefinition* PurchasedTool = Cast<UMD_ToolDefinition>(Definition);
	// 성공한 모든 구매의 이력과 유효한 소스 구매 금지를 함께 등록
	if (PurchasingPlayerState->RecordPurchasedShopItem(ItemAssetId,
		IsValid(PurchasedTool) == true ? PurchasedTool->UpgradeSourceAssetId : FPrimaryAssetId()) == false)
	{
		return EMD_ShopPurchaseResult::InvalidRequest;
	}

	// 지급 성공이 보장된 같은 프레임에 결제와 팀 구매 원장을 확정하는 원자적 처리
	// 상점 구매 비용을 기타 정산 분류로 기록
	AddCreditsInternal(EMD_DailyReportCategory::Other, -PurchasePrice);
	// 정의의 전체 AssetId를 사용하여 타입이 다른 구매 이력 구분
	int32& PurchasedCount = PurchasedItemAssetCounts.FindOrAdd(ItemAssetId);
	PurchasedCount += 1;

	// 정의의 전체 AssetId를 사용하여 타입이 다른 구매 이력 구분
	UE_LOG(MarineDemolition, Log, TEXT("[Merchant] 상점 구매 완료: %s (-%d, 잔액 %d, 누적 %d개)"), *ItemAssetId.ToString(), PurchasePrice, Credits, PurchasedCount);

	// GameState 복제 미러와 열려 있는 상점 UI를 함께 갱신하기 위한 변경 알림
	OnMerchantStateChanged.Broadcast();

	return EMD_ShopPurchaseResult::Success;
}

TArray<FPrimaryAssetId> UMD_MerchantSubsystem::GetOwnedItemIds() const
{
	// 팀 구매 원장의 전체 식별자 반환 목록
	TArray<FPrimaryAssetId> OwnedItemIds;
	// 품목별 누적 구매 원장에서 전체 AssetId 추출
	PurchasedItemAssetCounts.GenerateKeyArray(OwnedItemIds);

	// TMap 순회 순서 차이로 복제 배열이 불필요하게 변경되는 상황을 막기 위한 정렬
	OwnedItemIds.Sort([](FPrimaryAssetId Left, FPrimaryAssetId Right)
	{
		// 프로세스가 달라도 동일한 순서를 유지하는 타입 및 이름 비교
		return Left.LexicalLess(Right);
	});
	return OwnedItemIds;
}

int32 UMD_MerchantSubsystem::GetPurchasedItemCount(FPrimaryAssetId ItemAssetId) const
{
	// 타입과 이름이 모두 일치하는 품목의 누적 구매 수량 조회
	const int32* PurchasedCount = PurchasedItemAssetCounts.Find(ItemAssetId);
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

int32 UMD_MerchantSubsystem::GetLoanLimit() const
{
	return CalculateLoanLimit(Affinity);
}

int32 UMD_MerchantSubsystem::CalculateLoanLimit(float InAffinity)
{
	// 높은 호감도 보상과 낮은 호감도 제한을 기획 임계값에 맞춰 구분하는 계산
	float ClampedAffinity = FMath::Clamp(InAffinity, -2.0f, 2.0f);

	float ClampedAffinityFactor = FMath::GetRangePct(-2.0f, 2.0f, ClampedAffinity);

	// 호감도가 -2일 때 최소값(400), 호감도가 2일 때 최대값(1200)
	int32 PotentialLoanLimit = FMath::Lerp(400, 1200, ClampedAffinityFactor);

	// 100 단위로 끊기
	int32 ActualLoanLimit = FMath::RoundToInt32(PotentialLoanLimit * 0.01f) * 100;

	return ActualLoanLimit;
}

int32 UMD_MerchantSubsystem::GetAvailableLoanAmount() const
{
	// 호감도 하락으로 현재 부채가 새 한도를 넘더라도 강제 회수 없이 추가 대출만 차단하는 계산
	return FMath::Max(0, GetLoanLimit() - DebtBalance);
}

bool UMD_MerchantSubsystem::TryBorrowCredits(int32 Amount)
{
	// 잘못된 금액과 현재 호감도 한도를 넘는 대출 요청을 서버 원장에 반영하지 않기 위한 검사
	if (Amount <= 0 || Amount > GetAvailableLoanAmount())
	{
		return false;
	}

	// 플레이어 측 보유 금액 및 빚에 대하여 오버플로우 검사
	if (Credits > MAX_int32 - Amount || DebtBalance > MAX_int32 - Amount)
	{
		return false;
	}

	// 대출 실행
	// 대출 수령액을 기타 정산 분류로 기록
	AddCreditsInternal(EMD_DailyReportCategory::Other, Amount);
	DebtBalance += Amount;

	OnMerchantStateChanged.Broadcast();

	return true;
}

bool UMD_MerchantSubsystem::TryRepayLoan(int32 Amount)
{
	// 실제 부채와 보유 크레딧 범위를 넘는 상환 요청을 거부하여 두 원장의 불일치를 방지하는 검사
	if (Amount <= 0 || Amount > DebtBalance || Amount > Credits)
	{
		return false;
	}

	// 빚 상환 실행
	// 대출 상환액을 기타 정산 분류로 기록
	AddCreditsInternal(EMD_DailyReportCategory::Other, -Amount);
	DebtBalance -= Amount;
	OnMerchantStateChanged.Broadcast();

	return true;
}

int32 UMD_MerchantSubsystem::ApplyDailyLoanInterest()
{
	// 빚이 존재하지 않거나 최대값의 경우 추가되는 이자 없음
	if (DebtBalance <= 0 || DebtBalance == MAX_int32)
	{
		return 0;
	}

	// 기획 예시의 450₡ 대출 이자 23₡과 일치하도록 원 단위 반올림을 적용하는 계산
	float CalculatedInterest = DebtBalance * DailyInterestRate;
	int32 InterestAmount = FMath::Clamp(CalculatedInterest, 1, MAX_int32 - DebtBalance);	// 이자 값 보정
	if (InterestAmount <= 0)
	{
		return 0;
	}

	// 빚에 이자 추가
	DebtBalance += InterestAmount;
	OnMerchantStateChanged.Broadcast();

	// 추가된 이자 값 반환
	return InterestAmount;
}

void UMD_MerchantSubsystem::BeginContract(UMD_SiteDefinition* Site)
{
	if (IsValid(Site) == false)
	{
		return;
	}

	CurrentSite = Site;
	CurrentDay = 0;

	// 이전 계약의 상태가 새 계약으로 이어지지 않도록 초기화한 뒤 현재 현장 기준으로 최초 추첨하는 처리
	RepeatQuests.Reset();
	DrawRepeatQuestsForContract();

	//메인퀘스트 반복 방지
	MainQuests.Reset();
	MainQuests.Reserve(Site->MainQuestLine.Num());

	for (int32 Index = 0; Index < Site->MainQuestLine.Num(); ++Index)
	{
		const FPrimaryAssetId& DefinitionId = Site->MainQuestLine[Index];
		const UMD_QuestDefinition* Definition = UMD_QuestDefinition::LoadDefinition(DefinitionId);
		if (IsValid(Definition) == false || Definition->IsValidDefinition() == false)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Merchant] MainQuestLine[%d]가 비어 있다: %s"), Index, *DefinitionId.ToString());
			continue;
		}

		FMD_MainQuestState NewMain;
		NewMain.DefinitionId = DefinitionId;
		NewMain.UnlockDay = Index + 1;
		NewMain.Status = EMD_MainQuestStatus::Locked;

		MainQuests.Add(NewMain);
	}

	// 새 계약이므로 경고 모디파이어 초기화
	NextDepositMultiplier = 1.f;
	bConsumableDiscountActive = true;

	UE_LOG(LogTemp, Log, TEXT("[Merchant] 계약 시작: %s (메인 %d건, 반복 %d건)"), *Site->SiteName.ToString(), MainQuests.Num(), RepeatQuests.Num());

	OnMerchantStateChanged.Broadcast();
}

bool UMD_MerchantSubsystem::HasClearedCurrentSite() const
{
	return IsValid(CurrentSite) && ClearedSites.Contains(CurrentSite);
}

void UMD_MerchantSubsystem::BeginDay(int32 NewDay, int32 BaseAdvancePayment, bool bEmergencyDay)
{
	// 잘못된 설정값으로 팀 크레딧이 차감되지 않도록 기본 착수금을 0 이상으로 제한
	int32 ValidBaseAdvancePayment = FMath::Max(BaseAdvancePayment, 0);

	// 높은 호감도는 착수금을 10% 늘리고 낮은 호감도는 10% 줄이는 배율 계산
	float AffinityAdvanceMultiplier = FMath::GetRangePct(-2.0f, 2.0f, Affinity);
	AffinityAdvanceMultiplier = 0.9f + 0.2f * FMath::Clamp(AffinityAdvanceMultiplier, 0, 1);
	AffinityAdvanceMultiplier *= bEmergencyDay == true ? 0.7f : 1;

	// 비상 Day에는 호감도와 무관하게 착수금을 지급하지 않는 최종 금액 계산
	int32 GrantedAdvancePayment = FMath::RoundToInt32(ValidBaseAdvancePayment * AffinityAdvanceMultiplier);
	// 새 Day의 Enum Map 원장을 초기화한 뒤 실제 착수금을 기록
	DailyCreditTotals.Reset();
	AddCreditsInternal(EMD_DailyReportCategory::Retainer, GrantedAdvancePayment);

	// 현재 계약의 일차를 서버 상인 원장에 반영
	CurrentDay = NewDay;

	// Day에 도달하면 Locked에서 Active가 된다. 실패·완료 건은 그대로 둔다.
	for (FMD_MainQuestState& MainQuest : MainQuests)
	{
		if (MainQuest.Status == EMD_MainQuestStatus::Locked && MainQuest.UnlockDay <= NewDay)
		{
			MainQuest.Status = EMD_MainQuestStatus::Active;
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[Merchant] Day %d 시작 (기본 착수금 %d, 호감도 %.1f, 비상 %s, 지급 %d, 반복 %d건)"), NewDay, ValidBaseAdvancePayment, Affinity, bEmergencyDay == true ? TEXT("true") : TEXT("false"), GrantedAdvancePayment, RepeatQuests.Num());

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
	if (RepeatQuests.IsValidIndex(QuestIndex) == false)
	{
		return false;
	}

	FMD_RepeatQuestState& QuestState = RepeatQuests[QuestIndex];

	// 수락 상태에서는 원본을 한 번만 로드하여 마지막 목표 완료와 보상 수령을 함께 처리한다.
	if (QuestState.Status == EMD_RepeatQuestStatus::Accepted)
	{
		const UMD_QuestDefinition* Definition = UMD_QuestDefinition::LoadDefinition(QuestState.DefinitionId);
		if (IsValid(Definition) == false)
		{
			return false;
		}

		if (Definition->IsObjectiveComplete(QuestState.CurrentPhaseIndex, QuestState.CurrentProgress) == true)
		{
			// 보상 적용 중 재진입하더라도 동일 퀘스트를 다시 정산하지 않기 위한 선행 완료 확정
			QuestState.Status = EMD_RepeatQuestStatus::Completed;
			ApplyRepeatQuestReward(*Definition);
			OnMerchantStateChanged.Broadcast();
			return true;
		}
	}

	// 제시 상태가 아닌 카드를 다시 수락하거나 목표 달성 전 보상을 적용하지 못하게 하는 검사
	if (QuestState.Status != EMD_RepeatQuestStatus::Offered)
	{
		return false;
	}

	QuestState.Status = EMD_RepeatQuestStatus::Accepted;
	QuestState.CurrentPhaseIndex = 0;
	QuestState.CurrentProgress = 0;

	OnMerchantStateChanged.Broadcast();

	return true;
}

bool UMD_MerchantSubsystem::TryDeclineRepeatQuest(int32 QuestIndex)
{
	if (RepeatQuests.IsValidIndex(QuestIndex) == false)
	{
		return false;
	}

	FMD_RepeatQuestState& QuestState = RepeatQuests[QuestIndex];
	if (QuestState.Status != EMD_RepeatQuestStatus::Offered)
	{
		return false;
	}

	// 거절한 카드는 현재 계약이 끝날 때까지 유지하며 별도 페널티를 적용하지 않는 처리
	QuestState.Status = EMD_RepeatQuestStatus::Declined;

	OnMerchantStateChanged.Broadcast();

	return true;
}

bool UMD_MerchantSubsystem::TryAbandonRepeatQuest(int32 QuestIndex)
{
	if (RepeatQuests.IsValidIndex(QuestIndex) == false)
	{
		return false;
	}

	// 포기하려는 퀘스트
	FMD_RepeatQuestState& QuestState = RepeatQuests[QuestIndex];

	// 수락 상태가 아니거나 이미 목표를 달성한 퀘스트의 포기와 패널티 적용을 막는 검사
	if (QuestState.Status != EMD_RepeatQuestStatus::Accepted || IsRepeatQuestObjectiveComplete(QuestState) == true)
	{
		return false;
	}

	// 포기는 미회수와 동일한 에셋 패널티를 적용하는 실패 처리
	FailRepeatQuest(QuestState, false);

	// 퀘스트 상태 변경으로 인한 상인 업데이트 이벤트 호출
	OnMerchantStateChanged.Broadcast();

	return true;
}

bool UMD_MerchantSubsystem::TryDeliverQuestItem(AMD_QuestItem* SubmittedItem)
{
	bool bOutDelivered = false;
	bool bCanDeliver = IsValid(SubmittedItem) == true && SubmittedItem->HasAuthority() == true && SubmittedItem->IsActorBeingDestroyed() == false;
	if (bCanDeliver == false)
	{
		return bOutDelivered;
	}

	/** 아이템 제거가 확정된 뒤 진행도를 반영하기 위한 사전 판정 결과 */
	struct FAcceptedQuestDelivery
	{
		/** 상태 배열 위치 */
		int32 QuestIndex = INDEX_NONE;

		/** 제출 조건 판정에 사용한 퀘스트 원본 */
		UMD_QuestDefinition* Definition = nullptr;
	};

	FPrimaryAssetId QuestDefinitionId = SubmittedItem->QuestDefinitionId;
	TArray<FAcceptedQuestDelivery> AcceptedMainDeliveries;
	TArray<FAcceptedQuestDelivery> AcceptedRepeatDeliveries;

	for (int32 QuestIndex = 0; QuestIndex < MainQuests.Num(); ++QuestIndex)
	{
		FMD_MainQuestState& QuestState = MainQuests[QuestIndex];
		if (QuestState.IsActive() == false || QuestState.DefinitionId != QuestDefinitionId)
		{
			continue;
		}

		UMD_QuestDefinition* Definition = UMD_QuestDefinition::LoadDefinition(QuestState.DefinitionId);
		// 포인터 검증 이후에만 현재 단계와 납품 조건을 순서대로 평가하기 위한 결과
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

		bool bAcceptsQuestItem = Definition->AcceptsQuestItem(QuestState.CurrentPhaseIndex, SubmittedItem);
		if (bAcceptsQuestItem == false)
		{
			continue;
		}

		AcceptedMainDeliveries.Add({ QuestIndex, Definition });
	}

	for (int32 QuestIndex = 0; QuestIndex < RepeatQuests.Num(); ++QuestIndex)
	{
		FMD_RepeatQuestState& QuestState = RepeatQuests[QuestIndex];
		if (QuestState.IsActive() == false || QuestState.DefinitionId != QuestDefinitionId)
		{
			continue;
		}

		UMD_QuestDefinition* Definition = GetRepeatQuestDefinition(QuestState);
		// 유효 원본만 단계 완료와 샘플 수락 여부를 순서대로 평가하기 위한 결과
		bool bIsDefinitionValid = IsValid(Definition);
		if (bIsDefinitionValid == false)
		{
			continue;
		}

		bool bIsObjectiveComplete = Definition->IsObjectiveComplete(QuestState.CurrentPhaseIndex, QuestState.CurrentProgress);
		if (bIsObjectiveComplete == true)
		{
			continue;
		}

		bool bAcceptsQuestItem = Definition->AcceptsQuestItem(QuestState.CurrentPhaseIndex, SubmittedItem);
		if (bAcceptsQuestItem == false)
		{
			continue;
		}

		AcceptedRepeatDeliveries.Add({ QuestIndex, Definition });
	}

	// 제출 액터 제거 실패 시 사전 판정 결과를 적용하지 않는 처리
	bOutDelivered = SubmittedItem->Destroy();
	if (bOutDelivered == false)
	{
		return bOutDelivered;
	}

	bool bChanged = false;
	for (FAcceptedQuestDelivery& Delivery : AcceptedMainDeliveries)
	{
		if (MainQuests.IsValidIndex(Delivery.QuestIndex) == false || IsValid(Delivery.Definition) == false)
		{
			continue;
		}

		FMD_MainQuestState& QuestState = MainQuests[Delivery.QuestIndex];
		UMD_QuestDefinition* Definition = Delivery.Definition;
		++QuestState.CurrentProgress;
		bChanged = true;

		int32 RequiredCount = Definition->GetRequiredCount(QuestState.CurrentPhaseIndex);
		if (QuestState.CurrentProgress >= RequiredCount)
		{
			// 다단계 퀘스트의 경우 순서대로 진행하기 위한 전환
			bool bHasNextPhase = Definition->HasPhase(QuestState.CurrentPhaseIndex + 1);
			if (bHasNextPhase == true)
			{
				++QuestState.CurrentPhaseIndex;
				QuestState.CurrentProgress = 0;
			}
			else
			{
				QuestState.Status = EMD_MainQuestStatus::Completed;
				ApplyMainQuestReward(*Definition);
			}
		}
	}

	for (FAcceptedQuestDelivery& Delivery : AcceptedRepeatDeliveries)
	{
		if (RepeatQuests.IsValidIndex(Delivery.QuestIndex) == false || IsValid(Delivery.Definition) == false)
		{
			continue;
		}

		FMD_RepeatQuestState& QuestState = RepeatQuests[Delivery.QuestIndex];
		UMD_QuestDefinition* Definition = Delivery.Definition;
		int32 RequiredCount = FMath::Max(Definition->GetRequiredCount(QuestState.CurrentPhaseIndex), 1);
		QuestState.CurrentProgress = FMath::Min(QuestState.CurrentProgress + 1, RequiredCount);
		bChanged = true;

		if (QuestState.CurrentProgress >= RequiredCount)
		{
			// 다음 페이즈가 있으면 진행도를 초기화하고 마지막 페이즈에서는 수락 상태와 최대 진행도를 유지하는 처리
			bool bHasNextPhase = Definition->HasPhase(QuestState.CurrentPhaseIndex + 1);
			if (bHasNextPhase == true)
			{
				++QuestState.CurrentPhaseIndex;
				QuestState.CurrentProgress = 0;
			}
		}
	}

	// 아무 퀘스트도 반응하지 않았으면 UI를 갱신할 이유가 없다.
	if (bChanged == true)
	{
		OnMerchantStateChanged.Broadcast();
	}
	return bOutDelivered;
}

void UMD_MerchantSubsystem::NotifySampleDestroyed(const FPrimaryAssetId& QuestDefinitionId, EMD_SampleGrade Grade)
{
	FGameplayTag GradeTag = GradeTagFromSampleGrade(Grade);
	if (GradeTag.IsValid() == false)
	{
		return;
	}

	bool bChanged = false;

	for (FMD_MainQuestState& QuestState : MainQuests)
	{
		// 파괴된 샘플과 직접 연결된 활성 메인 퀘스트만 실패 대상으로 인정하는 식별자 검사
		if (QuestState.IsActive() == false || QuestState.DefinitionId != QuestDefinitionId)
		{
			continue;
		}

		// 메인 퀘스트의 현재 단계와 파괴된 목표를 비교하기 위해 공통 Definition을 복원한다.
		UMD_QuestDefinition* Definition = UMD_QuestDefinition::LoadDefinition(QuestState.DefinitionId);
		// 유효한 Definition만 목표 일치 여부를 판정하도록 복원 결과를 확인한다.
		bool bIsDefinitionValid = IsValid(Definition);
		if (bIsDefinitionValid == false)
		{
			continue;
		}

		bool bTargetsQuestItem = Definition->TargetsQuestItem(QuestState.CurrentPhaseIndex, GradeTag);
		if (bTargetsQuestItem == false)
		{
			continue;
		}

		// 유효한 Definition의 파괴 Penalty가 실제 적용된 경우에만 변경 상태를 기록하는 처리
		bool bQuestFailed = FailMainQuest(QuestState, EMD_QuestFailureReason::Destroyed);
		if (bQuestFailed == true)
		{
			bChanged = true;
		}
	}

	for (FMD_RepeatQuestState& QuestState : RepeatQuests)
	{
		if (QuestState.IsActive() == false || QuestState.DefinitionId != QuestDefinitionId)
		{
			continue;
		}

		UMD_QuestDefinition* Definition = GetRepeatQuestDefinition(QuestState);
		// 유효 원본만 완료 보호와 파괴 대상 수락 여부를 순서대로 평가하기 위한 결과
		bool bIsDefinitionValid = IsValid(Definition);
		if (bIsDefinitionValid == false)
		{
			continue;
		}

		bool bIsObjectiveComplete = Definition->IsObjectiveComplete(QuestState.CurrentPhaseIndex, QuestState.CurrentProgress);
		if (bIsObjectiveComplete == true)
		{
			continue;
		}

		bool bTargetsQuestItem = Definition->TargetsQuestItem(QuestState.CurrentPhaseIndex, GradeTag);
		if (bTargetsQuestItem == false)
		{
			continue;
		}

		FailRepeatQuest(QuestState, true);
		bChanged = true;
	}

	if (bChanged == true)
	{
		OnMerchantStateChanged.Broadcast();
	}
}

/**
 * Day 마감 시 해당 Day의 미완료 메인 퀘스트를 실패로 확정한다.
 *
 * 메인 퀘스트는 Day별 고정 라인이라 **다음날 재시도가 없다.** 마감 시점에 완료되지
 * 않았으면 그대로 실패다. 기획 09 §1.5의 "미회수 … 다음날 재시도 가능"은 파괴되지
 * 않은 **샘플 액터가 맵에 남는다**는 뜻이지(같은 표 "미회수 샘플" 행), 그 Day의 메인
 * 퀘스트가 다음날로 이월된다는 뜻이 아니다. 남은 샘플은 이후 Day의 퀘스트 조건을
 * 만족시킬 때 쓰인다. 반복 퀘스트는 계약 단위이므로 이 함수에서 변경하지 않는다.
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

		// 유효한 Definition의 미회수 Penalty가 실제 적용된 경우에만 변경 상태를 기록하는 처리
		bool bQuestFailed = FailMainQuest(QuestState, EMD_QuestFailureReason::NotDelivered);
		if (bQuestFailed == true)
		{
			bChanged = true;
		}
	}

	if (bChanged == true)
	{
		UE_LOG(LogTemp, Log, TEXT("[Merchant] Day %d 마감 — 미완료 메인 퀘스트 실패 처리"), CompletedDay);
		OnMerchantStateChanged.Broadcast();
	}
}

void UMD_MerchantSubsystem::FinalizeContract()
{
	// 미완료 반복만 실패로 정리하고 메인은 Day 마감 결과를 유지하기 위한 계약 정산
	for (FMD_RepeatQuestState& QuestState : RepeatQuests)
	{
		if (QuestState.IsActive() == true && IsRepeatQuestObjectiveComplete(QuestState) == false)
		{
			// 계약 종료 미회수도 에셋에 정의된 미회수 패널티를 적용하는 실패 처리
			FailRepeatQuest(QuestState, false);
		}
	}

	// 같은 Site에 재진입해도 새 계약으로 판정하여 반복 퀘스트를 다시 추첨하기 위한 활성 계약 해제
	CurrentSite = nullptr;
	CurrentDay = 0;

	UE_LOG(LogTemp, Log, TEXT("[Merchant] 계약 종료"));

	OnMerchantStateChanged.Broadcast();
}

FMD_MerchantData UMD_MerchantSubsystem::GetMerchantSaveData() const
{
	// 하나의 수집 시점에 속한 상인 원장만 슬롯에 기록하기 위한 값 복사
	FMD_MerchantData SaveData;
	SaveData.bConsumableDiscountActive = bConsumableDiscountActive;
	SaveData.Credits = Credits;
	SaveData.DebtBalance = DebtBalance;
	SaveData.Affinity = Affinity;
	SaveData.CurrentDay = CurrentDay;
	SaveData.NextDepositMultiplier = NextDepositMultiplier;
	SaveData.QuestCompletions = QuestCompletions;
	// 전체 AssetId별 누적 구매 수량을 현재 세이브 형식으로 복사
	SaveData.PurchasedItemAssetCounts = PurchasedItemAssetCounts;
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

	// 저장된 PrimaryAssetId가 실제 메인 퀘스트 에셋에서 복원 가능한지 확인하는 검사
	for (const FMD_MainQuestState& MainQuest : SaveData.MainQuests)
	{
		const UMD_QuestDefinition* Definition = UMD_QuestDefinition::LoadDefinition(MainQuest.DefinitionId);
		// 원본 포인터부터 단계까지 기존 단락 평가 순서로 검증하기 위한 결과
		bool bIsDefinitionValid = IsValid(Definition);
		if (bIsDefinitionValid == false)
		{
			return false;
		}

		bool bIsDefinitionDataValid = Definition->IsValidDefinition();
		if (bIsDefinitionDataValid == false)
		{
			return false;
		}

		bool bHasCurrentPhase = Definition->HasPhase(MainQuest.CurrentPhaseIndex);
		if (bHasCurrentPhase == false)
		{
			return false;
		}
	}

	// 저장된 PrimaryAssetId와 페이즈 인덱스가 실제 반복 퀘스트 에셋에서 복원 가능한지 확인하는 검사
	for (const FMD_RepeatQuestState& RepeatQuest : SaveData.RepeatQuests)
	{
		const UMD_QuestDefinition* Definition = UMD_QuestDefinition::LoadDefinition(RepeatQuest.DefinitionId);
		bool bIsDefinitionValid = IsValid(Definition);
		if (bIsDefinitionValid == false)
		{
			return false;
		}

		bool bHasCurrentPhase = Definition->HasPhase(RepeatQuest.CurrentPhaseIndex);
		if (bHasCurrentPhase == false)
		{
			return false;
		}
	}

	// 모든 참조 검증 이후 경제·호감도·퀘스트 원장을 한 번에 교체하는 처리
	bConsumableDiscountActive = SaveData.bConsumableDiscountActive;
	Credits = SaveData.Credits;
	DailyCreditTotals.Reset();
	DebtBalance = SaveData.DebtBalance;
	Affinity = SaveData.Affinity;
	CurrentDay = SaveData.CurrentDay;
	NextDepositMultiplier = SaveData.NextDepositMultiplier;
	QuestCompletions = SaveData.QuestCompletions;
	// 검증된 저장 원장의 전체 AssetId별 구매 수량 복원
	PurchasedItemAssetCounts = SaveData.PurchasedItemAssetCounts;
	CurrentSite = LoadedCurrentSite;
	MainQuests = SaveData.MainQuests;
	RepeatQuests = SaveData.RepeatQuests;

	// 실패 처리된 반복 퀘스트를 다시 수락할 수 있는 상태로 복구
	for (FMD_RepeatQuestState& RepeatQuest : RepeatQuests)
	{
		if (RepeatQuest.Status == EMD_RepeatQuestStatus::Failed)
		{
			RepeatQuest.Status = EMD_RepeatQuestStatus::Offered;
			RepeatQuest.CurrentPhaseIndex = 0;
			RepeatQuest.CurrentProgress = 0;
		}
	}

	ClearedSites = MoveTemp(LoadedClearedSites);
	OnMerchantStateChanged.Broadcast();

	return true;
}

void UMD_MerchantSubsystem::DrawRepeatQuestsForContract()
{
	// 최초 진입을 포함해 현재 계약 현장의 반복 퀘스트 풀을 즉시 사용할 수 있는지 확인하는 검사
	bool bIsCurrentSiteValid = IsValid(CurrentSite);
	if (bIsCurrentSiteValid == false)
	{
		return;
	}

	// 잘못된 ID와 목표가 없는 에셋을 후보에서 제외하여 유효한 계약 카드만 제공하는 처리
	TArray<FPrimaryAssetId> Pool = CurrentSite->RepeatQuestPool;
	Pool.RemoveAll([](const FPrimaryAssetId& Entry)
		{
			if (Entry.IsValid() == false)
			{
				return true;
			}

			const UMD_QuestDefinition* Definition = UMD_QuestDefinition::LoadDefinition(Entry);
			bool bRemoveEntry = IsValid(Definition) == false || Definition->IsValidDefinition() == false;
			return bRemoveEntry;
		});

	// 에셋의 최소·최대 입력 순서가 뒤바뀌어도 안전하게 추첨하기 위한 범위 보정
	const int32 MinCount = FMath::Min(CurrentSite->MinRepeatCount, CurrentSite->MaxRepeatCount);
	const int32 MaxCount = FMath::Max(CurrentSite->MinRepeatCount, CurrentSite->MaxRepeatCount);

	const int32 TargetCount = FMath::Min(FMath::RandRange(MinCount, MaxCount), Pool.Num());

	// 동일 에셋의 중복 추첨을 막기 위해 선택한 후보를 풀에서 즉시 제거하는 처리
	while (RepeatQuests.Num() < TargetCount && Pool.Num() > 0)
	{
		const int32 PickIndex = FMath::RandRange(0, Pool.Num() - 1);

		FMD_RepeatQuestState NewQuest;
		NewQuest.DefinitionId = Pool[PickIndex];
		NewQuest.Status = EMD_RepeatQuestStatus::Offered;
		RepeatQuests.Add(NewQuest);

		Pool.RemoveAtSwap(PickIndex);
	}
}

UMD_QuestDefinition* UMD_MerchantSubsystem::GetRepeatQuestDefinition(const FMD_RepeatQuestState& QuestState) const
{
	// 복제·저장 상태에는 역할과 진행 상태만 두고 공통 원본 데이터는 Primary Data Asset에서 읽는 처리
	UMD_QuestDefinition* OutDefinition = UMD_QuestDefinition::LoadDefinition(QuestState.DefinitionId);
	bool bIsDefinitionValid = IsValid(OutDefinition);
	if (bIsDefinitionValid == true)
	{
		bool bHasCurrentPhase = OutDefinition->HasPhase(QuestState.CurrentPhaseIndex);
		if (bHasCurrentPhase == false)
		{
			OutDefinition = nullptr;
		}
	}
	return OutDefinition;
}

bool UMD_MerchantSubsystem::IsRepeatQuestObjectiveComplete(const FMD_RepeatQuestState& QuestState) const
{
	const UMD_QuestDefinition* Definition = UMD_QuestDefinition::LoadDefinition(QuestState.DefinitionId);
	bool bOutComplete = IsValid(Definition) == true && Definition->IsObjectiveComplete(QuestState.CurrentPhaseIndex, QuestState.CurrentProgress) == true;
	return bOutComplete;
}

void UMD_MerchantSubsystem::ApplyQuestRewardData(const FMD_QuestRewardData& RewardData, bool bPenalty)
{
	// 잘못된 음수와 비정상 값이 적용 방향을 뒤집거나 원장을 오염시키지 않도록 하는 정산값 검증
	int32 CreditsAmount = FMath::Max(RewardData.Credits, 0);
	int32 ScoreAmount = FMath::Max(RewardData.Score, 0);
	float AffinityAmount = FMath::IsFinite(RewardData.Affinity) == true ? FMath::Max(RewardData.Affinity, 0.0f) : 0.0f;

	// 동일한 Definition 데이터를 성공은 가산하고 실패는 차감하기 위한 방향 변환
	int32 CreditsDelta = bPenalty == true ? -CreditsAmount : CreditsAmount;
	int32 ScoreDelta = bPenalty == true ? -ScoreAmount : ScoreAmount;
	float AffinityDelta = bPenalty == true ? -AffinityAmount : AffinityAmount;
	// 퀘스트 보상과 페널티를 실제 반영된 금액 기준으로 상세 보고서에 기록
	AddCreditsInternal(EMD_DailyReportCategory::Quest, CreditsDelta);

	// QuestDefinition에 명시된 점수를 성공·실패 원인 태그와 함께 서버 권위 평가 원장에 반영하는 처리
	AMD_GameStateBase* GameState = IsValid(GetWorld()) == true ? GetWorld()->GetGameState<AMD_GameStateBase>() : nullptr;
	if (IsValid(GameState) == true && ScoreAmount > 0)
	{
		FGameplayTag ScoreEventTag = bPenalty == true ? MDGameplayTags::Score_Penalty_Quest : MDGameplayTags::Score_Daily_Quest;
		GameState->AddScoreEvent(ScoreEventTag, ScoreDelta, true);
	}

	// QuestDefinition에 명시된 호감도만 상점 해금 범위 안에서 반영하는 처리
	Affinity = FMath::Clamp(Affinity + AffinityDelta, -4.0f, 5.0f);
}

void UMD_MerchantSubsystem::ApplyRepeatQuestReward(const UMD_QuestDefinition& Definition)
{
	// 반복 퀘스트 Definition의 성공 Reward를 공통 정산 경로로 적용하는 처리
	ApplyQuestRewardData(Definition.SuccessReward, false);
	++QuestCompletions;

	UE_LOG(LogTemp, Log, TEXT("[Merchant] 반복 퀘스트 완료: %s (크레딧 +%d | 점수 +%d | 호감도 +%.1f, 누적 성공 %d)"), *Definition.QuestName.ToString(), Definition.SuccessReward.Credits, Definition.SuccessReward.Score, Definition.SuccessReward.Affinity, QuestCompletions);
}

void UMD_MerchantSubsystem::ApplyRepeatQuestPenalty(const UMD_QuestDefinition& Definition, bool bDestroyed)
{
	// 파괴 여부에 따라 에셋이 구분한 미회수 또는 파괴 패널티를 선택하는 처리
	const FMD_QuestRewardData& Penalty = bDestroyed == true ? Definition.DestroyedFailureReward : Definition.UnrecoveredFailurePenalty;
	ApplyQuestRewardData(Penalty, true);

	UE_LOG(LogTemp, Log, TEXT("[Merchant] 반복 퀘스트 실패: %s (%s, 크레딧 -%d | 점수 -%d | 호감도 -%.1f)"), *Definition.QuestName.ToString(), bDestroyed == true ? TEXT("파괴") : TEXT("미회수"), Penalty.Credits, Penalty.Score, Penalty.Affinity);
}

void UMD_MerchantSubsystem::FailRepeatQuest(FMD_RepeatQuestState& QuestState, bool bDestroyed)
{
	if (QuestState.IsActive() == false)
	{
		return;
	}

	// 완료 보호와 실패 패널티에 같은 원본을 재사용하여 중복 Asset Manager 조회를 피한다.
	const UMD_QuestDefinition* Definition = UMD_QuestDefinition::LoadDefinition(QuestState.DefinitionId);
	if (IsValid(Definition) == true && Definition->IsObjectiveComplete(QuestState.CurrentPhaseIndex, QuestState.CurrentProgress) == true)
	{
		return;
	}

	// 패널티 적용 중 재진입하더라도 동일 퀘스트를 다시 정산하지 않기 위한 선행 실패 확정
	QuestState.Status = EMD_RepeatQuestStatus::Failed;

	if (IsValid(Definition) == true)
	{
		ApplyRepeatQuestPenalty(*Definition, bDestroyed);
	}
}

void UMD_MerchantSubsystem::AddCreditsInternal(EMD_DailyReportCategory Category, int32 Amount)
{
	if (Amount == 0)
	{
		return;
	}

	// 실제 잔액 반영액만 Map 원장에 기록하기 위한 변경 전 잔액 저장
	int32 CreditsBeforeChange = Credits;

	// int32 범위 안에서 여러 보상의 합산 상한과 차감 하한을 지키기 위한 원장 보호
	if (Amount > 0 && Credits > MAX_int32 - Amount)
	{
		Credits = MAX_int32;
	}
	else if (Amount < 0 && Amount < -Credits)
	{
		Credits = 0;
	}
	else
	{
		Credits += Amount;
	}

	// 요청액이 아닌 실제 잔액 변동을 Enum 분류의 Map 합계에 누적
	int32 AppliedAmount = Credits - CreditsBeforeChange;
	int64& CategoryTotal = DailyCreditTotals.FindOrAdd(Category);
	CategoryTotal += AppliedAmount;
}

bool UMD_MerchantSubsystem::FailMainQuest(FMD_MainQuestState& QuestState, EMD_QuestFailureReason Reason)
{
	bool bOutFailed = false;

	// 완료되거나 이미 실패한 퀘스트에 Penalty를 중복 적용하지 않기 위한 상태 검사
	if (QuestState.Status != EMD_MainQuestStatus::Active)
	{
		return bOutFailed;
	}

	// 상태 변경 전에 통합된 퀘스트 Definition과 실패 원인을 검증하기 위해 원본 데이터를 복원한다.
	UMD_QuestDefinition* Definition = UMD_QuestDefinition::LoadDefinition(QuestState.DefinitionId);
	if (IsValid(Definition) == false || Definition->IsValidDefinition() == false)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Merchant] 원본 Definition이 유효하지 않아 메인 퀘스트 실패 처리를 건너뜁니다."));
		return bOutFailed;
	}

	bool bSupportedFailureReason = Reason == EMD_QuestFailureReason::NotDelivered || Reason == EMD_QuestFailureReason::Destroyed;
	if (bSupportedFailureReason == false)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Merchant] 지원하지 않는 메인 퀘스트 실패 원인을 건너뜁니다: %s"), *StaticEnum<EMD_QuestFailureReason>()->GetNameStringByValue(static_cast<int64>(Reason)));
		return bOutFailed;
	}

	// 검증을 통과한 퀘스트만 실패 상태로 전환하고 원인별 Definition Penalty를 한 번 적용하는 처리
	QuestState.Status = EMD_MainQuestStatus::Failed;
	QuestState.FailureReason = Reason;
	const FMD_QuestRewardData& Penalty = Reason == EMD_QuestFailureReason::Destroyed ? Definition->DestroyedFailureReward : Definition->UnrecoveredFailurePenalty;
	ApplyQuestRewardData(Penalty, true);
	bOutFailed = true;

	UE_LOG(LogTemp, Log, TEXT("[Merchant] 메인 퀘스트 실패: %s (%s, 크레딧 -%d | 점수 -%d | 호감도 -%.1f)"), *Definition->QuestName.ToString(), *StaticEnum<EMD_QuestFailureReason>()->GetNameStringByValue(static_cast<int64>(Reason)), Penalty.Credits, Penalty.Score, Penalty.Affinity);

	return bOutFailed;
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

void UMD_MerchantSubsystem::ApplyMainQuestReward(const UMD_QuestDefinition& Definition)
{
	if (Definition.IsValidDefinition() == false)
	{
		return;
	}

	// 메인 퀘스트 Definition의 성공 Reward를 손상 비율 등의 별도 계산 없이 적용하는 처리
	ApplyQuestRewardData(Definition.SuccessReward, false);
	++QuestCompletions;

	UE_LOG(LogTemp, Log, TEXT("[Merchant] 메인 퀘스트 완료: %s (크레딧 +%d | 점수 +%d | 호감도 +%.1f, 누적 성공 %d)"), *Definition.QuestName.ToString(), Definition.SuccessReward.Credits, Definition.SuccessReward.Score, Definition.SuccessReward.Affinity, QuestCompletions);
}

void UMD_MerchantSubsystem::ResetContract()
{
	Credits = 0;
	DailyCreditTotals.Reset();
	DebtBalance = 0;
	Affinity = 0.0f;
	CurrentSite = nullptr;
	CurrentDay = 0;
	MainQuests.Reset();
	RepeatQuests.Reset();
	ClearedSites.Reset();
	NextDepositMultiplier = 1.f;
	bConsumableDiscountActive = true;
	QuestCompletions = 0;
	// 새 계약에 사용할 전체 AssetId 구매 원장 초기화
	PurchasedItemAssetCounts.Reset();
	OnMerchantStateChanged.Broadcast();
}
