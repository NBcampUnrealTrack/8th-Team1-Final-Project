// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Screen/MD_EncyclopediaWidget.h"

#include "Component/Lore/MD_LoreComponent.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Data/Lore/MD_LoreCatalog.h"
#include "Data/Lore/MD_LoreDefinition.h"
#include "GameFramework/GameStateBase.h"
#include "UI/MD_CommonButtonBase.h"

void UMD_EncyclopediaWidget::NativeOnActivated()
{
	// 열림 연출이 시작되기 전에 최신 목록과 상세 내용 준비
	RefreshLore();
	Super::NativeOnActivated();
}

void UMD_EncyclopediaWidget::NativeOnDeactivated()
{
	// 닫힌 화면에 수집 및 월드 변경 알림이 전달되는 상황 방지
	UnbindLoreSource();
	Super::NativeOnDeactivated();
}

void UMD_EncyclopediaWidget::NativeDestruct()
{
	// 활성 상태로 제거되는 경우의 외부 알림 구독 정리
	UnbindLoreSource();
	// 제거된 카드에서 이전 화면으로 클릭이 전달되는 상황 방지
	ReleaseEntryWidgets();
	// 제거된 화면으로 카테고리 클릭이 전달되는 상황 방지
	ReleaseCategoryBindings();
	Super::NativeDestruct();
}

void UMD_EncyclopediaWidget::RefreshLore()
{
	// 카드 표시 이벤트에서 중첩된 목록 구성 차단
	if (bIsRefreshingEntries == true)
	{
		// 진행 중인 카드 구성을 유지한 중복 요청 종료
		return;
	}
	// 디자이너의 현재 버튼 구성과 카테고리 데이터 순서 연결
	RebuildCategoryBindings();
	// 현재 플레이어 컨텍스트의 월드 조회
	UWorld* World = GetWorld();
	// 이전 월드의 알림이 새 화면 상태를 덮어쓰는 상황 방지
	if (BoundWorld.Get() != World)
	{
		// 이전 월드와 원장에 연결된 구독 정리
		UnbindLoreSource();
	}
	// 활성 화면에서만 늦은 GameState 생성과 교체 추적
	if (IsActivated() == true && IsValid(World) == true && GameStateChangedHandle.IsValid() == false)
	{
		// 구독 해제에 사용할 현재 월드 보관
		BoundWorld = World;
		// 최초 복제 이후에도 새로운 GameState와 재연결
		GameStateChangedHandle = World->GameStateSetEvent.AddUObject(this, &ThisClass::HandleGameStateSet);
	}
	// 로컬 클라이언트에도 복제되는 GameState 조회
	AGameStateBase* GameState = IsValid(World) == true ? World->GetGameState() : nullptr;
	// 서버 전용 서브시스템 대신 복제된 수집 원장 사용
	UMD_LoreComponent* LoreSource = IsValid(GameState) == true ? GameState->FindComponentByClass<UMD_LoreComponent>() : nullptr;
	// 교체된 원장에서 이전 콜백이 남는 상황 방지
	if (BoundLoreSource.Get() != LoreSource)
	{
		// 이전 원장이 남아 있을 때 구독 해제
		if (BoundLoreSource.IsValid() == true)
		{
			// 화면 전용 구독만 제거
			BoundLoreSource->OnLoreProgressUpdated.RemoveDynamic(this, &ThisClass::HandleLoreProgressUpdated);
		}
		// 이후 갱신과 해제에 사용할 새 원장 보관
		BoundLoreSource = LoreSource;
	}
	// 활성 화면에서 수집 및 저장 복원 알림 구독
	if (IsActivated() == true && IsValid(LoreSource) == true)
	{
		// 재활성화와 명시적 새로고침의 중복 구독 방지
		LoreSource->OnLoreProgressUpdated.AddUniqueDynamic(this, &ThisClass::HandleLoreProgressUpdated);
	}
	// 원장이 없는 월드에서는 이전 세션의 발견 상태 제거
	DiscoveredLoreIds = IsValid(LoreSource) == true ? LoreSource->GetDiscoveredLoreAssetIds() : TArray<FPrimaryAssetId>();
	// 현재 설정과 선택 식별자로 목록 재구성
	RebuildEntries();
}

void UMD_EncyclopediaWidget::ApplyLoreProgress(TArray<FPrimaryAssetId> InDiscoveredLoreIds)
{
	// 동일한 원장과 카드 표시 도중의 중첩 갱신 생략
	if (bIsRefreshingEntries == true || DiscoveredLoreIds == InDiscoveredLoreIds)
	{
		// 현재 표시를 유지한 불필요한 갱신 종료
		return;
	}
	// 표시용 발견 목록 갱신
	DiscoveredLoreIds = MoveTemp(InDiscoveredLoreIds);
	// 원장 갱신에서 카드 인스턴스와 선택 위치 유지
	RefreshEntryDisplay();
	// 선행 조건이 충족되거나 회수된 기록의 상세 내용 동기화
	RefreshSelectionDisplay();
}

void UMD_EncyclopediaWidget::SelectCategory(int32 CategoryIndex)
{
	// 존재하지 않는 탭과 중복 선택 및 구성 중 입력 차단
	if (bIsRefreshingEntries == true || Categories.IsValidIndex(CategoryIndex) == false || SelectedCategoryIndex == CategoryIndex)
	{
		// 현재 카테고리를 유지한 무효 요청 종료
		return;
	}
	// 새 카테고리 선택 반영
	SelectedCategoryIndex = CategoryIndex;
	// 다른 카테고리의 기록 인덱스 사용 방지
	SelectedEntryIndex = INDEX_NONE;
	// 새 카테고리에서는 첫 번째 기록 선택
	SelectedLoreId = FPrimaryAssetId();
	// 새 탭의 데이터와 빈 목록 상태 반영
	RebuildEntries();
}

void UMD_EncyclopediaWidget::SelectEntry(int32 EntryIndex)
{
	// 카탈로그 부재와 범위를 벗어난 선택 차단
	if (bIsRefreshingEntries == true || IsValid(CurrentCatalog) == false || CurrentCatalog->LoreEntries.IsValidIndex(EntryIndex) == false)
	{
		// 현재 상세 정보를 유지한 범위 밖 입력 종료
		return;
	}
	// 현재 기록의 재클릭에서 스크롤과 연출 유지
	if (SelectedEntryIndex == EntryIndex)
	{
		// 현재 본문 위치를 유지한 중복 선택 종료
		return;
	}
	// 새 기록의 카탈로그 인덱스 저장
	SelectedEntryIndex = EntryIndex;
	// 목록 재구성 이후에도 유지할 선택 식별자 저장
	SelectedLoreId = CurrentCatalog->LoreEntries[EntryIndex];
	// 새 기록의 공개 상태와 페이지 번호 반영
	RefreshSelectionDisplay();
	// 새 본문을 처음부터 표시
	if (IsValid(DetailScroll) == true)
	{
		// 이전 기록의 스크롤 위치 제거
		DetailScroll->ScrollToStart();
	}
	// 동적 카드로 이동한 경우 선택 항목을 화면 안에 표시
	if (IsValid(SB_Entries) == true && EntryWidgets.IsValidIndex(EntryIndex) == true && IsValid(EntryWidgets[EntryIndex]) == true)
	{
		// 긴 목록에서 현재 선택을 확인할 수 있도록 스크롤 조정
		SB_Entries->ScrollWidgetIntoView(EntryWidgets[EntryIndex], true);
	}
	// Blueprint에서 사용자 선택에 맞는 페이지 연출 재생
	OnEntrySelectionChanged();
}

void UMD_EncyclopediaWidget::SelectPreviousEntry()
{
	// 첫 번째 기록 이전으로 이동하는 요청 차단
	if (SelectedEntryIndex > 0)
	{
		// 공통 선택 검증과 상세 갱신 사용
		SelectEntry(SelectedEntryIndex - 1);
	}
}

void UMD_EncyclopediaWidget::SelectNextEntry()
{
	// 마지막 기록 이후로 이동하는 요청 차단
	if (IsValid(CurrentCatalog) == true && SelectedEntryIndex >= 0 && SelectedEntryIndex + 1 < CurrentCatalog->LoreEntries.Num())
	{
		// 공통 선택 검증과 상세 갱신 사용
		SelectEntry(SelectedEntryIndex + 1);
	}
}

void UMD_EncyclopediaWidget::RebuildEntries()
{
	// 목록 구성 중 Blueprint 이벤트의 재진입 차단
	TGuardValue<bool> RefreshGuard(bIsRefreshingEntries, true);
	// 이전 카드 구독과 표시 인스턴스 정리
	ReleaseEntryWidgets();
	// 설정이 바뀐 경우 첫 탭 또는 빈 선택으로 보정
	if (Categories.IsValidIndex(SelectedCategoryIndex) == false)
	{
		// 카테고리 부재와 첫 번째 카테고리 구분
		SelectedCategoryIndex = Categories.IsEmpty() == true ? INDEX_NONE : 0;
	}
	// 카탈로그 없는 탭도 정상적인 빈 화면으로 처리
	CurrentCatalog = Categories.IsValidIndex(SelectedCategoryIndex) == true ? Categories[SelectedCategoryIndex].LoreCatalog : nullptr;
	// 이전 선택 식별자를 새 목록의 인덱스로 변환
	SelectedEntryIndex = IsValid(CurrentCatalog) == true ? CurrentCatalog->LoreEntries.IndexOfByKey(SelectedLoreId) : INDEX_NONE;
	// 선택한 기록이 없는 유효한 목록의 첫 항목 선택
	if (SelectedEntryIndex == INDEX_NONE && IsValid(CurrentCatalog) == true && CurrentCatalog->LoreEntries.IsEmpty() == false)
	{
		// 첫 번째 기록 기본 선택
		SelectedEntryIndex = 0;
	}
	// 빈 목록에서 이전 기록 식별자 제거
	SelectedLoreId = SelectedEntryIndex != INDEX_NONE ? CurrentCatalog->LoreEntries[SelectedEntryIndex] : FPrimaryAssetId();
	// 데이터와 디자이너의 목록 및 카드 클래스가 준비된 경우에만 카드 생성
	if (IsValid(CurrentCatalog) == true && IsValid(SB_Entries) == true && IsValid(EntryWidgetClass) == true)
	{
		// 개별 카드 생성 실패에도 카탈로그 인덱스 대응 유지
		EntryWidgets.SetNum(CurrentCatalog->LoreEntries.Num());
		// 카탈로그의 등록 순서대로 모든 기록 표시
		for (int32 Index = 0; Index < EntryWidgets.Num(); ++Index)
		{
			// 기존 디자이너 카드 클래스로 항목 생성
			UMD_CommonButtonBase* EntryWidget = CreateWidget<UMD_CommonButtonBase>(this, EntryWidgetClass);
			// 생성 실패 항목의 접근 방지
			if (IsValid(EntryWidget) == false)
			{
				// 생성 가능한 다음 기록으로 처리 이동
				continue;
			}
			// 현재 기록을 표시할 선택 상태 활성화
			EntryWidget->SetIsSelectable(true);
			// 선택한 기록을 재클릭해도 선택 유지
			EntryWidget->SetIsToggleable(false);
			// 포커스 이동만으로 표시 내용과 선택이 어긋나는 상황 방지
			EntryWidget->SetShouldSelectUponReceivingFocus(false);
			// 동적 카드의 실제 카탈로그 인덱스로 클릭 연결
			EntryWidget->OnClicked().AddUObject(this, &ThisClass::SelectEntry, Index);
			// 선택 표시와 갱신에 사용할 인스턴스 보관
			EntryWidgets[Index] = EntryWidget;
			// 카탈로그 순서대로 스크롤 영역에 추가
			SB_Entries->AddChild(EntryWidget);
		}
	}
	// 발견 상태에 따라 카드 이름과 번호 갱신
	RefreshEntryDisplay();
	// 기본 선택과 빈 목록 및 진행률 동기화
	RefreshSelectionDisplay();
	// 카테고리 전환과 재구성에서 이전 본문 스크롤 제거
	if (IsValid(DetailScroll) == true)
	{
		// 첫 줄부터 본문 표시
		DetailScroll->ScrollToStart();
	}
}

void UMD_EncyclopediaWidget::RefreshEntryDisplay()
{
	// 카드 표시 이벤트에서 목록을 중간에 교체하는 상황 방지
	TGuardValue<bool> RefreshGuard(bIsRefreshingEntries, true);
	// 카탈로그가 없는 화면의 기록 조회 생략
	if (IsValid(CurrentCatalog) == false)
	{
		// 표시할 카탈로그가 없는 갱신 종료
		return;
	}
	// 기존 카드 인스턴스의 공개 가능한 제목과 발견 여부 갱신
	for (int32 Index = 0; Index < EntryWidgets.Num(); ++Index)
	{
		// 실패한 카드와 변경된 카탈로그 범위 보호
		if (IsValid(EntryWidgets[Index]) == false || CurrentCatalog->LoreEntries.IsValidIndex(Index) == false)
		{
			// 유효한 다음 카드로 갱신 이동
			continue;
		}
		// 카드와 해독 상태 조회에 사용할 기록 식별자
		FPrimaryAssetId LoreId = CurrentCatalog->LoreEntries[Index];
		// 발견 여부와 해독 여부를 분리한 카드 표시 판단
		bool bDiscovered = DiscoveredLoreIds.Contains(LoreId);
		// 미발견 또는 누락된 정의의 제목 비공개
		FText EntryTitle = UndiscoveredTitle;
		// 발견된 기록만 정의에서 제목 조회
		if (bDiscovered == true)
		{
			// 공개할 제목의 정의 조회
			UMD_LoreDefinition* Definition = CurrentCatalog->GetLoreDefinition(Index);
			// 유효한 정의만 실제 제목 표시
			if (IsValid(Definition) == true && Definition->IsValidDefinition() == true)
			{
				// 발견한 기록의 이름 공개
				EntryTitle = Definition->EntryName;
			}
		}
		// 카드 재구성에도 유지되는 공통 버튼의 제목 저장
		EntryWidgets[Index]->SetContentText(EntryTitle);
		// 기존 카드 Blueprint의 번호와 발견 상태 표시 함수 연결
		OnEntryUpdated(EntryWidgets[Index], Index + 1, EntryTitle, bDiscovered);
	}
}

void UMD_EncyclopediaWidget::RefreshSelectionDisplay()
{
	// 현재 카탈로그의 전체 기록 수량
	int32 TotalCount = IsValid(CurrentCatalog) == true ? CurrentCatalog->LoreEntries.Num() : 0;
	// 현재 카탈로그에 속하는 발견 수량 집계 준비
	int32 DiscoveredCount = 0;
	// 유효한 카탈로그에 한정한 진행률 집계
	if (IsValid(CurrentCatalog) == true)
	{
		// 다른 카테고리의 발견 기록을 진행률에서 제외
		for (FPrimaryAssetId LoreId : CurrentCatalog->LoreEntries)
		{
			// 발견된 기록만 수량에 반영
			if (DiscoveredLoreIds.Contains(LoreId) == true)
			{
				// 현재 카테고리의 발견 수량 증가
				++DiscoveredCount;
			}
		}
	}
	// 표시할 상세 데이터의 유효한 선택 여부
	bool bHasEntry = IsValid(CurrentCatalog) == true && CurrentCatalog->LoreEntries.IsValidIndex(SelectedEntryIndex) == true;
	// 카테고리 설정의 표시 이름 준비
	FText SiteTitle = Categories.IsValidIndex(SelectedCategoryIndex) == true ? Categories[SelectedCategoryIndex].DisplayName : EmptyTitle;
	// 카탈로그에서 현장 이름을 제공하는 경우 해당 이름 우선
	if (IsValid(CurrentCatalog) == true && CurrentCatalog->SiteDisplayName.IsEmpty() == false)
	{
		// 카탈로그의 현장 표시 이름 적용
		SiteTitle = CurrentCatalog->SiteDisplayName;
	}
	// 빈 목록의 상세 표시 준비
	FText DetailTitle = EmptyTitle;
	// 빈 목록에는 발견 상태를 표시하지 않는 기본값
	FText DetailMeta;
	// 빈 카테고리 안내 준비
	FText DetailBody = EmptyBody;
	// 실제 기록을 선택한 경우 공개 상태 확인
	if (bHasEntry == true)
	{
		// 기존 카탈로그의 선행 조건 판정 재사용
		EMD_LoreEntryState State = CurrentCatalog->ResolveLoreState(CurrentCatalog->LoreEntries[SelectedEntryIndex], DiscoveredLoreIds);
		// 미발견 기록의 기본 제목 적용
		DetailTitle = UndiscoveredTitle;
		// 미발견 기록의 상태 안내 적용
		DetailMeta = UndiscoveredStateText;
		// 미발견 기록의 본문 보호
		DetailBody = UndiscoveredBody;
		// 발견한 기록의 제목과 해독 상태 표시
		if (State != EMD_LoreEntryState::Undiscovered)
		{
			// 해독 판정을 통과한 기록 정의 조회
			UMD_LoreDefinition* Definition = CurrentCatalog->GetLoreDefinition(SelectedEntryIndex);
			// 정의 누락 시 기본 비공개 상태 유지
			if (IsValid(Definition) == true)
			{
				// 발견 이후 공개되는 제목 적용
				DetailTitle = Definition->EntryName;
				// 해독 여부에 맞는 상태 문구 적용
				DetailMeta = State == EMD_LoreEntryState::Decrypted ? DecryptedStateText : EncryptedStateText;
				// 선행 기록이 부족한 경우 실제 본문 노출 방지
				DetailBody = State == EMD_LoreEntryState::Decrypted ? Definition->EntryContent : EncryptedBody;
			}
		}
	}
	// 선택한 현장 이름 표시
	if (IsValid(Txt_SiteTitle) == true)
	{
		// 카테고리의 표시 제목 반영
		Txt_SiteTitle->SetText(SiteTitle);
	}
	// 발견 수량과 전체 수량 표시
	if (IsValid(Txt_Progress) == true)
	{
		// 편집 가능한 진행률 형식 적용
		Txt_Progress->SetText(FText::Format(ProgressTextFormat, FText::AsNumber(DiscoveredCount), FText::AsNumber(TotalCount)));
	}
	// 데이터 진행률을 슬라이더로 표시
	if (IsValid(Slider_LoreProgress) == true)
	{
		// 마우스 조작으로 실제 진행률과 다른 값을 표시하는 상황 방지
		Slider_LoreProgress->SetLocked(true);
		// 빈 목록에서 나누기 오류 없이 진행률 적용
		Slider_LoreProgress->SetValue(TotalCount > 0 ? static_cast<float>(DiscoveredCount) / TotalCount : 0.0f);
	}
	// 선택 기록의 공개 가능한 제목 반영
	if (IsValid(Txt_DetailTitle) == true)
	{
		// 미발견과 빈 목록을 포함하는 상세 제목 적용
		Txt_DetailTitle->SetText(DetailTitle);
	}
	// 현재 기록의 발견 및 해독 상태 반영
	if (IsValid(Txt_DetailMeta) == true)
	{
		// 상세 상태 문구 갱신
		Txt_DetailMeta->SetText(DetailMeta);
	}
	// 공개 정책을 적용한 본문 반영
	if (IsValid(Txt_DetailBody) == true)
	{
		// 이전 기록의 본문이 남지 않도록 전체 교체
		Txt_DetailBody->SetText(DetailBody);
	}
	// 전체 수량과 현재 기록 번호 표시
	if (IsValid(PageNumber) == true)
	{
		// 선택이 없는 경우 0번 표시
		PageNumber->SetText(FText::Format(PageTextFormat, FText::AsNumber(bHasEntry == true ? SelectedEntryIndex + 1 : 0), FText::AsNumber(TotalCount)));
	}
	// 첫 기록 또는 빈 목록에서 이전 이동 비활성화
	if (IsValid(Btn_Previous) == true)
	{
		// 이전 기록이 있는 경우에만 입력 허용
		Btn_Previous->SetIsEnabled(bHasEntry == true && SelectedEntryIndex > 0);
	}
	// 마지막 기록 또는 빈 목록에서 다음 이동 비활성화
	if (IsValid(Btn_Next) == true)
	{
		// 다음 기록이 있는 경우에만 입력 허용
		Btn_Next->SetIsEnabled(bHasEntry == true && SelectedEntryIndex + 1 < TotalCount);
	}
	// 기존 선택 상태를 모두 해제한 뒤 현재 기록 하나만 선택
	for (UMD_CommonButtonBase* EntryWidget : EntryWidgets)
	{
		// 생성에 실패한 카드 제외
		if (IsValid(EntryWidget) == true)
		{
			// 비토글 버튼의 선택 해제를 보장하는 공통 API 사용
			EntryWidget->ClearSelection();
		}
	}
	// 현재 기록의 카드가 있는 경우 선택 표시
	if (EntryWidgets.IsValidIndex(SelectedEntryIndex) == true && IsValid(EntryWidgets[SelectedEntryIndex]) == true)
	{
		// 선택 표시 변경으로 클릭 처리가 반복되는 상황 방지
		EntryWidgets[SelectedEntryIndex]->SetIsSelected(true, false);
	}
	// 디자이너에 배치된 모든 카테고리 버튼의 선택 표시 갱신
	for (int32 CategoryIndex = 0; CategoryIndex < CategoryButtons.Num(); ++CategoryIndex)
	{
		// 카테고리 개수와 무관하게 데이터 순서대로 표시 반영
		RefreshCategoryButton(CategoryButtons[CategoryIndex], CategoryIndex);
	}
}

void UMD_EncyclopediaWidget::HandleCategoryClicked(int32 CategoryIndex)
{
	// 목록 구성 도중의 입력과 설정에 없는 카테고리 클릭 차단
	if (bIsRefreshingEntries == true || Categories.IsValidIndex(CategoryIndex) == false)
	{
		// 유효한 클릭이 아닌 경우 데이터 변경과 연출 알림 생략
		return;
	}
	// Blueprint에서 새 카테고리의 내용을 사용할 수 있도록 선택 반영
	SelectCategory(CategoryIndex);
	// 실제 버튼 클릭에서만 Blueprint 연출 이벤트 호출
	OnCategoryClicked(CategoryIndex);
}

void UMD_EncyclopediaWidget::RebuildCategoryBindings()
{
	// 이전 패널 구성에서 남은 클릭 구독 제거
	ReleaseCategoryBindings();
	// 카테고리 패널이 없는 화면의 입력 연결 생략
	if (IsValid(VB_Categories) == false)
	{
		// 데이터 선택 기능을 유지한 표시 연결 종료
		return;
	}
	// 패널에 배치된 순서대로 버튼 확인
	for (UWidget* Child : VB_Categories->GetAllChildren())
	{
		// 장식 위젯을 제외할 공통 버튼 확인
		UMD_CommonButtonBase* Button = Cast<UMD_CommonButtonBase>(Child);
		// 카테고리 입력을 지원하는 위젯만 연결
		if (IsValid(Button) == true)
		{
			// 실제 버튼 순서를 보관하며 해당 카테고리로 클릭 연결
			Button->OnClicked().AddUObject(this, &ThisClass::HandleCategoryClicked, CategoryButtons.Add(Button));
		}
	}
}

void UMD_EncyclopediaWidget::ReleaseCategoryBindings()
{
	// 재연결과 화면 제거 시 기존 버튼의 구독 정리
	for (UMD_CommonButtonBase* Button : CategoryButtons)
	{
		// 이미 소멸한 버튼의 접근 방지
		if (IsValid(Button) == true)
		{
			// 현재 화면의 클릭 처리만 제거
			Button->OnClicked().RemoveAll(this);
		}
	}
	// 이전 버튼 구성에 대한 강한 참조 제거
	CategoryButtons.Reset();
}

void UMD_EncyclopediaWidget::RefreshCategoryButton(UMD_CommonButtonBase* Button, int32 CategoryIndex)
{
	// 선택적 카테고리 버튼 누락 처리
	if (IsValid(Button) == false)
	{
		// 선택 표시 대상이 없는 갱신 종료
		return;
	}
	// 카테고리 설정에서 버튼 표시 이름 적용
	if (Categories.IsValidIndex(CategoryIndex) == true)
	{
		// 데이터 순서 변경에도 올바른 카테고리 이름 표시
		Button->SetContentText(Categories[CategoryIndex].DisplayName);
	}
	// 카테고리 선택 표시 허용
	Button->SetIsSelectable(true);
	// 같은 카테고리의 재클릭 선택 해제 방지
	Button->SetIsToggleable(false);
	// 포커스만 이동했을 때 카테고리 표시 변경 방지
	Button->SetShouldSelectUponReceivingFocus(false);
	// 카테고리 설정이 없는 버튼의 입력 차단
	Button->SetIsEnabled(Categories.IsValidIndex(CategoryIndex));
	// 이전 카테고리의 선택 표시 제거
	Button->ClearSelection();
	// 현재 카테고리 버튼 하나만 선택
	if (SelectedCategoryIndex == CategoryIndex)
	{
		// 클릭을 발생시키지 않는 선택 표시 적용
		Button->SetIsSelected(true, false);
	}
}

void UMD_EncyclopediaWidget::ReleaseEntryWidgets()
{
	// 동적 카드의 화면 클릭 구독 정리
	for (UMD_CommonButtonBase* EntryWidget : EntryWidgets)
	{
		// 이미 제거된 카드 제외
		if (IsValid(EntryWidget) == true)
		{
			// 이전 목록 인덱스로 전달되는 클릭 차단
			EntryWidget->OnClicked().RemoveAll(this);
		}
	}
	// 이전 카드의 강한 참조 제거
	EntryWidgets.Reset();
	// 디자이너 예시 카드와 이전 목록 정리
	if (IsValid(SB_Entries) == true)
	{
		// 새 카탈로그 목록을 위한 영역 비우기
		SB_Entries->ClearChildren();
	}
}

void UMD_EncyclopediaWidget::UnbindLoreSource()
{
	// 유효한 이전 원장의 화면 구독 정리
	if (BoundLoreSource.IsValid() == true)
	{
		// 다른 화면의 원장 구독을 유지한 현재 화면 구독 해제
		BoundLoreSource->OnLoreProgressUpdated.RemoveDynamic(this, &ThisClass::HandleLoreProgressUpdated);
	}
	// 소멸한 원장에 대한 참조 제거
	BoundLoreSource.Reset();
	// 이전 월드에 등록한 GameState 교체 알림 해제
	if (BoundWorld.IsValid() == true)
	{
		// 해당 화면의 월드 구독만 제거
		BoundWorld->GameStateSetEvent.Remove(GameStateChangedHandle);
	}
	// 다음 활성화의 새 구독 허용
	GameStateChangedHandle.Reset();
	// 이전 월드 참조 제거
	BoundWorld.Reset();
}

void UMD_EncyclopediaWidget::HandleGameStateSet(AGameStateBase* GameState)
{
	// 현재 월드가 알린 새 GameState 기준으로 원장과 목록 갱신
	RefreshLore();
}

void UMD_EncyclopediaWidget::HandleLoreProgressUpdated()
{
	// 소멸한 복제 컴포넌트에서 데이터 조회 방지
	if (BoundLoreSource.IsValid() == true)
	{
		// 새 발견 목록으로 카드 인스턴스를 유지하며 표시 갱신
		ApplyLoreProgress(BoundLoreSource->GetDiscoveredLoreAssetIds());
	}
}
