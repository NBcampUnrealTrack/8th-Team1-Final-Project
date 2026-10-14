// Fill out your copyright notice in the Description page of Project Settings.


#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "Engine/AssetManager.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Data/Merchant/MD_SiteDefinition.h"
#include "Data/Shop/MD_ShopCatalog.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "Game/MD_GameStateBase.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Player/MD_PlayerState.h"
#include "Util/MD_LogUtility.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "TimerManager.h"


bool UMD_MerchantSubsystem::HasServerAuthority()
{
    // 경제 상태가 속한 현재 게임 월드 조회
    UWorld* World = GetWorld();
    // 월드 초기화 전과 클라이언트 및 편집기에서의 원장 변경 차단
    return IsValid(World) == true && World->IsGameWorld() == true && World->GetNetMode() != NM_Client;
}

void UMD_MerchantSubsystem::FlushPendingCreditChanges()
{
    // 서버 거래가 끝난 뒤 실제 대기 보상이 있는 경우에만 처리 허용
    if (HasServerAuthority() == false || bShopTransactionInProgress == true || PendingCreditChanges.IsEmpty() == true)
    {
        // 다른 거래의 진행 상태와 보관 중인 보상 유지
        return;
    }
    // 대기 예약 취소와 다음 틱 예약에 사용할 현재 서버 월드 조회
    UWorld* World = GetWorld();
    // 월드 종료 중에는 보관된 보상을 유지한 채 처리 중단
    if (IsValid(World) == false)
    {
        // 타이머 관리자에 접근할 수 없는 처리 종료
        return;
    }
    // 같은 대기 배치를 처리할 이전 예약의 중복 실행 방지
    World->GetTimerManager().ClearTimer(PendingCreditFlushTimerHandle);
    // 알림 중 추가되는 보상을 분리하기 위한 현재 배치 이동
    TArray<FPendingCreditChange> CreditChanges = MoveTemp(PendingCreditChanges);
    {
        // 배치 적용과 최종 알림 도중의 중첩 경제 거래 차단
        TGuardValue<bool> TransactionGuard(bShopTransactionInProgress, true);
        // 같은 금액도 서로 다른 보상으로 유지하는 발생 순서별 처리
        for (FPendingCreditChange& CreditChange : CreditChanges)
        {
            // 다른 대기 거래와 구분할 개별 금액 적용 전 잔액 보관
            int32 CreditsBeforeChange = Credits;
            // 개별 보상의 상한과 하한 및 분류별 원장 규칙 유지
            AddCreditsInternal(CreditChange.Category, CreditChange.Amount, CreditChange.ResponsiblePlayer.Get());
            // 실제 반영액을 요청한 거래에만 완료 결과 전달
            if (static_cast<bool>(CreditChange.AppliedAmountCallback) == true)
            {
                // 거래 가드 안에서 재진입 보상과 분리된 실제 증감 통지
                CreditChange.AppliedAmountCallback(Credits - CreditsBeforeChange);
            }
        }
        // 현재 배치의 완성된 잔액과 부채 상태를 한 번만 전달
        OnMerchantStateChanged.Broadcast();
    }
    // 알림 콜백 이후의 실제 월드로 다음 배치 예약 대상 갱신
    World = GetWorld();
    // 무조건 보상을 생성하는 알림도 같은 프레임의 무한 처리 방지
    if (PendingCreditChanges.IsEmpty() == false && IsValid(World) == true)
    {
        // 새로 발생한 독립 보상을 잃지 않는 다음 틱 단일 예약
        PendingCreditFlushTimerHandle = World->GetTimerManager().SetTimerForNextTick(this, &ThisClass::FlushPendingCreditChanges);
    }
    // 다음 틱 예약으로 들어온 배치가 음수를 만들었을 때도 종료 방송이 빠지지 않게 한다
    NotifyBankruptcyIfPending();
}

void UMD_MerchantSubsystem::NotifyBankruptcyIfPending()
{
    // 거래 가드 안이거나 계약이 이미 끝났으면 방송하지 않는다. 플래그는 계약 종료 시 함께 비운다
    if (bBankruptcyPending == false || bShopTransactionInProgress == true || HasServerAuthority() == false)
    {
        return;
    }
    bBankruptcyPending = false;
    if (IsValid(CurrentSite) == false)
    {
        return;
    }
    UE_LOG(LogTemp, Warning, TEXT("[Merchant] 팀 잔액 음수 진입 — 계약 즉시 종료"));
    OnBankrupt.Broadcast();
}

double UMD_MerchantSubsystem::GetCreditHistoryElapsedSeconds()
{
    // 최종 정산 이후에는 조회 시각과 무관한 종료점 사용
    if (CreditHistoryEndSeconds >= 0.0)
    {
        return CreditHistoryEndSeconds;
    }
    // 월드 전환이나 시각 오차에도 마지막 실제 거래가 누락되지 않는 하한
    double LastEntryTime = CreditLog.IsEmpty() == false ? CreditLog.Last().ElapsedSeconds : 0.0;
    // 새 월드의 영점 시각을 이전 계약에 섞지 않는 원본 월드 조회
    UWorld* World = CreditHistoryWorld.Get();
    // 아직 유효한 계약 월드의 실제 게임 시간 사용
    if (IsValid(World) == true && World == GetWorld())
    {
        // 현재 시각과 마지막 거래 시각 중 더 늦은 경과 초 사용
        return FMath::Max(LastEntryTime, FMath::Max(0.0, World->GetTimeSeconds() - CreditHistoryStartSeconds));
    }
    // 원본 월드가 사라진 경우에도 마지막 기록까지 보존
    return LastEntryTime;
}

int32 UMD_MerchantSubsystem::RegisterCreditPlayer(APlayerState* PlayerState)
{
    // 플레이어 없는 보상과 다른 월드 또는 클라이언트 상태의 개인 귀속 차단
    if (IsValid(PlayerState) == false || PlayerState->HasAuthority() == false
        || PlayerState->GetWorld() != CreditHistoryWorld.Get() || PlayerState->IsOnlyASpectator() == true)
    {
        return INDEX_NONE;
    }
    // 동명이인을 구분하고 온라인 재접속을 연결할 계정 식별자 조회
    FUniqueNetIdRepl UniqueId = PlayerState->GetUniqueId();
    // 온라인 계정이 없는 PIE 및 오프라인 환경은 서버 PlayerId로 구분
    FString PlayerKey = UniqueId.IsValid() == true
        ? TEXT("Online:") + UniqueId.ToString() : FString::Printf(TEXT("Local:%d"), PlayerState->GetPlayerId());
    // 아직 PlayerId가 할당되지 않은 서버 상태끼리의 충돌 방지
    if (UniqueId.IsValid() == false && PlayerState->GetPlayerId() < 0)
    {
        // 초기화 중인 서로 다른 객체의 개인 기록 분리
        PlayerKey = TEXT("Pending:") + PlayerState->GetPathName();
    }
    // 퇴장한 슬롯을 재활용하지 않고 기존 참가자의 기록 인덱스 유지
    for (int32 Index = 0; Index < CreditPlayers.Num(); ++Index)
    {
        // 온라인 식별자 준비 전후 및 같은 계정의 재접속을 동일 기록으로 연결
        if (CreditPlayerStates[Index].Get() == PlayerState || CreditPlayers[Index].PlayerKey == PlayerKey)
        {
            // 재접속한 실제 상태의 약한 참조 갱신
            CreditPlayerStates[Index] = PlayerState;
            // 늦게 준비된 온라인 식별자 반영
            CreditPlayers[Index].PlayerKey = PlayerKey;
            // 변경된 표시 이름을 개인 인덱스와 무관하게 갱신
            CreditPlayers[Index].PlayerName = PlayerState->GetPlayerName();
            // 기존 그래프 색상과 원시 로그 귀속 유지
            return Index;
        }
    }
    // 거래 없는 새 참가자도 이후 조회에서 영점 이력 제공
    FMD_PlayerCreditHistory Player;
    // 빈 UI 슬롯과 실제 참가자 구분
    Player.bHasPlayer = true;
    // 표시 이름과 독립적인 개인 식별자 보관
    Player.PlayerKey = PlayerKey;
    // 접속 종료 후에도 사용할 마지막 표시 이름 보관
    Player.PlayerName = PlayerState->GetPlayerName();
    // 새 참가자를 이전 참가자 뒤에만 등록
    CreditPlayers.Add(MoveTemp(Player));
    // 동일 인덱스의 서버 상태 연결 보관
    CreditPlayerStates.Add(PlayerState);
    // 네 UI 슬롯을 넘는 경우에도 원시 로그의 실제 귀속은 보존
    return CreditPlayers.Num() - 1;
}

void UMD_MerchantSubsystem::RegisterCurrentCreditPlayers()
{
    // 현재 계약의 서버 참가자 명단 조회
    UWorld* World = CreditHistoryWorld.Get();
    // 다른 월드나 클라이언트에서 명단을 변경하는 상황 차단
    if (IsValid(World) == false || World != GetWorld() || World->GetNetMode() == NM_Client)
    {
        return;
    }
    // 실제 게임 상태에 등록된 참가자 배열 조회
    AGameStateBase* GameState = World->GetGameState();
    // 초기화 전에는 거래 요청 시의 개별 등록 경로 유지
    if (IsValid(GameState) == false)
    {
        return;
    }
    // 수익이 없는 참가자도 정산 슬롯에 포함
    for (APlayerState* PlayerState : GameState->PlayerArray)
    {
        // 서버 명단 순서대로 기존 슬롯을 재사용하거나 신규 등록
        RegisterCreditPlayer(PlayerState);
    }
}

void UMD_MerchantSubsystem::FinishCreditHistory()
{
    // 최초 종료 시각만 고정하여 중복 정산 요청에 따른 그래프 이동 방지
    if (CreditHistoryEndSeconds < 0.0)
    {
        // 최종 참가자 이름과 거래 없는 참가자까지 기록
        RegisterCurrentCreditPlayers();
        // 최종 보상까지 포함된 실제 계약 경과 시간 확정
        CreditHistoryEndSeconds = GetCreditHistoryElapsedSeconds();
    }
}

void UMD_MerchantSubsystem::BuildCreditHistory(FMD_InspectionReport& Report)
{
    // 최종 보고서 이후의 참가자 변경은 확정된 정산에서 제외
    if (CreditHistoryEndSeconds < 0.0)
    {
        // 계약 도중 합류하거나 표시 이름이 바뀐 참가자 반영
        RegisterCurrentCreditPlayers();
    }
    // 시작부터 현재 조회 또는 최종 정산까지의 공통 가로 범위
    Report.CreditHistoryDuration = GetCreditHistoryElapsedSeconds();
    // 재사용된 보고서의 이전 시간축 제거
    Report.CreditSampleTimes.Reset();
    // 시작과 종료를 포함한 여섯 개 시간 라벨 제공
    Report.CreditSampleTimes.SetNumZeroed(6);
    // 재사용된 보고서의 이전 개인 이름과 이력 제거
    Report.PlayerCreditHistories.Reset();
    // UI에서 사용할 네 개인 슬롯을 항상 같은 배열 길이로 제공
    Report.PlayerCreditHistories.SetNum(4);
    // 이전 조회의 팀 공용 누적액 초기화
    Report.SharedCreditDelta = 0;
    // 이전 조회의 전체 순증감 초기화
    Report.TotalCreditDelta = 0;
    // 순증감과 구분되는 실제 차감 합계 초기화
    Report.TotalCreditsSpent = 0;
    // UI의 각 개인 슬롯에 이름과 거래 합계 및 여섯 점 준비
    for (int32 PlayerIndex = 0; PlayerIndex < Report.PlayerCreditHistories.Num(); ++PlayerIndex)
    {
        // 현재 UI 슬롯에 대응하는 출력 기록 조회
        FMD_PlayerCreditHistory& Player = Report.PlayerCreditHistories[PlayerIndex];
        // 실제 참가자만 이름과 수입·지출·순수익 반영
        if (CreditPlayers.IsValidIndex(PlayerIndex) == true)
        {
            // 원시 로그의 고정 인덱스 순서 유지
            Player = CreditPlayers[PlayerIndex];
        }
        // 현재 보고서의 시간축에 맞춰 여섯 좌표 재구성
        Player.Samples.SetNumZeroed(Report.CreditSampleTimes.Num());
    }
    // 네 UI 슬롯별 현재까지의 누적 순수익 보관
    TArray<int64> CumulativeCredits;
    // 이전 시점에 반영한 금액을 다음 시점에 이어서 사용
    CumulativeCredits.SetNumZeroed(Report.PlayerCreditHistories.Num());
    // 원시 로그를 매 시점마다 반복 검색하지 않는 단일 순회 커서
    int32 EntryIndex = 0;
    // 시작과 종료를 포함한 균등한 여섯 시점 구성
    for (int32 SampleIndex = 0; SampleIndex < Report.CreditSampleTimes.Num(); ++SampleIndex)
    {
        // 다섯 간격으로 나누어 마지막 점이 종료 시각과 일치하도록 계산
        double SampleTime = SampleIndex == Report.CreditSampleTimes.Num() - 1 ? Report.CreditHistoryDuration
            : Report.CreditHistoryDuration * SampleIndex / (Report.CreditSampleTimes.Num() - 1);
        // BP에서 시간 라벨에 사용할 실제 경과 초 보관
        Report.CreditSampleTimes[SampleIndex] = SampleTime;
        // 현재 표본 시각까지 실제로 발생한 거래만 누적
        while (CreditLog.IsValidIndex(EntryIndex) == true && CreditLog[EntryIndex].ElapsedSeconds <= SampleTime)
        {
            // 현재 시각에 새로 포함되는 실제 거래 조회
            FMD_CreditLogEntry& Entry = CreditLog[EntryIndex];
            // UI에 표시하는 개인의 순수익 누적
            if (CumulativeCredits.IsValidIndex(Entry.PlayerIndex) == true)
            {
                // 다른 플레이어 및 공용 보상과 분리된 누적 금액 반영
                CumulativeCredits[Entry.PlayerIndex] += Entry.Amount;
            }
            // 특정 플레이어에게 귀속되지 않은 금액만 공용 합계로 분리
            if (Entry.PlayerIndex == INDEX_NONE)
            {
                // 착수금 및 평가 보상 등의 실제 합계 반영
                Report.SharedCreditDelta += Entry.Amount;
            }
            // 개인과 공용 거래를 모두 포함한 실제 잔액 증감 집계
            Report.TotalCreditDelta += Entry.Amount;
            // 양수 보상과 상쇄되기 이전의 실제 차감액 기록
            if (Entry.Amount < 0)
            {
                // 음수 최솟값의 정수 넘침을 피하는 지출 합산
                Report.TotalCreditsSpent -= static_cast<int64>(Entry.Amount);
            }
            // 이미 반영한 거래를 다음 표본에서 중복 누적하지 않는 진행
            ++EntryIndex;
        }
        // 동일한 여섯 시각을 모든 개인 그래프에 적용
        for (int32 PlayerIndex = 0; PlayerIndex < Report.PlayerCreditHistories.Num(); ++PlayerIndex)
        {
            // 기존 WBP의 SetPlayerMoneyHistory가 받는 Vector2D 배열 구성
            Report.PlayerCreditHistories[PlayerIndex].Samples[SampleIndex] = FVector2D(SampleTime, static_cast<double>(CumulativeCredits[PlayerIndex]));
        }
    }
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

void UMD_MerchantSubsystem::AddCredits(EMD_DailyReportCategory Category, int32 Amount, APlayerState* ResponsiblePlayer,
	TFunction<void(int32)> AppliedAmountCallback)
{
	// 클라이언트와 게임 월드 외 변경 및 실제 금액 없는 알림 차단
	if (HasServerAuthority() == false || Amount == 0)
	{
		// 서버 확정 조건 또는 실제 변동이 없는 요청 종료
		return;
	}
	// 진행 중인 결제와 분류 환급을 섞지 않는 독립 보상 보관
	if (bShopTransactionInProgress == true)
	{
		// 이번 보상에 필요한 분류와 책임자 문맥 준비
		FPendingCreditChange CreditChange;
		// 실제 보상 원인 보관
		CreditChange.Category = Category;
		// 결제 확정 후 반영할 실제 요청액 보관
		CreditChange.Amount = Amount;
		// 퇴장 이후 유효하지 않은 책임자 접근을 막는 약한 연결 보관
		CreditChange.ResponsiblePlayer = ResponsiblePlayer;
		// 대기 거래가 실제 확정될 때 사용할 완료 알림 소유권 이전
		CreditChange.AppliedAmountCallback = MoveTemp(AppliedAmountCallback);
		// 같은 금액의 정상 보상도 생략하지 않는 순차 등록
		PendingCreditChanges.Add(MoveTemp(CreditChange));
		// 현재 거래를 중단하지 않은 보상 요청 종료
		return;
	}
	// 거래 가드가 해제된 다음에 대기 보상을 처리하는 종료 예약
	ON_SCOPE_EXIT
	{
		// 현재 변경과 알림에서 발생한 독립 보상 반영
		FlushPendingCreditChanges();
		// 가드가 풀린 뒤에 방송해야 GameMode의 FinalizeContract가 거래 중으로 막히지 않는다
		NotifyBankruptcyIfPending();
	};
	// 원장 확정과 알림 사이에 추가 거래가 끼어드는 상황 차단
	TGuardValue<bool> TransactionGuard(bShopTransactionInProgress, true);
	// 다른 재진입 거래와 구분할 현재 금액 적용 전 잔액 보관
	int32 CreditsBeforeChange = Credits;
	// 모든 외부 크레딧 변경을 단일 Enum 기반 기록 경로로 처리
	AddCreditsInternal(Category, Amount, ResponsiblePlayer);
	// 실제 반영액을 요청한 거래에만 완료 결과 전달
	if (static_cast<bool>(AppliedAmountCallback) == true)
	{
		// 방송 전 거래 가드 안에서 확정된 증감 통지
		AppliedAmountCallback(Credits - CreditsBeforeChange);
	}
	// 변경이 완료된 원장을 호스트와 복제 미러에 전달
	OnMerchantStateChanged.Broadcast();
}

EMD_ShopPurchaseResult UMD_MerchantSubsystem::TryPurchaseShopItem(AMD_PlayerState* PurchasingPlayerState, const FMD_ShopItem& ShopItem, TFunctionRef<bool()> FulfillPurchase)
{
	// 클라이언트와 게임 월드 외 소지품 및 결제 변경 차단
	if (HasServerAuthority() == false)
	{
		// 지급과 회수 전에 서버 확정 조건 미충족 전달
		return EMD_ShopPurchaseResult::InvalidRequest;
	}

	// 클라이언트가 로컬 원장을 변경하지 못하도록 막는 서버 권위 검사
	if (IsValid(PurchasingPlayerState) == false || PurchasingPlayerState->HasAuthority() == false || bShopTransactionInProgress == true)
	{
		return EMD_ShopPurchaseResult::InvalidRequest;
	}

	// 거래 가드가 해제된 다음에 대기 보상을 처리하는 종료 예약
	ON_SCOPE_EXIT
	{
		// 현재 거래와 알림에서 발생한 독립 보상 반영
		FlushPendingCreditChanges();
	};
	// 지급과 경제 알림이 완료될 때까지 중첩 경제 거래 차단
	TGuardValue<bool> TransactionGuard(bShopTransactionInProgress, true);

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
		|| ShopItem.IsUnlocked(PurchasedItemIds) == false)
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
	// 구매자의 실제 결제 금액과 개인별 발생 시각 기록
	AddCreditsInternal(EMD_DailyReportCategory::Other, -PurchasePrice, PurchasingPlayerState);
	// 정의의 전체 AssetId를 사용하여 타입이 다른 구매 이력 구분
	int32& PurchasedCount = PurchasedItemAssetCounts.FindOrAdd(ItemAssetId);
	PurchasedCount += 1;

	// 정의의 전체 AssetId를 사용하여 타입이 다른 구매 이력 구분
	UE_LOG(MarineDemolition, Log, TEXT("[Merchant] 상점 구매 완료: %s (-%d, 잔액 %d, 누적 %d개)"), *ItemAssetId.ToString(), PurchasePrice, Credits, PurchasedCount);

	// GameState 복제 미러와 열려 있는 상점 UI를 함께 갱신하기 위한 변경 알림
	OnMerchantStateChanged.Broadcast();

	return EMD_ShopPurchaseResult::Success;
}

EMD_ShopSaleResult UMD_MerchantSubsystem::TrySellShopItem(AMD_PlayerState* SellingPlayerState, FMD_ShopItem ShopItem, int32 SlotIndex, int32 Quantity)
{
	// 클라이언트와 게임 월드 외 소지품 및 결제 변경 차단
	if (HasServerAuthority() == false)
	{
		// 지급과 회수 전에 서버 확정 조건 미충족 전달
		return EMD_ShopSaleResult::InvalidRequest;
	}

	// 진행 중인 경제 원장 변경과 판매 요청의 중첩 차단
	if (bShopTransactionInProgress == true)
	{
		// 소지품과 대금이 변경되기 전 중첩 판매 거부
		return EMD_ShopSaleResult::InvalidRequest;
	}
	// 서버 소유권과 양수 수량 검사
	if (IsValid(SellingPlayerState) == false || SellingPlayerState->HasAuthority() == false || Quantity <= 0)
	{
		// 소지품과 원장을 변경하지 않은 요청 거부
		return EMD_ShopSaleResult::InvalidRequest;
	}
	// 거래 가드가 해제된 다음에 대기 보상을 처리하는 종료 예약
	ON_SCOPE_EXIT
	{
		// 현재 거래와 알림에서 발생한 독립 보상 반영
		FlushPendingCreditChanges();
	};
	// 소지품 변경과 크레딧 알림의 재진입을 전체 거래 동안 차단
	TGuardValue<bool> TransactionGuard(bShopTransactionInProgress, true);
	// 등록되지 않은 품목을 사용한 대금 요청 차단
	if (IsValid(ShopItem.GetDefinition()) == false || ShopItem.GetItemAssetId().IsValid() == false)
	{
		// 유효하지 않은 품목 결과 반환
		return EMD_ShopSaleResult::InvalidItem;
	}
	// 화면과 같은 단가를 서버 카탈로그 원본으로 계산
	int32 UnitPrice = ShopItem.GetSalePrice();
	// 인벤토리 외 즉시 효과 등 판매 불가 품목 차단
	if (UnitPrice < 0)
	{
		// 회수할 수 없는 품목 결과 반환
		return EMD_ShopSaleResult::NotSellable;
	}
	// 플레이어 상태가 실제 소유한 현재 Pawn에서만 소지품 조회
	APawn* SellingPawn = SellingPlayerState->GetPawn();
	// 현재 Pawn의 공통 슬롯 컴포넌트 조회
	UMD_EquipHotbarComponent* Hotbar = IsValid(SellingPawn) == true ? SellingPawn->FindComponentByClass<UMD_EquipHotbarComponent>() : nullptr;
	// 클라이언트가 선택했던 위치의 실제 서버 품목 조회
	UMD_ItemDefinition* StoredItem = IsValid(Hotbar) == true ? Hotbar->GetItemAtSlot(SlotIndex) : nullptr;
	// 슬롯 교환과 소진 및 다른 품목을 대상으로 한 오래된 요청 차단
	if (IsValid(StoredItem) == false || StoredItem->GetPrimaryAssetId() != ShopItem.GetItemAssetId() || Hotbar->GetItemCountAtSlot(SlotIndex) < Quantity)
	{
		// 보유 수량을 보존한 판매 거부
		return EMD_ShopSaleResult::NotOwned;
	}
	// 곱셈 오버플로 없이 판매 대금 계산
	int64 TotalPrice = static_cast<int64>(UnitPrice) * Quantity;
	// 대금 전액을 적립할 수 없는 거래는 회수 이전에 차단
	if (TotalPrice > MAX_int32 || static_cast<int64>(Credits) + TotalPrice > MAX_int32)
	{
		// 상한 초과로 소지품을 잃지 않도록 실패 전달
		return EMD_ShopSaleResult::CreditLimit;
	}
	// 회수 방식과 판매 후 초기화 대상을 함께 결정할 실제 보유 도구 조회
	UMD_ToolDefinition* SoldTool = Cast<UMD_ToolDefinition>(StoredItem);
	// 판매 도구와 모든 선행 도구의 개인 이력 초기화 목록
	TArray<FPrimaryAssetId> ToolAssetIds;
	// 가장 높은 현재 단계에서 기본 도구 방향으로 검증 시작
	UMD_ToolDefinition* UpgradeStep = SoldTool;
	// 소모품은 제외하고 도구의 전체 업그레이드 경로 수집
	while (IsValid(UpgradeStep) == true)
	{
		// 순환 검사와 초기화 목록에 사용할 현재 단계 식별자 조회
		FPrimaryAssetId StepAssetId = UpgradeStep->GetPrimaryAssetId();
		// 잘못된 식별자와 순환 업그레이드 설정의 부분 초기화 차단
		if (StepAssetId.IsValid() == false || ToolAssetIds.Contains(StepAssetId) == true)
		{
			// 소지품 회수 이전에 잘못된 도구 경로의 판매 거부
			return EMD_ShopSaleResult::InvalidItem;
		}
		// 현재 도구 단계의 구매 이력과 잠금을 초기화 대상으로 등록
		ToolAssetIds.Add(StepAssetId);
		// 선행 도구 로드와 식별자 일치 검사에 사용할 원본 설정 조회
		FPrimaryAssetId SourceAssetId = UpgradeStep->UpgradeSourceAssetId;
		// 기본 도구에 도달하면 전체 경로 수집 종료
		if (SourceAssetId.IsValid() == false)
		{
			// 선행 단계가 없는 기본 도구까지 검증 완료
			break;
		}
		// 현재 상점에 노출되지 않은 선행 도구도 등록된 원본 에셋으로 조회
		UpgradeStep = Cast<UMD_ToolDefinition>(UAssetManager::Get().GetPrimaryAssetPath(SourceAssetId).TryLoad());
		// 누락되거나 다른 유형의 선행 정의로 불완전한 초기화가 발생하는 상황 차단
		if (IsValid(UpgradeStep) == false || UpgradeStep->GetPrimaryAssetId() != SourceAssetId)
		{
			// 도구와 이력을 보존한 잘못된 업그레이드 설정 결과 전달
			return EMD_ShopSaleResult::InvalidItem;
		}
	}
	// 도구는 슬롯 전체 반환이며 소모품은 지정 수량만 회수
	if ((IsValid(SoldTool) == true
		? Hotbar->SetItemAtSlot(SlotIndex, nullptr, 0) : Hotbar->TryConsumeItem(StoredItem, Quantity)) == false)
	{
		// 실제 회수 실패 시 대금 지급 차단
		return EMD_ShopSaleResult::InvalidRequest;
	}
	// 회수 성공 이후 기존 정산 분류로 대금 적립
	// 판매자의 실제 수령 금액과 개인별 발생 시각 기록
	AddCreditsInternal(EMD_DailyReportCategory::Other, static_cast<int32>(TotalPrice), SellingPlayerState);
	// 도구를 실제로 회수하고 대금을 확정한 뒤 업그레이드 이력 초기화
	if (IsValid(SoldTool) == true)
	{
		// 판매한 단계의 무료 재구매를 해제하고 기본 도구부터 재구매 허용
		SellingPlayerState->ResetShopToolProgress(ToolAssetIds);
	}
	// 공용 크레딧 복제와 현재 상점 화면 갱신
	OnMerchantStateChanged.Broadcast();
	// 회수 수량과 대금을 함께 확정한 판매 결과 전달
	return EMD_ShopSaleResult::Success;
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

int32 UMD_MerchantSubsystem::GetLoanLimit() const
{
	// 기본 대출 한도를 서버와 UI에 동일하게 제공
	return CalculateLoanLimit();
}

int32 UMD_MerchantSubsystem::CalculateLoanLimit()
{
	// 기본 대출 한도 2,000크레딧. 착수금 1,300이 부채로 잡힌 뒤에도 700까지 추가 대출 가능
	return 2000;
}

int32 UMD_MerchantSubsystem::GetAvailableLoanAmount() const
{
	// 이자 등으로 부채가 기본 한도를 넘은 경우 추가 대출만 차단
	return FMath::Max(0, GetLoanLimit() - DebtBalance);
}

bool UMD_MerchantSubsystem::TryBorrowCredits(int32 Amount, APlayerState* ResponsiblePlayer)
{
	// 클라이언트와 게임 월드 외 변경 및 중첩 경제 거래 차단
	if (HasServerAuthority() == false || bShopTransactionInProgress == true)
	{
		// 서버 확정 조건을 충족하지 않은 변경 요청 종료
		return false;
	}

	// 거래 가드가 해제된 다음에 대기 보상을 처리하는 종료 예약
	ON_SCOPE_EXIT
	{
		// 현재 거래와 알림에서 발생한 독립 보상 반영
		FlushPendingCreditChanges();
	};
	// 원장 변경과 최종 알림이 끝날 때까지 다른 경제 거래 차단
	TGuardValue<bool> TransactionGuard(bShopTransactionInProgress, true);

	// 잘못된 금액과 잔여 대출 한도를 초과하는 요청 차단
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
	// 대출 요청자의 실제 현금 수령 이력 기록
	AddCreditsInternal(EMD_DailyReportCategory::Other, Amount, ResponsiblePlayer);
	DebtBalance += Amount;
	// 추가 대출은 고정 이자의 기준 원금에 합산
	LoanPrincipal = FMath::Min(LoanPrincipal, MAX_int32 - Amount) + Amount;

	OnMerchantStateChanged.Broadcast();

	return true;
}

bool UMD_MerchantSubsystem::TryRepayLoan(int32 Amount, APlayerState* ResponsiblePlayer)
{
	// 클라이언트와 게임 월드 외 변경 및 중첩 경제 거래 차단
	if (HasServerAuthority() == false || bShopTransactionInProgress == true)
	{
		// 서버 확정 조건을 충족하지 않은 변경 요청 종료
		return false;
	}

	// 거래 가드가 해제된 다음에 대기 보상을 처리하는 종료 예약
	ON_SCOPE_EXIT
	{
		// 현재 거래와 알림에서 발생한 독립 보상 반영
		FlushPendingCreditChanges();
	};
	// 원장 변경과 최종 알림이 끝날 때까지 다른 경제 거래 차단
	TGuardValue<bool> TransactionGuard(bShopTransactionInProgress, true);

	// 실제 부채와 보유 크레딧 범위를 넘는 상환 요청을 거부하여 두 원장의 불일치를 방지하는 검사
	if (Amount <= 0 || Amount > DebtBalance || Amount > Credits)
	{
		return false;
	}

	// 빚 상환 실행
	// 대출 상환액을 기타 정산 분류로 기록
	// 상환 요청자의 실제 현금 차감 이력 기록
	AddCreditsInternal(EMD_DailyReportCategory::Other, -Amount, ResponsiblePlayer);
	DebtBalance -= Amount;
	// 완납 시 이자 종료. 다음 대출은 새 원금으로 시작
	if (DebtBalance <= 0)
	{
		LoanPrincipal = 0;
	}
	OnMerchantStateChanged.Broadcast();

	return true;
}

int32 UMD_MerchantSubsystem::CalculateLoanInterest(int32 InDebtBalance, int32 InLoanPrincipal)
{
	// 상환 완료 또는 원장 상한 도달 시 추가 이자 없음
	if (InDebtBalance <= 0 || InDebtBalance == MAX_int32 || InLoanPrincipal <= 0)
	{
		// 증가 가능한 부채가 없는 경우의 부과액
		return 0;
	}
	// 원금의 2%를 올림한 고정 이자. 잔액이 줄어도 금액은 유지되며 원장 상한 안에서 최소 1크레딧 보장
	int32 FixedInterest = FMath::CeilToInt32(static_cast<double>(InLoanPrincipal) * LoanInterestRate);
	return FMath::Clamp(FixedInterest, 1, MAX_int32 - InDebtBalance);
}

int32 UMD_MerchantSubsystem::ApplyDailyLoanInterest()
{
	// 클라이언트와 게임 월드 외 변경 및 중첩 경제 거래 차단
	if (HasServerAuthority() == false || bShopTransactionInProgress == true)
	{
		// 서버 확정 조건을 충족하지 않은 변경 요청 종료
		return 0;
	}

	// 거래 가드가 해제된 다음에 대기 보상을 처리하는 종료 예약
	ON_SCOPE_EXIT
	{
		// 현재 거래와 알림에서 발생한 독립 보상 반영
		FlushPendingCreditChanges();
	};
	// 원장 변경과 최종 알림이 끝날 때까지 다른 경제 거래 차단
	TGuardValue<bool> TransactionGuard(bShopTransactionInProgress, true);

	// 대출 UI와 동일한 현재 원금 기준의 실제 부과액 계산
	int32 InterestAmount = CalculateLoanInterest(DebtBalance, LoanPrincipal);
	// 변화가 없는 부채의 불필요한 복제 알림 생략
	if (InterestAmount <= 0)
	{
		// 이자 미부과 결과 반환
		return 0;
	}
	// 이전 이자를 포함한 잔액에 이번 이자 합산
	DebtBalance += InterestAmount;
	// 호스트와 클라이언트의 부채 및 대출 가능액 갱신
	OnMerchantStateChanged.Broadcast();
	// 이번 주기에 실제 추가한 이자 반환
	return InterestAmount;
}

void UMD_MerchantSubsystem::FinalizeContract()
{
	// 클라이언트와 게임 월드 외 변경 및 중첩 경제 거래 차단
	if (HasServerAuthority() == false || bShopTransactionInProgress == true)
	{
		// 서버 확정 조건을 충족하지 않은 변경 요청 종료
		return;
	}

	// 거래 가드가 해제된 다음에 대기 보상을 처리하는 종료 예약
	ON_SCOPE_EXIT
	{
		// 현재 거래와 알림에서 발생한 독립 보상 반영
		FlushPendingCreditChanges();
	};
	// 원장 변경과 최종 알림이 끝날 때까지 다른 경제 거래 차단
	TGuardValue<bool> TransactionGuard(bShopTransactionInProgress, true);

	// 이미 종료된 계약의 종료 시각과 상태 알림 중복 변경 차단
	if (IsValid(CurrentSite) == false)
	{
		// 기존 확정 원장을 유지한 중복 종료 요청 중단
		return;
	}

    // 원장을 보존한 채 계약 종료 시각 고정
    FinishCreditHistory();
	CurrentSite = nullptr;
	// 종료된 계약의 파산 예약은 다음 계약으로 넘기지 않는다
	bBankruptcyPending = false;

	UE_LOG(LogTemp, Log, TEXT("[Merchant] 계약 종료"));

	OnMerchantStateChanged.Broadcast();
}


void UMD_MerchantSubsystem::AddCreditsInternal(EMD_DailyReportCategory Category, int32 Amount, APlayerState* ResponsiblePlayer)
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
		// 잔액 이상의 차감 = 음수 진입. 원장은 0에서 멈추되 계약 즉시 종료를 예약한다
		Credits = 0;
		if (IsValid(CurrentSite) == true)
		{
			bBankruptcyPending = true;
		}
	}
	else
	{
		Credits += Amount;
	}

	// 요청액이 아닌 실제 잔액 변동을 Enum 분류의 Map 합계에 누적
	int32 AppliedAmount = Credits - CreditsBeforeChange;
	int64& CategoryTotal = DailyCreditTotals.FindOrAdd(Category);
	CategoryTotal += AppliedAmount;
    // 실제 거래가 없는 요청과 종료된 계약의 원시 로그 추가 방지
    if (AppliedAmount == 0 || IsValid(CurrentSite) == false || CreditHistoryEndSeconds >= 0.0)
    {
        return;
    }
    // 현재 계약의 서버 월드에서 발생한 금액만 기록
    UWorld* World = GetWorld();
    // 다른 맵의 시각 또는 클라이언트 로컬 금액의 혼입 차단
    if (IsValid(World) == false || World->GetNetMode() == NM_Client || CreditHistoryWorld.Get() != World)
    {
        return;
    }
    // 승인된 실제 거래 시각과 책임 플레이어 및 분류를 하나의 로그로 구성
    FMD_CreditLogEntry Entry;
    // 같은 프레임 내 거래도 발생 순서대로 보존
    Entry.ElapsedSeconds = GetCreditHistoryElapsedSeconds();
    // 책임 플레이어가 없는 금액은 팀 공용 인덱스로 기록
    Entry.PlayerIndex = RegisterCreditPlayer(ResponsiblePlayer);
    // 기존 분류별 원장과 같은 거래 원인 유지
    Entry.Category = Category;
    // 잔액 상한과 하한을 적용한 뒤의 실제 금액만 저장
    Entry.Amount = AppliedAmount;
    // 여섯 점으로 축약하기 이전의 모든 거래 보존
    CreditLog.Add(Entry);
    // 공용 거래는 개인 수익에 배분하지 않는 처리
    if (CreditPlayers.IsValidIndex(Entry.PlayerIndex) == true)
    {
        // 기존 인덱스의 개인 합계 갱신
        FMD_PlayerCreditHistory& Player = CreditPlayers[Entry.PlayerIndex];
        // 음수 손실도 허용하는 개인 순수익 누적
        Player.NetCredits += AppliedAmount;
        // 원시 수입과 지출을 합계 상쇄 이전에 별도 누적
        if (AppliedAmount > 0)
        {
            // 실제 수령한 금액의 합계
            Player.CreditsGained += AppliedAmount;
        }
        else
        {
            // int32 최솟값의 부호 변환 넘침을 방지하는 차감 합계
            Player.CreditsSpent -= static_cast<int64>(AppliedAmount);
        }
    }

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


void UMD_MerchantSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

void UMD_MerchantSubsystem::Deinitialize()
{
    // 게임 종료 중 남아 있는 보상 예약을 정리할 월드 조회
    UWorld* World = GetWorld();
    // 월드가 살아 있는 경우에만 타이머 관리자 접근
    if (IsValid(World) == true)
    {
        // 종료된 원장으로 전달되는 다음 틱 콜백 차단
        World->GetTimerManager().ClearTimer(PendingCreditFlushTimerHandle);
    }
    // 이미 월드가 종료된 경우까지 예약 식별자 초기화
    PendingCreditFlushTimerHandle.Invalidate();
    // 종료된 게임 인스턴스의 책임자 연결과 대기 보상 정리
    PendingCreditChanges.Reset();
    Super::Deinitialize();
}

void UMD_MerchantSubsystem::BeginContract(UMD_SiteDefinition* Site, int32 BaseAdvancePayment)
{
	// 클라이언트와 게임 월드 외 변경 및 중첩 경제 거래 차단
	if (HasServerAuthority() == false || bShopTransactionInProgress == true)
	{
		// 서버 확정 조건을 충족하지 않은 변경 요청 종료
		return;
	}

	// 거래 가드가 해제된 다음에 대기 보상을 처리하는 종료 예약
	ON_SCOPE_EXIT
	{
		// 현재 거래와 알림에서 발생한 독립 보상 반영
		FlushPendingCreditChanges();
	};
	// 원장 변경과 최종 알림이 끝날 때까지 다른 경제 거래 차단
	TGuardValue<bool> TransactionGuard(bShopTransactionInProgress, true);

	if (IsValid(Site) == false)
	{
		return;
	}

	// 현재 월드의 동일 활성 계약을 다시 시작하는 착수금 중복 지급 차단
	if (CurrentSite == Site && CreditHistoryWorld.Get() == GetWorld() && CreditHistoryEndSeconds < 0.0)
	{
		// 기존 시간과 개인 거래 이력을 보존한 중복 계약 요청 종료
		return;
	}

	CurrentSite = Site;
    // 이전 계약의 시간 및 개인 슬롯과 거래 원장 초기화
    CreditLog.Reset();
    // 이전 참가자 이름과 합계 제거
    CreditPlayers.Reset();
    // 이전 PlayerState의 약한 연결 정리
    CreditPlayerStates.Reset();
    // 새 계약에서 사용할 월드 시간 기준 설정
    CreditHistoryWorld = GetWorld();
    // 초기화가 완료된 실제 계약 시작 시각 기록
    CreditHistoryStartSeconds = CreditHistoryWorld.IsValid() == true ? CreditHistoryWorld->GetTimeSeconds() : 0.0;
    // 새 계약의 거래 기록 활성화
    CreditHistoryEndSeconds = -1.0;
    // 거래 순서와 무관한 참가자별 고정 슬롯 준비
    RegisterCurrentCreditPlayers();

	// 잘못된 설정값으로 팀 크레딧이 차감되지 않도록 기본 착수금을 0 이상으로 제한
	int32 ValidBaseAdvancePayment = FMath::Max(BaseAdvancePayment, 0);
	// 지급액과 부채를 같은 금액으로 기록하도록 두 원장의 잔여 범위 안에서 착수금 확정
	int32 GrantedAdvancePayment = FMath::Min(ValidBaseAdvancePayment, FMath::Min(MAX_int32 - Credits, MAX_int32 - DebtBalance));
	// 새 계약의 Enum Map 원장을 초기화한 뒤 실제 착수금을 기록
	DailyCreditTotals.Reset();
	AddCreditsInternal(EMD_DailyReportCategory::Retainer, GrantedAdvancePayment);
	// 착수금은 대출이므로 실제 수령액을 기존 미상환 부채에 합산
	DebtBalance += GrantedAdvancePayment;
	// 착수금은 고정 이자의 기준 원금에도 합산
	LoanPrincipal = FMath::Min(LoanPrincipal, MAX_int32 - GrantedAdvancePayment) + GrantedAdvancePayment;
	OnMerchantStateChanged.Broadcast();
}
