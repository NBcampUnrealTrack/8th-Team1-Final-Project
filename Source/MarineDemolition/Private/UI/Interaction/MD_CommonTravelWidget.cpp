// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Interaction/MD_CommonTravelWidget.h"

#include "Components/ScrollBox.h"
#include "Groups/CommonButtonGroupBase.h"
#include "Player/MD_PlayerController.h"
#include "UI/MD_CommonButtonBase.h"
#include "World/Interaction/MD_TravelConsole.h"

void UMD_CommonTravelWidget::NativeOnActivated()
{
	Super::NativeOnActivated();

	// 화면을 다시 열 때 표준 난이도를 기본 선택값으로 사용한다.
	SelectedDifficulty = EMD_Difficulty::Standard;

	// 현재 콘솔 데이터와 단일 화면의 선택 상태를 함께 갱신한다.
	RefreshTravelLayout();
}

void UMD_CommonTravelWidget::SetOwningConsole(AMD_TravelConsole* InConsole)
{
	// 화면을 연 콘솔을 현장 데이터 조회와 출항 요청에 사용하도록 저장한다.
	OwningConsole = InConsole;

	// 새 콘솔의 첫 번째 사용 가능한 현장을 다시 선택하도록 기존 인덱스를 초기화한다.
	SelectedSiteIndex = INDEX_NONE;

	// Widget Tree가 이미 구성된 경우 전달받은 콘솔 데이터를 즉시 반영한다.
	RefreshTravelLayout();
}

void UMD_CommonTravelWidget::RefreshTravelLayout()
{
	// 기존 선택값이 현재 콘솔의 사용 가능한 현장을 가리키는지 확인한다.
	bool bHasAvailableSelection = false;
	if (IsValid(OwningConsole) == true)
	{
		// 콘솔의 현장 목록을 선택값 검증과 기본값 탐색에 사용한다.
		const TArray<FMD_TravelSiteEntry>& Sites = OwningConsole->GetSites();
		if (Sites.IsValidIndex(SelectedSiteIndex) == true)
		{
			bHasAvailableSelection = Sites[SelectedSiteIndex].bAvailable;
		}

		// 선택값이 없으면 목록에서 첫 번째 사용 가능한 현장을 기본값으로 찾는다.
		if (bHasAvailableSelection == false)
		{
			SelectedSiteIndex = INDEX_NONE;
			int32 SiteCount = Sites.Num();
			for (int32 SiteIndex = 0; SiteIndex < SiteCount; ++SiteIndex)
			{
				// 준비된 첫 번째 현장을 찾으면 기본 선택값으로 저장하고 탐색을 종료한다.
				if (Sites[SiteIndex].bAvailable == true)
				{
					SelectedSiteIndex = SiteIndex;
					break;
				}
			}
		}
	}

	// 단일 갱신 경로에서 동적 목록과 선택 상태 및 출항 가능 상태를 순서대로 반영한다.
	RebuildMapList();
	RefreshDifficultySelection();
	RefreshTravelAvailability();
}

bool UMD_CommonTravelWidget::IsHost() const
{
	// 현재 UI를 소유한 플레이어 컨트롤러를 가져온다.
	APlayerController* PlayerController = GetOwningPlayer();
	if (IsValid(PlayerController) == false)
	{
		bool bHasAuthority = false;
		return bHasAuthority;
	}

	// 플레이어 컨트롤러의 서버 권한 보유 여부를 반환한다.
	bool bHasAuthority = PlayerController->HasAuthority();
	return bHasAuthority;
}

void UMD_CommonTravelWidget::RebuildMapList()
{
	// MapCard를 배치할 실제 WBP ScrollBox가 없으면 동적 목록을 생성하지 않는다.
	if (IsValid(SB_MapList) == false)
	{
		return;
	}

	// 동적 MapCard 그룹이 없으면 현재 위젯을 Outer로 사용해 생성한다.
	if (IsValid(MapButtonGroup) == false)
	{
		// 동적 MapCard의 단일 선택 상태를 관리할 CommonButtonGroup을 생성한다.
		MapButtonGroup = NewObject<UCommonButtonGroupBase>(this);
	}
	if (IsValid(MapButtonGroup) == true)
	{
		// 이전 MapCard 참조를 제거하고 새 목록에서도 하나의 선택을 필수로 유지한다.
		MapButtonGroup->RemoveAll();
		MapButtonGroup->SetSelectionRequired(true);
	}

	// 이전에 생성한 카드와 디자이너 임시 항목을 ScrollBox에서 모두 제거한다.
	SB_MapList->ClearChildren();
	MapCards.Reset();
	MapCardSiteIndices.Reset();

	// 연결된 콘솔이 없으면 빈 맵 목록을 유지한다.
	if (IsValid(OwningConsole) == false)
	{
		return;
	}

	// 등록된 MapCard Widget Blueprint 클래스가 없으면 생성을 중단한다.
	if (IsValid(MapCardWidgetClass) == false)
	{
		return;
	}

	// 현재 플레이어 권한을 각 MapCard의 활성 상태에 공통 적용한다.
	bool bIsHost = IsHost();

	// 콘솔의 전체 현장 목록을 MapCard 생성 순서와 선택 인덱스로 사용한다.
	const TArray<FMD_TravelSiteEntry>& Sites = OwningConsole->GetSites();
	int32 SiteCount = Sites.Num();
	for (int32 SiteIndex = 0; SiteIndex < SiteCount; ++SiteIndex)
	{
		// 등록된 WidgetClass로 현재 현장의 MapCard를 생성한다.
		UMD_CommonButtonBase* MapCard = CreateWidget<UMD_CommonButtonBase>(this, MapCardWidgetClass);
		if (IsValid(MapCard) == false)
		{
			continue;
		}

		// 준비 중인 현장은 잠금 안내가 포함된 이름을 카드에 표시한다.
		FText SiteDisplayName = Sites[SiteIndex].DisplayName;
		if (Sites[SiteIndex].bAvailable == false)
		{
			SiteDisplayName = FText::Format(NSLOCTEXT("MDTravel", "SiteLocked", "{0}  (준비 중)"), Sites[SiteIndex].DisplayName);
		}

		// 현장 준비 상태와 호스트 권한을 조합해 카드 선택 가능 여부를 계산한다.
		bool bCanSelectSite = Sites[SiteIndex].bAvailable;
		if (bIsHost == false)
		{
			bCanSelectSite = false;
		}

		// MapCard의 공통 텍스트와 선택 정책을 현재 현장 데이터로 초기화한다.
		MapCard->SetContentText(SiteDisplayName);
		MapCard->SetIsSelectable(true);
		MapCard->SetIsEnabled(bCanSelectSite);

		// 모든 MapCard를 같은 선택 필수 그룹에 추가해 동시에 둘 이상 선택되지 않도록 한다.
		if (IsValid(MapButtonGroup) == true)
		{
			MapButtonGroup->AddWidget(MapCard);
		}

		// 모든 MapCard 클릭을 하나의 함수에 연결하고 생성 순서 인덱스를 함께 전달한다.
		auto& ClickedEvent = MapCard->OnClicked();
		ClickedEvent.AddUObject(this, &UMD_CommonTravelWidget::HandleMapCardClicked, SiteIndex);

		// 생성한 MapCard와 원본 현장 인덱스를 런타임 배열에 같은 순서로 보관한다.
		SB_MapList->AddChild(MapCard);
		MapCards.Add(MapCard);
		MapCardSiteIndices.Add(SiteIndex);
	}

	// 목록 생성이 끝난 뒤 현재 현장에 대응하는 카드만 선택 상태로 표시한다.
	RefreshMapCardSelection();
}

void UMD_CommonTravelWidget::RefreshMapCardSelection()
{
	// 맵 선택 그룹이 없으면 선택 상태를 변경하지 않는다.
	if (IsValid(MapButtonGroup) == false)
	{
		return;
	}

	// 생성된 카드와 원본 현장 인덱스를 같은 배열 위치에서 비교한다.
	int32 MapCardCount = MapCards.Num();
	for (int32 MapCardIndex = 0; MapCardIndex < MapCardCount; ++MapCardIndex)
	{
		// 대응하는 현장 인덱스가 없는 비정상 항목은 선택 대상에서 제외한다.
		if (MapCardSiteIndices.IsValidIndex(MapCardIndex) == false)
		{
			continue;
		}

		// 현재 현장을 나타내는 MapCard를 그룹에서 선택하고 다른 카드는 자동으로 해제한다.
		if (MapCardSiteIndices[MapCardIndex] == SelectedSiteIndex)
		{
			MapButtonGroup->SelectButtonAtIndex(MapCardIndex, false);
			return;
		}
	}
}

void UMD_CommonTravelWidget::SelectSite(int32 SiteIndex)
{
	// 호스트가 아닌 플레이어는 현장을 선택할 수 없다.
	if (IsHost() == false)
	{
		return;
	}

	// 현장 데이터를 제공할 콘솔이 없으면 선택을 중단한다.
	if (IsValid(OwningConsole) == false)
	{
		return;
	}

	// 요청한 인덱스가 실제 현장 목록에 포함되는지 확인한다.
	const TArray<FMD_TravelSiteEntry>& Sites = OwningConsole->GetSites();
	if (Sites.IsValidIndex(SiteIndex) == false)
	{
		return;
	}

	// 준비되지 않은 현장은 현재 선택값으로 저장하지 않는다.
	if (Sites[SiteIndex].bAvailable == false)
	{
		return;
	}

	// 검증한 현장 인덱스를 출항 요청에 사용할 선택값으로 저장한다.
	SelectedSiteIndex = SiteIndex;

	// 페이지 전환 없이 같은 화면의 맵 선택 스타일과 출항 상태만 갱신한다.
	RefreshMapCardSelection();
	RefreshTravelAvailability();
	ScrollSelectedMapIntoView();
}

void UMD_CommonTravelWidget::SelectPreviousMap()
{
	// 현재 선택값을 기준으로 이전 방향의 사용 가능한 맵을 선택한다.
	SelectAdjacentMap(-1);
}

void UMD_CommonTravelWidget::SelectNextMap()
{
	// 현재 선택값을 기준으로 다음 방향의 사용 가능한 맵을 선택한다.
	SelectAdjacentMap(1);
}

void UMD_CommonTravelWidget::SelectAdjacentMap(int32 Direction)
{
	// 이동 방향이 없으면 선택 인덱스를 변경하지 않는다.
	if (Direction == 0)
	{
		return;
	}

	// 현장 데이터를 제공할 콘솔이 없으면 인접 맵 탐색을 중단한다.
	if (IsValid(OwningConsole) == false)
	{
		return;
	}

	// 순환 탐색에 사용할 전체 현장 수를 저장한다.
	const TArray<FMD_TravelSiteEntry>& Sites = OwningConsole->GetSites();
	int32 SiteCount = Sites.Num();
	if (SiteCount <= 1)
	{
		return;
	}

	// 현재 선택값이 없을 때도 방향에 맞는 목록 끝에서 탐색을 시작한다.
	int32 CurrentSiteIndex = SelectedSiteIndex;
	if (Sites.IsValidIndex(CurrentSiteIndex) == false)
	{
		CurrentSiteIndex = Direction > 0 ? -1 : 0;
	}

	// 현재 위치 다음부터 전체 목록을 한 바퀴만 순환하며 선택 가능한 맵을 찾는다.
	for (int32 Step = 1; Step <= SiteCount; ++Step)
	{
		// 음수 나머지를 보정해 이전 방향에서도 유효한 순환 인덱스를 계산한다.
		int32 CandidateSiteIndex = (CurrentSiteIndex + Direction * Step) % SiteCount;
		if (CandidateSiteIndex < 0)
		{
			CandidateSiteIndex += SiteCount;
		}

		// 현재 맵으로 다시 돌아온 경우에는 선택 인덱스를 변경하지 않는다.
		if (CandidateSiteIndex == SelectedSiteIndex)
		{
			continue;
		}

		// 준비되지 않은 맵은 건너뛰고 같은 방향으로 탐색을 계속한다.
		if (Sites[CandidateSiteIndex].bAvailable == false)
		{
			continue;
		}

		// 찾은 맵을 공통 선택 함수에 전달하고 추가 탐색을 종료한다.
		SelectSite(CandidateSiteIndex);
		return;
	}
}

void UMD_CommonTravelWidget::ScrollSelectedMapIntoView()
{
	// 동적 MapCard를 담는 ScrollBox가 없으면 스크롤 갱신을 생략한다.
	if (IsValid(SB_MapList) == false)
	{
		return;
	}

	// 생성된 카드와 원본 현장 인덱스를 같은 배열 위치에서 탐색한다.
	int32 MapCardCount = MapCards.Num();
	for (int32 MapCardIndex = 0; MapCardIndex < MapCardCount; ++MapCardIndex)
	{
		// 원본 현장 인덱스가 없는 비정상 항목은 스크롤 대상에서 제외한다.
		if (MapCardSiteIndices.IsValidIndex(MapCardIndex) == false)
		{
			continue;
		}

		// 현재 선택된 현장을 나타내지 않는 카드는 건너뛴다.
		if (MapCardSiteIndices[MapCardIndex] != SelectedSiteIndex)
		{
			continue;
		}

		// 선택된 현장에 대응하는 실제 UObject 카드가 유지되는지 확인한다.
		UMD_CommonButtonBase* SelectedMapCard = MapCards[MapCardIndex];
		if (IsValid(SelectedMapCard) == false)
		{
			return;
		}

		// 선택된 MapCard가 목록 중앙에 보이도록 ScrollBox를 부드럽게 이동한다.
		SB_MapList->ScrollWidgetIntoView(SelectedMapCard, true, EDescendantScrollDestination::Center, 0.0f);
		return;
	}
}

void UMD_CommonTravelWidget::SelectIntegratedDifficulty(EMD_Difficulty Difficulty)
{
	// 현재 레이아웃에 존재하는 Easy, Normal, Hard 난이도인지 순서대로 확인한다.
	bool bIsSupportedDifficulty = Difficulty == EMD_Difficulty::Safe;
	if (bIsSupportedDifficulty == false)
	{
		bIsSupportedDifficulty = Difficulty == EMD_Difficulty::Standard;
	}
	if (bIsSupportedDifficulty == false)
	{
		bIsSupportedDifficulty = Difficulty == EMD_Difficulty::Dangerous;
	}
	if (bIsSupportedDifficulty == false)
	{
		return;
	}

	// 검증한 난이도를 출항 요청에 사용할 선택값으로 저장한다.
	SelectedDifficulty = Difficulty;

	// 세 난이도 버튼 중 선택값에 대응하는 버튼만 선택 상태로 갱신한다.
	RefreshDifficultySelection();
}

void UMD_CommonTravelWidget::RefreshDifficultySelection()
{
	// 난이도 버튼을 선택 필수 그룹으로 구성해 동시에 둘 이상 선택되지 않도록 한다.
	InitializeDifficultyButtonGroup();

	// 현재 난이도에 대응하는 WBP 버튼을 선택 대상으로 결정한다.
	UMD_CommonButtonBase* SelectedDifficultyButton = Btn_Normal;
	if (SelectedDifficulty == EMD_Difficulty::Safe)
	{
		SelectedDifficultyButton = Btn_Easy;
	}
	if (SelectedDifficulty == EMD_Difficulty::Dangerous)
	{
		SelectedDifficultyButton = Btn_Hard;
	}

	// 선택 대상 버튼과 난이도 그룹이 모두 유효할 때 그룹 선택을 갱신한다.
	if (IsValid(SelectedDifficultyButton) == true)
	{
		if (IsValid(DifficultyButtonGroup) == true)
		{
			// 그룹 안의 실제 버튼 위치를 찾아 현재 난이도 하나만 선택한다.
			int32 DifficultyButtonIndex = DifficultyButtonGroup->FindButtonIndex(SelectedDifficultyButton);
			if (DifficultyButtonIndex != INDEX_NONE)
			{
				DifficultyButtonGroup->SelectButtonAtIndex(DifficultyButtonIndex, false);
			}
		}
	}

	// 현재 플레이어 권한을 세 난이도 버튼의 활성 상태에 공통 적용한다.
	bool bIsHost = IsHost();

	// Easy 버튼이 연결된 경우 현재 플레이어 권한을 반영한다.
	if (IsValid(Btn_Easy) == true)
	{
		Btn_Easy->SetIsEnabled(bIsHost);
	}

	// Normal 버튼이 연결된 경우 현재 플레이어 권한을 반영한다.
	if (IsValid(Btn_Normal) == true)
	{
		Btn_Normal->SetIsEnabled(bIsHost);
	}

	// Hard 버튼이 연결된 경우 현재 플레이어 권한을 반영한다.
	if (IsValid(Btn_Hard) == true)
	{
		Btn_Hard->SetIsEnabled(bIsHost);
	}
}

void UMD_CommonTravelWidget::InitializeDifficultyButtonGroup()
{
	// 이미 구성된 난이도 그룹은 버튼 참조와 선택 상태를 그대로 재사용한다.
	if (IsValid(DifficultyButtonGroup) == true)
	{
		return;
	}

	// 난이도 버튼의 단일 선택 상태를 관리할 CommonButtonGroup을 생성한다.
	DifficultyButtonGroup = NewObject<UCommonButtonGroupBase>(this);
	if (IsValid(DifficultyButtonGroup) == false)
	{
		return;
	}

	// 등록되는 첫 버튼부터 항상 하나의 난이도가 선택되도록 설정한다.
	DifficultyButtonGroup->SetSelectionRequired(true);

	// Easy 버튼을 선택 가능한 그룹 항목으로 등록한다.
	if (IsValid(Btn_Easy) == true)
	{
		Btn_Easy->SetIsSelectable(true);
		DifficultyButtonGroup->AddWidget(Btn_Easy);
	}

	// Normal 버튼을 선택 가능한 그룹 항목으로 등록한다.
	if (IsValid(Btn_Normal) == true)
	{
		Btn_Normal->SetIsSelectable(true);
		DifficultyButtonGroup->AddWidget(Btn_Normal);
	}

	// Hard 버튼을 선택 가능한 그룹 항목으로 등록한다.
	if (IsValid(Btn_Hard) == true)
	{
		Btn_Hard->SetIsSelectable(true);
		DifficultyButtonGroup->AddWidget(Btn_Hard);
	}
}

void UMD_CommonTravelWidget::RefreshTravelAvailability()
{
	// 출항 버튼이 WBP에 연결되지 않은 경우 상태 갱신을 생략한다.
	if (IsValid(Btn_Travel) == false)
	{
		return;
	}

	// 호스트만 출항 버튼을 사용할 수 있도록 기본 상태를 계산한다.
	if (IsHost() == false)
	{
		Btn_Travel->SetIsEnabled(false);
		return;
	}

	// 콘솔 데이터가 없으면 출항 버튼을 비활성화한다.
	if (IsValid(OwningConsole) == false)
	{
		Btn_Travel->SetIsEnabled(false);
		return;
	}

	// 선택된 현장이 실제 목록에 포함되는지 확인한다.
	const TArray<FMD_TravelSiteEntry>& Sites = OwningConsole->GetSites();
	bool bCanTravel = Sites.IsValidIndex(SelectedSiteIndex);
	if (bCanTravel == true)
	{
		bCanTravel = Sites[SelectedSiteIndex].bAvailable;
	}

	// 최종 검증 결과를 WBP 출항 버튼의 활성 상태에 반영한다.
	Btn_Travel->SetIsEnabled(bCanTravel);
}

void UMD_CommonTravelWidget::HandleMapCardClicked(int32 SiteIndex)
{
	// 생성한 카드의 원본 현장 인덱스를 공통 현장 선택 함수에 전달한다.
	SelectSite(SiteIndex);
}

void UMD_CommonTravelWidget::RequestTravel()
{
	// 현장을 선택하지 않은 요청은 서버에 전달하지 않는다.
	// 출항 데이터를 제공한 콘솔이 유지되는지 확인한다.
	if (SelectedSiteIndex == INDEX_NONE || IsValid(OwningConsole) == false)
	{
		return;
	}

	// 서버 출항 RPC를 제공하는 프로젝트 플레이어 컨트롤러를 가져온다.
	AMD_PlayerController* MDPlayerController = Cast<AMD_PlayerController>(GetOwningPlayer());
	if (IsValid(MDPlayerController) == false)
	{
		return;
	}

	// 선택한 현장과 현재 난이도로 서버 트래블을 요청한다.
	MDPlayerController->ServerRequestTravel(OwningConsole, SelectedSiteIndex, SelectedDifficulty);

	// 출항 요청 후 현재 CommonActivatableWidget 화면을 비활성화한다.
	DeactivateWidget();
}
