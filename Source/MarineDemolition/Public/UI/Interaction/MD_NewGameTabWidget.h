// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "Types/Travel/MD_TravelTypes.h"
#include "MD_NewGameTabWidget.generated.h"

class AMD_TravelConsole;
class UMD_CommonButtonBase;
class UScrollBox;
class UTextBlock;

// 현장과 난이도 선택 상태를 관리하는 새 항해 탭
UCLASS()
class MARINEDEMOLITION_API UMD_NewGameTabWidget : public UCommonActivatableWidget
{
    GENERATED_BODY()

protected:
    // 새 항해를 열 때 적용할 기본 난이도
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Travel|Difficulty")
    EMD_Difficulty DefaultDifficulty{};

    // 안전 난이도의 선택 표시와 입력 상태를 갱신하는 버튼
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel|Difficulty", meta = (BindWidgetOptional))
    TObjectPtr<UMD_CommonButtonBase> Btn_Easy;

    // 표준 난이도의 선택 표시와 입력 상태를 갱신하는 버튼
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel|Difficulty", meta = (BindWidgetOptional))
    TObjectPtr<UMD_CommonButtonBase> Btn_Normal;

    // 위험 난이도의 선택 표시와 입력 상태를 갱신하는 버튼
    UPROPERTY(BlueprintReadOnly, Category = "MD|Travel|Difficulty", meta = (BindWidgetOptional))
    TObjectPtr<UMD_CommonButtonBase> Btn_Hard;

    // 현재 탭의 선택 정보를 표시하는 버튼
    UPROPERTY(BlueprintReadOnly, Category = "MD|TravelTab", meta = (BindWidgetOptional))
    TObjectPtr<UMD_CommonButtonBase> Btn_Travel;

    // 현재 탭의 선택 정보를 표시하는 텍스트
    UPROPERTY(BlueprintReadOnly, Category = "MD|TravelTab", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_DetailName;

    // 현장 목록을 표시할 스크롤 영역
    UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MD|NewGame")
    TObjectPtr<UScrollBox> SB_MapList;

    // 디자이너에서 지정하는 현장 카드 클래스
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|NewGame")
    TSubclassOf<UMD_CommonButtonBase> MapCardWidgetClass;

    // 현장 정보와 서버 출항 검증에 사용하는 콘솔
    UPROPERTY(Transient, BlueprintReadOnly, Category = "MD|NewGame")
    TObjectPtr<AMD_TravelConsole> OwningConsole;

    // 선택된 현장의 원본 목록 인덱스
    UPROPERTY(Transient, BlueprintReadOnly, Category = "MD|NewGame")
    int32 SelectedSiteIndex = INDEX_NONE;

    // 이번 항해에 적용할 난이도
    UPROPERTY(Transient, BlueprintReadOnly, Category = "MD|NewGame")
    EMD_Difficulty SelectedDifficulty{};

    // 원본 현장 인덱스 순서로 유지하는 런타임 카드
    UPROPERTY(Transient)
    TArray<TObjectPtr<UMD_CommonButtonBase>> MapCards;

public:
    // 선택 변경 이벤트에서 호출하는 기본 레이아웃 표시 갱신
    UFUNCTION(BlueprintCallable, Category = "MD|TravelTab")
    void RefreshSelectionDisplay();

    // 새 콘솔 연결과 현장 목록 초기화
    UFUNCTION(BlueprintCallable, Category = "MD|NewGame")
    void SetOwningConsole(AMD_TravelConsole* InConsole);

    // 탭 활성화 이벤트에서 호출하는 현장 목록 갱신
    UFUNCTION(BlueprintCallable, Category = "MD|NewGame")
    void RefreshSites();

    // 준비된 현장만 선택값으로 적용
    UFUNCTION(BlueprintCallable, Category = "MD|NewGame")
    bool SelectSite(int32 SiteIndex);

    // 설정에 등록된 난이도 선택
    UFUNCTION(BlueprintCallable, Category = "MD|NewGame")
    bool SelectDifficulty(EMD_Difficulty Difficulty);

    // 출항 버튼의 활성 여부를 판단하는 로컬 사전 검증
    UFUNCTION(BlueprintPure, Category = "MD|NewGame")
    bool CanRequestTravel();

    // 서버에 출항 요청 전달 여부 반환, 실제 이동 성공 여부와 별개
    UFUNCTION(BlueprintCallable, Category = "MD|NewGame")
    bool RequestTravel();

    // 상세 화면에서 사용하는 선택 현장 조회
    UFUNCTION(BlueprintPure, Category = "MD|NewGame")
    bool GetSelectedSite(FMD_TravelSiteEntry& Site);

    // 난이도 버튼의 선택 표시용 값 조회
    UFUNCTION(BlueprintPure, Category = "MD|NewGame")
    FORCEINLINE EMD_Difficulty GetSelectedDifficulty() { return SelectedDifficulty; }

    // 콘솔 현장 인덱스 조회
    UFUNCTION(BlueprintPure, Category = "MD|NewGame")
    FORCEINLINE int32 GetSelectedSiteIndex() { return SelectedSiteIndex; }

    // 이름 외 카드 이미지와 설명을 추가하는 확장 이벤트
    UFUNCTION(BlueprintImplementableEvent, Category = "MD|NewGame")
    void OnSiteCardCreated(UMD_CommonButtonBase* Card, int32 SiteIndex);

    // 상세 정보와 난이도 및 출항 버튼을 갱신하는 이벤트
    UFUNCTION(BlueprintImplementableEvent, Category = "MD|NewGame")
    void OnSelectionChanged();

protected:
    // 생성된 현장 카드의 클릭 전달
    void HandleSiteClicked(int32 SiteIndex);

    // 카드 선택 표시와 상세 정보 이벤트의 공통 갱신
    void RefreshSelection();

    // 새 항해에서 지원하는 난이도 값의 유효성 확인
    bool IsSupportedDifficulty(EMD_Difficulty Difficulty);
};
