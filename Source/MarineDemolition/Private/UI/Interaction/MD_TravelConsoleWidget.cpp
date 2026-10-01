// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Interaction/MD_TravelConsoleWidget.h"
#include "Groups/CommonButtonGroupBase.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Data/SaveGame/MD_SaveSlotManager.h"
#include "Engine/GameInstance.h"
#include "ImageUtils.h"
#include "Online/Session/MD_MultiplayManager.h"
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
    // 화면이 다시 열리면 저장 정보의 최신 상태 재조회
    bHasSaveList = false;
    // 활성화 연출 전에 모드와 동적 표시 준비
    SetSavedMode(bSavedMode);
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
        // 화면 초기 모드는 새 항해
        bSavedMode = false;
        // 새 콘솔의 저장 목록과 지도 대응 재조회
        bHasSaveList = false;
    }
    // 기존 현장이 삭제되거나 잠긴 경우 첫 사용 가능한 현장 선택
    if (IsValid(OwningConsole) == true && IsSiteAvailable(SelectedSiteIndex) == false)
    {
        // 콘솔 API가 제공하는 읽기 전용 현장 배열 사용
        SelectedSiteIndex = OwningConsole->GetSites().IndexOfByPredicate([](const FMD_TravelSiteEntry& Site) { return Site.bAvailable; });
    }
    // 초기화 시점에는 저장 조회 없이 화면 데이터만 반영
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

void UMD_TravelConsoleWidget::SetSavedMode(bool bInSavedMode)
{
    // 새 항해와 저장 항해 선택값을 분리한 모드 변경
    bSavedMode = bInSavedMode;
    // 최초 저장 진입과 실패 후 재진입에서만 비동기 조회
    if (bSavedMode == true && bHasSaveList == false && bIsRefreshing == false)
    {
        // 조회 함수에서 로딩 상태까지 갱신
        RefreshSaveSlots();
        // 현재 경로의 처리 종료
        return;
    }
    // 저장 결과가 있는 탭 왕복에서는 카드와 썸네일 재사용
    RefreshSelectionDisplay();
}

bool UMD_TravelConsoleWidget::SelectSite(int32 SiteIndex)
{
    // 저장 항해의 목적지 변경과 비호스트 및 사용할 수 없는 현장 선택 차단
    if (bSavedMode == true || IsLocalHost() == false || IsSiteAvailable(SiteIndex) == false)
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
    // 새 항해의 선택을 저장 모드와 독립적으로 보관
    SelectedSiteIndex = SiteIndex;
    // 지도와 상세 및 출항 영역 동기화
    RefreshSelectionDisplay();
    // 검증된 새 항해 목적지 선택 완료
    return true;
}

bool UMD_TravelConsoleWidget::SelectDifficulty(EMD_Difficulty Difficulty)
{
    // 저장의 난이도 변경과 비호스트 및 잘못된 Enum 입력 차단
    if (bSavedMode == true || IsLocalHost() == false || IsSupportedDifficulty(Difficulty) == false)
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

int32 UMD_TravelConsoleWidget::FindSavedSiteIndex(int32 SlotIndex)
{
    // 유효한 저장과 현장 제공자 확인
    if (SaveSlots.IsValidIndex(SlotIndex) == false || IsValid(OwningConsole) == false)
    {
        // 현재 콘솔에 대응하는 저장 현장 부재
        return INDEX_NONE;
    }
    // 저장의 패키지 경로와 콘솔의 소프트 참조를 로딩 없이 비교
    for (int32 Index = 0; Index < OwningConsole->GetSites().Num(); ++Index)
    {
        // 이름이 같은 다른 폴더의 레벨과 혼동 방지
        if (OwningConsole->GetSites()[Index].Level.IsNull() == false
            && OwningConsole->GetSites()[Index].Level.ToSoftObjectPath().GetLongPackageName() == SaveSlots[SlotIndex].MapPackagePath)
        {
            // 현재 조건에 대응하는 표시 또는 조회 결과 전달
            return Index;
        }
    }
    // 현재 콘솔에 대응하는 저장 현장 부재
    return INDEX_NONE;
}

bool UMD_TravelConsoleWidget::SelectSaveSlot(int32 SlotIndex)
{
    // 숨겨진 탭과 조회 중인 목록 및 존재하지 않는 슬롯의 입력 차단
    if (bSavedMode == false || bIsRefreshing == true || IsLocalHost() == false || SaveSlots.IsValidIndex(SlotIndex) == false)
    {
        // 검증을 통과하지 못한 요청의 실행 중단
        return false;
    }
    // 그룹 선택과 클릭이 같은 슬롯을 통지하는 경우 재진입과 중복 갱신 생략
    if (SelectedSlotIndex == SlotIndex)
    {
        // 현재 저장 선택 유지
        return true;
    }
    // 카드의 원본 슬롯 인덱스 보관
    SelectedSlotIndex = SlotIndex;
    // 저장 상세와 고정 난이도 표시 갱신
    RefreshSelectionDisplay();
    // 검증된 요청의 전달 또는 선택 적용 완료
    return true;
}

bool UMD_TravelConsoleWidget::RefreshSaveSlots()
{
    // 동일 화면의 비동기 조회 직렬화
    if (bIsRefreshing == true)
    {
        // 검증을 통과하지 못한 요청의 실행 중단
        return false;
    }
    // 이전 목록에 대한 출항 요청 차단
    bIsRefreshing = true;
    // 조회 중의 버튼 상태와 안내 반영
    RefreshSelectionDisplay();
    // 로컬 저장 파일 조회 권한과 게임 인스턴스 확인
    if (IsLocalHost() == false || IsValid(GetGameInstance()) == false)
    {
        // 실패 시 이전 호스트의 저장 목록 제거
        HandleSaveSlotsLoaded(TArray<FMD_GameData>(), false);
        // 검증을 통과하지 못한 요청의 실행 중단
        return false;
    }
    // 현재 게임 인스턴스의 저장 관리자 사용
    UMD_SaveSlotManager* SaveManager = GetGameInstance()->GetSubsystem<UMD_SaveSlotManager>();
    // 저장 관리자를 사용할 수 없는 환경 처리
    if (IsValid(SaveManager) == false)
    {
        // 실패와 빈 목록을 구분하는 완료 처리
        HandleSaveSlotsLoaded(TArray<FMD_GameData>(), false);
        // 검증을 통과하지 못한 요청의 실행 중단
        return false;
    }
    // 위젯 수명을 따르는 약한 UObject 콜백 구성
    FOnLoadMDGameDataListComplete Completion;
    // 저장 요약 목록 완료 함수 연결
    Completion.BindDynamic(this, &ThisClass::HandleSaveSlotsLoaded);
    // 전체 월드 데이터 없이 목록 요약만 비동기 조회
    SaveManager->AsyncLoadAllGameDataList(Completion);
    // 검증된 요청의 전달 또는 선택 적용 완료
    return true;
}

void UMD_TravelConsoleWidget::HandleSaveSlotsLoaded(const TArray<FMD_GameData>& GameDataList, bool bWasSuccessful)
{
    // 정렬 후에도 같은 저장을 유지할 고유 식별자 보관
    FString PreviousName;
    // 최초 조회에서는 이전 선택 부재
    if (SaveSlots.IsValidIndex(SelectedSlotIndex) == true)
    {
        // 배열 인덱스 대신 저장 고유 이름 사용
        PreviousName = SaveSlots[SelectedSlotIndex].UniqueName;
    }
    // 완료 시점의 로컬 호스트 권한 재검증
    bWasSuccessful = bWasSuccessful && IsLocalHost();
    // 이전 이미지와 새 응답을 비교할 저장 요약의 소유권 이동
    TArray<FMD_GameData> PreviousSlots = MoveTemp(SaveSlots);
    // 순서가 달라진 목록에서도 재사용할 기존 썸네일 보관
    TArray<TObjectPtr<UTexture2D>> PreviousThumbnails = MoveTemp(SaveThumbnails);
    // 정렬 순서와 무관한 이전 저장 위치 조회표
    TMap<FString, int32> PreviousIndices;
    // 조회표의 반복 확장 방지
    PreviousIndices.Reserve(PreviousSlots.Num());
    // 저장 고유 이름으로 이전 이미지 위치 연결
    for (int32 Index = 0; Index < PreviousSlots.Num(); ++Index)
    {
        // 동일 저장의 썸네일을 찾기 위한 인덱스 보관
        PreviousIndices.Add(PreviousSlots[Index].UniqueName, Index);
    }
    // 실패한 조회에는 이전 목록을 사용하지 않도록 교체
    SaveSlots = bWasSuccessful == true ? GameDataList : TArray<FMD_GameData>();
    // 최근 저장 우선의 안정적인 표시 순서
    SaveSlots.Sort([](const FMD_GameData& A, const FMD_GameData& B)
    {
        // 저장 시간이 같으면 고유 이름으로 순서 고정
        return A.SaveTime == B.SaveTime ? A.UniqueName < B.UniqueName : A.SaveTime > B.SaveTime;
    });
    // 갱신 전에 선택한 저장을 새 배열에서 복원
    SelectedSlotIndex = SaveSlots.IndexOfByPredicate([&PreviousName](const FMD_GameData& Data) { return Data.UniqueName == PreviousName; });
    // 삭제된 저장 선택을 최근 슬롯으로 대체
    if (SelectedSlotIndex == INDEX_NONE && SaveSlots.IsEmpty() == false)
    {
        // 최근 저장 우선 정렬된 첫 슬롯 선택
        SelectedSlotIndex = 0;
    }
    // 새 배열 정렬에 맞춘 썸네일 캐시 준비
    SaveThumbnails.Reset(SaveSlots.Num());
    // 압축 이미지가 변경된 저장만 새 텍스처 생성
    for (FMD_GameData& GameData : SaveSlots)
    {
        // 이전 정렬 위치와 독립적인 저장 식별
        int32* PreviousIndex = PreviousIndices.Find(GameData.UniqueName);
        // 실제 이미지 내용까지 동일한 유효 텍스처 재사용
        if (PreviousIndex != nullptr && PreviousThumbnails.IsValidIndex(*PreviousIndex) == true
            && IsValid(PreviousThumbnails[*PreviousIndex]) == true && PreviousSlots[*PreviousIndex].ThumbnailImage == GameData.ThumbnailImage)
        {
            // 기존 텍스처의 중복 디코딩과 리소스 생성 생략
            SaveThumbnails.Add(PreviousThumbnails[*PreviousIndex]);
        }
        else
        {
            // 캡처가 없거나 손상된 경우 빈 이미지 참조 보관
            SaveThumbnails.Add(GameData.ThumbnailImage.IsEmpty() == false ? FImageUtils::ImportBufferAsTexture2D(GameData.ThumbnailImage) : nullptr);
        }
    }
    // 조회한 모든 슬롯을 카드 영역에 적용
    RebuildSaveCards();
    // 완료 이후 입력과 명시적인 새로고침 허용
    bIsRefreshing = false;
    // 성공한 빈 목록도 탭 왕복에서 재사용
    bHasSaveList = bWasSuccessful;
    // 조회 도중 탭이 바뀌었으면 현재 모드의 표시 유지
    RefreshSelectionDisplay();
    // 프로젝트별 실패 안내 또는 추가 연출 전달
    OnSaveListUpdated(bWasSuccessful);
}

void UMD_TravelConsoleWidget::RebuildSaveCards()
{
    // 카드 생성 이벤트에서 목록을 다시 교체하는 재진입 차단
    TGuardValue<bool> RefreshGuard(bIsRefreshing, true);
    // 새 클래스를 만들지 않고 CommonUI의 단일 선택 그룹 사용
    if (IsValid(SaveCardButtonGroup) == false)
    {
        // 위젯 수명에 종속되는 선택 관리자 생성
        SaveCardButtonGroup = NewObject<UCommonButtonGroupBase>(this);
        // 실제 버튼 선택 변경을 저장 데이터 선택과 동기화
        SaveCardButtonGroup->NativeOnSelectedButtonBaseChanged.AddUObject(this, &ThisClass::HandleSaveCardSelectionChanged);
    }
    // 개수와 배치가 같은 목록의 카드 및 Slate 재생성 생략
    bool bReuseCards = IsValid(SB_SlotList) == true && IsValid(SaveCardWidgetClass) == true
        && SaveCards.Num() == SaveSlots.Num() && SB_SlotList->GetChildrenCount() == SaveCards.Num()
        && SaveCardButtonGroup->GetButtonCount() == SaveCards.Num();
    // 외부에서 교체되거나 생성에 실패한 카드의 재사용 차단
    for (int32 Index = 0; bReuseCards == true && Index < SaveCards.Num(); ++Index)
    {
        // 현재 클래스와 실제 자식 순서까지 일치하는 목록만 유지
        bReuseCards = IsValid(SaveCards[Index]) == true && SaveCards[Index]->GetClass() == SaveCardWidgetClass
            && SB_SlotList->GetChildAt(Index) == SaveCards[Index];
    }
    // 카드 구성이 달라진 경우에만 기존 목록 해제
    if (bReuseCards == false)
    {
        // 제거된 카드의 선택 이벤트와 그룹 참조 정리
        SaveCardButtonGroup->RemoveAll();
        // 이전 카드의 클릭 전달 해제
        for (UMD_CommonButtonBase* Card : SaveCards)
        {
            // 유효한 카드에만 구독 해제
            if (IsValid(Card) == true)
            {
                // 제거된 목록의 입력 차단
                Card->OnClicked().RemoveAll(this);
            }
        }
        // 생성 실패 위치도 원본 슬롯 인덱스와 대응
        SaveCards.Empty(SaveSlots.Num());
        // 새 목록의 카드 참조 공간 확보
        SaveCards.SetNum(SaveSlots.Num());
        // 목록 영역이 생략된 파생 화면 지원
        if (IsValid(SB_SlotList) == true)
        {
            // 디자이너 예제 카드와 이전 목록 제거
            SB_SlotList->ClearChildren();
        }
    }
    // 선택 가능한 카드가 있으면 반드시 하나의 선택 유지
    SaveCardButtonGroup->SetSelectionRequired(true);
    // 목록 영역이나 카드 클래스가 없는 파생 화면 지원
    if (IsValid(SB_SlotList) == false || IsValid(SaveCardWidgetClass) == false)
    {
        // 카드 없이 상세 데이터만 갱신
        return;
    }
    // 캡처 없는 기록으로 교체할 때 사용할 디자이너의 기본 이미지
    FSlateBrush DefaultCardBrush = SaveCardWidgetClass.GetDefaultObject()->GetContentImage();
    // 목적지에 관계없이 전체 저장 목록 표시
    for (int32 Index = 0; Index < SaveSlots.Num(); ++Index)
    {
        // 기존 카드 재사용 또는 지정된 BP 카드 클래스 생성
        UMD_CommonButtonBase* Card = bReuseCards == true ? SaveCards[Index].Get() : CreateWidget<UMD_CommonButtonBase>(this, SaveCardWidgetClass);
        // 실패한 카드의 원본 인덱스 유지
        if (IsValid(Card) == false)
        {
            // 현재 목록에 표시하지 않는 항목 제외
            continue;
        }
        // 저장 경로를 현재 콘솔의 현장과 대응
        int32 SiteIndex = FindSavedSiteIndex(Index);
        // 등록되지 않은 현장도 저장 식별 이름으로 표시
        FText MapName = FText::FromString(SaveSlots[Index].UniqueName);
        // 현재 콘솔의 현장 이름이 있으면 우선 사용
        if (IsValid(OwningConsole) == true && OwningConsole->GetSites().IsValidIndex(SiteIndex) == true)
        {
            // 출항 상세와 같은 현장명 적용
            MapName = OwningConsole->GetSites()[SiteIndex].DisplayName;
        }
        // 설정되지 않은 저장 필드의 빈 값 표시
        FText Missing = FText::FromString(TEXT("—"));
        // 데이터 복사 없이 저장된 일차와 난이도 조회
        const FMD_GameStateData* State = SaveSlots[Index].FindDataField<FMD_GameStateData>(FMD_GameStateData::DataFieldKey);
        // 화면 설정에서 현장 식별 문구 조회
        FMD_TravelMapPresentation* Presentation = MapPresentation.Find(SiteIndex);
        // 카드에는 식별자와 중복되지 않는 짧은 현장명 사용
        if (Presentation != nullptr && Presentation->MapName.IsEmpty() == false)
        {
            // 참고 디자인의 식별자와 현장명 분리 표시
            MapName = Presentation->MapName;
        }
        // 카드의 현재 표시와 비교할 현장 식별 문구
        FText CardTitle = Presentation != nullptr ? Presentation->MapId : MapName;
        // 동일 문구의 반복 레이아웃 갱신 생략
        if (Card->GetContentText().EqualTo(CardTitle) == false)
        {
            // 기존 공통 버튼의 현장 식별 문구 갱신
            Card->SetContentText(CardTitle);
        }
        // 목록 재사용 시 기존 선택 표시 보존
        UpdateButton(Card, bReuseCards == true && Card->GetSelected() == true, true);
        // 카드의 크기와 선택 색상을 보존하는 현재 브러시
        FSlateBrush Brush = Card->GetContentImage();
        // 유효한 캡처의 존재 여부를 이미지와 UV 선택에 공유
        bool bHasThumbnail = SaveThumbnails.IsValidIndex(Index) == true && IsValid(SaveThumbnails[Index]) == true;
        // 캡처가 사라진 기록에는 이전 썸네일 대신 기본 이미지 복원
        Brush.SetResourceObject(bHasThumbnail == true ? SaveThumbnails[Index].Get() : DefaultCardBrush.GetResourceObject());
        // 캡처 전체 영역 또는 디자이너 기본 이미지 영역 복원
        Brush.SetUVRegion(bHasThumbnail == true ? FBox2f() : FBox2f(DefaultCardBrush.GetUVRegion()));
        // 실제 이미지가 달라진 카드에만 브러시 적용
        if (Card->GetContentImage() != Brush)
        {
            // 재구성 시에도 새 이미지 유지
            Card->SetContentImage(Brush);
        }
        // 새 카드에만 그룹과 클릭 및 스크롤 슬롯 연결
        if (bReuseCards == false)
        {
            // 화면 목록과 원본 저장 배열 대응
            SaveCards[Index] = Card;
            // 상호 배타적 선택 그룹에 등록
            SaveCardButtonGroup->AddWidget(Card);
            // 실제 카드 클릭의 원본 저장 인덱스 연결
            Card->OnClicked().AddUObject(this, &ThisClass::HandleSaveCardClicked, Index);
            // 카드 외부 여백을 설정할 스크롤 슬롯 생성
            UScrollBoxSlot* CardSlot = Cast<UScrollBoxSlot>(SB_SlotList->AddChild(Card));
            // 생성된 슬롯에만 저장 카드 간격 적용
            if (IsValid(CardSlot) == true)
            {
                // 좌우 여백 없이 위아래에 각각 6의 패딩 적용
                CardSlot->SetPadding(FMargin(0.0f, 6.0f));
            }
        }
        // 새 카드 클래스 없이 기존 BP의 텍스트 구성에 실제 저장 정보 전달
        OnSaveCardCreated(Card, Index, SaveSlots[Index].SaveTime, MapName,
            State != nullptr ? FText::Format(NSLOCTEXT("MDTravel", "DayFormat", "{0}일차"), FText::AsNumber(State->ElapsedDay)) : Missing,
            State != nullptr ? GetDifficultyText(State->ContractDifficulty) : Missing);
    }
}

void UMD_TravelConsoleWidget::HandleSaveCardClicked(int32 SlotIndex)
{
    // 권한과 슬롯 범위 검증을 포함하는 공통 선택 경로 사용
    SelectSaveSlot(SlotIndex);
}

void UMD_TravelConsoleWidget::HandleSaveCardSelectionChanged(UCommonButtonBase* Card, int32 ButtonIndex)
{
    // 카드 생성과 목록 교체 중의 임시 선택 통지 무시
    if (bIsRefreshing == true || bSavedMode == false || IsValid(Card) == false)
    {
        // 현재 표시 갱신의 선택 정책 유지
        return;
    }
    // 일반적인 목록에서는 그룹이 전달한 인덱스를 탐색 없이 사용
    if (SaveCards.IsValidIndex(ButtonIndex) == true && SaveCards[ButtonIndex] == Card)
    {
        // 이미 확인된 원본 저장 슬롯 선택
        SelectSaveSlot(ButtonIndex);
    }
    else
    {
        // 생성 실패로 그룹 순서가 달라진 경우에만 원본 인덱스 탐색
        SelectSaveSlot(SaveCards.IndexOfByPredicate([Card](UMD_CommonButtonBase* Entry) { return Entry == Card; }));
    }
}

bool UMD_TravelConsoleWidget::CanRequestTravel()
{
    // 참가자와 소유자 없는 화면의 세션 변경 차단
    if (IsLocalHost() == false)
    {
        // 검증을 통과하지 못한 요청의 실행 중단
        return false;
    }
    // 저장 모드의 출항은 실제 슬롯 복원 가능 여부로 판단
    if (bSavedMode == true)
    {
        // 조회 중이거나 현재 목록에서 사라진 이전 선택 제외
        if (bIsRefreshing == true || SaveSlots.IsValidIndex(SelectedSlotIndex) == false)
        {
            // 검증을 통과하지 못한 요청의 실행 중단
            return false;
        }
        // 실제 세션 관리자를 사용할 수 있는 환경 확인
        if (IsValid(GetGameInstance()) == false || IsValid(GetGameInstance()->GetSubsystem<UMD_MultiplayManager>()) == false)
        {
            // 검증을 통과하지 못한 요청의 실행 중단
            return false;
        }
        // 저장 관리자와 동일한 형식 및 목적지 사전 검증
        return SaveSlots[SelectedSlotIndex].UniqueName.IsEmpty() == false
            && SaveSlots[SelectedSlotIndex].MapPackagePath.IsEmpty() == false
            && SaveSlots[SelectedSlotIndex].SchemaVersion == FMD_GameData::CurrentSchemaVersion;
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
    // 저장 항해는 기존 세션 복원 경로 사용
    if (bSavedMode == true)
    {
        // 실제 성공 여부는 기존 세션 관리자에서 통지
        GetGameInstance()->GetSubsystem<UMD_MultiplayManager>()->StartSession(SaveSlots[SelectedSlotIndex], true);
    }
    else
    {
        // 기존 서버의 권한 및 현장 검증을 유지하는 출항 RPC
        Cast<AMD_PlayerController>(GetOwningPlayer())->ServerRequestTravel(OwningConsole, SelectedSiteIndex, SelectedDifficulty);
    }
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
    // 선택 버튼이 없는 난이도도 저장된 Enum 표시명 사용
    return StaticEnum<EMD_Difficulty>()->GetDisplayNameTextByValue(static_cast<int64>(Difficulty));
}

int32 UMD_TravelConsoleWidget::GetDisplayedSiteIndex()
{
    // 저장 모드에서는 실제 선택한 기록의 목적지 사용
    if (bSavedMode == true)
    {
        // 조회 중이거나 선택이 비어 있는 경우 이전 경로 표시 차단
        return bIsRefreshing == false ? FindSavedSiteIndex(SelectedSlotIndex) : INDEX_NONE;
    }
    // 새 항해에서는 현재 이용 가능한 선택 목적지만 반환
    return IsSiteAvailable(SelectedSiteIndex) == true ? SelectedSiteIndex : INDEX_NONE;
}

void UMD_TravelConsoleWidget::RefreshSelectionDisplay()
{
    // 지도와 상세 및 하단 영역의 모드를 동시에 전환
    for (UWidgetSwitcher* Switcher : { WS_Map, WS_DetailInfo, WS_Difficulty })
    {
        // 일부 영역을 생략한 파생 화면 허용
        if (IsValid(Switcher) == true)
        {
            // 현재 디자이너의 새 항해 0번과 저장 항해 1번 대응
            Switcher->SetActiveWidgetIndex(bSavedMode == true ? 1 : 0);
        }
    }
    // 이번 표시 갱신에서 공유할 소유자 권한
    bool bCanSelect = IsLocalHost();
    // 탭 자체는 비호스트도 읽기 모드로 전환 허용
    UpdateButton(Btn_NewGameTab, bSavedMode == false, true);
    // 저장 모드 선택 상태 표시
    UpdateButton(Btn_LoadGameTab, bSavedMode, true);
    // 새 항해 지도와 난이도의 선택 표시
    UpdateButton(Btn_X7, SelectedSiteIndex == X7SiteIndex, bCanSelect && bSavedMode == false && IsSiteAvailable(X7SiteIndex));
    // 두 번째 현장의 준비 상태 반영
    UpdateButton(Btn_TR9, SelectedSiteIndex == TR9SiteIndex, bCanSelect && bSavedMode == false && IsSiteAvailable(TR9SiteIndex));
    // 쉬움 난이도 선택 반영
    UpdateButton(Btn_Easy, SelectedDifficulty == EMD_Difficulty::Safe, bCanSelect && bSavedMode == false);
    // 보통 난이도 선택 반영
    UpdateButton(Btn_Normal, SelectedDifficulty == EMD_Difficulty::Standard, bCanSelect && bSavedMode == false);
    // 어려움 난이도 선택 반영
    UpdateButton(Btn_Hard, SelectedDifficulty == EMD_Difficulty::Dangerous, bCanSelect && bSavedMode == false);
    // 조회 중인 이전 저장의 지도 선택 표시 제거
    int32 SavedSiteIndex = bIsRefreshing == false ? FindSavedSiteIndex(SelectedSlotIndex) : INDEX_NONE;
    // 저장 지도는 슬롯의 목적지 표시만 제공하며 직접 입력 차단
    UpdateButton(Btn_X7_Load, SavedSiteIndex == X7SiteIndex, false);
    // 두 번째 저장 목적지도 선택된 슬롯에만 종속
    UpdateButton(Btn_TR9_Load, SavedSiteIndex == TR9SiteIndex, false);
    // 목록 전체가 공유하는 입력 허용 상태 계산
    bool bCanSelectSave = bCanSelect && bSavedMode == true && bIsRefreshing == false;
    // 저장 카드의 입력 가능 상태를 실제 모드와 조회 상태에 대응
    for (UMD_CommonButtonBase* Card : SaveCards)
    {
        // 입력 허용 상태가 달라진 유효 카드만 갱신
        if (IsValid(Card) == true && Card->GetIsEnabled() != bCanSelectSave)
        {
            // 비동기 조회 중 또는 숨겨진 탭의 입력 차단
            Card->SetIsEnabled(bCanSelectSave);
        }
    }
    // 현재 슬롯을 공통 그룹의 유일한 선택으로 적용
    if (IsValid(SaveCardButtonGroup) == true && SaveCards.IsValidIndex(SelectedSlotIndex) == true
        && IsValid(SaveCards[SelectedSlotIndex]) == true && SaveCardButtonGroup->GetSelectedButtonBase() != SaveCards[SelectedSlotIndex])
    {
        // 새 선택에 따라 나머지 카드의 선택과 BP 체크 표시 자동 해제
        SaveCardButtonGroup->SelectButtonAtIndex(SaveCardButtonGroup->FindButtonIndex(SaveCards[SelectedSlotIndex]), false);
    }
    // 선택 부재 시 디자이너의 예시 문구가 남지 않도록 초기화
    FText MapId = FText::FromString(bSavedMode == true ? TEXT("—") : TEXT("-"));
    // 식별자를 제외한 상세 현장명
    FText MapName = MapId;
    // 식별자와 현장명을 함께 표시하는 출항 목적지
    FText TicketDestination = MapId;
    // 현장 안내는 설정하지 않은 경우 빈 문구
    FText Description;
    // 저장 메타데이터의 빈 값 표시
    FText DayText = MapId;
    // 저장 난이도의 빈 값 표시
    FText DifficultyText = MapId;
    // 저장 시각의 빈 값 표시
    FText SaveTimeText = MapId;
    // 이전 선택의 이미지 잔류 방지
    UTexture2D* Preview = nullptr;
    // 저장 선택은 새 항해의 선택 인덱스와 분리
    int32 SiteIndex = bSavedMode == true ? SavedSiteIndex : SelectedSiteIndex;
    // 현재 모드의 실제 데이터 존재 여부 확인
    bool bHasSelection = bSavedMode == true ? SaveSlots.IsValidIndex(SelectedSlotIndex) : IsSiteAvailable(SelectedSiteIndex);
    // 조회 중에는 이전 저장의 상세를 노출하지 않도록 처리
    bHasSelection = bHasSelection && (bSavedMode == false || bIsRefreshing == false);
    // 유효한 선택의 현장 정보를 표시
    if (bHasSelection == true)
    {
        // 콘솔에 등록된 현장 표시명 사용
        if (IsValid(OwningConsole) == true && OwningConsole->GetSites().IsValidIndex(SiteIndex) == true)
        {
            // 실제 출항 대상의 이름 표시
            MapName = OwningConsole->GetSites()[SiteIndex].DisplayName;
        }
        // 별도 표시 설정이 없는 현장은 콘솔의 전체 이름 유지
        TicketDestination = MapName;
        // 디자이너가 지정한 식별자와 이미지 및 설명 조회
        FMD_TravelMapPresentation* Presentation = MapPresentation.Find(SiteIndex);
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
        // 저장 항해의 메타데이터는 현재 콘솔 설정 대신 저장값 사용
        if (bSavedMode == true)
        {
            // 콘솔 로스터에서 제거된 저장도 식별할 이름 제공
            if (SiteIndex == INDEX_NONE)
            {
                // 실제 저장 식별자 표시
                MapName = FText::FromString(SaveSlots[SelectedSlotIndex].UniqueName);
                // 등록되지 않은 저장도 하단에서 같은 식별 문구 사용
                TicketDestination = MapName;
            }
            // 참고 디자인의 월·일과 24시간 시각 형식으로 표시
            SaveTimeText = FormatSaveTime(SaveSlots[SelectedSlotIndex].SaveTime);
            // 큰 점수 기록 배열의 복사를 피하는 검증된 읽기 전용 저장 필드 조회
            const FMD_GameStateData* State = SaveSlots[SelectedSlotIndex].FindDataField<FMD_GameStateData>(FMD_GameStateData::DataFieldKey);
            // 없는 저장 필드는 기본값으로 오인하지 않도록 유지
            if (State != nullptr)
            {
                // 저장된 진행 일차 표시
                DayText = FText::Format(NSLOCTEXT("MDTravel", "DayFormat", "{0}일차"), FText::AsNumber(State->ElapsedDay));
                // 현재 버튼 용어와 일치하는 저장 난이도 표시
                DifficultyText = GetDifficultyText(State->ContractDifficulty);
            }
            // 유효한 저장 캡처가 있으면 현장 기본 이미지보다 우선
            if (SaveThumbnails.IsValidIndex(SelectedSlotIndex) == true && IsValid(SaveThumbnails[SelectedSlotIndex]) == true)
            {
                // 캐시된 캡처 재사용
                Preview = SaveThumbnails[SelectedSlotIndex];
            }
        }
    }
    // 조회 중 상태와 정상적으로 비어 있는 저장 선택 구분
    bool bEmptySave = bSavedMode == true && bIsRefreshing == false && bHasSelection == false;
    // 참고 화면과 동일한 빈 저장 안내 적용
    if (bEmptySave == true)
    {
        // 목적지 이름 대신 빈 저장 상태 안내
        MapName = NSLOCTEXT("MDTravel", "EmptySaveName", "저장 기록 없음");
        // 출항 영역에서 새 항해 시작 경로 안내
        TicketDestination = NSLOCTEXT("MDTravel", "EmptySaveDestination", "새 항해를 시작하세요");
    }
    // 탭 전환에 맞춘 화면 제목 갱신
    SetDisplayText(Txt_Map_Header, bSavedMode == true ? NSLOCTEXT("MDTravel", "SavedTitle", "이어서 떠날까요?")
        : NSLOCTEXT("MDTravel", "NewTitle", "어디로 떠날까요?"));
    // 현재 모드의 선택 안내 갱신
    SetDisplayText(Txt_Map_SubHeader, bSavedMode == true ? NSLOCTEXT("MDTravel", "SavedSubtitle", "저장된 항해 선택")
        : NSLOCTEXT("MDTravel", "NewSubtitle", "항해 목적지 선택"));
    // 새 목적지와 기존 항해 기록의 상세 제목 구분
    SetDisplayText(Txt_Detail_Header, bSavedMode == true ? NSLOCTEXT("MDTravel", "SavedDetailHeader", "선택한 항해")
        : NSLOCTEXT("MDTravel", "NewDetailHeader", "선택한 목적지"));
    // 하단 출항 영역의 모드 표시
    SetDisplayText(Txt_Difficulty_Header, bSavedMode == true ? NSLOCTEXT("MDTravel", "SavedTicketHeader", "항해 이어가기")
        : NSLOCTEXT("MDTravel", "NewTicketHeader", "출항 준비"));
    // 버튼의 실제 출항 동작과 문구 일치
    SetDisplayText(Txt_Button_Travel, bSavedMode == true ? NSLOCTEXT("MDTravel", "ResumeTravel", "이어서 항해")
        : NSLOCTEXT("MDTravel", "DepartTravel", "출항하기"));
    // 저장이 없는 경우 난이도 잠금 안내 대신 새 기록 생성 안내
    SetDisplayText(Txt_Difficulty_Header_2, bEmptySave == true ? NSLOCTEXT("MDTravel", "EmptySaveHint", "새 항해에서 기록을 만들 수 있어요.")
        : NSLOCTEXT("MDTravel", "SavedSettingsHint", "저장된 설정으로 이어갑니다."));
    // 빈 목록에 최근 저장순 안내가 남지 않도록 처리
    SetDisplayText(Txt_MapHint_LoadGame, bIsRefreshing == true ? NSLOCTEXT("MDTravel", "Loading", "조회 중")
        : (bEmptySave == true ? FText::GetEmpty() : NSLOCTEXT("MDTravel", "SaveSortHint", "최근 저장순")));
    // 실제 디자이너 이름에 대응하는 상세 식별자 표시
    SetDisplayText(Txt_Detail_MapName, MapId);
    // 상세 현장명 표시
    SetDisplayText(Txt_Detail_MapName_1, MapName);
    // 출항 영역의 현장명 동기화
    SetDisplayText(Txt_Difficulty_MapName, TicketDestination);
    // 새 항해 설명 갱신
    SetDisplayText(Txt_Detail_Description, Description);
    // 저장 상세 진행 일차 갱신
    SetDisplayText(Txt_Detail_Day, DayText);
    // 하단의 빈 저장 안내 또는 실제 진행 일차 표시
    SetDisplayText(Txt_Day, bEmptySave == true ? NSLOCTEXT("MDTravel", "NoSavedRecord", "저장된 기록 없음") : DayText);
    // 저장 상세 난이도 갱신
    SetDisplayText(Txt_Detail_Difficulty, DifficultyText);
    // 빈 저장에서는 하단 안내 뒤의 난이도 문구 제거
    SetDisplayText(Txt_Difficulty, bSavedMode == true && bHasSelection == false ? FText::GetEmpty() : DifficultyText);
    // 저장 정보가 있는 경우에만 디자인의 가운데점 구분자 표시
    SetDisplayText(Txt_Dash, bHasSelection == true && bSavedMode == true ? FText::FromString(TEXT(" · ")) : FText::GetEmpty());
    // 저장 시각 갱신
    SetDisplayText(Txt_Detail_SaveTime, SaveTimeText);
    // 새 목적지와 저장 기록을 구분하는 배지 문구 표시
    SetDisplayText(Txt_Badge, bHasSelection == false ? FText::GetEmpty()
        : (bSavedMode == true ? NSLOCTEXT("MDTravel", "SavedRecord", "저장 기록") : NSLOCTEXT("MDTravel", "Selected", "선택됨")));
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
        // 캡처나 현장 이미지의 전체 영역 사용
        Brush.SetUVRegion(FBox2f());
        // 이미지 부재 시 흰 사각형 대신 투명한 빈 영역 표시
        Brush.DrawAs = IsValid(Preview) == true ? ESlateBrushDrawType::Image : ESlateBrushDrawType::NoDrawType;
        // 실제 이미지 리소스와 그리기 방식 반영
        Img_Preview->SetBrush(Brush);
    }
    // 일반 UButton인 출항 버튼의 허용 상태 반영
    if (IsValid(Btn_Travel) == true)
    {
        // 현재 탭의 실제 요청 검증과 활성 상태 통일
        Btn_Travel->SetIsEnabled(CanRequestTravel());
    }
    // 추가 연출은 이벤트 그래프에서 확장
    OnSelectionChanged();
}

FText UMD_TravelConsoleWidget::FormatSaveTime(FDateTime SaveTime)
{
    // 이미 로컬 시각으로 저장된 값을 재변환하지 않는 날짜와 분 단위 표시
    return FText::Format(NSLOCTEXT("MDTravel", "SaveTimeFormat", "{0}월 {1}일 {2}"), FText::AsNumber(SaveTime.GetMonth()),
        FText::AsNumber(SaveTime.GetDay()), FText::FromString(SaveTime.ToString(TEXT("%H:%M"))));
}
