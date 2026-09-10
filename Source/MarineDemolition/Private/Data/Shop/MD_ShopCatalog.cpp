// Fill out your copyright notice in the Description page of Project Settings.


#include "Data/Shop/MD_ShopCatalog.h"
#include "Data/Equipment/MD_ItemDefinition.h"
#include "Engine/AssetManager.h"
#include "Util/MD_LogUtility.h"

UMD_ItemDefinition* FMD_ShopItem::GetDefinition() const
{
	// 에셋 로드 시스템의 초기화 상태 조회
	UAssetManager* AssetManager = UAssetManager::GetIfInitialized();
	// 잘못된 ID 또는 준비되지 않은 로드 시스템의 조회 차단
	if (ItemAssetId.IsValid() == false || IsValid(AssetManager) == false)
	{
		// 아직 사용할 수 없는 정의 부재 전달
		return nullptr;
	}
	// 저장한 전체 ID에 해당하는 로드된 공통 정의 조회
	UMD_ItemDefinition* Definition = AssetManager->GetPrimaryAssetObject<UMD_ItemDefinition>(ItemAssetId);
	// 등록 정보와 실제 정의 ID가 일치하는 객체만 제공
	return IsValid(Definition) == true && Definition->GetPrimaryAssetId() == ItemAssetId ? Definition : nullptr;
}

bool FMD_ShopItem::IsUnlocked(float Affinity, int32 QuestCompletions, const TArray<FPrimaryAssetId>& OwnedItemAssetIds) const
{
	// 미충족 진행 조건의 해금 차단
	if ((bRequiresAffinity == true && Affinity < AffinityRequired) || (QuestCompletionsRequired > 0 && QuestCompletions < QuestCompletionsRequired))
	{
		// 조건을 충족하지 않은 품목의 잠금 유지
		return false;
	}
	// 유효한 선행 조건이 지정된 경우에만 전체 AssetId의 보유 여부 확인
	if ((PrerequisiteItemAssetId.IsValid() == true && OwnedItemAssetIds.Contains(PrerequisiteItemAssetId) == false))
	{
		// 조건을 충족하지 않은 품목의 잠금 유지
		return false;
	}
	// 모든 진행 및 선행 에셋 보유 조건 통과
	return true;
}

FText FMD_ShopItem::GetUnlockConditionText() const
{
	TArray<FString> Parts;

	if (bRequiresAffinity)
	{
		// 기획서 표기를 그대로 따른다: `호감 ≥0`, `호감 ≥+1`
		const FString Sign = AffinityRequired > 0.f ? TEXT("+") : TEXT("");
		Parts.Add(FString::Printf(TEXT("호감 ≥%s%g"), *Sign, AffinityRequired));
	}

	if (QuestCompletionsRequired > 0)
	{
		Parts.Add(FString::Printf(TEXT("퀘스트 %d회"), QuestCompletionsRequired));
	}

	// 선행 품목이 필요한 경우 공통 정의의 표시 이름으로 조건 구성
	if (PrerequisiteItemAssetId.IsValid() == true)
	{
		// 선행 에셋의 공통 정의 조회 항목 구성
		FMD_ShopItem PrerequisiteItem;
		// 설정된 전체 식별자를 그대로 사용하여 다른 에셋 타입과의 혼동 방지
		PrerequisiteItem.ItemAssetId = PrerequisiteItemAssetId;
		// 준비된 선행 품목 정의에서 표시 이름 조회
		UMD_ItemDefinition* PrerequisiteDefinition = PrerequisiteItem.GetDefinition();
		// 표시 이름이 있는 선행 품목의 보유 조건 생성
		if (IsValid(PrerequisiteDefinition) == true && PrerequisiteDefinition->ItemName.IsEmpty() == false)
		{
			// 예를 들어 열선 헤라 보유 형태의 사용자 문구 추가
			Parts.Add(FText::Format(NSLOCTEXT("MDShop", "PrerequisiteItemOwned", "{0} 보유"), PrerequisiteDefinition->ItemName).ToString());
		}
		else
		{
			// 이름 조회 실패 시에도 해금 조건을 유지하는 기본 문구 추가
			Parts.Add(NSLOCTEXT("MDShop", "PrerequisiteItemOwnedWithoutName", "선행 아이템 보유").ToString());
		}
	}

	if (Parts.Num() == 0)
	{
		return FText::FromString(TEXT("상점 상시"));
	}

	return FText::FromString(FString::Join(Parts, TEXT(" · ")));
}

FText FMD_ShopItem::GetPriceText() const
{
	if (Price <= 0)
	{
		// `07` §4에서 기본 장비는 "0 (지급)"으로 표기한다.
		return FText::FromString(TEXT("지급"));
	}

	return FText::FromString(bPricePerUnit
		? FString::Printf(TEXT("%d₡/개"), Price)
		: FString::Printf(TEXT("%d₡"), Price));
}

TArray<FMD_ShopItem> UMD_ShopCatalog::GetItemsInCategory(EMD_ShopCategory InCategory) const
{
	TArray<FMD_ShopItem> Result;

	for (const FMD_ShopItem& Item : Items)
	{
		if (Item.Category == InCategory)
		{
			Result.Add(Item);
		}
	}

	return Result;
}

FPrimaryAssetId UMD_ShopCatalog::GetItemAssetId(FMD_ShopItem Item)
{
	// BP 호출에 판매 항목 정의의 전체 식별자 전달
	return Item.GetItemAssetId();
}

UMD_ItemDefinition* UMD_ShopCatalog::GetItemDefinition(FMD_ShopItem Item)
{
	// BP에도 동기 로드 없이 준비된 정의만 전달
	return Item.GetDefinition();
}

bool UMD_ShopCatalog::IsItemDefinitionsLoaded()
{
	// 로드된 객체가 있어도 모호한 판매 목록은 준비 완료에서 제외
	TSet<FPrimaryAssetId> AssetIds;
	// 전체 목록을 표시하기 전에 각 정의의 로드 완료 여부 확인
	for (FMD_ShopItem& Item : Items)
	{
		// 등록 누락 또는 로드되지 않은 품목이 있는 목록 차단
		if (IsValid(Item.GetDefinition()) == false || AssetIds.Contains(Item.ItemAssetId) == true)
		{
			// 아직 준비되지 않은 상점 상태 전달
			return false;
		}
		// 확인한 정의의 ID를 보관하여 중복 검사에 사용
		AssetIds.Add(Item.ItemAssetId);
		// 지정된 선행 아이템도 표시 이름을 조회할 수 있도록 준비 여부 확인
		if (Item.PrerequisiteItemAssetId.IsValid() == true)
		{
			// 선행 아이템의 정확한 전체 식별자로 공통 정의 조회 구성
			FMD_ShopItem PrerequisiteItem;
			// 판매 항목의 타입과 독립적인 선행 식별자 전달
			PrerequisiteItem.ItemAssetId = Item.PrerequisiteItemAssetId;
			// 선행 정의가 없거나 준비되지 않은 목록 차단
			if (IsValid(PrerequisiteItem.GetDefinition()) == false)
			{
				// 선행 정보의 로드 대기 상태 전달
				return false;
			}
		}
	}
	// 모든 판매 정의가 준비된 상태 전달
	return true;
}

TSharedPtr<FStreamableHandle> UMD_ShopCatalog::LoadItemDefinitions(FStreamableDelegate OnLoaded)
{
	// 요청한 정의를 등록 정보에서 찾을 공통 로드 시스템 조회
	UAssetManager* AssetManager = UAssetManager::GetIfInitialized();
	// 초기화 전 요청의 명시적 실패 처리
	if (IsValid(AssetManager) == false)
	{
		// 실패 원인을 프로젝트 로그로 전달
		UE_LOG(MarineDemolition, Warning, TEXT("%s 상점 정의 로드 시스템이 준비되지 않음"), *UE_CALL_LOCATIONINFO);
		// 호출자가 로딩 중 UI를 종료할 기회 제공
		OnLoaded.ExecuteIfBound();
		// 소유할 로드 요청 부재 전달
		return nullptr;
	}
	// 현재 카탈로그의 일괄 비동기 로드 대상 구성
	TArray<FPrimaryAssetId> AssetIds;
	// 판매 순서를 유지하면서 잘못된 등록과 중복 검사
	for (FMD_ShopItem& Item : Items)
	{
		// 반복 사용할 판매 정의의 전체 식별자 조회
		FPrimaryAssetId AssetId = Item.GetItemAssetId();
		// 잘못된 ID 또는 미등록 정의와 중복 판매 행의 로드 차단
		if (AssetId.IsValid() == false || AssetIds.Contains(AssetId) == true || AssetManager->GetPrimaryAssetPath(AssetId).IsValid() == false)
		{
			// 조회할 수 없는 판매 정의의 실패 사유 기록
			UE_LOG(MarineDemolition, Warning, TEXT("%s 상점 AssetId가 유효하지 않거나 미등록 또는 중복됨 (Item=%s)"), *UE_CALL_LOCATIONINFO, *AssetId.ToString());
			// 실패한 목록을 호출부에서 비울 수 있도록 완료 통지
			OnLoaded.ExecuteIfBound();
			// 불완전한 목록의 로드 요청 차단
			return nullptr;
		}
		// 검증된 정의만 요청 목록에 포함
		AssetIds.Add(AssetId);
	}
	// 판매 행의 중복 검사와 분리하여 선행 정의의 로드 대상 구성
	for (FMD_ShopItem& Item : Items)
	{
		// 선행 조건을 지정하지 않은 품목의 추가 조회 생략
		if (Item.PrerequisiteItemAssetId.IsValid() == false)
		{
			// 다음 품목의 선행 정의 확인
			continue;
		}
		// 미등록 선행 에셋의 로드 차단
		if (AssetManager->GetPrimaryAssetPath(Item.PrerequisiteItemAssetId).IsValid() == false)
		{
			// 선행 정의 설정 오류의 프로젝트 로그 기록
			UE_LOG(MarineDemolition, Warning, TEXT("%s 선행 아이템 AssetId가 미등록됨 (Item=%s)"), *UE_CALL_LOCATIONINFO, *Item.PrerequisiteItemAssetId.ToString());
			// 잘못된 선행 조건을 호출부에서도 확인할 수 있도록 완료 통지
			OnLoaded.ExecuteIfBound();
			// 불완전한 선행 정보의 로드 요청 차단
			return nullptr;
		}
		// 여러 판매 행이 같은 선행 품목을 요구해도 한 번만 로드
		AssetIds.AddUnique(Item.PrerequisiteItemAssetId);
	}
	// 판매 항목이 없는 카탈로그의 불필요한 로드 생략
	if (AssetIds.IsEmpty() == true)
	{
		// 빈 상점의 화면 갱신 통지
		OnLoaded.ExecuteIfBound();
		// 유지할 에셋이 없는 요청 종료
		return nullptr;
	}
	// 호출자별 핸들로 수명을 관리하여 다른 상점의 로드 상태 보존
	return AssetManager->PreloadPrimaryAssets(AssetIds, TArray<FName>(), false, MoveTemp(OnLoaded));
}

bool UMD_ShopCatalog::FindItemByAssetId(FPrimaryAssetId ItemAssetId, FMD_ShopItem& OutItem)
{
	// 실패 시 이전 조회 결과가 남지 않도록 초기화
	OutItem = FMD_ShopItem();
	// 유효하지 않은 전체 식별자의 조회 차단
	if (ItemAssetId.IsValid() == false)
	{
		// 잘못된 판매 항목 식별자 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 조회할 상점 AssetId가 유효하지 않음"), *UE_CALL_LOCATIONINFO);
		// 조회 요청 실패 전달
		return false;
	}
	// 중복된 ID의 첫 항목을 임의 선택하지 않기 위한 위치
	int32 FoundIndex = INDEX_NONE;
	// 카탈로그 전체에서 정의의 AssetId 비교
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		// 다른 유형 또는 품목명을 가진 항목 제외
		if (Items[Index].GetItemAssetId() != ItemAssetId)
		{
			// 다음 판매 항목 확인
			continue;
		}
		// 동일 AssetId를 중복 등록한 카탈로그의 구매 차단
		if (FoundIndex != INDEX_NONE)
		{
			// 가격 또는 지급 설정을 확정할 수 없는 중복 기록
			UE_LOG(MarineDemolition, Warning, TEXT("%s 상점에 동일 AssetId가 중복됨 (Item=%s)"), *UE_CALL_LOCATIONINFO, *ItemAssetId.ToString());
			// 모호한 판매 항목 조회 실패 전달
			return false;
		}
		// 일치하는 판매 항목 위치 저장
		FoundIndex = Index;
	}
	// 판매 목록에 없는 식별자 조회 실패
	if (FoundIndex == INDEX_NONE)
	{
		// 미등록 품목의 프로젝트 로그 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 상점에 AssetId가 등록되지 않음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *ItemAssetId.ToString());
		// 미등록 품목 조회 실패 전달
		return false;
	}
	// 유일하게 일치한 판매 항목 복사
	OutItem = Items[FoundIndex];
	// 정확한 AssetId 조회 성공 전달
	return true;
}
