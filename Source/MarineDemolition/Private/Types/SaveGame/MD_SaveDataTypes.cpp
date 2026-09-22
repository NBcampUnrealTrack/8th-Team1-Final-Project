// Fill out your copyright notice in the Description page of Project Settings.

#include "Types/SaveGame/MD_SaveDataTypes.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "Data/Merchant/MD_QuestDefinition.h"
#include "Data/Merchant/MD_SiteDefinition.h"
#include "World/Support/MD_SupportBrace.h"
#include "World/Wall/MD_WallPiece.h"
#include "Engine/AssetManager.h"
#include "Gameplay/Contamination/MD_SurfaceStainPersistence.h"
#include "Gameplay/Persistence/MD_WorldActorPersistence.h"

#include "GameplayTags/MD_GameplayTags.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceLayout.h"
#include "Types/Lore/MD_LoreTypes.h"
#include "World/Trash/MD_TrashBin.h"

const FGameplayTag FMD_GameStateData::DataFieldKey = MDGameplayTags::Save_Data_GameState;
const FGameplayTag FMD_MerchantData::DataFieldKey = MDGameplayTags::Save_Data_Merchant;
const FGameplayTag FMD_WorldData::DataFieldKey = MDGameplayTags::Save_Data_World;

FSoftObjectPath FMD_HotbarItemData::GetItemAssetPath() const
{
	// 엔진의 등록 정보와 경로 리다이렉트를 함께 사용하는 조회
	UAssetManager* AssetManager = UAssetManager::GetIfInitialized();
	// 미초기화 관리자의 자산 조회 차단
	if (IsValid(AssetManager) == false)
	{
		// 해석할 수 없는 로딩 위치 반환
		return FSoftObjectPath();
	}
	// 새 저장의 식별자를 현재 등록 경로로 변환
	if (ItemId.IsValid() == true)
	{
		// ID 리다이렉트를 포함한 엔진의 품목 조회 결과 반환
		return AssetManager->GetPrimaryAssetPath(ItemId);
	}
	// 불완전한 식별자를 기존 경로로 대체하는 상황 차단
	if (ItemId != FPrimaryAssetId() || Item.IsNull() == true)
	{
		// 손상되거나 비어 있는 항목의 로딩 생략
		return FSoftObjectPath();
	}
	// 구형 경로를 자산 로드 없이 등록된 품목으로 조회
	FPrimaryAssetId RegisteredId = AssetManager->GetPrimaryAssetIdForPath(Item.ToSoftObjectPath());
	// 이동 규칙이 적용된 현재 품목의 등록 경로 사용
	if (RegisteredId.IsValid() == true)
	{
		// 구형 경로의 중간 자산을 로드하지 않는 현재 경로 반환
		return AssetManager->GetPrimaryAssetPath(RegisteredId);
	}
	// 엔진 설정에 명시된 이전 파일 경로 변경 규칙 조회
	FSoftObjectPath RedirectedPath = AssetManager->GetRedirectedAssetPath(Item.ToSoftObjectPath());
	// 등록 전 구형 파일의 패키지 리다이렉터도 로더에서 해석 가능하도록 유지
	return RedirectedPath.IsValid() == true ? RedirectedPath : Item.ToSoftObjectPath();
}

UMD_ItemDefinition* FMD_HotbarItemData::ResolveItemDefinition() const
{
	// 등록된 품목과 실제 정의의 일치를 검사할 엔진 관리자 조회
	UAssetManager* AssetManager = UAssetManager::GetIfInitialized();
	// 종료 중인 엔진의 자산 로딩 차단
	if (IsValid(AssetManager) == false)
	{
		// 품목 해석 실패 반환
		return nullptr;
	}
	// ID 및 구형 경로의 공통 리다이렉트 처리
	FSoftObjectPath DefinitionPath = GetItemAssetPath();
	// 잘못된 ID와 미등록 새 품목의 로딩 차단
	if (DefinitionPath.IsValid() == false)
	{
		// 유효한 로딩 대상 부재 반환
		return nullptr;
	}
	// 일괄 비동기 준비가 끝난 자산을 메모리에서 우선 재사용
	UMD_ItemDefinition* Definition = Cast<UMD_ItemDefinition>(DefinitionPath.ResolveObject());
	// 직접 호출하는 동기 복원 API의 기존 동작 유지
	if (IsValid(Definition) == false)
	{
		// 준비 단계를 거치지 않은 단일 복원에서만 동기 로딩
		Definition = Cast<UMD_ItemDefinition>(DefinitionPath.TryLoad());
	}
	// 제거된 파일과 다른 자산 종류의 Hotbar 유입 차단
	if (IsValid(Definition) == false)
	{
		// 잘못된 아이템 정의 반환
		return nullptr;
	}
	// 구형 패키지 리다이렉터도 실제 등록 객체 기준으로 품목 식별
	FPrimaryAssetId DefinitionId = ItemId.IsValid() == true ? ItemId : AssetManager->GetPrimaryAssetIdForObject(Definition);
	// 현재 등록되지 않은 구형 품목의 ID 저장 변환 차단
	if (DefinitionId.IsValid() == false)
	{
		// 등록된 아이템 식별자 부재 반환
		return nullptr;
	}
	// 정상 품목의 불필요한 문자열 리다이렉트 조회 생략
	if (Definition->GetPrimaryAssetId() != DefinitionId)
	{
		// 프로젝트가 명시한 이전 식별자의 변경 규칙 조회
		FPrimaryAssetId RedirectedId = AssetManager->GetRedirectedPrimaryAssetId(DefinitionId);
		// 실제 품목과 연결되지 않는 잘못된 등록 정보 차단
		if (RedirectedId.IsValid() == false || Definition->GetPrimaryAssetId() != RedirectedId)
		{
			// 다른 품목의 잘못된 복원 거부
			return nullptr;
		}
	}
	// 엔진의 등록 및 리다이렉트 검증을 마친 품목 반환
	return Definition;
}

bool FMD_PlayerData::IsStructurallyValid() const
{
	// 이력 형식 표시와 실제 기록이 어긋난 저장 거부
	if (bHasShopPurchaseState == false && (PurchasedShopItemIds.IsEmpty() == false || BlockedShopItemIds.IsEmpty() == false))
	{
		// 구형 데이터의 불완전한 이력 차단
		return false;
	}
	// 두 개인 이력에서 잘못된 식별자와 중복 기록 검사
	for (const TArray<FPrimaryAssetId>* ItemIds : { &PurchasedShopItemIds, &BlockedShopItemIds })
	{
		// 현재 목록에서 이미 검증한 식별자 집합
		TSet<FPrimaryAssetId> SeenIds;
		// 구매 및 금지 목록을 각각 독립적으로 검증
		for (FPrimaryAssetId ItemId : *ItemIds)
		{
			// 손상된 해금 조건과 중복 이력의 부분 적용 차단
			if (ItemId.IsValid() == false || SeenIds.Contains(ItemId) == true)
			{
				// 현재 개인 기록의 구조 오류 반환
				return false;
			}
			// 다음 식별자의 중복 검사 기준 보관
			SeenIds.Add(ItemId);
		}
	}
	// 상세 정보가 없는 기존 저장은 경로 슬롯 범위만 확인
	if (bHasHotbarState == false)
	{
		// 실제 보관함보다 긴 목록의 묵시적 잘림 방지
		return HotbarToolList.Num() <= 4;
	}
	// 빈 칸까지 포함한 전체 보관함과 선택 위치 검사
	if (HotbarItems.Num() != 4 || HotbarItems.IsValidIndex(SelectedHotbarSlot) == false)
	{
		// 불완전한 상세 상태 거부
		return false;
	}
	// 읽기 전용 저장 구조에서 슬롯별 숫자와 품목 식별 정합성 확인
	for (const FMD_HotbarItemData& Item : HotbarItems)
	{
		// 과도하거나 손상된 사용 상태의 실행 데이터 유입 차단
		if (Item.Count < 0 || FMath::IsFinite(Item.Charge) == false || Item.Charge < 0.0f
			|| FMath::IsFinite(Item.CostPerOnce) == false || Item.CostPerOnce < 0.0f)
		{
			// 잘못된 숫자 상태 거부
			return false;
		}
		// 분류나 이름 한쪽만 기록된 식별자의 경로 대체 복원 차단
		if (Item.ItemId.IsValid() == false && Item.ItemId != FPrimaryAssetId())
		{
			// 불완전한 아이템 식별자 거부
			return false;
		}
		// 새 식별자 또는 기존 경로의 존재와 보관 수량 일치 검사
		if ((Item.ItemId.IsValid() == true || Item.Item.IsNull() == false) != (Item.Count > 0)
			|| (Item.Count == 0 && (Item.Charge != 0.0f || Item.CostPerOnce != 0.0f)))
		{
			// 빈 칸의 불완전한 기록 거부
			return false;
		}
	}
	// 저장 시점에 검사할 수 있는 구조 검증 완료
	return true;
}

bool FMD_PlayerData::AreAssetsValid() const
{
	// 현재 형식의 잘못된 수량과 빈 슬롯을 자산 조회 이전에 거부
	if (IsStructurallyValid() == false)
	{
		// 복원할 수 없는 개인 기록의 실패 반환
		return false;
	}
	// 기존 파일의 장비와 업그레이드 선행 정의를 실제 적용 전에 함께 검증
	if (bHasHotbarState == false || bHasShopPurchaseState == false)
	{
		// 구형 기록만 변환용 복사본을 사용하여 원본 보호
		FMD_PlayerData Legacy = *this;
		// 이전 도구 기본값을 포함한 호환 변환 결과 반환
		return Legacy.MigrateHotbarItemIds() == true && Legacy.MigrateShopPurchaseState() == true;
	}
	// 최대 네 슬롯의 중복 검사를 추가 힙 할당 없이 처리
	TArray<FPrimaryAssetId, TInlineAllocator<4>> ItemIds;
	// 현재 형식의 원본 슬롯을 복사와 변환 없이 검사
	for (const FMD_HotbarItemData& Saved : HotbarItems)
	{
		// 명시적인 빈 칸의 자산 조회 생략
		if (Saved.Count == 0)
		{
			// 다음 슬롯의 품목 확인
			continue;
		}
		// 리다이렉트를 반영한 실제 품목 정의 조회
		UMD_ItemDefinition* Definition = Saved.ResolveItemDefinition();
		// 미등록 자산과 같은 품목의 중복 슬롯 거부
		if (IsValid(Definition) == false || ItemIds.Contains(Definition->GetPrimaryAssetId()) == true)
		{
			// 개인 기록의 불완전한 복원 차단
			return false;
		}
		// 후속 슬롯과 비교할 실제 품목 식별자 보관
		ItemIds.Add(Definition->GetPrimaryAssetId());
	}
	// 원본 변경 없이 전체 품목 검증 완료
	return true;
}

bool FMD_PlayerData::MigrateHotbarItemIds()
{
	// 손상된 기록이 정상 형식으로 변환되는 상황 차단
	if (IsStructurallyValid() == false)
	{
		// 원본 개인 기록을 유지한 채 실패 반환
		return false;
	}
	// 과거에 개인 기록이 없던 상태의 기본 장비 지급 정책 유지
	if (bHasHotbarState == false && HotbarToolList.IsEmpty() == true)
	{
		// 변환할 보관 기록이 없는 정상 종료
		return true;
	}
	// 모든 품목 검증을 마칠 때까지 원본 경로와 상태 보호
	FMD_PlayerData Converted = *this;
	// 수량과 충전량이 없던 최초 저장 형식의 네 슬롯 구성
	if (bHasHotbarState == false)
	{
		// 과거 경로 목록과 무관한 상세 기록의 혼입 방지
		Converted.HotbarItems.Empty(4);
		// 빈 칸을 포함한 전체 슬롯 확보
		Converted.HotbarItems.SetNum(4);
		// 기록되지 않은 선택 위치를 유효한 기본 범위로 제한
		Converted.SelectedHotbarSlot = FMath::Clamp(SelectedHotbarSlot, 0, 3);
		// 기존 목록의 배치 순서와 품목을 상세 슬롯으로 연결
		for (int32 Index = 0; Index < HotbarToolList.Num(); ++Index)
		{
			// 과거 경로를 식별자로 해석할 임시 연결
			Converted.HotbarItems[Index].Item = HotbarToolList[Index];
			// 수량이 없던 기록의 기존 복원 기본값 적용
			Converted.HotbarItems[Index].Count = HotbarToolList[Index].IsNull() == true ? 0 : 1;
		}
	}
	// 최대 네 품목의 중복 검사를 추가 힙 할당 없이 처리
	TArray<FPrimaryAssetId, TInlineAllocator<4>> ItemIds;
	// 접속 중인 Pawn이 없는 기록도 실제 아이템 정의로 검증
	for (FMD_HotbarItemData& Saved : Converted.HotbarItems)
	{
		// 품목이 없던 빈 슬롯의 위치 유지
		if (Saved.Count == 0)
		{
			// 빈 슬롯의 자산 조회 생략
			continue;
		}
		// 식별자와 기존 경로를 공통 등록 체계로 해석
		UMD_ItemDefinition* Definition = Saved.ResolveItemDefinition();
		// 해석 실패와 같은 품목의 중복 슬롯 차단
		if (IsValid(Definition) == false || ItemIds.Contains(Definition->GetPrimaryAssetId()) == true)
		{
			// 변환 도중의 부분 결과를 원본에 반영하지 않고 실패 반환
			return false;
		}
		// 충전 상태가 없던 최초 저장만 기존 도구 기본값 보충
		if (bHasHotbarState == false)
		{
			// 일반 아이템과 도구의 사용 상태 구분
			UMD_ToolDefinition* Tool = Cast<UMD_ToolDefinition>(Definition);
			// 도구에만 충전량과 사용 비용 적용
			if (IsValid(Tool) == true)
			{
				// 최초 저장에 없던 충전량의 기존 복원 기본값 적용
				Saved.Charge = Tool->MaxCharge;
				// 무제한 도구와 충전 도구의 사용 비용 구분
				Saved.CostPerOnce = Tool->MaxCharge > 0.0f ? Tool->CostPerOnce : 0.0f;
			}
		}
		// 명시적 리다이렉트까지 반영한 현재 품목 식별자 기록
		Saved.ItemId = Definition->GetPrimaryAssetId();
		// 새 저장에서 파일 위치에 다시 의존하지 않도록 과거 경로 제거
		Saved.Item.Reset();
		// 후속 슬롯의 동일 품목 중복 검사에 식별자 등록
		ItemIds.Add(Saved.ItemId);
	}
	// 최초 저장의 경로 목록을 새 파일에 다시 기록하지 않도록 제거
	Converted.HotbarToolList.Reset();
	// 기본 상태를 보충한 결과도 전체 슬롯 기록으로 구분
	Converted.bHasHotbarState = true;
	// 변환 결과의 숫자와 빈 슬롯 정합성 최종 검사
	if (Converted.IsStructurallyValid() == false)
	{
		// 잘못된 아이템 기본값으로 인한 변환 실패 반환
		return false;
	}
	// 검증을 마친 개인 기록을 한 번에 교체
	*this = MoveTemp(Converted);
	// 경로 없는 아이템 식별자 변환 성공 반환
	return true;
}

bool FMD_TrashBinRecord::IsStructurallyValid() const
{
	const UClass* const SavedBinClass = TrashBinClass.Get();
	const FVector SavedLocation = Transform.GetLocation();
	const FVector SavedScale = Transform.GetScale3D();
	const FQuat SavedRotation = Transform.GetRotation();
	if (!StableBinId.IsValid()
		|| SourceDispenserName.IsNone()
		|| !IsValid(SavedBinClass)
		|| !SavedBinClass->IsChildOf(AMD_TrashBin::StaticClass())
		|| SavedLocation.ContainsNaN()
		|| SavedScale.ContainsNaN()
		|| SavedRotation.ContainsNaN()
		|| !SavedRotation.IsNormalized()
		|| LiquidRevision < 0)
	{
		return false;
	}

	FMD_TrashBinLiquidState SavedLiquidState;
	SavedLiquidState.LiquidUnits = LiquidUnits;
	SavedLiquidState.RewardUnits = RewardUnits;
	SavedLiquidState.Revision = LiquidRevision;
	if (!SavedLiquidState.IsValid(5.0))
	{
		return false;
	}

	const double SavedCarrierAmount =
		FMD_ScrapeRecoveryLedger::GetTotalCarrierAmount(LiquidRecoveryLots);
	const double SavedRewardUnits =
		FMD_ScrapeRecoveryLedger::GetTotalRewardUnits(LiquidRecoveryLots);
	return SavedCarrierAmount >= 0.0
		&& SavedRewardUnits >= 0.0
		&& FMath::IsNearlyEqual(
			SavedCarrierAmount,
			LiquidUnits,
			1.0e-9)
		&& FMath::IsNearlyEqual(
			SavedRewardUnits,
			RewardUnits,
			1.0e-12);
}

bool FMD_PlayerData::MigrateShopPurchaseState()
{
	// 이미 기록된 이력과 현재 장비를 임의로 합치지 않기 위한 선행 검사
	if (IsStructurallyValid() == false)
	{
		// 손상된 개인 기록을 보존한 실패 반환
		return false;
	}
	// 명시적으로 저장된 빈 이력도 그대로 유지
	if (bHasShopPurchaseState == true)
	{
		// 현재 형식의 이력 재구성 생략
		return true;
	}
	// 선행 도구 검증을 모두 마칠 때까지 원본 저장 보호
	FMD_PlayerData Converted = *this;
	// 구형 경로 목록과 상세 슬롯의 품목 해석 통일
	if (Converted.MigrateHotbarItemIds() == false)
	{
		// 해석 불가능한 장비의 부분 이력 복구 차단
		return false;
	}
	// 플레이어가 실제 보유한 도구에서만 과거 업그레이드 단계 복구
	for (FMD_HotbarItemData& Saved : Converted.HotbarItems)
	{
		// 빈 슬롯은 이력 복구 대상에서 제외
		if (Saved.Count == 0)
		{
			// 다음 보유 슬롯 검사
			continue;
		}
		// 소모품의 구매 여부를 보유 수량으로 추정하지 않기 위한 도구 분류
		UMD_ToolDefinition* Tool = Cast<UMD_ToolDefinition>(Saved.ResolveItemDefinition());
		// 각 도구 계열의 순환 참조 검사 집합
		TSet<FPrimaryAssetId> VisitedIds;
		// 보유 단계부터 기본 도구까지 실제 업그레이드 경로 확인
		while (IsValid(Tool) == true)
		{
			// 현재 단계의 전체 품목 식별자 조회
			FPrimaryAssetId ToolId = Tool->GetPrimaryAssetId();
			// 잘못된 식별자 또는 순환 경로의 무한 탐색 차단
			if (ToolId.IsValid() == false || VisitedIds.Contains(ToolId) == true)
			{
				// 원본 개인 기록을 유지한 호환 복구 실패 반환
				return false;
			}
			// 다음 선행 단계와의 순환 검사 기준 보관
			VisitedIds.Add(ToolId);
			// 보유 단계와 선행 단계의 해금 및 재구매 이력 복구
			Converted.PurchasedShopItemIds.AddUnique(ToolId);
			// 기본 도구에 도달한 경로의 탐색 종료
			if (Tool->UpgradeSourceAssetId == FPrimaryAssetId())
			{
				// 선행 단계가 없는 도구의 경로 수집 완료
				break;
			}
			// 기존 Hotbar와 같은 Asset Manager 식별자 해석 정책 적용
			FMD_HotbarItemData SourceItem;
			// 현재 단계가 소비한 선행 도구 지정
			SourceItem.ItemId = Tool->UpgradeSourceAssetId;
			// 등록된 정의에서 실제 선행 도구 조회
			Tool = Cast<UMD_ToolDefinition>(SourceItem.ResolveItemDefinition());
			// 누락되거나 잘못된 종류의 선행 정의를 부분 이력으로 확정하는 상황 차단
			if (IsValid(Tool) == false)
			{
				// 원본 저장을 유지한 복구 실패 반환
				return false;
			}
			// 상위 도구로 대체된 선행 단계의 재구매 금지 복구
			Converted.BlockedShopItemIds.AddUnique(Tool->GetPrimaryAssetId());
		}
	}
	// 이후 저장과 복원에서는 명시적인 개인 이력으로 처리
	Converted.bHasShopPurchaseState = true;
	// 모든 경로 검증이 성공한 개인 기록만 한 번에 교체
	*this = MoveTemp(Converted);
	// 기존 저장의 상점 진행 복구 성공 반환
	return true;
}

bool FMD_GameStateData::IsStructurallyValid() const
{
    // 알 수 없는 분류가 보고서에 유입되는 손상 데이터 차단
    for (const TPair<EMD_DailyReportCategory, int64>& Entry : DailyScoreTotals)
    {
        // 손실 없는 음수 감점과 양수 점수는 모두 허용
        if (Entry.Key > EMD_DailyReportCategory::Other)
        {
            // 정의되지 않은 원장 분류 거부
            return false;
        }
    }
    // 다른 일차로 오염된 점수 로그의 합산 방지
    for (const FMD_ScoreEntry& Entry : ScoreLog)
    {
        // 계약 내 일차만 점수 이벤트로 허용
        if (Entry.Day < 1 || Entry.Day > 4)
        {
            // 유효하지 않은 로그 일차 거부
            return false;
        }
    }
    // 유효한 보고서의 일차와 판정 및 마감 원인 검사
    if (LastInspectionReport.bValid == true
        && (LastInspectionReport.Day < 1 || LastInspectionReport.Day > 4
            || LastInspectionReport.Result > EMD_InspectionResult::GameOver
            || LastInspectionReport.EndReason > EMD_DayEndReason::TimeExpired))
    {
        // 잘못된 확정 보고서 거부
        return false;
    }
    // 보고서 내 분류 중복으로 정산 표시가 달라지는 상황 차단
    for (const TArray<FMD_DailyReportValue>* Totals : {&LastInspectionReport.DailyScoreTotals, &LastInspectionReport.DailyCreditTotals})
    {
        // 점수와 크레딧의 독립적인 분류 집합 구성
        TSet<EMD_DailyReportCategory> Categories;
        // 확정 보고서의 각 분류 검사
        for (const FMD_DailyReportValue& Entry : *Totals)
        {
            // 유효 범위를 벗어나거나 두 번 기록된 분류 거부
            if (Entry.Category > EMD_DailyReportCategory::Other || Categories.Contains(Entry.Category) == true)
            {
                // 손상된 표시 원장 거부
                return false;
            }
            // 다음 항목의 중복 검사에 사용
            Categories.Add(Entry.Category);
        }
    }
	// 계약 범위와 하루 길이를 벗어난 값이 시간·점검 로직을 오염시키지 않도록 하는 검사
	return ElapsedDay >= 1 && ElapsedDay <= 4 && ElapsedSeconds >= 0.0 && FMath::IsFinite(ElapsedSeconds) == true && ContractCrewCount >= 0 && ContractDifficulty >= EMD_Difficulty::Safe && ContractDifficulty <= EMD_Difficulty::Extreme;
}

bool FMD_MerchantData::IsStructurallyValid() const
{
	// 경제와 호감도 범위가 런타임 규칙에서 허용하는 범위인지 확인하는 검사
	if (Credits < 0 || DebtBalance < 0 || Affinity < -4.0f || Affinity > 5.0f || FMath::IsFinite(Affinity) == false || CurrentDay < 0 || CurrentDay > 4 || NextDepositMultiplier < 0.0f || FMath::IsFinite(NextDepositMultiplier) == false || QuestCompletions < 0)
	{
		return false;
	}

    // 당일 지출의 음수 금액을 유지하면서 분류의 유효성만 검사
    for (const TPair<EMD_DailyReportCategory, int64>& Entry : DailyCreditTotals)
    {
        // 엔진 열거형에 정의되지 않은 저장 분류 차단
        if (Entry.Key > EMD_DailyReportCategory::Other)
        {
            // 손상된 크레딧 원장 거부
            return false;
        }
    }

	// 활성 계약 없이 퀘스트 상태만 남은 손상 데이터의 부분 적용을 막기 위한 검사
	if (CurrentSite.IsNull() == true && (CurrentDay > 0 || MainQuests.Num() > 0 || RepeatQuests.Num() > 0))
	{
		return false;
	}

	// 식별자가 없거나 수량이 유효하지 않은 구매 원장이 해금과 중복 구매 판정을 오염시키지 않도록 하는 검사
	for (const TPair<FPrimaryAssetId, int32>& PurchasedItemEntry : PurchasedItemAssetCounts)
	{
		// 전체 AssetId와 양수 구매 수량을 갖춘 저장 항목만 허용
		if (PurchasedItemEntry.Key.IsValid() == false || PurchasedItemEntry.Value <= 0)
		{
			return false;
		}
	}

	// 메인 퀘스트 상태가 원본 Primary Data Asset을 다시 찾을 식별자와 유효한 진행도를 갖는지 확인하는 검사
	for (const FMD_MainQuestState& MainQuest : MainQuests)
	{
		bool bHasValidDefinitionId = MainQuest.HasValidDefinitionId();
		if (bHasValidDefinitionId == false || MainQuest.UnlockDay <= 0)
		{
			return false;
		}

		if (MainQuest.CurrentPhaseIndex < 0 || MainQuest.CurrentProgress < 0
            || MainQuest.Status > EMD_MainQuestStatus::Failed || MainQuest.FailureReason > EMD_QuestFailureReason::Destroyed)
		{
			return false;
		}
	}

	// 반복 퀘스트 상태가 Primary Data Asset과 페이즈 진행을 다시 찾을 최소 식별 정보를 갖는지 확인하는 검사
	for (const FMD_RepeatQuestState& RepeatQuest : RepeatQuests)
	{
		bool bHasValidDefinitionId = RepeatQuest.HasValidDefinitionId();
		if (bHasValidDefinitionId == false || RepeatQuest.CurrentPhaseIndex < 0 || RepeatQuest.CurrentProgress < 0
            || RepeatQuest.Status > EMD_RepeatQuestStatus::Failed)
		{
			return false;
		}
	}

	return true;
}

UMD_SiteDefinition* FMD_MerchantData::ResolveSite(TSoftObjectPtr<UMD_SiteDefinition> Site)
{
    // 실제로 저장된 현장 참조의 경로 조회
    FSoftObjectPath Path = Site.ToSoftObjectPath();
    // 자산 경로 리다이렉트를 제공하는 엔진 관리자 조회
    UAssetManager* AssetManager = UAssetManager::GetIfInitialized();
    // 엔진 등록 정보가 준비된 경우 명시적 경로 이동 반영
    if (IsValid(AssetManager) == true)
    {
        // 준비 단계와 같은 자산 경로 변경 규칙 조회
        FSoftObjectPath Redirected = AssetManager->GetRedirectedAssetPath(Path);
        // 변경 설정이 없는 경로는 패키지 리다이렉터로 해석
        if (Redirected.IsValid() == true)
        {
            // 현재 자산 경로를 실제 복원에도 사용
            Path = Redirected;
        }
    }
    // 이미 준비된 객체를 우선 사용하고 직접 동기 API 호출도 지원
    return Cast<UMD_SiteDefinition>(Path.TryLoad());
}

bool FMD_MerchantData::AreAssetsValid() const
{
    // 계약 현장 타입과 구형 경로 리다이렉터의 해석 결과 확인
    if (CurrentSite.IsNull() == false && IsValid(ResolveSite(CurrentSite)) == false)
    {
        // 필수 현장 자산 누락 거부
        return false;
    }
    // 완료 현장도 살아 있는 실제 현장 정의로 복원 가능한지 확인
    for (const TSoftObjectPtr<UMD_SiteDefinition>& Site : ClearedSites)
    {
        // 빈 경로 및 다른 타입의 자산 차단
        if (Site.IsNull() == true || IsValid(ResolveSite(Site)) == false)
        {
            // 해석할 수 없는 현장 기록 거부
            return false;
        }
    }
    // 메인과 반복 퀘스트에 동일한 정의 및 페이즈 검증 적용
    TMap<FPrimaryAssetId, TArray<int32>> QuestPhases;
    // 메인 퀘스트의 현재 단계 수집
    for (const FMD_MainQuestState& Quest : MainQuests)
    {
        // 같은 정의의 여러 사용 단계도 누락 없이 보존
        QuestPhases.FindOrAdd(Quest.DefinitionId).AddUnique(Quest.CurrentPhaseIndex);
    }
    // 반복 퀘스트의 현재 단계 수집
    for (const FMD_RepeatQuestState& Quest : RepeatQuests)
    {
        // 두 역할에서 사용하는 동일 정의의 중복 로드 방지
        QuestPhases.FindOrAdd(Quest.DefinitionId).AddUnique(Quest.CurrentPhaseIndex);
    }
    // 실제 로드된 정의를 원장 변경 전에 검증
    for (TPair<FPrimaryAssetId, TArray<int32>>& Entry : QuestPhases)
    {
        // 준비된 정의 재사용과 직접 복원 API의 동기 로드 호환
        UMD_QuestDefinition* Definition = UMD_QuestDefinition::LoadDefinition(Entry.Key);
        // 목표가 사라진 반복 퀘스트도 메인 퀘스트와 동일하게 거부
        if (IsValid(Definition) == false || Definition->IsValidDefinition() == false)
        {
            // 잘못된 퀘스트 정의의 부분 적용 차단
            return false;
        }
        // 저장된 모든 역할의 페이즈 존재 여부 확인
        for (int32 Phase : Entry.Value)
        {
            // 에셋 편집으로 제거된 단계의 진행 복원 차단
            if (Definition->HasPhase(Phase) == false)
            {
                // 사라진 페이즈 거부
                return false;
            }
        }
    }
    // 현장 및 퀘스트 복원 준비 완료
    return true;
}

bool FMD_WorldData::IsStructurallyValid() const
{
    // 추가 월드 기록의 클래스와 식별자 및 수치 무결성 확인
    if (FMD_WorldActorPersistence::Validate(ActorSnapshot) == false)
    {
        // 손상된 물품 기록의 파일 저장과 월드 적용 차단
        return false;
    }
    // 이름 목록의 중복과 빈 키로 서로 다른 월드 상태가 덮이는 상황 차단
    for (const TArray<FName>* Names : {&SurvivingWallNames, &SupportedWallNames, &ClosedMarkerNames})
    {
        // 각 역할별 식별자 중복 검사 집합
        TSet<FName> UniqueNames;
        // 저장에 기록된 모든 월드 이름 검사
        for (FName Name : *Names)
        {
            // 빈 이름과 반복 기록 거부
            if (Name.IsNone() == true || UniqueNames.Contains(Name) == true)
            {
                // 모호한 복원 목록 차단
                return false;
            }
            // 다음 기록의 중복 비교 기준 보관
            UniqueNames.Add(Name);
        }
    }
    // 지지대의 클래스와 트랜스폼을 액터 제거 전에 검증
    for (const FMD_BraceRecord& Record : Braces)
    {
        // 추상 클래스나 손상된 회전의 동적 스폰 차단
        if (IsValid(Record.BraceClass.Get()) == false
            || Record.BraceClass->IsChildOf(AMD_SupportBrace::StaticClass()) == false
            || Record.BraceClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) == true
            || Record.Transform.ContainsNaN() == true || Record.Transform.GetRotation().IsNormalized() == false)
        {
            // 복원할 수 없는 지지대 기록 거부
            return false;
        }
        // 중복 벽 연결의 적용 방지
        TSet<FName> LinkedWalls;
        // 기록된 모든 연결 벽 검사
        for (FName Name : Record.SupportedWallNames)
        {
            // 빈 이름과 중복 연결 차단
            if (Name.IsNone() == true || LinkedWalls.Contains(Name) == true)
            {
                // 잘못된 연결 목록 반환
                return false;
            }
            // 검사한 벽 식별자 보관
            LinkedWalls.Add(Name);
        }
    }
    // 잔해의 클래스와 트랜스폼도 생성 전에 검사
    for (const FMD_PieceRecord& Record : Pieces)
    {
        // 생성할 수 없는 클래스 및 손상 위치의 조용한 누락 차단
        if (IsValid(Record.PieceClass.Get()) == false
            || Record.PieceClass->IsChildOf(AMD_WallPiece::StaticClass()) == false
            || Record.PieceClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) == true
            || Record.Transform.ContainsNaN() == true || Record.Transform.GetRotation().IsNormalized() == false)
        {
            // 불완전한 잔해 복원 거부
            return false;
        }
    }
    // 새 스냅샷이 없는 파일에만 기존 이름 기반 샘플 목록 사용
    if (ActorSnapshot.bCaptured == false)
    {
        // 최초 파일 형식에서 사용하는 샘플 기록의 모호성 검사
        TSet<FName> LegacySamples;
        // 현재 형식에서 비어 있는 호환 목록도 동일한 무결성 검사 적용
        for (const FMD_SampleRecord& Record : Samples)
        {
            // 중복 이름과 유효하지 않은 트랜스폼 차단
            if (Record.ActorName.IsNone() == true || LegacySamples.Contains(Record.ActorName) == true
                || Record.Transform.ContainsNaN() == true || Record.Transform.GetRotation().IsNormalized() == false)
            {
                // 손상된 구형 샘플 기록 거부
                return false;
            }
            // 다음 기록의 동일 이름 중복 확인
            LegacySamples.Add(Record.ActorName);
        }
    }
    const TSet<FName> Surviving(SurvivingWallNames);
    TSet<FName> PendingLoads;
    for (const FName WallName : PendingCollapseWallNames)
    {
        if (WallName.IsNone() || Surviving.Contains(WallName) || PendingLoads.Contains(WallName)) return false;
        PendingLoads.Add(WallName);
    }
    if (!FMD_SurfaceStainPersistence::Validate(SurfaceStains)) return false;
    TSet<FGuid> PendingBatchIds;
    for (const FMD_ContaminationPendingDrop& Drop : PendingContaminationDrops)
    {
        if (!Drop.IsStructurallyValid() || PendingBatchIds.Contains(Drop.BatchId)) return false;
        PendingBatchIds.Add(Drop.BatchId);
    }

	// 같은 stable ID가 두 record를 가리키거나 lot 합이 요약과 다르면 일부만 복원하지 않는다.
	TSet<FGuid> SavedTrashBinIds;
	for (const FMD_TrashBinRecord& TrashBinRecord : TrashBins)
	{
		if (!TrashBinRecord.IsStructurallyValid()
			|| SavedTrashBinIds.Contains(TrashBinRecord.StableBinId))
		{
			return false;
		}
		SavedTrashBinIds.Add(TrashBinRecord.StableBinId);
	}

	// 오염 셀의 병렬 배열 길이가 다르면 잘못된 인덱스 복원을 수행하지 않기 위한 검사
	for (const FMD_WallContaminationRecord& ContaminationRecord : WallContaminations)
	{
        int32 MaximumCellCount = FMD_ScrapeSurfaceLayout::LegacyMaximumCombinedCellCount;
        // 현재 월드 기록에는 유효한 생성 정보와 액터 및 컴포넌트 식별자 필수
        if (ContaminationRecord.Generation.IsValid() == false || ContaminationRecord.SurfaceComponentName.IsNone() == true
            || ContaminationRecord.WallName.IsNone() == true
            || FMD_ScrapeSurfaceLayout::IsLayoutMetadataValid(
                ContaminationRecord.LayoutVersion, ContaminationRecord.LayoutId,
                ContaminationRecord.GridDimensions) == false
            || (ContaminationRecord.LayoutVersion == 1
                && FMD_ScrapeSurfaceLayout::IsCarrierScaleValid(ContaminationRecord.CarrierScale) == false))
        {
            return false;
        }
        if (ContaminationRecord.GridDimensions != FIntPoint::ZeroValue)
        {
            int32 CellsPerFace = 0;
            if (!FMD_ScrapeSurfaceLayout::TryCalculateCellCounts(
                ContaminationRecord.GridDimensions, CellsPerFace, MaximumCellCount)) return false;
            if (ContaminationRecord.LayoutVersion == 1) MaximumCellCount = CellsPerFace;
        }
        if (!ContaminationRecord.ValidateCellIndices(MaximumCellCount)) return false;
	}

    // 배치기별 제거 이력의 중복 적용 검사
    TSet<FName> SpawnerNames;
	// 배치기 식별자가 없는 따개비 기록을 다른 액터에 적용하지 않기 위한 검사
	for (const FMD_BarnacleRemovalRecord& RemovalRecord : BarnacleRemovals)
	{
        // 동일 배치기의 여러 이력이나 빈 이름 거부
        if (RemovalRecord.SpawnerName.IsNone() == true || SpawnerNames.Contains(RemovalRecord.SpawnerName) == true)
        {
            // 모호한 배치기 복원 차단
            return false;
        }
        // 배치기 중복 검사 기준 보관
        SpawnerNames.Add(RemovalRecord.SpawnerName);
        // 제거된 순번의 음수 및 중복 검사
        TSet<int32> RemovedIndices;
        // 각 제거 이력의 유효성 확인
        for (int32 Index : RemovalRecord.RemovedSpawnIndices)
        {
            // 존재할 수 없는 순번과 중복 기록 차단
            if (Index < 0 || RemovedIndices.Contains(Index) == true)
            {
                // 손상된 배치 제거 기록 반환
                return false;
            }
            // 다음 순번의 중복 확인 기준 보관
            RemovedIndices.Add(Index);
        }
	}

	return true;
}

bool FMD_GameData::IsEmpty() const
{
	return UniqueName.IsEmpty() == true || MapPackagePath.IsEmpty() == true;
}

bool FMD_GameData::IsValidForLoad(bool bRequireWorldData) const
{
	// 슬롯 식별자와 프로젝트 맵 경로가 없으면 안전한 적용 대상을 결정할 수 없으므로 거부하는 검사
	if (bIsSlotSummary || SchemaVersion != CurrentSchemaVersion || IsEmpty() == true || MapPackagePath.StartsWith(TEXT("/Game/")) == false)
	{
		return false;
	}

	// 점수 기록을 복사하지 않는 읽기 전용 진행 필드 조회
	const FMD_GameStateData* GameStateData = FindDataField<FMD_GameStateData>(FMD_GameStateData::DataFieldKey);
	// 구매와 퀘스트 원장을 복사하지 않는 읽기 전용 상인 필드 조회
	const FMD_MerchantData* MerchantData = FindDataField<FMD_MerchantData>(FMD_MerchantData::DataFieldKey);
	// 필수 필드의 존재와 타입 및 내용 정합성을 함께 검사
	if (GameStateData == nullptr || MerchantData == nullptr
		|| GameStateData->IsStructurallyValid() == false || MerchantData->IsStructurallyValid() == false)
	{
		return false;
	}

	// 진행 중인 계약에서 GameState와 상인 퀘스트 기준 일차가 어긋난 슬롯을 거부하는 검사
	if (MerchantData->CurrentDay != GameStateData->ElapsedDay)
	{
		return false;
	}

    // 대용량 월드 필드를 복사하지 않고 현재 형식의 타입과 구조 검사
    const FInstancedStruct* WorldField = DataFields.Find(FMD_WorldData::DataFieldKey);
    // 파일 저장 데이터에는 월드 필드를 필수로 요구
    if (WorldField == nullptr && bRequireWorldData == true)
    {
        // 파일에서 월드 필드가 누락된 경우 거부
        return false;
    }
    // 현재 회차 이동의 의도적인 생략을 제외한 월드 필드 전체 검증
    if (WorldField != nullptr && (WorldField->IsValid() == false || WorldField->GetScriptStruct() != FMD_WorldData::StaticStruct()
        || WorldField->GetPtr<FMD_WorldData>()->IsStructurallyValid() == false))
    {
        // 잘못된 월드 타입과 손상된 기록의 적용 거부
        return false;
    }

    // 최초 저장 형식부터 포함되는 로어 발견 원장 조회
    const FMD_LoreData* LoreData = FindDataField<FMD_LoreData>(FMD_LoreData::DataFieldKey);
    // 로어 필드 누락을 빈 진행으로 대체하지 않고 거부
    if (LoreData == nullptr || LoreData->IsStructurallyValid() == false)
    {
        // 누락되거나 손상된 로어 원장의 적용 거부
        return false;
    }

	// 빈 플레이어 식별자가 다른 사용자의 개인 데이터를 가리키지 않도록 하는 검사
	for (const TPair<FString, FMD_PlayerData>& PlayerField : PlayerFields)
	{
		if (PlayerField.Key.IsEmpty() == true || PlayerField.Value.IsStructurallyValid() == false)
		{
			return false;
		}
	}

	return true;
}

bool FMD_GameData::AreAssetsValid() const
{
    // 대용량 월드 기록의 복사를 피하는 상인 필드 조회
    const FMD_MerchantData* MerchantField = FindDataField<FMD_MerchantData>(FMD_MerchantData::DataFieldKey);
    // 필수 원장의 타입 및 실제 자산을 먼저 확인
    if (MerchantField == nullptr || MerchantField->AreAssetsValid() == false)
    {
        // 현장 또는 퀘스트를 복원할 수 없는 데이터 거부
        return false;
    }
    // 오프라인 플레이어도 다른 필드가 적용되기 전에 검사
    for (const TPair<FString, FMD_PlayerData>& Entry : PlayerFields)
    {
        // 현재 기록의 복사 없이 등록 타입과 수량 및 동일 품목 중복 검사
        if (Entry.Value.AreAssetsValid() == false)
        {
            // 일부 플레이어만 복원되는 상태 차단
            return false;
        }
    }
    // 모든 필수 자산의 복원 가능성 확인 완료
    return true;
}
