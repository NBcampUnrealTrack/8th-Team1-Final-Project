// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Interaction/MD_TravelConsoleWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/GameInstance.h"
#include "Player/MD_PlayerController.h"
#include "UI/MD_CommonButtonBase.h"
#include "World/Interaction/MD_TravelConsole.h"

void UMD_TravelConsoleWidget::NativeOnActivated()
{
    // 재활성화 사이에 변경된 콘솔 잠금 상태 반영
    if (IsSiteAvailable(SelectedSiteIndex) == false)
    {
        // 출항 가능한 첫 현장으로 재선택
        SelectedSiteIndex = INDEX_NONE;
        // 유효한 콘솔의 현재 목록만 순회
        if (IsValid(OwningConsole) == true)
        {
            // 콘솔 API가 제공하는 읽기 전용 현장 배열 사용
            SelectedSiteIndex = OwningConsole->GetSites().IndexOfByPredicate([](const FMD_TravelSiteEntry& Site) { return Site.bAvailable; });
        }
    }
    // 활성화 연출 전에 동적 표시 준비
    RefreshSelectionDisplay();
    Super::NativeOnActivated();
}

void UMD_TravelConsoleWidget::SetOwningConsole(AMD_TravelConsole* InConsole)
{
    // 같은 화면의 반복 초기화에서는 사용자 선택 보존
    if (OwningConsole != InConsole)
    {
        // 현재 현장 데이터의 제공자 교체
        OwningConsole = InConsole;
        // 다른 콘솔의 인덱스를 재사용하지 않도록 초기화
        SelectedSiteIndex = INDEX_NONE;
        // 새 콘솔의 초기 난이도 검증
        SelectedDifficulty = IsSupportedDifficulty(DefaultDifficulty) == true ? DefaultDifficulty : EMD_Difficulty::Safe;
    }
    // 기존 현장이 삭제되거나 잠긴 경우 첫 사용 가능한 현장 선택
    if (IsValid(OwningConsole) == true && IsSiteAvailable(SelectedSiteIndex) == false)
    {
        // 콘솔 API가 제공하는 읽기 전용 현장 배열 사용
        SelectedSiteIndex = OwningConsole->GetSites().IndexOfByPredicate([](const FMD_TravelSiteEntry& Site) { return Site.bAvailable; });
    }
    // 초기화 시점의 화면 데이터 반영
    RefreshSelectionDisplay();
}

bool UMD_TravelConsoleWidget::IsLocalHost()
{
    // 이 화면을 소유하는 로컬 플레이어 확인
    APlayerController* PlayerController = GetOwningPlayer();
    // 플레이어 없는 디자이너와 원격 참가자의 입력 차단
    if (IsValid(PlayerController) == false)
    {
        // 검증을 통과하지 못한 요청의 실행 중단
        return false;
    }
    // 전용 서버와 참가자의 세션 변경 차단
    return PlayerController->IsLocalController() && PlayerController->HasAuthority();
}

bool UMD_TravelConsoleWidget::IsSiteAvailable(int32 SiteIndex)
{
    // 콘솔 수명 확인 후 현장 배열 접근
    if (IsValid(OwningConsole) == false)
    {
        // 검증을 통과하지 못한 요청의 실행 중단
        return false;
    }
    // 범위 밖 인덱스와 잠긴 현장 제외
    return OwningConsole->GetSites().IsValidIndex(SiteIndex) && OwningConsole->GetSites()[SiteIndex].bAvailable;
}

bool UMD_TravelConsoleWidget::IsSupportedDifficulty(EMD_Difficulty Difficulty)
{
    // 현재 화면의 세 난이도 버튼과 동일한 선택 범위
    return Difficulty == EMD_Difficulty::Safe || Difficulty == EMD_Difficulty::Standard || Difficulty == EMD_Difficulty::Dangerous;
}

bool UMD_TravelConsoleWidget::SelectSite(int32 SiteIndex)
{
    // 비호스트 및 사용할 수 없는 현장 선택 차단
    if (IsLocalHost() == false || IsSiteAvailable(SiteIndex) == false)
    {
        // 검증을 통과하지 못한 요청의 실행 중단
        return false;
    }
    // 같은 지도 재선택에 따른 전체 화면 갱신 생략
    if (SelectedSiteIndex == SiteIndex)
    {
        // 검증된 기존 목적지 선택 유지
        return true;
    }
    // 새 항해의 선택 보관
    SelectedSiteIndex = SiteIndex;
    // 지도와 상세 및 출항 영역 동기화
    RefreshSelectionDisplay();
    // 검증된 새 항해 목적지 선택 완료
    return true;
}

bool UMD_TravelConsoleWidget::SelectDifficulty(EMD_Difficulty Difficulty)
{
    // 비호스트 및 잘못된 Enum 입력 차단
    if (IsLocalHost() == false || IsSupportedDifficulty(Difficulty) == false)
    {
        // 검증을 통과하지 못한 요청의 실행 중단
        return false;
    }
    // 같은 난이도 재선택에 따른 전체 화면 갱신 생략
    if (SelectedDifficulty == Difficulty)
    {
        // 검증된 기존 난이도 선택 유지
        return true;
    }
    // 새 항해 RPC에 사용할 난이도 보관
    SelectedDifficulty = Difficulty;
    // 버튼의 선택 상태와 상세 표시 갱신
    RefreshSelectionDisplay();
    // 검증된 요청의 전달 또는 선택 적용 완료
    return true;
}

bool UMD_TravelConsoleWidget::CanRequestTravel()
{
    // 참가자와 소유자 없는 화면의 세션 변경 차단
    if (IsLocalHost() == false)
    {
        // 검증을 통과하지 못한 요청의 실행 중단
        return false;
    }
    // 새 항해 RPC 제공자와 현장 및 난이도 확인
    if (IsValid(Cast<AMD_PlayerController>(GetOwningPlayer())) == false
        || IsSiteAvailable(SelectedSiteIndex) == false || IsSupportedDifficulty(SelectedDifficulty) == false)
    {
        // 검증을 통과하지 못한 요청의 실행 중단
        return false;
    }
    // 콘솔의 서버 공통 검증을 통해 지정된 목적지 확인
    return OwningConsole->ResolveSiteLevel(SelectedSiteIndex).IsNull() == false;
}

bool UMD_TravelConsoleWidget::RequestTravel()
{
    // 화면 표시와 동일한 출항 조건 재검증
    if (CanRequestTravel() == false)
    {
        // 검증을 통과하지 못한 요청의 실행 중단
        return false;
    }
    // 기존 서버의 권한 및 현장 검증을 유지하는 출항 RPC
    Cast<AMD_PlayerController>(GetOwningPlayer())->ServerRequestTravel(OwningConsole, SelectedSiteIndex, SelectedDifficulty);
    // 요청 전달 완료이며 이동 완료와 구분
    return true;
}

void UMD_TravelConsoleWidget::UpdateButton(UMD_CommonButtonBase* Button, bool bSelected, bool bEnabled)
{
    // 선택적 버튼 바인딩 누락 허용
    if (IsValid(Button) == false)
    {
        // 현재 경로의 처리 종료
        return;
    }
    // 선택 버튼의 단일 선택 표현 활성화
    Button->SetIsSelectable(true);
    // 현재 항목 재클릭에 의한 선택 해제 방지
    Button->SetIsToggleable(false);
    // 포커스 이동만으로 선택 데이터와 스타일이 달라지는 상황 방지
    Button->SetShouldSelectUponReceivingFocus(false);
    // 입력 상태가 달라진 버튼에만 CommonUI 상태 갱신
    if (Button->GetIsEnabled() != bEnabled)
    {
        // 현재 소유자의 입력 권한 반영
        Button->SetIsEnabled(bEnabled);
    }
    // 같은 값의 반복 선택 이벤트 방지
    if (Button->GetSelected() != bSelected)
    {
        // 현재 선택의 스타일 적용
        if (bSelected == true)
        {
            // 선택 변경의 외부 입력 콜백 생략
            Button->SetIsSelected(true, false);
        }
        else
        {
            // 비토글 버튼의 이전 선택 표시 해제
            Button->ClearSelection();
        }
    }
}

void UMD_TravelConsoleWidget::SetDisplayText(UTextBlock* TextBlock, FText Text)
{
    // 실제 문구가 변경된 유효한 위젯만 갱신
    if (IsValid(TextBlock) == true && TextBlock->GetText().EqualTo(Text) == false)
    {
        // 불필요한 텍스트 레이아웃 무효화 방지
        TextBlock->SetText(Text);
    }
}

FText UMD_TravelConsoleWidget::GetDifficultyText(EMD_Difficulty Difficulty)
{
    // 화면의 버튼에 설정한 쉬움 문구 재사용
    if (Difficulty == EMD_Difficulty::Safe && IsValid(Btn_Easy) == true)
    {
        // 현재 조건에 대응하는 표시 또는 조회 결과 전달
        return Btn_Easy->GetContentText();
    }
    // 화면의 버튼에 설정한 보통 문구 재사용
    if (Difficulty == EMD_Difficulty::Standard && IsValid(Btn_Normal) == true)
    {
        // 현재 조건에 대응하는 표시 또는 조회 결과 전달
        return Btn_Normal->GetContentText();
    }
    // 화면의 버튼에 설정한 어려움 문구 재사용
    if (Difficulty == EMD_Difficulty::Dangerous && IsValid(Btn_Hard) == true)
    {
        // 현재 조건에 대응하는 표시 또는 조회 결과 전달
        return Btn_Hard->GetContentText();
    }
    // 선택 버튼이 없는 난이도는 Enum 표시명 사용
    return StaticEnum<EMD_Difficulty>()->GetDisplayNameTextByValue(static_cast<int64>(Difficulty));
}

int32 UMD_TravelConsoleWidget::GetDisplayedSiteIndex()
{
    // 현재 이용 가능한 선택 목적지만 반환
    return IsSiteAvailable(SelectedSiteIndex) == true ? SelectedSiteIndex : INDEX_NONE;
}

void UMD_TravelConsoleWidget::RefreshSelectionDisplay()
{
    // 저장 항해 페이지가 사라졌으므로 모든 전환기는 새 항해 페이지 고정
    for (UWidgetSwitcher* Switcher : { WS_Map, WS_DetailInfo, WS_Difficulty })
    {
        // 일부 영역을 생략한 파생 화면 허용
        if (IsValid(Switcher) == true)
        {
            // 디자이너의 새 항해 0번 페이지
            Switcher->SetActiveWidgetIndex(0);
        }
    }
    // 이번 표시 갱신에서 공유할 소유자 권한
    bool bCanSelect = IsLocalHost();
    // 새 항해 지도와 난이도의 선택 표시
    UpdateButton(Btn_X7, SelectedSiteIndex == X7SiteIndex, bCanSelect && IsSiteAvailable(X7SiteIndex));
    // 두 번째 현장의 준비 상태 반영
    UpdateButton(Btn_TR9, SelectedSiteIndex == TR9SiteIndex, bCanSelect && IsSiteAvailable(TR9SiteIndex));
    // 쉬움 난이도 선택 반영
    UpdateButton(Btn_Easy, SelectedDifficulty == EMD_Difficulty::Safe, bCanSelect);
    // 보통 난이도 선택 반영
    UpdateButton(Btn_Normal, SelectedDifficulty == EMD_Difficulty::Standard, bCanSelect);
    // 어려움 난이도 선택 반영
    UpdateButton(Btn_Hard, SelectedDifficulty == EMD_Difficulty::Dangerous, bCanSelect);
    // 선택 부재 시 디자이너의 예시 문구가 남지 않도록 초기화
    FText MapId = FText::FromString(TEXT("-"));
    // 식별자를 제외한 상세 현장명
    FText MapName = MapId;
    // 식별자와 현장명을 함께 표시하는 출항 목적지
    FText TicketDestination = MapId;
    // 현장 안내는 설정하지 않은 경우 빈 문구
    FText Description;
    // 이전 선택의 이미지 잔류 방지
    UTexture2D* Preview = nullptr;
    // 현재 선택의 실제 데이터 존재 여부 확인
    bool bHasSelection = IsSiteAvailable(SelectedSiteIndex);
    // 유효한 선택의 현장 정보를 표시
    if (bHasSelection == true)
    {
        // 콘솔에 등록된 현장 표시명 사용
        if (IsValid(OwningConsole) == true && OwningConsole->GetSites().IsValidIndex(SelectedSiteIndex) == true)
        {
            // 실제 출항 대상의 이름 표시
            MapName = OwningConsole->GetSites()[SelectedSiteIndex].DisplayName;
        }
        // 별도 표시 설정이 없는 현장은 콘솔의 전체 이름 유지
        TicketDestination = MapName;
        // 디자이너가 지정한 식별자와 이미지 및 설명 조회
        FMD_TravelMapPresentation* Presentation = MapPresentation.Find(SelectedSiteIndex);
        // 해당 현장의 표현 설정이 있는 경우 적용
        if (Presentation != nullptr)
        {
            // 현장 식별 문구 적용
            MapId = Presentation->MapId;
            // 식별자가 별도 영역에 표시되는 상세 이름 구성
            if (Presentation->MapName.IsEmpty() == false)
            {
                // 참고 디자인의 짧은 현장명 적용
                MapName = Presentation->MapName;
                // 하단 출항 영역에는 식별자와 현장명을 함께 표시
                TicketDestination = MapId.IsEmpty() == true ? MapName
                    : FText::Format(NSLOCTEXT("MDTravel", "DestinationFormat", "{0} {1}"), MapId, MapName);
            }
            // 현장 설명 적용
            Description = Presentation->Description;
            // 하드코딩 없는 이미지 참조 사용
            Preview = Presentation->Preview;
        }
    }
    // 화면 제목 갱신
    SetDisplayText(Txt_Map_Header, NSLOCTEXT("MDTravel", "NewTitle", "어디로 떠날까요?"));
    // 선택 안내 갱신
    SetDisplayText(Txt_Map_SubHeader, NSLOCTEXT("MDTravel", "NewSubtitle", "항해 목적지 선택"));
    // 상세 제목
    SetDisplayText(Txt_Detail_Header, NSLOCTEXT("MDTravel", "NewDetailHeader", "선택한 목적지"));
    // 하단 출항 영역 제목
    SetDisplayText(Txt_Difficulty_Header, NSLOCTEXT("MDTravel", "NewTicketHeader", "출항 준비"));
    // 버튼의 출항 동작 문구
    SetDisplayText(Txt_Button_Travel, NSLOCTEXT("MDTravel", "DepartTravel", "출항하기"));
    // 출항 설정 안내
    SetDisplayText(Txt_Difficulty_Header_2, NSLOCTEXT("MDTravel", "NewSettingsHint", "난이도를 고르고 출항하세요."));
    // 실제 디자이너 이름에 대응하는 상세 식별자 표시
    SetDisplayText(Txt_Detail_MapName, MapId);
    // 상세 현장명 표시
    SetDisplayText(Txt_Detail_MapName_1, MapName);
    // 출항 영역의 현장명 동기화
    SetDisplayText(Txt_Difficulty_MapName, TicketDestination);
    // 새 항해 설명 갱신
    SetDisplayText(Txt_Detail_Description, Description);
    // 하단의 선택 난이도 표시
    SetDisplayText(Txt_Difficulty, bHasSelection == true ? GetDifficultyText(SelectedDifficulty) : FText::GetEmpty());
    // 선택 배지 문구 표시
    SetDisplayText(Txt_Badge, bHasSelection == false ? FText::GetEmpty() : NSLOCTEXT("MDTravel", "Selected", "선택됨"));
    // 텍스트가 비어도 체크 이미지가 남지 않도록 배지 전체 처리
    if (IsValid(BDR_Badge_Outline) == true)
    {
        // 참고 디자인의 빈 선택에서는 선택 도장 숨김
        BDR_Badge_Outline->SetVisibility(bHasSelection == true ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    }
    // 선택적 상세 이미지 영역 갱신
    if (IsValid(Img_Preview) == true)
    {
        // 캔버스 크기와 프레임 및 Tint를 보존하는 브러시 복사
        FSlateBrush Brush = Img_Preview->GetBrush();
        // 유효하지 않은 이전 이미지의 잔류 방지
        Brush.SetResourceObject(IsValid(Preview) == true ? Preview : nullptr);
        // 현장 이미지의 전체 영역 사용
        Brush.SetUVRegion(FBox2f());
        // 이미지 부재 시 흰 사각형 대신 투명한 빈 영역 표시
        Brush.DrawAs = IsValid(Preview) == true ? ESlateBrushDrawType::Image : ESlateBrushDrawType::NoDrawType;
        // 실제 이미지 리소스와 그리기 방식 반영
        Img_Preview->SetBrush(Brush);
    }
    // 일반 UButton인 출항 버튼의 허용 상태 반영
    if (IsValid(Btn_Travel) == true)
    {
        // 실제 요청 검증과 활성 상태 통일
        Btn_Travel->SetIsEnabled(CanRequestTravel());
    }
    // 추가 연출은 이벤트 그래프에서 확장
    OnSelectionChanged();
}
