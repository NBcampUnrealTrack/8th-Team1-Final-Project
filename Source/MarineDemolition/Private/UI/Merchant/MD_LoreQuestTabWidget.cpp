// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Merchant/MD_LoreQuestTabWidget.h"
#include "Components/TextBlock.h"
#include "Components/WrapBox.h"
#include "Data/Lore/MD_LoreCatalog.h"
#include "Data/Lore/MD_LoreDefinition.h"
#include "UI/Foundation/MD_FilledSlider.h"
#include "UI/Merchant/MD_LoreCardWidget.h"

void UMD_LoreQuestTabWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// 재구성 시 저장된 데이터로 목록과 헤더 동기화
	RefreshDisplay(true);
}

void UMD_LoreQuestTabWidget::SetLoreData(UMD_LoreCatalog* InLoreCatalog, const TArray<FPrimaryAssetId>& InDiscoveredLoreAssetIds, FText InSiteName)
{
	// 카탈로그나 획득 원장이 바뀐 경우에만 카드 갱신 요청
	bool bUpdateCards = LoreCatalog != InLoreCatalog || DiscoveredLoreAssetIds != InDiscoveredLoreAssetIds;
	// 데이터와 현장명의 지역화 식별자가 모두 같으면 전체 갱신 생략
	if (bUpdateCards == false && LoreSiteName.IdenticalTo(InSiteName) == true)
	{
		// 반복 전달된 동일 데이터 처리 중단
		return;
	}
	// 현장명만 바뀌었을 때 획득 원장의 불필요한 복사 생략
	if (bUpdateCards == true)
	{
		// 다음 재구성에 사용할 카탈로그 보관
		LoreCatalog = InLoreCatalog;
		// 다음 재구성에 사용할 획득 원장 보관
		DiscoveredLoreAssetIds = InDiscoveredLoreAssetIds;
	}
	// 현장명의 지역화 정보를 유지한 표시값 보관
	LoreSiteName = InSiteName;
	// 변경된 데이터에 필요한 범위만 표시 갱신
	RefreshDisplay(bUpdateCards);
}

void UMD_LoreQuestTabWidget::RefreshDisplay(bool bUpdateCards)
{
	// 현장명 누락 시 카탈로그 표시명으로 보완할 제목 준비
	FText SiteName = LoreSiteName;
	// 카탈로그 누락 시 빈 진행률로 초기화할 전체 수량
	int32 TotalLoreCount = 0;
	// 카드와 진행률에서 공유할 획득 수량
	int32 AcquiredLoreCount = 0;
	// 유효한 현장의 수량과 기본 표시명 조회
	if (IsValid(LoreCatalog) == true)
	{
		// 원본 순서를 그대로 사용할 전체 기록 수량 조회
		TotalLoreCount = LoreCatalog->LoreEntries.Num();
		// 별도로 전달된 현장명이 없는 경우 카탈로그 이름 사용
		if (SiteName.IsEmpty() == true)
		{
			// 카탈로그의 지역화된 현장명 적용
			SiteName = LoreCatalog->SiteDisplayName;
		}
	}

	// 목록 갱신 요청이 있는 경우에만 기존 카드 구성 검사
	if (bUpdateCards == true && IsValid(WB_LoreList) == true)
	{
		// 카드 클래스나 항목 수가 달라 재사용할 수 없는 구성 구분
		if (IsValid(LoreCardClass.Get()) == false || WB_LoreList->GetChildrenCount() != TotalLoreCount)
		{
			// 스크롤과 주변 위젯을 유지하면서 이전 카드만 제거
			WB_LoreList->ClearChildren();
		}
		else
		{
			// 목업이나 다른 클래스가 섞인 목록의 잘못된 재사용 방지
			for (int32 LoreIndex = 0; LoreIndex < TotalLoreCount; ++LoreIndex)
			{
				// 현재 순서의 카드 인스턴스 조회
				UWidget* ExistingCard = WB_LoreList->GetChildAt(LoreIndex);
				// 실제 생성 클래스와 일치하는 목록만 재사용
				if (IsValid(ExistingCard) == false || ExistingCard->GetClass() != LoreCardClass.Get())
				{
					// 구성이 다른 이전 목록 제거
					WB_LoreList->ClearChildren();
					// 초기화된 목록의 추가 검사 중단
					break;
				}
			}
		}
	}

	// 현장에 등록된 기록만 카드 표시와 수집 수량에 반영
	if (IsValid(LoreCatalog) == true)
	{
		// 전역 원장의 반복 선형 탐색을 줄이기 위한 이번 갱신의 조회 집합
		TSet<FPrimaryAssetId> DiscoveredIds(DiscoveredLoreAssetIds);
		// 카드 갱신과 수집 진행률 집계를 같은 순회에서 수행
		for (int32 LoreIndex = 0; LoreIndex < TotalLoreCount; ++LoreIndex)
		{
			// 중복 획득 식별자와 관계없이 현재 항목의 획득 여부 판정
			bool bIsAcquired = DiscoveredIds.Contains(LoreCatalog->LoreEntries[LoreIndex]);
			// 현재 현장의 획득 수량에만 누적
			if (bIsAcquired == true)
			{
				// 획득한 기록 한 건 반영
				++AcquiredLoreCount;
			}
			// 제목만 변경되거나 목록 구성이 없으면 카드 처리 생략
			if (bUpdateCards == false || IsValid(WB_LoreList) == false || IsValid(LoreCardClass.Get()) == false)
			{
				// 진행률 집계는 유지하면서 다음 기록으로 이동
				continue;
			}

			// 동일한 순서의 기존 카드 재사용
			UMD_LoreCardWidget* LoreCard = Cast<UMD_LoreCardWidget>(WB_LoreList->GetChildAt(LoreIndex));
			// 기존 카드가 없는 경우에만 위젯 생성
			if (IsValid(LoreCard) == false)
			{
				// 디자이너에서 지정한 클래스의 카드 생성
				LoreCard = CreateWidget<UMD_LoreCardWidget>(this, LoreCardClass);
				// 생성 실패가 다음 기록 표시를 막지 않도록 처리
				if (IsValid(LoreCard) == false)
				{
					// 생성하지 못한 카드의 갱신 생략
					continue;
				}
				// 디자이너의 WrapBox 간격과 자동 줄바꿈 설정 유지
				WB_LoreList->AddChildToWrapBox(LoreCard);
			}

			// 미획득 제목은 카드가 처리하므로 원본 조회 생략
			FText EntryName = FText::GetEmpty();
			// 획득한 기록에 대해서만 원본 제목 조회
			if (bIsAcquired == true)
			{
				// 정의 누락 시 이전 동작과 같은 제목 대체 문구 준비
				EntryName = NSLOCTEXT("MDLoreTab", "MissingTitle", "???");
				// 획득한 기록의 정의를 한 번 조회
				UMD_LoreDefinition* LoreDefinition = LoreCatalog->GetLoreDefinition(LoreIndex);
				// 유효한 원본 제목의 지역화 정보 유지
				if (IsValid(LoreDefinition) == true)
				{
					// 획득한 기록의 실제 제목 적용
					EntryName = LoreDefinition->EntryName;
				}
			}
			// 기존 카드의 제목과 획득 연출을 현재 데이터로 동기화
			LoreCard->SetLoreEntry(LoreIndex, EntryName, bIsAcquired);
		}
	}

	// 사이트와 카탈로그에 제목이 없는 경우 기본 표시명 준비
	if (SiteName.IsEmpty() == true)
	{
		// 기본 현장 제목의 지역화 정보 유지
		SiteName = NSLOCTEXT("MDLoreTab", "DefaultSiteName", "로어 기록 보관함");
	}
	// 실제 현장명이 변경된 경우에만 텍스트 적용
	if (IsValid(Txt_Location) == true && Txt_Location->GetText().IdenticalTo(SiteName) == false)
	{
		// 선택한 현장 이름 표시
		Txt_Location->SetText(SiteName);
	}
	// 수집 수량 위젯이 있을 때만 표시 문자열 구성
	if (IsValid(Txt_Progress) == true)
	{
		// 별도 수집 접두사와 중복되지 않는 수량 표시 구성
		FString ProgressString = FString::Printf(TEXT("%d / %d"), AcquiredLoreCount, TotalLoreCount);
		// 수집 수량이 변경된 경우에만 FText 생성과 표시 갱신
		if (Txt_Progress->GetText().ToString() != ProgressString)
		{
			// 현재 현장의 획득 수량과 전체 수량 표시
			Txt_Progress->SetText(FText::FromString(ProgressString));
		}
	}
	// 현재 레이아웃의 채움 슬라이더에 수집 수량 반영
	if (IsValid(Slider_Progress) == true)
	{
		// 진행률을 사용자 입력으로 바꾸지 못하도록 표시 전용 설정
		if (Slider_Progress->IsLocked() == false)
		{
			// 비활성화 없이 슬라이더 조작만 잠금
			Slider_Progress->SetLocked(true);
		}
		// 수집 수량의 시작점을 영으로 유지
		if (Slider_Progress->GetMinValue() != 0.0f)
		{
			// 수집 진행률의 최솟값 지정
			Slider_Progress->SetMinValue(0.0f);
		}
		// 빈 카탈로그에서 영으로 나누지 않는 슬라이더 범위 준비
		float ProgressMaximum = static_cast<float>(FMath::Max(TotalLoreCount, 1));
		// 전체 수량이 변경된 경우만 슬라이더 범위 갱신
		if (Slider_Progress->GetMaxValue() != ProgressMaximum)
		{
			// 현재 현장의 전체 수량을 채움 기준으로 적용
			Slider_Progress->SetMaxValue(ProgressMaximum);
		}
		// 획득 수량이 변경된 경우만 채움 값 갱신
		if (Slider_Progress->GetValue() != static_cast<float>(AcquiredLoreCount))
		{
			// 현재 현장의 획득 수량 반영
			Slider_Progress->SetValue(static_cast<float>(AcquiredLoreCount));
		}
	}
}
