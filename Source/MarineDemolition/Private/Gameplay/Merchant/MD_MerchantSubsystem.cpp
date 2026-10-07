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
	if (IsValid(PurchasingPlayerState) == false || PurchasingPlayerState->HasAuthority() == false || bShopTransactionInProgress == true)
	{
		return EMD_ShopPurchaseResult::InvalidRequest;
	}

	// 지급과 경제 알림이 완료될 때까지 중첩 상점 거래 차단
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

EMD_ShopSaleResult UMD_MerchantSubsystem::TrySellShopItem(AMD_PlayerState* SellingPlayerState, FMD_ShopItem ShopItem, int32 SlotIndex, int32 Quantity)
{
	// 서버 소유권과 양수 수량 및 진행 중 거래 검사
	if (IsValid(SellingPlayerState) == false || SellingPlayerState->HasAuthority() == false || Quantity <= 0 || bShopTransactionInProgress == true)
	{
		// 소지품과 원장을 변경하지 않은 요청 거부
		return EMD_ShopSaleResult::InvalidRequest;
	}
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
	AddCreditsInternal(EMD_DailyReportCategory::Other, static_cast<int32>(TotalPrice));
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

bool UMD_MerchantSubsystem::TryBorrowCredits(int32 Amount)
{
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
	AddCreditsInternal(EMD_DailyReportCategory::Other, Amount);
	DebtBalance += Amount;
	// 추가 대출은 고정 이자의 기준 원금에 합산
	LoanPrincipal = FMath::Min(LoanPrincipal, MAX_int32 - Amount) + Amount;

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

bool UMD_MerchantSubsystem::HasClearedCurrentSite() const
{
	return IsValid(CurrentSite) && ClearedSites.Contains(CurrentSite);
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




void UMD_MerchantSubsystem::FinalizeContract()
{
	CurrentSite = nullptr;

	UE_LOG(LogTemp, Log, TEXT("[Merchant] 계약 종료"));

	OnMerchantStateChanged.Broadcast();
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


void UMD_MerchantSubsystem::ResetContract()
{
	Credits = 0;
	DailyCreditTotals.Reset();
	DebtBalance = 0;
	LoanPrincipal = 0;
	CurrentSite = nullptr;
	ClearedSites.Reset();
	bConsumableDiscountActive = true;
	// 새 계약에 사용할 전체 AssetId 구매 원장 초기화
	PurchasedItemAssetCounts.Reset();
	OnMerchantStateChanged.Broadcast();
}

void UMD_MerchantSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

void UMD_MerchantSubsystem::Deinitialize()
{
    Super::Deinitialize();
}

void UMD_MerchantSubsystem::BeginContract(UMD_SiteDefinition* Site, int32 BaseAdvancePayment)
{
	if (IsValid(Site) == false)
	{
		return;
	}

	CurrentSite = Site;
	bConsumableDiscountActive = true;
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
