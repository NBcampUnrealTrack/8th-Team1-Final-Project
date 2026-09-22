// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Interaction/MD_LoadGameTabWidget.h"

#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Data/SaveGame/MD_SaveSlotManager.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "ImageUtils.h"
#include "Online/Session/MD_MultiplayManager.h"
#include "UI/MD_CommonButtonBase.h"

bool UMD_LoadGameTabWidget::IsLocalHost()
{
    // UI 소유 플레이어의 로컬 서버 권한 확인
    APlayerController* PlayerController = GetOwningPlayer();
    if (IsValid(PlayerController) == false)
    {
        // 소유 플레이어 부재
        return false;
    }
    // 참가자의 로컬 파일을 호스트 저장으로 오인하는 상황 방지
    return PlayerController->IsLocalController() && PlayerController->HasAuthority();
}

bool UMD_LoadGameTabWidget::RefreshSaveSlots()
{
    // 같은 위젯의 조회를 직렬화하여 늦은 결과 덮어쓰기 방지
    if (bIsRefreshing == true)
    {
        // 기존 조회 결과 대기
        return false;
    }
    // 조회 완료 전 이전 항목의 복원 요청 차단
    bIsRefreshing = true;
    // 로딩 표현 시작
    OnSaveListLoading();
    // 출항 버튼의 활성 상태 동기화
    RefreshSelection();
    if (IsLocalHost() == false)
    {
        // 권한 변경 시 이전 호스트 저장 목록 제거
        HandleSaveSlotsLoaded(TArray<FMD_GameData>(), false);
        // 저장 조회 권한 부재
        return false;
    }
    // 저장 관리자 수명 검증
    UGameInstance* GameInstance = GetGameInstance();
    if (IsValid(GameInstance) == false)
    {
        // 조회 실패 통지
        HandleSaveSlotsLoaded(TArray<FMD_GameData>(), false);
        // 게임 인스턴스 부재
        return false;
    }
    // 현재 게임 인스턴스의 저장 경로 재사용
    UMD_SaveSlotManager* SaveManager = GameInstance->GetSubsystem<UMD_SaveSlotManager>();
    if (IsValid(SaveManager) == false)
    {
        // 조회 실패 통지
        HandleSaveSlotsLoaded(TArray<FMD_GameData>(), false);
        // 저장 관리자 부재
        return false;
    }
    // UObject 수명을 따르는 약한 동적 콜백 구성
    FOnLoadMDGameDataListComplete Completion;
    // 조회 완료 수신
    Completion.BindDynamic(this, &ThisClass::HandleSaveSlotsLoaded);
    // 목록 요약만 비동기로 조회
    SaveManager->AsyncLoadAllGameDataList(Completion);
    // 조회 요청 전달 완료
    return true;
}

void UMD_LoadGameTabWidget::HandleSaveSlotsLoaded(const TArray<FMD_GameData>& GameDataList, bool bWasSuccessful)
{
    // 동일 저장의 선택을 정렬 이후에도 유지할 식별자 보관
    FString PreviousName;
    if (SaveSlots.IsValidIndex(SelectedSlotIndex) == true)
    {
        // 인덱스 대신 슬롯 고유 이름 사용
        PreviousName = SaveSlots[SelectedSlotIndex].UniqueName;
    }
    // 실패하거나 소유 권한이 바뀐 목록은 빈 상태로 교체
    SaveSlots.Reset();
    if (bWasSuccessful == true)
    {
        // 완료 시점의 사용자 권한 재검증
        bWasSuccessful = IsLocalHost();
        if (bWasSuccessful == true)
        {
            // GC가 추적하는 속성으로 요약 보관
            SaveSlots = GameDataList;
        }
    }
    // 최근 저장 우선이며 저장 시각이 같으면 고유 이름으로 안정 정렬
    SaveSlots.Sort([](const FMD_GameData& A, const FMD_GameData& B)
    {
        // 같은 시각의 저장 항목 순서 고정
        if (A.SaveTime == B.SaveTime)
        {
            // 고유 이름을 보조 정렬 키로 사용
            return A.UniqueName < B.UniqueName;
        }
        // 최근 저장 우선
        return A.SaveTime > B.SaveTime;
    });
    // 갱신 전 선택한 슬롯을 새 목록에서 탐색
    SelectedSlotIndex = SaveSlots.IndexOfByPredicate([&PreviousName](const FMD_GameData& Data) { return Data.UniqueName == PreviousName; });
    if (SelectedSlotIndex == INDEX_NONE && SaveSlots.IsEmpty() == false)
    {
        // 최초 진입 또는 삭제된 선택은 최근 저장으로 대체
        SelectedSlotIndex = 0;
    }
    // 이전 목록 이미지의 참조를 해제하고 새 정렬 순서와 대응
    SaveThumbnails.Reset(SaveSlots.Num());
    // 선택을 바꿀 때마다 압축 이미지를 다시 해제하는 비용 방지
    for (FMD_GameData& GameData : SaveSlots)
    {
        // 캡처가 없거나 해제에 실패한 슬롯은 디자이너 기본 이미지 사용
        SaveThumbnails.Add(GameData.ThumbnailImage.IsEmpty() == false
            ? FImageUtils::ImportBufferAsTexture2D(GameData.ThumbnailImage) : nullptr);
    }
    // 카드 생성 이벤트에서 목록을 다시 조회하여 현재 배열을 교체하는 재진입 차단
    bIsRefreshing = true;
    // 실제 저장 개수로 목록 갱신
    RebuildSaveCards();
    // 카드 생성 완료 후 선택과 완료 이벤트의 후속 요청 허용
    bIsRefreshing = false;
    // 상세 정보와 불러오기 활성 상태 동기화
    RefreshSelection();
    // 성공한 빈 목록과 조회 실패 구분
    OnSaveListUpdated(bWasSuccessful);
}

void UMD_LoadGameTabWidget::RebuildSaveCards()
{
    // 이전 목록의 클릭 구독 정리
    for (UMD_CommonButtonBase* Card : SaveCards)
    {
        if (IsValid(Card) == true)
        {
            // 제거된 카드의 입력 전달 차단
            Card->OnClicked().RemoveAll(this);
        }
    }
    // 새 저장 배열과 카드 인덱스 대응 준비
    SaveCards.Reset();
    if (IsValid(SB_DataList) == false)
    {
        // 목록 영역이 없는 데이터 전용 화면 지원
        return;
    }
    // 디자이너 임시 저장과 이전 항목 제거
    SB_DataList->ClearChildren();
    if (IsValid(SaveCardWidgetClass) == false)
    {
        // 카드 클래스 미지정
        return;
    }
    // 생성 실패에도 원본 인덱스 유지
    SaveCards.SetNum(SaveSlots.Num());
    // 같은 목록의 카드에 적용할 호스트 권한을 한 번만 확인
    bool bCanSelect = IsLocalHost();
    for (int32 Index = 0; Index < SaveSlots.Num(); ++Index)
    {
        // 저장 항목의 공통 카드 생성
        UMD_CommonButtonBase* Card = CreateWidget<UMD_CommonButtonBase>(this, SaveCardWidgetClass);
        if (IsValid(Card) == false)
        {
            continue;
        }
        // 실제 저장 이름 표시
        Card->SetContentText(FText::FromString(SaveSlots[Index].UniqueName));
        // 유효한 캡처가 있는 카드만 디자이너 기본 이미지 교체
        if (IsValid(SaveThumbnails[Index]) == true)
        {
            // 디자이너의 카드 크기와 모서리를 유지할 브러시 복사
            FSlateBrush ThumbnailBrush = Card->GetContentImage();
            // 슬롯과 같은 정렬 순서의 장면 텍스처 연결
            ThumbnailBrush.SetResourceObject(SaveThumbnails[Index]);
            // 별도 잘라내기 없이 캡처 텍스처 전체 사용
            ThumbnailBrush.SetUVRegion(FBox2f());
            // 재구성 이후에도 유지되는 카드 이미지 속성 갱신
            Card->SetContentImage(ThumbnailBrush);
        }
        // 단일 선택 표시 허용
        Card->SetIsSelectable(true);
        // 선택한 저장의 재클릭 해제 방지
        Card->SetIsToggleable(false);
        // 포커스 이동에 의한 저장 선택 불일치 방지
        Card->SetShouldSelectUponReceivingFocus(false);
        // 호스트만 저장 선택 허용
        Card->SetIsEnabled(bCanSelect);
        // 원본 저장 인덱스 전달
        Card->OnClicked().AddUObject(this, &ThisClass::HandleSaveCardClicked, Index);
        // 원본 저장과 카드 연결
        SaveCards[Index] = Card;
        // 스크롤 목록에 배치
        SB_DataList->AddChild(Card);
        // 날짜와 이미지 표현 확장
        OnSaveCardCreated(Card, Index, SaveSlots[Index].SaveTime);
    }
}

bool UMD_LoadGameTabWidget::SelectSaveSlot(int32 SlotIndex)
{
    // 갱신 중 인덱스 변경 차단
    if (bIsRefreshing == true)
    {
        // 새 목록 대기
        return false;
    }
    if (IsLocalHost() == false)
    {
        // 저장 선택 권한 부재
        return false;
    }
    if (SaveSlots.IsValidIndex(SlotIndex) == false)
    {
        // 빈 슬롯과 범위 밖 인덱스 거부
        return false;
    }
    // 복원할 저장의 현재 인덱스 보관
    SelectedSlotIndex = SlotIndex;
    // 상세 패널과 선택 표시 동기화
    RefreshSelection();
    // 저장 선택 완료
    return true;
}

bool UMD_LoadGameTabWidget::GetSelectedGameData(FMD_GameData& GameData)
{
    // 실패 시 이전 데이터 잔류 방지
    GameData = FMD_GameData();
    if (SaveSlots.IsValidIndex(SelectedSlotIndex) == false)
    {
        // 선택한 저장 부재
        return false;
    }
    // 상세 패널에 선택 요약 전달
    GameData = SaveSlots[SelectedSlotIndex];
    // 조회 완료
    return true;
}

bool UMD_LoadGameTabWidget::GetSelectedGameStateData(FMD_GameStateData& GameStateData)
{
    // 누락 필드의 이전 값 잔류 방지
    GameStateData = FMD_GameStateData();
    if (SaveSlots.IsValidIndex(SelectedSlotIndex) == false)
    {
        // 선택한 저장 부재
        return false;
    }
    // 저장 구조체의 태그 기반 필드 조회 재사용
    return SaveSlots[SelectedSlotIndex].GetDataField(FMD_GameStateData::DataFieldKey, GameStateData);
}

bool UMD_LoadGameTabWidget::GetSelectedMerchantData(FMD_MerchantData& MerchantData)
{
    // 누락 필드의 이전 값 잔류 방지
    MerchantData = FMD_MerchantData();
    if (SaveSlots.IsValidIndex(SelectedSlotIndex) == false)
    {
        // 선택한 저장 부재
        return false;
    }
    // 경제 필드만 조회하여 월드 복원 데이터 복사 방지
    return SaveSlots[SelectedSlotIndex].GetDataField(FMD_MerchantData::DataFieldKey, MerchantData);
}

bool UMD_LoadGameTabWidget::CanRequestLoad()
{
    if (bIsRefreshing == true)
    {
        // 목록 조회 중 복원 요청 차단
        return false;
    }
    if (IsLocalHost() == false)
    {
        // 복원 요청 권한 부재
        return false;
    }
    if (SaveSlots.IsValidIndex(SelectedSlotIndex) == false)
    {
        // 빈 목록 또는 잘못된 선택
        return false;
    }
    // 목록 요약의 최소 식별 정보 검증, 전체 파일 검증은 저장 관리자에서 처리
    FMD_GameData& Data = SaveSlots[SelectedSlotIndex];
    return Data.UniqueName.IsEmpty() == false && Data.MapPackagePath.IsEmpty() == false && Data.SchemaVersion == FMD_GameData::CurrentSchemaVersion;
}

bool UMD_LoadGameTabWidget::RequestLoadGame()
{
    if (CanRequestLoad() == false)
    {
        // 잘못된 선택과 비호스트 요청 차단
        return false;
    }
    // 기존 세션 관리자의 소유 인스턴스 조회
    UGameInstance* GameInstance = GetGameInstance();
    if (IsValid(GameInstance) == false)
    {
        // 게임 인스턴스 부재
        return false;
    }
    // 온라인 이동 경로 재사용
    UMD_MultiplayManager* SessionManager = GameInstance->GetSubsystem<UMD_MultiplayManager>();
    if (IsValid(SessionManager) == false)
    {
        // 세션 관리자 부재
        return false;
    }
    // 선택 파일의 전체 검증과 맵 이동 후 복원 요청
    SessionManager->StartSession(SaveSlots[SelectedSlotIndex], true);
    // 요청 전달 완료이며 실제 결과는 OnStartGameComplete에서 통지
    return true;
}

void UMD_LoadGameTabWidget::HandleSaveCardClicked(int32 SlotIndex)
{
    // 동적 카드의 클릭을 공통 선택 함수로 전달
    SelectSaveSlot(SlotIndex);
}

void UMD_LoadGameTabWidget::RefreshSelection()
{
    // 카드마다 반복하던 동일한 입력 권한 검사를 목록 단위로 수행
    bool bCanSelect = bIsRefreshing == false && IsLocalHost();
    // 선택한 저장 하나만 카드에 표시
    for (int32 Index = 0; Index < SaveCards.Num(); ++Index)
    {
        if (IsValid(SaveCards[Index]) == true)
        {
            // 비토글 카드의 이전 선택 표시를 명시적으로 해제
            if (Index != SelectedSlotIndex && SaveCards[Index]->GetSelected() == true)
            {
                // 새 저장의 선택 표시보다 이전 저장의 해제를 우선 처리
                SaveCards[Index]->ClearSelection();
            }
            // 조회 중 이전 목록 입력 차단
            SaveCards[Index]->SetIsEnabled(bCanSelect);
        }
    }
    // 이전 선택 해제 후 현재 저장 하나만 선택 표시
    if (SaveCards.IsValidIndex(SelectedSlotIndex) == true)
    {
        // 생성에 성공한 선택 카드에만 표시 적용
        if (IsValid(SaveCards[SelectedSlotIndex]) == true)
        {
            // 추가 클릭 이벤트 없이 현재 저장 선택 표시
            SaveCards[SelectedSlotIndex]->SetIsSelected(true, false);
        }
    }
    // 상세 화면과 불러오기 버튼 갱신 이벤트
    OnSelectionChanged();
}

void UMD_LoadGameTabWidget::RefreshSelectionDisplay()
{
    // 빈 목록과 선택 항목 구분
    bool bHasSelection = SaveSlots.IsValidIndex(SelectedSlotIndex);
    // 선택 변경과 빈 목록에서 이전 슬롯 장면 잔류 방지
    if (IsValid(Img_DetailPreview) == true)
    {
        // 현재 선택에 대응하는 캐시 텍스처 조회
        UTexture2D* Thumbnail = SaveThumbnails.IsValidIndex(SelectedSlotIndex) == true ? SaveThumbnails[SelectedSlotIndex].Get() : nullptr;
        // 현재 미리보기의 크기와 색상을 유지할 브러시 복사
        FSlateBrush ThumbnailBrush = Img_DetailPreview->GetBrush();
        // 읽기에 성공한 캡처만 상세 미리보기에 표시
        if (IsValid(Thumbnail) == true)
        {
            // 선택한 슬롯의 저장 장면 적용
            ThumbnailBrush.SetResourceObject(Thumbnail);
            // 별도 잘라내기 없이 캡처 텍스처 전체 사용
            ThumbnailBrush.SetUVRegion(FBox2f());
            // 빈 미리보기에서 유효한 슬롯 선택 시 이미지 그리기 복원
            ThumbnailBrush.DrawAs = ESlateBrushDrawType::Image;
        }
        else
        {
            // 이전 캡처와 디자이너 기본 이미지의 참조 제거
            ThumbnailBrush.SetResourceObject(nullptr);
            // 텍스처 없는 흰 사각형도 그리지 않고 미리보기 비우기
            ThumbnailBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
        }
        // 캡처 여부에 따라 선택한 브러시 표시
        Img_DetailPreview->SetBrush(ThumbnailBrush);
    }
    // 저장 필드 전체를 복사하지 않고 표시할 이름만 보관
    FText SaveName;
    // 경제 필드가 없을 때 표시할 문구로 초기화
    FText CreditText = MissingFieldText;
    if (bHasSelection == true)
    {
        // 저장 목록의 원본 요약을 직접 참조
        FMD_GameData& GameData = SaveSlots[SelectedSlotIndex];
        // 표시 갱신 전 저장 이름만 텍스트로 변환
        SaveName = FText::FromString(GameData.UniqueName);
        // 퀘스트와 구매 원장 복사 없이 크레딧 필드 조회
        FInstancedStruct* MerchantField = GameData.DataFields.Find(FMD_MerchantData::DataFieldKey);
        if (MerchantField != nullptr && MerchantField->IsValid() == true && MerchantField->GetScriptStruct() == FMD_MerchantData::StaticStruct())
        {
            // 구조체 조회 API의 읽기 전용 반환형으로 원본 경제 데이터 참조
            const FMD_MerchantData* MerchantData = MerchantField->GetPtr<FMD_MerchantData>();
            if (MerchantData != nullptr)
            {
                // 원본 데이터 참조를 화면 이벤트에 유지하지 않도록 표시 값만 보관
                CreditText = FText::AsNumber(MerchantData->Credits);
            }
        }
    }
    // 저장된 계약 진행 정보
    FMD_GameStateData GameStateData;
    // 누락된 진행 정보 구분
    bool bHasGameState = GetSelectedGameStateData(GameStateData);
    // 존재하지 않는 필드의 표시
    FText MissingText = MissingFieldText;
    // 빈 목록 안내
    FText Description = EmptySaveListText;
    if (bHasSelection == true)
    {
        // 선택 저장의 복원 안내
        Description = ContinueSaveText;
    }
    if (bIsRefreshing == true)
    {
        // 비동기 조회 진행 안내
        Description = LoadingSaveListText;
    }
    if (IsValid(Txt_DetailName) == true)
    {
        // 선택한 저장의 고유 이름 표시
        Txt_DetailName->SetText(SaveName);
    }
    if (IsValid(Txt_DetailDescription) == true)
    {
        // 빈 목록과 조회 상태 및 목적지 표시
        Txt_DetailDescription->SetText(Description);
    }
    if (IsValid(Txt_Value_Credit) == true)
    {
        // 저장된 보유 크레딧 표시
        Txt_Value_Credit->SetText(CreditText);
    }
    if (IsValid(Txt_Value_Score) == true)
    {
        // 근거 없는 백분율 대신 저장 점수 표시
        Txt_Value_Score->SetText(bHasGameState ? FText::AsNumber(GameStateData.Score) : MissingText);
    }
    if (IsValid(Txt_Value_PlayTime) == true)
    {
        // 저장 데이터에 존재하는 일차 표시
        Txt_Value_PlayTime->SetText(bHasGameState ? FText::Format(DayFormat, FText::AsNumber(GameStateData.ElapsedDay)) : MissingText);
    }
    if (IsValid(Txt_Value_Difficulty) == true)
    {
        // 선택 저장의 계약 난이도 표시
        Txt_Value_Difficulty->SetText(bHasGameState ? StaticEnum<EMD_Difficulty>()->GetDisplayNameTextByValue(static_cast<int64>(GameStateData.ContractDifficulty)) : MissingText);
    }
    if (IsValid(Btn_Travel) == true)
    {
        // 빈 목록과 조회 중 복원 입력 차단
        Btn_Travel->SetIsEnabled(CanRequestLoad());
    }
}
