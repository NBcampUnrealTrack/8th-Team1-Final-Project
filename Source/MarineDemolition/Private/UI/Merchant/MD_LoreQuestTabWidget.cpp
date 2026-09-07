// Fill out your copyright notice in the Description page of Project Settings.

// 로어 탭 선언 포함
#include "UI/Merchant/MD_LoreQuestTabWidget.h"
// 진행률 갱신 기능 사용
#include "Components/ProgressBar.h"
// 카드 목록 구성 기능 사용
#include "Components/ScrollBox.h"
// 현장명과 진행 문구 갱신 기능 사용
#include "Components/TextBlock.h"
// 현장별 로어 식별자 목록 사용
#include "Data/Lore/MD_LoreCatalog.h"
// 카드 제목 조회 기능 사용
#include "Data/Lore/MD_LoreDefinition.h"
// 획득 여부 카드 생성 기능 사용
#include "UI/Merchant/MD_LoreCardWidget.h"

void UMD_LoreQuestTabWidget::NativeConstruct()
{
	// 부모 위젯 초기화
	Super::NativeConstruct();
	// 현재 획득 원장으로 카드 구성
	RebuildLoreList();
	// 현장 이름과 수집 진행률 갱신
	RefreshLoreHeader();
}

void UMD_LoreQuestTabWidget::SetLoreData(UMD_LoreCatalog* InLoreCatalog, const TArray<FPrimaryAssetId>& InDiscoveredLoreAssetIds, FText InSiteName)
{
	// 표시 대상 카탈로그 저장
	LoreCatalog = InLoreCatalog;
	// 선택한 사이트 정의의 표시 이름 저장
	LoreSiteName = InSiteName;
	// 복제된 획득 원장 저장
	DiscoveredLoreAssetIds = InDiscoveredLoreAssetIds;
	// 변경된 획득 상태를 카드 목록에 반영
	RebuildLoreList();
	// 변경된 획득 수량을 진행률에 반영
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
	// 카탈로그 누락 여부 확인
	if (IsValid(LoreCatalog) == false)
	{
		// 카탈로그 부재로 빈 목록 유지
		return;
	}
	// 생성할 카드 클래스 조회
	UClass* CardWidgetClass = LoreCardClass.Get();
	// 카드 클래스 누락 여부 확인
	if (IsValid(CardWidgetClass) == false)
	{
		// 생성 클래스 부재로 빈 목록 유지
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
		// 사이트 이름이 전달되지 않은 경우 기존 카탈로그 표시명 사용
		bool bSiteNameEmpty = SiteName.IsEmpty();
		// 사이트 표시명 누락 여부 확인
		if (bSiteNameEmpty == true)
		{
			// 카탈로그 표시명으로 보완
			SiteName = LoreCatalog->SiteDisplayName;
		}
		// 카드 목록과 동일한 전체 항목 수 적용
		TotalLoreCount = LoreCatalog->LoreEntries.Num();
		// 현재 현장에 등록된 식별자만 획득 수량에 반영
		for (FPrimaryAssetId LoreAssetId : LoreCatalog->LoreEntries)
		{
			// 카드와 동일한 획득 원장 기준 확인
			bool bIsAcquired = DiscoveredLoreAssetIds.Contains(LoreAssetId);
			// 획득한 항목만 집계
			if (bIsAcquired == true)
			{
				// 현장 내 획득 수량 증가
				++AcquiredLoreCount;
			}
		}
	}

	// 모든 데이터의 표시명이 비어 있는지 확인
	bool bDisplayNameEmpty = SiteName.IsEmpty();
	// 빈 제목 표시 방지
	if (bDisplayNameEmpty == true)
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
		// 획득 수량과 전체 수량을 진행 문구로 변환
		FString ProgressString = FString::Printf(TEXT("수집 진행  %d / %d"), AcquiredLoreCount, TotalLoreCount);
		// 진행 문구 표시용 텍스트 생성
		FText ProgressText = FText::FromString(ProgressString);
		// 수집 진행 수량 갱신
		TXT_LoreProgress->SetText(ProgressText);
	}
	// 진행 막대 표시 대상 확인
	if (IsValid(PB_LoreProgress) == true)
	{
		// 빈 카탈로그의 0 나누기를 방지하는 수집 비율 계산
		float ProgressRatio = TotalLoreCount > 0 ? static_cast<float>(AcquiredLoreCount) / static_cast<float>(TotalLoreCount) : 0.0f;
		// 수집 진행률 갱신
		PB_LoreProgress->SetPercent(ProgressRatio);
	}
}
