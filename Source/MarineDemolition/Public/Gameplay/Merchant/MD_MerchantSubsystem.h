// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Types/Merchant/MD_MerchantTypes.h"
#include "Types/Score/MD_ScoreTypes.h"
#include "MD_MerchantSubsystem.generated.h"

/**
 * DayPersistenceSubsystem과 같은 패턴으로, Day전환해도 계약3일동안 크레딧과 퀘스트가 유지되어야하므로
 * Gameinstance에 수명을 둠. 값 변경 처리는 서버에서 하고 클라 복제는 GameState에서 담당
 */


DECLARE_MULTICAST_DELEGATE(FMD_OnMerchantStateChanged);

class UMD_SiteDefinition;
class UMD_QuestDefinition;
class AMD_PlayerState;
class AMD_QuestItem;
struct FGameplayTag;
struct FMD_MerchantData;
struct FMD_QuestRewardData;
struct FMD_RepeatQuestPhaseData;
struct FMD_ShopItem;

enum class EMD_ShopPurchaseResult : uint8;
enum class EMD_ShopSaleResult : uint8;

UCLASS()
class MARINEDEMOLITION_API UMD_MerchantSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
	
public:

	FMD_OnMerchantStateChanged OnMerchantStateChanged;

protected:
	// 아이템 변경 알림에서 중첩 구매와 판매가 실행되는 상황 차단
	bool bShopTransactionInProgress = false;

public:

	int32 GetCredits() const { return Credits; }
	// 현재 Day의 Enum Map 크레딧 원장을 네트워크 전달용 항목 배열로 반환하는 함수
	TArray<FMD_DailyReportValue> GetDailyCreditTotals() const;
	int32 GetCurrentDay() const { return CurrentDay; }
	// 상점 해금과 대화 분기에 사용하는 현재 호감도를 반환하는 함수
	float GetAffinity() const { return Affinity; }

	/** BeginContract 이후 현장 계약이 살아 있는지. GameInstance 수명 동안 유지된다. */
	bool HasActiveContract() const { return CurrentSite != nullptr; }

	/** 현재 맵에 맞는 현장 데이터와 기존 계약을 비교하기 위한 활성 현장 반환 함수 */
	UMD_SiteDefinition* GetCurrentSite() const
	{
		// GameInstance 수명 동안 유지되는 현재 현장 참조
		UMD_SiteDefinition* OutSite = CurrentSite.Get();
		return OutSite;
	}

	//상점 해금 판정에 넘길 값
	int32 GetQuestCompletions() const { return QuestCompletions; }
	//수락 퀘스트 보수
	int32 GetActiveQuestReward() const;

	// Enum 분류와 금액을 잔액 및 현재 Day Map 원장에 함께 기록하는 단일 함수
	void AddCredits(EMD_DailyReportCategory Category, int32 Amount);

	/** 플레이어별 소유권을 검증하고 아이템 지급 성공 시에만 팀 재화 차감과 구매 원장을 확정하는 서버 구매 함수 */
	EMD_ShopPurchaseResult TryPurchaseShopItem(AMD_PlayerState* PurchasingPlayerState, const FMD_ShopItem& ShopItem, TFunctionRef<bool()> FulfillPurchase);

	// 서버 소지 슬롯을 회수한 경우에만 팀 크레딧과 일일 원장에 판매 대금 적립
	EMD_ShopSaleResult TrySellShopItem(AMD_PlayerState* SellingPlayerState, FMD_ShopItem ShopItem, int32 SlotIndex, int32 Quantity);

	/** 상점 해금과 UI 갱신에 사용할 구매 완료 품목 식별자 목록 */
	TArray<FPrimaryAssetId> GetOwnedItemIds() const;

	/** 소모품을 포함한 품목별 누적 구매 횟수 */
	int32 GetPurchasedItemCount(FPrimaryAssetId ItemAssetId) const;

	// 호감도를 기획 범위 안에서 증감하고 복제 미러 갱신을 요청하는 함수
	void AddAffinity(float Amount);

	// 현재 미상환 대출 잔액을 반환하는 함수
	int32 GetDebtBalance() const { return DebtBalance; }

	// 현재 호감도 구간에 맞는 대출 한도를 반환하는 함수
	int32 GetLoanLimit() const;

	// 서버 원장과 클라이언트 복제 UI가 동일한 호감도 구간별 한도를 사용하기 위한 계산 함수
	static int32 CalculateLoanLimit(float InAffinity);

	// 현재 부채를 제외하고 추가로 빌릴 수 있는 금액을 반환하는 함수
	int32 GetAvailableLoanAmount() const;

	// 기획에서 확정한 일일 대출 이자율을 반환하는 함수
	float GetDailyInterestRate() const { return DailyInterestRate; }

	// 요청 금액과 현재 한도를 검증한 뒤 크레딧과 부채를 함께 증가시키는 함수
	bool TryBorrowCredits(int32 Amount);

	// 보유 크레딧과 미상환 잔액 범위에서 대출금을 상환하는 함수
	bool TryRepayLoan(int32 Amount);

	// 일 마감 시 미상환 잔액에 일일 이자를 한 번 합산하고 부과액을 반환하는 함수
	int32 ApplyDailyLoanInterest();

	// 새 계약의 퀘스트 상태를 구성하고 반복 퀘스트를 최초 추첨하는 함수
	void BeginContract(UMD_SiteDefinition* Site);

	// 게임오버 시 전체 리셋
	void ResetContract();

	// 다음 정규일 착수금 배율 (경고 0.7 / 비상 0). 착수금 지급 지점에서 소비
	void SetNextDepositMultiplier(float Multiplier) { NextDepositMultiplier = Multiplier; }
	float GetNextDepositMultiplier() const { return NextDepositMultiplier; }

	// 소모품 할인 활성 여부 (경고 시 해제). 상점에서 조회
	void SetConsumableDiscountActive(bool bActive) { bConsumableDiscountActive = bActive; }
	bool IsConsumableDiscountActive() const { return bConsumableDiscountActive; }

	const TArray<FMD_MainQuestState>& GetMainQuests() const { return MainQuests; }
	const TArray<FMD_RepeatQuestState>& GetRepeatQuests() const { return RepeatQuests; }

	/** 현재 계약 현장이 과거에 클리어된 적 있는지 저장 이력에서 확인하는 함수 */
	bool HasClearedCurrentSite() const;

	/** Day 시작 시 호감도·비상 상태를 반영한 착수금을 지급하고 메인 퀘스트의 해금 상태를 갱신하는 함수 */
	void BeginDay(int32 NewDay, int32 BaseAdvancePayment, bool bEmergencyDay);

	/** 계약 클리어 시 호출하여 현장 완료 이력을 남기는 함수 */
	void MarkSiteCleared();

	/** 제시 상태에서는 반복 퀘스트를 수락하고 목표를 달성한 수락 상태에서는 상인 메뉴 보상을 수령하는 함수 */
	bool TryAcceptRepeatQuest(int32 QuestIndex);

	/** 제시된 반복 퀘스트를 페널티 없이 거절 상태로 전환하는 함수 */
	bool TryDeclineRepeatQuest(int32 QuestIndex);

	/** 포기하는 반복 퀘스트를 실패 처리 및 패널티(호감도 감소) 처리하는 함수 */
	bool TryAbandonRepeatQuest(int32 QuestIndex);

	/**
	 * Interaction 페이즈의 선행 조건(카드 확보 등)을 충족시키기 위해 진행도를 올린다.
	 * 레벨 내 카드 액터 상호작용 시 서버에서 호출한다.
	 * 페이즈 전환은 하지 않으며, 브릭스 대화(TryAdvanceMainQuestPhase)에서 처리한다.
	 *
	 * @param QuestDefinitionId  대상 퀘스트 DA 식별자
	 * @param ExpectedPhaseIndex 현재 클라이언트가 보고 있는 페이즈. 서버 값과 불일치하면 거부
	 * @return Progress 증가 성공 여부
	 */
	bool NotifyInteractionProgress(const FPrimaryAssetId& QuestDefinitionId, int32 ExpectedPhaseIndex);

	/**
	 * NPC 상호작용(카드 제시 등)으로 메인 퀘스트의 현재 페이즈를 다음으로 넘긴다.
	 * 납품 경로(TryDeliverQuestItem)가 아닌 비회수 목표(Interaction)의 서버 진입점이다.
	 * Interaction 페이즈에서는 CurrentProgress >= RequiredCount를 만족해야 전환한다.
	 *
	 * @param QuestDefinitionId  대상 퀘스트 DA 식별자
	 * @param ExpectedPhaseIndex 현재 클라이언트가 보고 있는 페이즈. 서버 값과 불일치하면 거부
	 * @return 페이즈 전환 성공 여부. 성공 시 중간 보상(PhaseCompletionReward)을 적용하고
	 *         OnMerchantStateChanged를 브로드캐스트한다.
	 */
	bool TryAdvanceMainQuestPhase(const FPrimaryAssetId& QuestDefinitionId, int32 ExpectedPhaseIndex);

	/** 제출 아이템 액터를 검사하고 제거 성공 이후 활성 퀘스트 진행도를 반영하는 함수 */
	bool TryDeliverQuestItem(AMD_QuestItem* SubmittedItem);

	/** 회수 대상 샘플이 파괴될 때 퀘스트 Asset ID·등급이 일치하는 활성 퀘스트를 실패 처리하는 함수 */
	void NotifySampleDestroyed(const FPrimaryAssetId& QuestDefinitionId, EMD_SampleGrade Grade);

	/** Day 마감 시 해당 Day의 미완료 메인 퀘스트만 실패로 정리하고 미회수 스폰물을 디스폰하는 함수 */
	void ResolveActiveQuestsAtDayEnd(int32 CompletedDay);

	/** 계약 종료 시 남은 반복 퀘스트를 정산하고 활성 현장을 해제하는 함수 */
	void FinalizeContract();

	// 현재 경제·호감도·퀘스트 원장을 저장 필드로 복사하는 함수
	FMD_MerchantData GetMerchantSaveData() const;

	// 검증된 저장 필드를 서버 권위 상인 원장에 원자적으로 적용하는 함수
	bool ApplyMerchantSaveData(const FMD_MerchantData& SaveData);

	/** `Resource.Sample.Grade.X` 태그 변환. 등급 태그가 필요한 곳에서 공용으로 쓴다. */
	static FGameplayTag GradeTagFromSampleGrade(EMD_SampleGrade Grade);

private:

	/** 활성 메인 퀘스트를 검증하고 Definition의 실패 원인별 Penalty를 한 번 적용하는 함수 */
	bool FailMainQuest(FMD_MainQuestState& QuestState, EMD_QuestFailureReason Reason);

	/** QuestDefinition의 Reward 또는 Penalty를 성공·실패 방향으로 서버 원장에 적용하는 함수 */
	void ApplyQuestRewardData(const FMD_QuestRewardData& RewardData, bool bPenalty);

	/** 반복 퀘스트 상태에서 공통 원본 에셋과 현재 목표 인덱스를 함께 검증하여 반환하는 함수 */
	UMD_QuestDefinition* GetRepeatQuestDefinition(const FMD_RepeatQuestState& QuestState) const;

	/** 수락한 반복 퀘스트가 마지막 페이즈 목표를 충족했는지 원본 에셋 기준으로 판정하는 함수 */
	bool IsRepeatQuestObjectiveComplete(const FMD_RepeatQuestState& QuestState) const;

	/** 성공한 반복 퀘스트 에셋의 고정 크레딧·점수·호감도를 서버 원장에 적용하는 함수 */
	void ApplyRepeatQuestReward(const UMD_QuestDefinition& Definition);

	/** 실패 원인에 맞는 반복 퀘스트 에셋의 패널티를 서버 원장에 적용하는 함수 */
	void ApplyRepeatQuestPenalty(const UMD_QuestDefinition& Definition, bool bDestroyed);

	/** 활성 반복 퀘스트를 실패로 전환하고 원본 에셋의 패널티를 한 번 적용하는 함수 */
	void FailRepeatQuest(FMD_RepeatQuestState& QuestState, bool bDestroyed);

	/** 실패 확정된 메인 퀘스트에 귀속된 월드 내 퀘스트 아이템을 제거하는 함수 */
	void DespawnMainQuestItems(const TArray<FPrimaryAssetId>& FailedQuestIds);

	/** 현재 진행 중인 현장으로 메인·반복 퀘스트 구성과 완료 이력 기록에 사용하는 참조 */
	UPROPERTY()
	TObjectPtr<UMD_SiteDefinition> CurrentSite;

	/** 메인 퀘스트 상태. 원본 데이터는 PrimaryAssetId로 조회한다. */
	UPROPERTY()
	TArray<FMD_MainQuestState> MainQuests;

	/** 계약 시작 시 한 번 추첨되어 계약 기간 동안 유지되는 반복 퀘스트 상태 */
	UPROPERTY()
	TArray<FMD_RepeatQuestState> RepeatQuests;

	UPROPERTY()
	TSet<TObjectPtr<UMD_SiteDefinition>> ClearedSites;

	/** 현재 현장의 반복 퀘스트 풀에서 계약에 사용할 카드를 최초 한 번 추첨하는 함수 */
	void DrawRepeatQuestsForContract();

	/** 방송하지 않고 잔액과 Enum Map 원장을 함께 변경하는 내부 함수 */
	void AddCreditsInternal(EMD_DailyReportCategory Category, int32 Amount);

	/** 성공한 메인 퀘스트 에셋의 크레딧·점수·호감도를 서버 원장에 적용하는 함수 */
	void ApplyMainQuestReward(const UMD_QuestDefinition& Definition);

	int32 Credits = 0;

	// 현재 Day의 크레딧 증감을 정산 분류별로 누적하는 서버 Map 원장
	TMap<EMD_DailyReportCategory, int64> DailyCreditTotals;

	// 계약과 게임 재시작을 넘어 유지되는 팀 공유 미상환 대출 잔액
	int32 DebtBalance = 0;

	// 상점 해금과 대화 분기를 계약 사이에도 유지하기 위한 누적 호감도
	float Affinity = 0.0f;

	int32 CurrentDay = 0;

	// 다음 정규일 착수금 배율 (1.0 기본)
	float NextDepositMultiplier = 1.f;

	// 소모품 할인 활성 여부
	bool bConsumableDiscountActive = true;

	/** 누적 퀘스트 성공 횟수. 계약을 넘어 유지되며 상점 해금 조건에 쓴다. */
	int32 QuestCompletions = 0;

	/** 계약 중 구매 결과와 선행 품목 해금을 유지하기 위한 서버 권위 구매 원장 */
	UPROPERTY()
	TMap<FPrimaryAssetId, int32> PurchasedItemAssetCounts;

	// 기획상 모든 호감도 구간에 공통으로 적용되는 일일 대출 이자율
	static constexpr float DailyInterestRate = 0.05f;
};
