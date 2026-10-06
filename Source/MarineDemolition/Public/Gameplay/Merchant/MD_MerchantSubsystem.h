// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Types/Merchant/MD_MerchantTypes.h"
#include "Types/Score/MD_ScoreTypes.h"
#include "MD_MerchantSubsystem.generated.h"

/**
 * 계약 동안 크레딧과 퀘스트가 맵 이동을 넘어 유지되어야 하므로
 * Gameinstance에 수명을 둠. 값 변경 처리는 서버에서 하고 클라 복제는 GameState에서 담당
 */


DECLARE_MULTICAST_DELEGATE(FMD_OnMerchantStateChanged);

class UMD_SiteDefinition;
class AMD_PlayerState;
struct FGameplayTag;
struct FMD_ShopItem;

enum class EMD_ShopPurchaseResult : uint8;
enum class EMD_ShopSaleResult : uint8;

UCLASS()
class MARINEDEMOLITION_API UMD_MerchantSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
	
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    virtual void Deinitialize() override;

public:

	FMD_OnMerchantStateChanged OnMerchantStateChanged;

protected:
	// 아이템 변경 알림에서 중첩 구매와 판매가 실행되는 상황 차단
	bool bShopTransactionInProgress = false;

public:

	int32 GetCredits() const { return Credits; }
	// 계약 동안 누적된 분류별 크레딧 원장을 네트워크 전달용 항목 배열로 반환하는 함수
	TArray<FMD_DailyReportValue> GetDailyCreditTotals() const;
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

	// 요청 금액과 현재 한도를 검증한 뒤 크레딧과 부채를 함께 증가시키는 함수
	bool TryBorrowCredits(int32 Amount);

	// 보유 크레딧과 미상환 잔액 범위에서 대출금을 상환하는 함수
	bool TryRepayLoan(int32 Amount);

	// 새 현장 계약 시작. 크레딧 원장을 초기화하고 호감도 배율이 반영된 착수금을 지급
	void BeginContract(UMD_SiteDefinition* Site, int32 BaseAdvancePayment = 0);

	// 게임오버 시 전체 리셋
	void ResetContract();

	// 소모품 할인 활성 여부 (경고 시 해제). 상점에서 조회
	void SetConsumableDiscountActive(bool bActive) { bConsumableDiscountActive = bActive; }
	bool IsConsumableDiscountActive() const { return bConsumableDiscountActive; }


	/** 현재 계약 현장이 과거에 클리어된 적 있는지 저장 이력에서 확인하는 함수 */
	bool HasClearedCurrentSite() const;

	/** 계약 클리어 시 호출하여 현장 완료 이력을 남기는 함수 */
	void MarkSiteCleared();


	/** 계약 종료 시 활성 현장과 일차 해제 */
	void FinalizeContract();

	/** `Resource.Sample.Grade.X` 태그 변환. 등급 태그가 필요한 곳에서 공용으로 쓴다. */
	static FGameplayTag GradeTagFromSampleGrade(EMD_SampleGrade Grade);

private:


	/** 계약 진행과 현장 완료 이력에 사용하는 현재 현장 참조 */
	UPROPERTY()
	TObjectPtr<UMD_SiteDefinition> CurrentSite;


	UPROPERTY()
	TSet<TObjectPtr<UMD_SiteDefinition>> ClearedSites;


	/** 방송하지 않고 잔액과 Enum Map 원장을 함께 변경하는 내부 함수 */
	void AddCreditsInternal(EMD_DailyReportCategory Category, int32 Amount);


	int32 Credits = 0;

	// 계약 동안 크레딧 증감을 정산 분류별로 누적하는 서버 Map 원장
	TMap<EMD_DailyReportCategory, int64> DailyCreditTotals;

	// 계약과 게임 재시작을 넘어 유지되는 팀 공유 미상환 대출 잔액
	int32 DebtBalance = 0;

	// 상점 해금과 대화 분기를 계약 사이에도 유지하기 위한 누적 호감도
	float Affinity = 0.0f;

	// 소모품 할인 활성 여부
	bool bConsumableDiscountActive = true;


	/** 계약 중 구매 결과와 선행 품목 해금을 유지하기 위한 서버 권위 구매 원장 */
	UPROPERTY()
	TMap<FPrimaryAssetId, int32> PurchasedItemAssetCounts;
};
