// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/Foundation/MD_CommonGameMenuWidget.h"
#include "Types/SaveGame/MD_SaveDataTypes.h"
#include "Types/Travel/MD_TravelTypes.h"
#include "MD_TravelConsoleWidget.generated.h"

class AMD_TravelConsole;
class UMD_CommonButtonBase;
class UCommonButtonBase;
class UCommonButtonGroupBase;
class UButton;
class UImage;
class UScrollBox;
class UTextBlock;
class UWidgetSwitcher;

// 콘솔 현장 인덱스에 대응하는 화면 전용 상세 정보
USTRUCT(BlueprintType)
struct FMD_TravelMapPresentation
{
    GENERATED_BODY()

    // 지도에 표시할 현장 식별 문구
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Travel")
    FText MapId;

    // 상세와 카드에 사용할 식별자 없는 현장 이름
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Travel")
    FText MapName;

    // 새 항해의 현장 안내 문구
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Travel")
    FText Description;

    // 디자이너에서 지정하는 현장 미리보기
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Travel")
    TObjectPtr<UTexture2D> Preview;
};

// 지도와 저장 목록을 하나의 화면에서 관리하는 항해 메뉴
UCLASS(Abstract, Blueprintable)
class MARINEDEMOLITION_API UMD_TravelConsoleWidget : public UMD_CommonGameMenuWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeOnActivated() override;

protected:
    // 콘솔의 현장 인덱스를 키로 사용하는 상세 표시 설정
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Travel|Presentation")
    TMap<int32, FMD_TravelMapPresentation> MapPresentation;

    // 저장 목록에서 생성할 카드 클래스
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Travel|Presentation")
    TSubclassOf<UMD_CommonButtonBase> SaveCardWidgetClass;

    // 새 항해를 처음 준비할 때 선택할 난이도
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Travel")
    EMD_Difficulty DefaultDifficulty = EMD_Difficulty::Safe;

    // 화면의 X7 지도 버튼에 대응하는 콘솔 인덱스
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Travel")
    int32 X7SiteIndex = 0;

    // 화면의 TR9 지도 버튼에 대응하는 콘솔 인덱스
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Travel")
    int32 TR9SiteIndex = 1;

    // 초기화 이벤트에서 전달받은 현장 제공자
    UPROPERTY(Transient, BlueprintReadOnly, Category = "MD|Travel")
    TObjectPtr<AMD_TravelConsole> OwningConsole;

    // 탭을 왕복해도 보존하는 새 항해 선택
    UPROPERTY(Transient, BlueprintReadOnly, Category = "MD|Travel")
    int32 SelectedSiteIndex = INDEX_NONE;

    // 새 항해에만 적용하는 선택 난이도
    UPROPERTY(Transient, BlueprintReadOnly, Category = "MD|Travel")
    EMD_Difficulty SelectedDifficulty = EMD_Difficulty::Safe;

    // 현재 화면의 저장 항해 모드 여부
    UPROPERTY(Transient, BlueprintReadOnly, Category = "MD|Travel")
    bool bSavedMode = false;

    // 전체 저장 배열에서 선택한 슬롯 인덱스
    UPROPERTY(Transient, BlueprintReadOnly, Category = "MD|Travel")
    int32 SelectedSlotIndex = INDEX_NONE;

    // 비동기 조회 중 이전 저장의 출항 차단
    UPROPERTY(Transient, BlueprintReadOnly, Category = "MD|Travel")
    bool bIsRefreshing = false;

    // 성공한 조회 결과의 재사용 여부
    UPROPERTY(Transient)
    bool bHasSaveList = false;

    // 전체 월드 대신 저장 목록 요약 보관
    UPROPERTY(Transient)
    TArray<FMD_GameData> SaveSlots;

    // 원본 슬롯 인덱스와 대응하는 카드 참조
    UPROPERTY(Transient)
    TArray<TObjectPtr<UMD_CommonButtonBase>> SaveCards;

    // 모든 저장 카드의 상호 배타적 선택을 관리하는 기존 CommonUI 그룹
    UPROPERTY(Transient)
    TObjectPtr<UCommonButtonGroupBase> SaveCardButtonGroup;

    // 선택 및 목록 갱신에서 변경되지 않은 캡처를 재사용하는 썸네일
    UPROPERTY(Transient)
    TArray<TObjectPtr<UTexture2D>> SaveThumbnails;

    // 현재 모드의 출항 가능 상태
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UButton> Btn_Travel;

    // 새 항해 탭의 선택 표시
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UMD_CommonButtonBase> Btn_NewGameTab;

    // 저장 항해 탭의 선택 표시
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UMD_CommonButtonBase> Btn_LoadGameTab;

    // 첫 번째 새 현장의 선택 표시
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UMD_CommonButtonBase> Btn_X7;

    // 두 번째 새 현장의 선택 표시
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UMD_CommonButtonBase> Btn_TR9;

    // 첫 번째 저장 현장의 선택 표시
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UMD_CommonButtonBase> Btn_X7_Load;

    // 두 번째 저장 현장의 선택 표시
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UMD_CommonButtonBase> Btn_TR9_Load;

    // 쉬움 난이도의 선택 표시
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UMD_CommonButtonBase> Btn_Easy;

    // 보통 난이도의 선택 표시
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UMD_CommonButtonBase> Btn_Normal;

    // 어려움 난이도의 선택 표시
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UMD_CommonButtonBase> Btn_Hard;

    // 새 지도와 저장 지도 전환
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UWidgetSwitcher> WS_Map;

    // 현장 설명과 저장 상세 전환
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UWidgetSwitcher> WS_DetailInfo;

    // 난이도 입력과 저장된 난이도 전환
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UWidgetSwitcher> WS_Difficulty;

    // 실제 저장 카드의 배치 영역
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UScrollBox> SB_SlotList;

    // 현장 이미지 또는 저장 썸네일 표시
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UImage> Img_Preview;

    // 상세 영역의 현장 식별 문구
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Detail_MapName;

    // 상세 영역의 현장 표시명
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Detail_MapName_1;

    // 새 항해 현장의 안내
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Detail_Description;

    // 출항 영역의 선택 현장명
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Difficulty_MapName;

    // 저장 상세의 진행 일차
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Detail_Day;

    // 저장 상세의 난이도
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Detail_Difficulty;

    // 저장 상세의 마지막 저장 시각
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Detail_SaveTime;

    // 하단 저장 정보의 진행 일차
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Day;

    // 하단 저장 정보의 난이도
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Difficulty;

    // 조회 및 선택 상태 안내
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Badge;

    // 새 항해와 저장 항해의 화면 제목
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Map_Header;

    // 현재 항해 모드의 선택 안내
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Map_SubHeader;

    // 목적지와 저장 항해를 구분하는 상세 제목
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Detail_Header;

    // 출항 준비와 항해 이어가기를 구분하는 하단 제목
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Difficulty_Header;

    // 현재 항해 모드의 출항 동작 문구
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Button_Travel;

    // 저장 유무에 따른 이어가기 안내
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Difficulty_Header_2;

    // 저장 조회 상태와 목록 정렬 안내
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_MapHint_LoadGame;

    // 저장 일차와 난이도 사이의 구분 문구
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Dash;

    // 선택 부재 시 체크 배지를 함께 숨기는 외곽 영역
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UWidget> BDR_Badge_Outline;

public:
    // 화면 초기화 인터페이스에서 호출하는 콘솔 연결
    UFUNCTION(BlueprintCallable, Category = "MD|Travel")
    void SetOwningConsole(AMD_TravelConsole* InConsole);

    // 이벤트 그래프의 탭 클릭에서 호출하는 모드 변경
    UFUNCTION(BlueprintCallable, Category = "MD|Travel")
    void SetSavedMode(bool bInSavedMode);

    // 새 항해의 목적지 선택이며 저장 모드에서는 변경 차단
    UFUNCTION(BlueprintCallable, Category = "MD|Travel")
    bool SelectSite(int32 SiteIndex);

    // 이벤트 그래프에서 Enum을 지정하는 새 항해 난이도 선택
    UFUNCTION(BlueprintCallable, Category = "MD|Travel")
    bool SelectDifficulty(EMD_Difficulty Difficulty);

    // 현재 표시 중인 저장 목록의 항목 선택
    UFUNCTION(BlueprintCallable, Category = "MD|Travel")
    bool SelectSaveSlot(int32 SlotIndex);

    // 저장 모드 첫 진입 및 명시적인 새로고침에 사용할 비동기 조회
    UFUNCTION(BlueprintCallable, Category = "MD|Travel")
    bool RefreshSaveSlots();

    // 현재 선택과 권한을 화면의 동적 요소에 반영
    UFUNCTION(BlueprintCallable, Category = "MD|Travel")
    void RefreshSelectionDisplay();

    // 저장된 로컬 시각을 월·일과 24시간 형식으로 표시
    UFUNCTION(BlueprintPure, Category = "MD|Travel")
    static FText FormatSaveTime(FDateTime SaveTime);

    // 현재 화면에서 유효한 선택의 현장 인덱스이며 선택 부재 시 INDEX_NONE
    UFUNCTION(BlueprintPure, Category = "MD|Travel")
    int32 GetDisplayedSiteIndex();

    // 현재 모드의 출항 사전 조건 검증
    UFUNCTION(BlueprintPure, Category = "MD|Travel")
    bool CanRequestTravel();

    // 새 항해 RPC 또는 저장 세션 복원 요청 전달
    UFUNCTION(BlueprintCallable, Category = "MD|Travel")
    bool RequestTravel();

    // 카드 생성 및 재사용 갱신에서 상세 문구를 기존 BP에 전달하는 이벤트
    UFUNCTION(BlueprintImplementableEvent, Category = "MD|Travel")
    void OnSaveCardCreated(UMD_CommonButtonBase* Card, int32 SlotIndex, FDateTime SaveTime, const FText& MapName, const FText& Day, const FText& Difficulty);

    // 조회 실패와 정상적인 빈 목록을 구분하는 이벤트
    UFUNCTION(BlueprintImplementableEvent, Category = "MD|Travel")
    void OnSaveListUpdated(bool bWasSuccessful);

    // 표시 갱신 이후 추가 연출을 실행하는 이벤트
    UFUNCTION(BlueprintImplementableEvent, Category = "MD|Travel")
    void OnSelectionChanged();

protected:
    // 저장 관리자 콜백과 동일한 읽기 전용 인자 서명
    UFUNCTION()
    void HandleSaveSlotsLoaded(const TArray<FMD_GameData>& GameDataList, bool bWasSuccessful);

    // 런타임 카드 클릭의 원본 슬롯 인덱스 전달
    void HandleSaveCardClicked(int32 SlotIndex);

    // 클릭 이외의 CommonUI 선택 변경도 실제 저장 슬롯과 동기화
    void HandleSaveCardSelectionChanged(UCommonButtonBase* Card, int32 ButtonIndex);

    // 기존 카드를 재사용하며 전체 저장 목록의 표시 갱신
    void RebuildSaveCards();

    // 저장된 레벨 경로를 현재 콘솔 현장과 대응
    int32 FindSavedSiteIndex(int32 SlotIndex);

    // 로컬 호스트의 선택과 출항 권한 검증
    bool IsLocalHost();

    // 콘솔 현장 범위와 새 항해 가능 여부 검증
    bool IsSiteAvailable(int32 SiteIndex);

    // 현재 화면에서 제공하는 난이도 검증
    bool IsSupportedDifficulty(EMD_Difficulty Difficulty);

    // 단일 선택 표시와 버튼 권한 갱신
    void UpdateButton(UMD_CommonButtonBase* Button, bool bSelected, bool bEnabled);

    // 변경된 문구만 선택적 텍스트에 적용
    void SetDisplayText(UTextBlock* TextBlock, FText Text);

    // 저장 난이도와 버튼의 표시 용어 통일
    FText GetDifficultyText(EMD_Difficulty Difficulty);
};
