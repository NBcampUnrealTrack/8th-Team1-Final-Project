// Fill out your copyright notice in the Description page of Project Settings.

#include "Types/Session/MD_PlayerCarryTypes.h"
#include "Data/Equipment/MD_ItemDefinition.h"
#include "Engine/AssetManager.h"

FSoftObjectPath FMD_HotbarItemData::GetItemAssetPath() const
{
	// 엔진의 등록 정보와 경로 리다이렉트를 함께 사용하는 조회
	UAssetManager* AssetManager = UAssetManager::GetIfInitialized();
	// 미초기화 관리자와 비어 있는 식별자의 자산 조회 차단
	if (IsValid(AssetManager) == false || ItemId.IsValid() == false)
	{
		// 해석할 수 없는 로딩 위치 반환
		return FSoftObjectPath();
	}
	// ID 리다이렉트를 포함한 엔진의 품목 조회 결과 반환
	return AssetManager->GetPrimaryAssetPath(ItemId);
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
	// 식별자 리다이렉트 처리를 거친 로딩 위치
	FSoftObjectPath DefinitionPath = GetItemAssetPath();
	// 잘못된 ID와 미등록 품목의 로딩 차단
	if (DefinitionPath.IsValid() == false)
	{
		// 유효한 로딩 대상 부재 반환
		return nullptr;
	}
	// 메모리에 있는 자산 우선 재사용
	UMD_ItemDefinition* Definition = Cast<UMD_ItemDefinition>(DefinitionPath.ResolveObject());
	// 아직 로드되지 않은 품목만 동기 로딩
	if (IsValid(Definition) == false)
	{
		// 세션 내 운반이라 소량의 동기 로딩 허용
		Definition = Cast<UMD_ItemDefinition>(DefinitionPath.TryLoad());
	}
	// 제거된 파일과 다른 자산 종류의 Hotbar 유입 차단
	if (IsValid(Definition) == false)
	{
		// 잘못된 아이템 정의 반환
		return nullptr;
	}
	// 정상 품목의 불필요한 문자열 리다이렉트 조회 생략
	if (Definition->GetPrimaryAssetId() != ItemId)
	{
		// 프로젝트가 명시한 이전 식별자의 변경 규칙 조회
		FPrimaryAssetId RedirectedId = AssetManager->GetRedirectedPrimaryAssetId(ItemId);
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
	// 이력 미수집 표시와 실제 목록의 불일치 차단
	if (bHasShopPurchaseState == false && (PurchasedShopItemIds.IsEmpty() == false || BlockedShopItemIds.IsEmpty() == false))
	{
		return false;
	}
	// 구매 이력과 구매 금지 목록의 식별자 유효성과 중복 검사
	for (const TArray<FPrimaryAssetId>* ItemIds : { &PurchasedShopItemIds, &BlockedShopItemIds })
	{
		TSet<FPrimaryAssetId> SeenIds;
		for (FPrimaryAssetId ItemId : *ItemIds)
		{
			if (ItemId.IsValid() == false || SeenIds.Contains(ItemId) == true)
			{
				return false;
			}
			SeenIds.Add(ItemId);
		}
	}
	// 핫바는 항상 네 슬롯이며 선택 슬롯은 그 범위 안
	if (HotbarItems.Num() != 4 || HotbarItems.IsValidIndex(SelectedHotbarSlot) == false)
	{
		return false;
	}
	// 각 슬롯의 수량과 충전 값 의미 검사
	for (const FMD_HotbarItemData& Item : HotbarItems)
	{
		if (Item.Count < 0 || FMath::IsFinite(Item.Charge) == false || Item.Charge < 0.0f
			|| FMath::IsFinite(Item.CostPerOnce) == false || Item.CostPerOnce < 0.0f)
		{
			return false;
		}
		// 식별자와 수량의 존재 여부 일치 및 빈 칸의 충전 값 부재
		if (Item.ItemId.IsValid() != (Item.Count > 0)
			|| (Item.Count == 0 && (Item.Charge != 0.0f || Item.CostPerOnce != 0.0f)))
		{
			return false;
		}
	}
	return true;
}
