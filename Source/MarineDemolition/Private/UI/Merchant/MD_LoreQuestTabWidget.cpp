// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Merchant/MD_LoreQuestTabWidget.h"
#include "Components/ProgressBar.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Data/Lore/MD_LoreCatalog.h"
#include "Data/Lore/MD_LoreDefinition.h"
#include "UI/Merchant/MD_LoreCardWidget.h"

void UMD_LoreQuestTabWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// 현재 획득 원장으로 카드 구성
	RebuildLoreList();
	// 현장 이름과 수집 진행률 갱신
	RefreshLoreHeader();
}

void UMD_LoreQuestTabWidget::SetLoreData(UMD_LoreCatalog* InLoreCatalog, const TArray<FPrimaryAssetId>& InDiscoveredLoreAssetIds, FText InSiteName)
{
	// 동일한 원장 갱신에서 카드 전체 재생성을 피하기 위한 변경 확인
	bool bListChanged = LoreCatalog != InLoreCatalog || DiscoveredLoreAssetIds != InDiscoveredLoreAssetIds;
	// 현장명까지 동일하면 하위 텍스트 및 진행률 갱신 생략
	if (bListChanged == false && LoreSiteName.EqualTo(InSiteName) == true)
	{
		return;
	}
	// 현재 현장명 보관
	LoreSiteName = InSiteName;
	// 카탈로그 또는 획득 원장이 바뀐 경우에만 카드 구성
	if (bListChanged == true)
	{
		// 변경된 카탈로그 적용
		LoreCatalog = InLoreCatalog;
		// 복제된 획득 원장 갱신
		DiscoveredLoreAssetIds = InDiscoveredLoreAssetIds;
		// 변경된 데이터에 대한 카드 재구성
		RebuildLoreList();
	}
	// 변경된 현장명과 수집 진행률 반영
	RefreshLoreHeader();
}

void UMD_LoreQuestTabWidget::RebuildLoreList()
{
	// 목록 컨테이너 누락 여부 확인
	if (IsValid(SL_LoreList) == false)
	{
		// 표시 대상 부재로 갱신 중단
		return;
	}

	// 목업 또는 이전 카탈로그 카드 제거
	SL_LoreList->ClearChildren();
	// 목록 생성에 필요한 카탈로그와 카드 클래스 확인
	if (IsValid(LoreCatalog) == false || IsValid(LoreCardClass.Get()) == false)
	{
		// 생성 데이터 부재로 빈 목록 유지
		return;
	}

	// 카탈로그에 등록된 전체 항목 수 조회
	int32 LoreCount = LoreCatalog->LoreEntries.Num();
	// 정의 에셋 로드 여부와 무관하게 모든 등록 항목의 획득 상태 표시
	for (int32 LoreIndex = 0; LoreIndex < LoreCount; ++LoreIndex)
	{
		// 현재 카드의 로어 식별자 조회
		FPrimaryAssetId LoreAssetId = LoreCatalog->LoreEntries[LoreIndex];
		// 해독 선행 조건과 무관한 획득 여부 확인
		bool bIsAcquired = DiscoveredLoreAssetIds.Contains(LoreAssetId);
		// 정의가 없는 항목에 사용할 제목 준비
		FText EntryName = FText::FromString(TEXT("???"));
		// 미획득 항목의 이름 조회 생략
		if (bIsAcquired == true)
		{
			// 획득한 기록물의 제목 조회
			UMD_LoreDefinition* LoreDefinition = LoreCatalog->GetLoreDefinition(LoreIndex);
			// 제목 조회에 사용할 정의 유효성 확인
			if (IsValid(LoreDefinition) == true)
			{
				// 획득한 기록물의 이름 적용
				EntryName = LoreDefinition->EntryName;
			}
		}

		// 항목별 공통 버튼 카드 생성
		UMD_LoreCardWidget* LoreCard = CreateWidget<UMD_LoreCardWidget>(this, LoreCardClass);
		// 카드 생성 성공 여부 확인
		if (IsValid(LoreCard) == false)
		{
			// 생성 실패 항목 생략
			continue;
		}
		// 카드 번호와 제목 및 획득 여부 적용
		LoreCard->SetLoreEntry(LoreIndex, EntryName, bIsAcquired);
		// 카탈로그 순서대로 카드 배치
		SL_LoreList->AddChild(LoreCard);
	}
}

void UMD_LoreQuestTabWidget::RefreshLoreHeader()
{
	// 선택한 사이트 정의에서 전달받은 현장 이름 준비
	FText SiteName = LoreSiteName;
	// 현재 카탈로그의 획득 수량 초기화
	int32 AcquiredLoreCount = 0;
	// 현재 카탈로그의 전체 수량 초기화
	int32 TotalLoreCount = 0;
	// 집계할 카탈로그 유효성 확인
	if (IsValid(LoreCatalog) == true)
	{
		// 사이트 표시명 누락 여부 확인
		if (SiteName.IsEmpty() == true)
		{
			// 카탈로그 표시명으로 보완
			SiteName = LoreCatalog->SiteDisplayName;
		}
		// 카드 목록과 동일한 전체 항목 수 적용
		TotalLoreCount = LoreCatalog->LoreEntries.Num();
		// 현재 현장에 등록된 식별자만 획득 수량에 반영
		for (FPrimaryAssetId LoreAssetId : LoreCatalog->LoreEntries)
		{
			// 획득한 항목만 집계
			if (DiscoveredLoreAssetIds.Contains(LoreAssetId) == true)
			{
				// 현장 내 획득 수량 증가
				++AcquiredLoreCount;
			}
		}
	}

	// 빈 제목 표시 방지
	if (SiteName.IsEmpty() == true)
	{
		// 표시할 사이트 이름이 없는 경우 기본 제목 사용
		SiteName = FText::FromString(TEXT("로어 기록 보관함"));
	}

	// 현장 이름 표시 대상 확인
	if (IsValid(TXT_SiteName) == true)
	{
		// 현재 현장 이름 갱신
		TXT_SiteName->SetText(SiteName);
	}
	// 진행 수량 표시 대상 확인
	if (IsValid(TXT_LoreProgress) == true)
	{
		// 수집 진행 수량 갱신
		TXT_LoreProgress->SetText(FText::FromString(FString::Printf(TEXT("수집 진행  %d / %d"), AcquiredLoreCount, TotalLoreCount)));
	}
	// 진행 막대 표시 대상 확인
	if (IsValid(PB_LoreProgress) == true)
	{
		// 수집 진행률 갱신
		PB_LoreProgress->SetPercent((TotalLoreCount > 0 ? static_cast<float>(AcquiredLoreCount) / static_cast<float>(TotalLoreCount) : 0.0f));
	}
}
