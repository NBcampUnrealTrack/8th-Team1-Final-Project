// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/Foundation/MD_CommonGameMenuWidget.h"
#include "Types/Travel/MD_TravelTypes.h"
#include "MD_TravelConsoleWidget.generated.h"

class AMD_TravelConsole;
class UMD_CommonButtonBase;
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

// 지도에서 목적지와 난이도를 고르는 항해 메뉴
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

    // 화면을 다시 열어도 보존하는 새 항해 선택
    UPROPERTY(Transient, BlueprintReadOnly, Category = "MD|Travel")
    int32 SelectedSiteIndex = INDEX_NONE;

    // 새 항해에만 적용하는 선택 난이도
    UPROPERTY(Transient, BlueprintReadOnly, Category = "MD|Travel")
    EMD_Difficulty SelectedDifficulty = EMD_Difficulty::Safe;

    // 현재 모드의 출항 가능 상태
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UButton> Btn_Travel;

    // 첫 번째 새 현장의 선택 표시
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UMD_CommonButtonBase> Btn_X7;

    // 두 번째 새 현장의 선택 표시
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UMD_CommonButtonBase> Btn_TR9;

    // 쉬움 난이도의 선택 표시
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UMD_CommonButtonBase> Btn_Easy;

    // 보통 난이도의 선택 표시
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UMD_CommonButtonBase> Btn_Normal;

    // 어려움 난이도의 선택 표시
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UMD_CommonButtonBase> Btn_Hard;

    // 새 지도 영역 (항상 0번 페이지)
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UWidgetSwitcher> WS_Map;

    // 현장 설명 영역 (항상 0번 페이지)
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UWidgetSwitcher> WS_DetailInfo;

    // 난이도 입력 영역 (항상 0번 페이지)
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UWidgetSwitcher> WS_Difficulty;

    // 현장 이미지 표시
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

    // 하단의 선택 난이도 표시
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Difficulty;

    // 선택 상태 안내
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Badge;

    // 화면 제목
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Map_Header;

    // 선택 안내
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Map_SubHeader;

    // 상세 제목
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Detail_Header;

    // 하단 제목
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Difficulty_Header;

    // 출항 동작 문구
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Button_Travel;

    // 출항 설정 안내
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Difficulty_Header_2;

    // 선택 부재 시 체크 배지를 함께 숨기는 외곽 영역
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
    TObjectPtr<UWidget> BDR_Badge_Outline;

public:
    // 화면 초기화 인터페이스에서 호출하는 콘솔 연결
    UFUNCTION(BlueprintCallable, Category = "MD|Travel")
    void SetOwningConsole(AMD_TravelConsole* InConsole);

    // 새 항해의 목적지 선택
    UFUNCTION(BlueprintCallable, Category = "MD|Travel")
    bool SelectSite(int32 SiteIndex);

    // 이벤트 그래프에서 Enum을 지정하는 새 항해 난이도 선택
    UFUNCTION(BlueprintCallable, Category = "MD|Travel")
    bool SelectDifficulty(EMD_Difficulty Difficulty);

    // 현재 선택과 권한을 화면의 동적 요소에 반영
    UFUNCTION(BlueprintCallable, Category = "MD|Travel")
    void RefreshSelectionDisplay();

    // 현재 화면에서 유효한 선택의 현장 인덱스이며 선택 부재 시 INDEX_NONE
    UFUNCTION(BlueprintPure, Category = "MD|Travel")
    int32 GetDisplayedSiteIndex();

    // 현재 모드의 출항 사전 조건 검증
    UFUNCTION(BlueprintPure, Category = "MD|Travel")
    bool CanRequestTravel();

    // 새 항해 RPC 요청 전달
    UFUNCTION(BlueprintCallable, Category = "MD|Travel")
    bool RequestTravel();

    // 표시 갱신 이후 추가 연출을 실행하는 이벤트
    UFUNCTION(BlueprintImplementableEvent, Category = "MD|Travel")
    void OnSelectionChanged();

protected:
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

    // 난이도 버튼의 표시 용어 통일
    FText GetDifficultyText(EMD_Difficulty Difficulty);
};
