// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Interaction/MD_NewGameTabWidget.h"

#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Player/MD_PlayerController.h"
#include "UI/MD_CommonButtonBase.h"
#include "World/Interaction/MD_TravelConsole.h"

void UMD_NewGameTabWidget::SetOwningConsole(AMD_TravelConsole* InConsole)
{
    // 같은 화면의 탭 재활성화에서는 사용자 선택 유지
    if (OwningConsole != InConsole)
    {
        // 다른 콘솔의 인덱스와 난이도 혼용 방지
        OwningConsole = InConsole;
        // 새 콘솔의 기본 현장 재선택
        SelectedSiteIndex = INDEX_NONE;
        // 새 항해 기본 난이도
        SelectedDifficulty = DefaultDifficulty;
    }
    // 표시 중인 탭만 즉시 갱신하고 비활성 탭은 활성화 이벤트에서 목록 구성
    if (IsActivated() == true)
    {
        // 콘솔 연결 직후 활성화 경로에서 발생하는 중복 재생성 방지
        RefreshSites();
    }
}

void UMD_NewGameTabWidget::RefreshSites()
{
    // 이전 카드의 클릭 구독과 참조 정리
    for (UMD_CommonButtonBase* Card : MapCards)
    {
        // 제거된 카드의 잔여 클릭 전달 차단
        if (IsValid(Card) == true)
        {
            // 목록 재생성 시 구독 누적 방지
            Card->OnClicked().RemoveAll(this);
        }
    }
    // 새 목록과 원본 인덱스의 일대일 대응 준비
    MapCards.Reset();
    // 선택적 목록 영역이 있는 경우 임시 디자이너 카드 제거
    if (IsValid(SB_MapList) == true)
    {
        // 현재 데이터로 목록 재구성
        SB_MapList->ClearChildren();
    }
    // 콘솔 소멸 시 이전 현장 선택 제거
    if (IsValid(OwningConsole) == false)
    {
        // 출항 대상이 없는 상태 반영
        SelectedSiteIndex = INDEX_NONE;
        // 상세 화면의 빈 선택 통지
        RefreshSelection();
        return;
    }
    // 콘솔 API의 읽기 전용 현장 목록 참조
    const TArray<FMD_TravelSiteEntry>& Sites = OwningConsole->GetSites();
    // 삭제되거나 잠긴 현장을 기본 선택으로 교체
    if (Sites.IsValidIndex(SelectedSiteIndex) == false || Sites[SelectedSiteIndex].bAvailable == false)
    {
        // 준비된 첫 번째 현장 탐색
        SelectedSiteIndex = Sites.IndexOfByPredicate([](const FMD_TravelSiteEntry& Site) { return Site.bAvailable; });
    }
    // 실패한 카드 생성에도 원본 인덱스 유지
    MapCards.SetNum(Sites.Num());
    // 호스트가 아닌 참가자의 현장 변경 차단
    APlayerController* PlayerController = GetOwningPlayer();
    // 로컬 호스트 확인 전 기본 비활성
    bool bCanSelect = false;
    if (IsValid(PlayerController) == true)
    {
        // 원격 참가자와 전용 서버 화면 제외
        bCanSelect = PlayerController->IsLocalController() && PlayerController->HasAuthority();
    }
    // 카드 클래스와 목록 영역이 준비된 경우 목록 구성
    if (IsValid(SB_MapList) == true)
    {
        // 디자이너에서 제공한 카드 클래스 검증
        if (IsValid(MapCardWidgetClass) == true)
        {
            // 준비 중 현장도 잠긴 카드로 표시
            for (int32 Index = 0; Index < Sites.Num(); ++Index)
            {
                // 현재 현장을 나타내는 카드 인스턴스 생성
                UMD_CommonButtonBase* Card = CreateWidget<UMD_CommonButtonBase>(this, MapCardWidgetClass);
                if (IsValid(Card) == false)
                {
                    continue;
                }
                // 공통 카드의 라벨과 선택 정책 초기화
                Card->SetContentText(Sites[Index].DisplayName);
                // 선택된 현장 표시 허용
                Card->SetIsSelectable(true);
                // 선택한 현장의 재클릭 해제 방지
                Card->SetIsToggleable(false);
                // 포커스 이동에 의한 현장 선택 불일치 방지
                Card->SetShouldSelectUponReceivingFocus(false);
                // 준비된 호스트 현장만 선택 허용
                Card->SetIsEnabled(bCanSelect && Sites[Index].bAvailable);
                // 원본 인덱스 전달
                Card->OnClicked().AddUObject(this, &ThisClass::HandleSiteClicked, Index);
                // 상세 선택과 카드의 대응 유지
                MapCards[Index] = Card;
                // 스크롤 목록에 카드 배치
                SB_MapList->AddChild(Card);
                // 프로젝트별 이미지와 설명 구성
                OnSiteCardCreated(Card, Index);
            }
        }
    }
    // 재구성된 카드와 상세 화면 동기화
    RefreshSelection();
}

bool UMD_NewGameTabWidget::SelectSite(int32 SiteIndex)
{
    // 호스트의 로컬 입력만 선택 변경 허용
    APlayerController* PlayerController = GetOwningPlayer();
    if (IsValid(PlayerController) == false)
    {
        // 소유 플레이어 부재
        return false;
    }
    if (PlayerController->IsLocalController() == false || PlayerController->HasAuthority() == false)
    {
        // 출항 권한 부재
        return false;
    }
    // 콘솔 수명 검증
    if (IsValid(OwningConsole) == false)
    {
        // 현장 제공자 부재
        return false;
    }
    // 콘솔 API의 읽기 전용 목록으로 요청 검증
    const TArray<FMD_TravelSiteEntry>& Sites = OwningConsole->GetSites();
    if (Sites.IsValidIndex(SiteIndex) == false || Sites[SiteIndex].bAvailable == false)
    {
        // 범위 밖 또는 준비 중 현장
        return false;
    }
    // 검증한 현장으로 선택 변경
    SelectedSiteIndex = SiteIndex;
    // 카드와 상세 정보 동기화
    RefreshSelection();
    // 선택 적용 완료
    return true;
}

bool UMD_NewGameTabWidget::SelectDifficulty(EMD_Difficulty Difficulty)
{
    // 버튼 구성과 무관하게 지원하는 난이도 값인지 검증
    if (IsSupportedDifficulty(Difficulty) == false)
    {
        // 지원하지 않는 난이도
        return false;
    }
    // 로컬 호스트 입력 검증
    APlayerController* PlayerController = GetOwningPlayer();
    if (IsValid(PlayerController) == false)
    {
        // 소유 플레이어 부재
        return false;
    }
    if (PlayerController->IsLocalController() == false || PlayerController->HasAuthority() == false)
    {
        // 출항 권한 부재
        return false;
    }
    // 출항 옵션으로 사용할 난이도 보관
    SelectedDifficulty = Difficulty;
    // 난이도 선택 이벤트 전달
    OnSelectionChanged();
    // 선택 적용 완료
    return true;
}

bool UMD_NewGameTabWidget::GetSelectedSite(FMD_TravelSiteEntry& Site)
{
    // 실패 시 이전 호출의 출력값 잔류 방지
    Site = FMD_TravelSiteEntry();
    if (IsValid(OwningConsole) == false)
    {
        // 현장 제공자 부재
        return false;
    }
    // 콘솔 API의 읽기 전용 목록에서 선택값 검증
    const TArray<FMD_TravelSiteEntry>& Sites = OwningConsole->GetSites();
    if (Sites.IsValidIndex(SelectedSiteIndex) == false)
    {
        // 유효한 선택 부재
        return false;
    }
    // 상세 표시용 현장 데이터 전달
    Site = Sites[SelectedSiteIndex];
    // 조회 완료
    return true;
}

bool UMD_NewGameTabWidget::CanRequestTravel()
{
    // 서버 RPC를 제공하는 로컬 호스트 확인
    AMD_PlayerController* PlayerController = Cast<AMD_PlayerController>(GetOwningPlayer());
    if (IsValid(PlayerController) == false)
    {
        // 프로젝트 플레이어 부재
        return false;
    }
    if (PlayerController->IsLocalController() == false || PlayerController->HasAuthority() == false)
    {
        // 출항 권한 부재
        return false;
    }
    // 현장 수명과 이동할 레벨 지정 여부 확인
    if (IsValid(OwningConsole) == false)
    {
        // 출항 콘솔 부재
        return false;
    }
    // 표시 위젯과 무관하게 출항에 사용할 난이도 값 검증
    if (IsSupportedDifficulty(SelectedDifficulty) == false)
    {
        // 유효한 난이도 선택 부재
        return false;
    }
    // 잠긴 현장과 빈 목적지를 콘솔의 공통 검증으로 차단
    return OwningConsole->ResolveSiteLevel(SelectedSiteIndex).IsNull() == false;
}

bool UMD_NewGameTabWidget::RequestTravel()
{
    // 잘못된 선택과 비호스트 요청 차단
    if (CanRequestTravel() == false)
    {
        // 출항 사전 검증 실패
        return false;
    }
    // 기존 서버 검증과 멀티플레이 이동 경로 재사용
    Cast<AMD_PlayerController>(GetOwningPlayer())->ServerRequestTravel(OwningConsole, SelectedSiteIndex, SelectedDifficulty);
    // RPC 전달 완료이며 이동 완료는 세션 관리자 이벤트로 통지
    return true;
}

void UMD_NewGameTabWidget::HandleSiteClicked(int32 SiteIndex)
{
    // 동적 카드 클릭을 공통 선택 함수로 전달
    SelectSite(SiteIndex);
}

void UMD_NewGameTabWidget::RefreshSelection()
{
    // 원본 인덱스에 대응하는 카드 하나만 선택 표시
    for (int32 Index = 0; Index < MapCards.Num(); ++Index)
    {
        // 생성에 성공한 카드만 갱신
        if (IsValid(MapCards[Index]) == true)
        {
            // 비토글 카드도 다른 현장을 선택하면 이전 선택 표시 해제
            if (Index != SelectedSiteIndex && MapCards[Index]->GetSelected() == true)
            {
                // 새 현장의 선택 표시보다 이전 현장의 해제를 우선 처리
                MapCards[Index]->ClearSelection();
            }
        }
    }
    // 이전 선택 해제 후 현재 현장 하나만 선택 표시
    if (MapCards.IsValidIndex(SelectedSiteIndex) == true)
    {
        // 생성에 성공한 선택 카드에만 표시 적용
        if (IsValid(MapCards[SelectedSiteIndex]) == true)
        {
            // 추가 클릭 이벤트 없이 현재 현장 선택 표시
            MapCards[SelectedSiteIndex]->SetIsSelected(true, false);
        }
    }
    // 상세 설명과 고정 버튼의 표현을 화면 이벤트에 전달
    OnSelectionChanged();
}

void UMD_NewGameTabWidget::RefreshSelectionDisplay()
{
    // 선택 현장 정보와 빈 선택 상태 조회
    FMD_TravelSiteEntry Site;
    // 실패 시 초기화된 빈 현장 사용
    GetSelectedSite(Site);
    if (IsValid(Txt_DetailName) == true)
    {
        // 선택 현장 이름 표시
        Txt_DetailName->SetText(Site.DisplayName);
    }
    // 난이도 입력 권한과 현재 선택 스타일 반영
    APlayerController* PlayerController = GetOwningPlayer();
    // 로컬 호스트 확인 전 입력 차단
    bool bCanSelect = false;
    if (IsValid(PlayerController) == true)
    {
        // 호스트 입력만 허용
        bCanSelect = PlayerController->IsLocalController() && PlayerController->HasAuthority();
    }
    // 바인딩된 버튼의 권한과 단일 선택 정책 갱신
    for (UMD_CommonButtonBase* Button : { Btn_Easy, Btn_Normal, Btn_Hard })
    {
        if (IsValid(Button) == true)
        {
            // 난이도 선택 표시 허용
            Button->SetIsSelectable(true);
            // 선택된 난이도의 재클릭 해제 방지
            Button->SetIsToggleable(false);
            // 포커스 이동에 의한 추가 선택 방지
            Button->SetShouldSelectUponReceivingFocus(false);
            // 로컬 호스트 권한 반영
            Button->SetIsEnabled(bCanSelect);
        }
    }
    // 새 선택을 표시하기 전에 다른 난이도의 선택 상태 해제
    if (IsValid(Btn_Easy) == true && SelectedDifficulty != EMD_Difficulty::Safe)
    {
        // 안전 난이도의 이전 선택 표시 해제
        Btn_Easy->ClearSelection();
    }
    if (IsValid(Btn_Normal) == true && SelectedDifficulty != EMD_Difficulty::Standard)
    {
        // 표준 난이도의 이전 선택 표시 해제
        Btn_Normal->ClearSelection();
    }
    if (IsValid(Btn_Hard) == true && SelectedDifficulty != EMD_Difficulty::Dangerous)
    {
        // 위험 난이도의 이전 선택 표시 해제
        Btn_Hard->ClearSelection();
    }
    // 바인딩된 각 버튼에 현재 난이도의 선택 상태 직접 반영
    if (IsValid(Btn_Easy) == true && SelectedDifficulty == EMD_Difficulty::Safe)
    {
        // 안전 난이도 선택 표시
        Btn_Easy->SetIsSelected(true, false);
    }
    if (IsValid(Btn_Normal) == true && SelectedDifficulty == EMD_Difficulty::Standard)
    {
        // 표준 난이도 선택 표시
        Btn_Normal->SetIsSelected(true, false);
    }
    if (IsValid(Btn_Hard) == true && SelectedDifficulty == EMD_Difficulty::Dangerous)
    {
        // 위험 난이도 선택 표시
        Btn_Hard->SetIsSelected(true, false);
    }
    if (IsValid(Btn_Travel) == true)
    {
        // 현재 목적지와 권한에 따른 출항 허용
        Btn_Travel->SetIsEnabled(CanRequestTravel());
    }
}

bool UMD_NewGameTabWidget::IsSupportedDifficulty(EMD_Difficulty Difficulty)
{
    // 버튼 수명이나 바인딩 상태에 의존하지 않는 난이도 정책
    return Difficulty == EMD_Difficulty::Safe || Difficulty == EMD_Difficulty::Standard || Difficulty == EMD_Difficulty::Dangerous;
}
