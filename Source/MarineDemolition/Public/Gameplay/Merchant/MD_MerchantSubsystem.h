// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Types/Merchant/MD_MerchantTypes.h"
#include "MD_MerchantSubsystem.generated.h"

/**
 * DayPersistenceSubsystem과 같은 패턴으로, Day전환해도 계약3일동안 크레딧과 퀘스트가 유지되어야하므로
 * Gameinstance에 수명을 둠. 값 변경 처리는 서버에서 하고 클라 복제는 GameState에서 담당
 */


DECLARE_MULTICAST_DELEGATE(FMD_OnMerchantStateChanged);

class UMD_SiteDefinition;
class AMD_PlayerState;
struct FGameplayTag;
struct FMD_MerchantData;
struct FMD_ShopItem;

enum class EMD_SampleGrade : uint8;
enum class EMD_ShopPurchaseResult : uint8;

UCLASS()
class MARINEDEMOLITION_API UMD_MerchantSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
	
public:

	FMD_OnMerchantStateChanged OnMerchantStateChanged;

public:

	int32 GetCredits() const { return Credits; }
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

	/** 크레딧 증감. 단독 호출용이며 변경 후 델리게이트 알림. */
	void AddCredits(int32 Amount);

	/** 플레이어별 소유권을 검증하고 아이템 지급 성공 시에만 팀 재화 차감과 구매 원장을 확정하는 서버 구매 함수 */
	EMD_ShopPurchaseResult TryPurchaseShopItem(AMD_PlayerState* PurchasingPlayerState, const FMD_ShopItem& ShopItem, TFunctionRef<bool()> FulfillPurchase);

	/** 상점 해금과 UI 갱신에 사용할 구매 완료 품목 식별자 목록 */
	TArray<FName> GetOwnedItemIds() const;

	/** 소모품을 포함한 품목별 누적 구매 횟수 */
	int32 GetPurchasedItemCount(FName ItemId) const;

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

	//계약 시작, 착수금 지급
	void BeginContract(UMD_SiteDefinition* Site, int32 AdvancePayment);

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

	/** 반복 퀘스트 노출 조건. 현장을 한 번 클리어해야 열린다. (기획 09 §4) */
	bool HasClearedCurrentSite() const;

	/** Day 시작 시 호출. 메인 언락 판정 + 반복 카드 재롤. */
	void BeginDay(int32 NewDay);

	/** 계약 클리어 시 호출. 반복 퀘스트 게이트를 여는 이력을 남긴다. */
	void MarkSiteCleared();

	/** 반복 퀘스트 수락. Offered 상태에서만 가능하다. */
	bool TryAcceptRepeatQuest(int32 QuestIndex);

	/** 거절. 페널티 없이 다음 Day 재롤 대상이 된다. */
	bool TryDeclineRepeatQuest(int32 QuestIndex);

	/** 포기하는 반복 퀘스트를 실패 처리 및 패널티(호감도 감소) 처리하는 함수 */
	bool TryAbandonRepeatQuest(int32 QuestIndex);

	/** 샘플이 모선에 납품될 때 호출한다. 메인·반복 활성 퀘스트를 함께 검사한다. */
	void NotifySampleDelivered(const FGameplayTag& GradeTag, bool bIntact);

	/** 회수 대상 샘플이 파괴될 때 호출한다. 해당 등급 활성 퀘스트를 즉시 실패 처리한다. */
	void NotifySampleDestroyed(EMD_SampleGrade Grade);

	/** Day 마감 시 미완료 활성 퀘스트를 실패로 정리한다. */
	void ResolveActiveQuestsAtDayEnd(int32 CompletedDay);

	/** 계약 종료 시 남은 활성 반복 퀘스트를 실패로 정리한다. */
	void FinalizeContract();

	// 현재 경제·호감도·퀘스트 원장을 저장 필드로 복사하는 함수
	FMD_MerchantData GetMerchantSaveData() const;

	// 검증된 저장 필드를 서버 권위 상인 원장에 원자적으로 적용하는 함수
	bool ApplyMerchantSaveData(const FMD_MerchantData& SaveData);

	/** `Resource.Sample.Grade.X` 태그 변환. 등급 태그가 필요한 곳에서 공용으로 쓴다. */
	static FGameplayTag GradeTagFromSampleGrade(EMD_SampleGrade Grade);

private:

	void FailMainQuest(FMD_MainQuestState& QuestState, EMD_QuestFailureReason Reason);

	bool DoesQuestTargetGrade(const FMD_QuestDefinitionData& Definition, const FGameplayTag& GradeTag) const;

	/** 현재 진행 중인 현장. 반복 퀘스트 풀과 클리어 이력 판정에 쓴다. */
	UPROPERTY()
	TObjectPtr<UMD_SiteDefinition> CurrentSite;

	/** 메인 퀘스트 상태. UPROPERTY라 GC가 Definition 참조를 추적한다. */
	UPROPERTY()
	TArray<FMD_MainQuestState> MainQuests;

	/** 반복 퀘스트 상태. Day마다 재롤된다. */
	UPROPERTY()
	TArray<FMD_RepeatQuestState> RepeatQuests;

	UPROPERTY()
	TSet<TObjectPtr<UMD_SiteDefinition>> ClearedSites;

	void RerollRepeatQuests();

	/** 방송하지 않는 내부용. 여러 변경을 한 번에 묶을 때 쓴다. */
	void AddCreditsInternal(int32 Amount);

	/** 목표 등급·Intact 조건이 이 퀘스트에 맞는지 판정한다. */
	bool DoesDeliveryMatch(const FMD_QuestDefinitionData& Definition, const FGameplayTag& GradeTag, bool bIntact) const;

	void ApplyQuestReward(const FMD_QuestDefinitionData& Definition);

	int32 Credits = 0;

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
	TMap<FName, int32> PurchasedItemCounts;

	// 기획상 모든 호감도 구간에 공통으로 적용되는 일일 대출 이자율
	static constexpr float DailyInterestRate = 0.05f;
};
