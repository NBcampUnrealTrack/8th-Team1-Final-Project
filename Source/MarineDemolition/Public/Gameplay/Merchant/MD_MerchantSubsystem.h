// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/TimerHandle.h"
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
class APlayerState;
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
	// 경제 원장 변경과 알림 중 중첩 거래가 실행되는 상황 차단
	bool bShopTransactionInProgress = false;

public:

	int32 GetCredits() const { return Credits; }
	// 계약 동안 누적된 분류별 크레딧 원장을 네트워크 전달용 항목 배열로 반환하는 함수
	TArray<FMD_DailyReportValue> GetDailyCreditTotals() const;

	/** BeginContract 이후 현장 계약이 살아 있는지. GameInstance 수명 동안 유지된다. */
	bool HasActiveContract() const { return CurrentSite != nullptr; }

	/** 현재 맵에 맞는 현장 데이터와 기존 계약을 비교하기 위한 활성 현장 반환 함수 */
	UMD_SiteDefinition* GetCurrentSite() const
	{
		// GameInstance 수명 동안 유지되는 현재 현장 참조
		UMD_SiteDefinition* OutSite = CurrentSite.Get();
		return OutSite;
	}


	// Enum 분류별 원장 변경과 실제 적용 금액의 완료 알림
	void AddCredits(EMD_DailyReportCategory Category, int32 Amount, APlayerState* ResponsiblePlayer = nullptr,
		TFunction<void(int32)> AppliedAmountCallback = {});

	/** 플레이어별 소유권을 검증하고 아이템 지급 성공 시에만 팀 재화 차감과 구매 원장을 확정하는 서버 구매 함수 */
	EMD_ShopPurchaseResult TryPurchaseShopItem(AMD_PlayerState* PurchasingPlayerState, const FMD_ShopItem& ShopItem, TFunctionRef<bool()> FulfillPurchase);

	// 서버 소지 슬롯을 회수한 경우에만 팀 크레딧과 일일 원장에 판매 대금 적립
	EMD_ShopSaleResult TrySellShopItem(AMD_PlayerState* SellingPlayerState, FMD_ShopItem ShopItem, int32 SlotIndex, int32 Quantity);

	/** 상점 해금과 UI 갱신에 사용할 구매 완료 품목 식별자 목록 */
	TArray<FPrimaryAssetId> GetOwnedItemIds() const;

	/** 소모품을 포함한 품목별 누적 구매 횟수 */
	int32 GetPurchasedItemCount(FPrimaryAssetId ItemAssetId) const;


	// 현재 미상환 대출 잔액을 반환하는 함수
	int32 GetDebtBalance() const { return DebtBalance; }

	// 이자 계산의 기준이 되는 누적 대출 원금(착수금 + 추가 대출, 이자 제외)을 반환하는 함수
	int32 GetLoanPrincipal() const { return LoanPrincipal; }

	// 현재 상인의 기본 대출 한도 조회
	int32 GetLoanLimit() const;

	// 서버와 클라이언트 UI에 공통으로 적용하는 기본 대출 한도
	static int32 CalculateLoanLimit();

	// 현재 부채를 제외하고 추가로 빌릴 수 있는 금액을 반환하는 함수
	int32 GetAvailableLoanAmount() const;

	// 요청 금액과 현재 한도를 검증한 뒤 크레딧과 부채를 함께 증가시키는 함수
	bool TryBorrowCredits(int32 Amount, APlayerState* ResponsiblePlayer = nullptr);

	// 보유 크레딧과 미상환 잔액 범위에서 대출금을 상환하는 함수
	bool TryRepayLoan(int32 Amount, APlayerState* ResponsiblePlayer = nullptr);

	// 현장 타이머가 주기마다 요청하는 고정 이자 합산. 원금의 2%(올림)이며 일부 상환 후에도 유지, 완납 시 종료
	int32 ApplyDailyLoanInterest();

	// 서버 부과액과 대출 UI가 공유하는 고정 이자 계산. 미상환 잔액이 없으면 0, 있으면 원금 × 이자율 올림
	static int32 CalculateLoanInterest(int32 InDebtBalance, int32 InLoanPrincipal);

	// 주기마다 대출 원금에 적용하는 고정 이자율 (대출·경제 회의 계산기: 최초 원금의 2%)
	static constexpr float LoanInterestRate = 0.02f;

	// 새 현장 계약 시작. 크레딧 원장을 초기화하고 착수금을 지급(착수금은 부채에 가산)
	void BeginContract(UMD_SiteDefinition* Site, int32 BaseAdvancePayment = 0);


	/** 계약 종료 시 활성 현장과 일차 해제 */
	void FinalizeContract();

	/** `Resource.Sample.Grade.X` 태그 변환. 등급 태그가 필요한 곳에서 공용으로 쓴다. */
	static FGameplayTag GradeTagFromSampleGrade(EMD_SampleGrade Grade);

    // 정산 보고서에 네 개인 슬롯과 여섯 시점의 누적 금액 반영
    void BuildCreditHistory(FMD_InspectionReport& Report);

    // 최종 평가 보상 이후의 그래프 종료 시각 고정
    void FinishCreditHistory();

    // 서버의 실제 거래 발생 시각과 금액 및 개인 인덱스 조회
    FORCEINLINE TArray<FMD_CreditLogEntry>& GetCreditLog() { return CreditLog; }

    // 원시 로그의 개인 인덱스를 이름과 식별자로 해석하는 목록 조회
    FORCEINLINE TArray<FMD_PlayerCreditHistory>& GetCreditPlayers() { return CreditPlayers; }

protected:
    // 계약 단위로 초기화하며 조회 시에도 원본을 유지하는 서버 금액 로그
    TArray<FMD_CreditLogEntry> CreditLog;

    // 퇴장과 재접속에도 색상 및 기록 인덱스를 유지하는 개인 목록
    TArray<FMD_PlayerCreditHistory> CreditPlayers;

    // 온라인 식별자 준비 이전과 이후의 같은 플레이어 연결 유지
    TArray<TWeakObjectPtr<APlayerState>> CreditPlayerStates;

    // 맵 이동으로 다른 월드의 시계가 섞이지 않도록 계약 월드 보관
    TWeakObjectPtr<UWorld> CreditHistoryWorld;

    // 원시 로그의 경과 시간 기준
    double CreditHistoryStartSeconds = 0.0;

    // 최종 보고서가 확정된 경우 고정한 종료 경과 초
    double CreditHistoryEndSeconds = -1.0;

    // 최종 보고서 또는 월드 이동 이전까지의 실제 기록 경과 초
    double GetCreditHistoryElapsedSeconds();

    // 현재 서버의 참가자를 고정 인덱스에 등록하거나 기존 기록과 재연결
    int32 RegisterCreditPlayer(APlayerState* PlayerState);

    // 거래가 없는 참가자도 영점 그래프를 표시하기 위한 현재 명단 등록
    void RegisterCurrentCreditPlayers();

    // 서버 게임 월드에서만 경제 원장 변경을 허용하기 위한 권한 검사
    bool HasServerAuthority();

private:


	/** 계약 진행과 현장 완료 이력에 사용하는 현재 현장 참조 */
	UPROPERTY()
	TObjectPtr<UMD_SiteDefinition> CurrentSite;



	/** 방송하지 않고 잔액과 Enum Map 원장을 함께 변경하는 내부 함수 */
	void AddCreditsInternal(EMD_DailyReportCategory Category, int32 Amount, APlayerState* ResponsiblePlayer = nullptr);


	int32 Credits = 0;

	// 계약 동안 크레딧 증감을 정산 분류별로 누적하는 서버 Map 원장
	TMap<EMD_DailyReportCategory, int64> DailyCreditTotals;

	// 계약과 게임 재시작을 넘어 유지되는 팀 공유 미상환 대출 잔액
	int32 DebtBalance = 0;

	// 고정 이자의 기준 원금. 착수금과 추가 대출의 합이며 이자는 포함하지 않고 완납 시 0으로 초기화
	int32 LoanPrincipal = 0;



	/** 계약 중 구매 결과와 선행 품목 해금을 유지하기 위한 서버 권위 구매 원장 */
	UPROPERTY()
	TMap<FPrimaryAssetId, int32> PurchasedItemAssetCounts;

protected:
    // 거래 도중 발생한 독립 보상의 원인과 금액 및 약한 책임자 참조 보관
    struct FPendingCreditChange
    {
        // 기존 정산 원장에 사용할 보상 분류
        EMD_DailyReportCategory Category = EMD_DailyReportCategory::Other;
        // 보상 발생 순서대로 반영할 실제 요청 금액
        int32 Amount = 0;
        // 퇴장한 플레이어의 수명을 연장하지 않는 책임자 연결
        TWeakObjectPtr<APlayerState> ResponsiblePlayer;
        // 대기 금액이 실제 반영된 뒤 요청자에게 전달할 완료 알림
        TFunction<void(int32)> AppliedAmountCallback;
    };

    // 현재 거래가 확정된 다음 순차 반영할 독립 보상 목록
    TArray<FPendingCreditChange> PendingCreditChanges;

    // 보상 알림의 재진입을 다음 틱으로 넘기는 단일 예약
    FTimerHandle PendingCreditFlushTimerHandle;

    // 거래 가드 해제 이후 보관된 보상을 원장과 표시 상태에 반영
    void FlushPendingCreditChanges();
};
