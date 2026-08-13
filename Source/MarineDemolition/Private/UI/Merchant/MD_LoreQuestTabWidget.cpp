// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Merchant/MD_LoreQuestTabWidget.h"

#include "Components/ProgressBar.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Data/Lore/MD_LoreCatalog.h"
#include "UI/Merchant/MD_LoreCardWidget.h"

void UMD_LoreQuestTabWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 카탈로그 유무와 관계없이 일관된 초기 화면을 보장하기 위한 표시 구성
	RebuildLoreList();
	RefreshLoreHeader();
	RefreshLoreDetails();
}

void UMD_LoreQuestTabWidget::SetLoreData(UMD_LoreCatalog* InLoreCatalog, const FMD_LoreSiteProgress& InLoreProgress)
{
	// 동일 시점의 정적 정의와 복제 진행 정보를 사용하기 위한 데이터 저장
	LoreCatalog = InLoreCatalog;
	LoreProgress = InLoreProgress;
	// 다른 현장의 진행 정보가 잘못 표시되는 것을 방지하기 위한 빈 원장 대체
	if (IsValid(LoreCatalog) == true && LoreProgress.SiteId != LoreCatalog->SiteId)
	{
		LoreProgress = FMD_LoreSiteProgress();
		LoreProgress.SiteId = LoreCatalog->SiteId;
	}

	// 수집 상태 변경을 카드와 상세 정보에 동시에 반영하기 위한 전체 표시 갱신
	RebuildLoreList();
	RefreshLoreHeader();
	RefreshLoreDetails();
}

void UMD_LoreQuestTabWidget::RebuildLoreList()
{
	// 블루프린트 목록 컨테이너 누락 시 안전한 갱신 중단
	if (IsValid(SL_LoreList) == false)
	{
		return;
	}

	// 디자인 목업 카드를 실제 카탈로그 카드로 교체하기 위한 목록 초기화
	SL_LoreList->ClearChildren();
	SelectedLoreIndex = INDEX_NONE;
	// 정적 정의 또는 생성 클래스 누락 시 빈 목록 유지
	if (IsValid(LoreCatalog) == false || LoreCardClass == nullptr)
	{
		return;
	}

	// 처음 열 때 읽을 수 있는 기록을 우선 표시하기 위한 후보 인덱스
	int32 FirstReadableLoreIndex = INDEX_NONE;
	// 카탈로그 배열 순서와 화면 번호를 동일하게 유지하기 위한 카드 생성 반복
	for (int32 LoreIndex = 0; LoreIndex < LoreCatalog->LoreEntries.Num(); ++LoreIndex)
	{
		// 현재 항목의 정적 정의와 수집 원장으로 계산한 공개 상태
		const FMD_LoreDefinition& LoreDefinition = LoreCatalog->LoreEntries[LoreIndex];
		EMD_LoreEntryState LoreState = LoreCatalog->ResolveLoreState(LoreDefinition.LoreId, LoreProgress.DiscoveredLoreIds);
		// 항목별 선택 이벤트를 독립적으로 전달하기 위한 카드 인스턴스
		UMD_LoreCardWidget* LoreCard = CreateWidget<UMD_LoreCardWidget>(this, LoreCardClass);
		if (IsValid(LoreCard) == false)
		{
			continue;
		}

		// 데이터 표시와 상세 선택 및 스크롤 목록 소유 관계의 구성
		LoreCard->SetLoreEntry(LoreIndex, LoreDefinition.EntryName, LoreState);
		LoreCard->OnLoreCardSelected.AddUniqueDynamic(this, &UMD_LoreQuestTabWidget::HandleLoreCardSelected);
		SL_LoreList->AddChild(LoreCard);

		// 실제 내용을 열람할 수 있는 첫 번째 항목의 우선 선택
		if (FirstReadableLoreIndex == INDEX_NONE && LoreState != EMD_LoreEntryState::Undiscovered)
		{
			FirstReadableLoreIndex = LoreIndex;
		}
	}

	// 읽을 수 있는 기록 우선 정책과 빈 수집 상태의 첫 카드 대체 정책
	if (FirstReadableLoreIndex != INDEX_NONE)
	{
		SelectedLoreIndex = FirstReadableLoreIndex;
	}
	else if (LoreCatalog->LoreEntries.Num() > 0)
	{
		SelectedLoreIndex = 0;
	}
}

void UMD_LoreQuestTabWidget::RefreshLoreHeader()
{
	// 현장 정의가 없는 메뉴 진입에서도 유효한 빈 진행 화면을 위한 처리
	if (IsValid(LoreCatalog) == false)
	{
		if (IsValid(TXT_SiteName) == true)
		{
			TXT_SiteName->SetText(FText::FromString(TEXT("로어 기록 보관함")));
		}
		if (IsValid(TXT_LoreProgress) == true)
		{
			TXT_LoreProgress->SetText(FText::FromString(TEXT("수집 진행  0 / 0")));
		}
		if (IsValid(PB_LoreProgress) == true)
		{
			PB_LoreProgress->SetPercent(0.0f);
		}
		return;
	}

	// 유효한 카탈로그 항목만 집계하여 오래된 원장 식별자를 제외하기 위한 수집 수량
	int32 DiscoveredLoreCount = 0;
	for (const FMD_LoreDefinition& LoreDefinition : LoreCatalog->LoreEntries)
	{
		if (LoreProgress.DiscoveredLoreIds.Contains(LoreDefinition.LoreId) == true)
		{
			++DiscoveredLoreCount;
		}
	}

	// 진행 문구와 진행 막대가 공유하는 카탈로그 전체 수량
	int32 TotalLoreCount = LoreCatalog->LoreEntries.Num();
	if (IsValid(TXT_SiteName) == true)
	{
		TXT_SiteName->SetText(LoreCatalog->SiteDisplayName);
	}
	if (IsValid(TXT_LoreProgress) == true)
	{
		TXT_LoreProgress->SetText(FText::FromString(FString::Printf(TEXT("수집 진행  %d / %d"), DiscoveredLoreCount, TotalLoreCount)));
	}
	if (IsValid(PB_LoreProgress) == true)
	{
		// 기록물이 없는 카탈로그에서 0 나누기를 방지하는 진행 비율
		float ProgressRatio = TotalLoreCount > 0 ? static_cast<float>(DiscoveredLoreCount) / static_cast<float>(TotalLoreCount) : 0.0f;
		PB_LoreProgress->SetPercent(ProgressRatio);
	}
}

void UMD_LoreQuestTabWidget::RefreshLoreDetails()
{
	// 유효한 선택이 없는 상태를 명확히 안내하기 위한 빈 상세 정보
	if (IsValid(LoreCatalog) == false || LoreCatalog->LoreEntries.IsValidIndex(SelectedLoreIndex) == false)
	{
		if (IsValid(TXT_DetailTitle) == true)
		{
			TXT_DetailTitle->SetText(FText::FromString(TEXT("선택된 기록물 없음")));
		}
		if (IsValid(TXT_DetailMeta) == true)
		{
			TXT_DetailMeta->SetText(FText::GetEmpty());
		}
		if (IsValid(TXT_DetailBody) == true)
		{
			TXT_DetailBody->SetText(FText::FromString(TEXT("수집한 기록물을 선택하면 상세 정보가 표시된다.")));
		}
		if (IsValid(TXT_DetailReward) == true)
		{
			TXT_DetailReward->SetText(FText::GetEmpty());
		}
		return;
	}

	// 선택 인덱스에 대응하는 정적 정의와 현재 공개 상태
	const FMD_LoreDefinition& LoreDefinition = LoreCatalog->LoreEntries[SelectedLoreIndex];
	EMD_LoreEntryState LoreState = LoreCatalog->ResolveLoreState(LoreDefinition.LoreId, LoreProgress.DiscoveredLoreIds);
	// 완전 해독 상태를 기준으로 구성한 상세 표시 기본값
	FText DetailTitle = LoreDefinition.EntryName;
	FText DetailMeta = FText::FromString(FString::Printf(TEXT("기록물 %02d"), SelectedLoreIndex + 1));
	FText DetailBody = LoreDefinition.EntryContent;
	FText DetailReward = FText::FromString(FString::Printf(TEXT("수집 보상  %d₡"), LoreDefinition.RewardCredits));

	// 미발견 정보의 원문과 보상을 숨기기 위한 대체 표시
	if (LoreState == EMD_LoreEntryState::Undiscovered)
	{
		DetailTitle = FText::FromString(TEXT("미발견 기록물"));
		DetailMeta = FText::FromString(FString::Printf(TEXT("기록물 %02d · 미발견"), SelectedLoreIndex + 1));
		DetailBody = LoreDefinition.DiscoveryHint.IsEmpty() == false ? LoreDefinition.DiscoveryHint : FText::FromString(TEXT("아직 발견하지 못한 기록물이다."));
		DetailReward = FText::FromString(TEXT("수집 보상  비공개"));
	}
	// 선행 기록 미충족 상태에서 원문만 제한하기 위한 암호화 표시
	else if (LoreState == EMD_LoreEntryState::Encrypted)
	{
		DetailMeta = FText::FromString(FString::Printf(TEXT("기록물 %02d · 암호화됨"), SelectedLoreIndex + 1));
		DetailBody = FText::FromString(TEXT("선행 기록을 수집하면 내용을 해독할 수 있다."));
	}

	// 계산된 공개 단계별 정보를 블루프린트 상세 영역에 반영하는 처리
	if (IsValid(TXT_DetailTitle) == true)
	{
		TXT_DetailTitle->SetText(DetailTitle);
	}
	if (IsValid(TXT_DetailMeta) == true)
	{
		TXT_DetailMeta->SetText(DetailMeta);
	}
	if (IsValid(TXT_DetailBody) == true)
	{
		TXT_DetailBody->SetText(DetailBody);
	}
	if (IsValid(TXT_DetailReward) == true)
	{
		TXT_DetailReward->SetText(DetailReward);
	}
}

void UMD_LoreQuestTabWidget::HandleLoreCardSelected(int32 LoreIndex)
{
	// 제거되었거나 잘못된 카드 인덱스의 상세 접근을 차단하는 검증
	if (IsValid(LoreCatalog) == false || LoreCatalog->LoreEntries.IsValidIndex(LoreIndex) == false)
	{
		return;
	}

	// 선택된 카드와 우측 상세 영역을 연결하기 위한 인덱스 적용
	SelectedLoreIndex = LoreIndex;
	RefreshLoreDetails();
}
