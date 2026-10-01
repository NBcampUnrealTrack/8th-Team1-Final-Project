// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "Types/SaveGame/MD_SaveDataTypes.h"
#include "MD_LoadGameTabWidget.generated.h"

class UMD_CommonButtonBase;
class UImage;
class UScrollBox;
class UTextBlock;

// 저장 목록 조회와 선택을 관리하는 이어하기 탭
UCLASS()
class MARINEDEMOLITION_API UMD_LoadGameTabWidget : public UCommonActivatableWidget
{
    GENERATED_BODY()

protected:
    // 저장 필드가 없을 때 표시할 문구
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Travel|Presentation")
    FText MissingFieldText;

    // 저장 목록이 비었을 때 표시할 안내
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Travel|Presentation")
    FText EmptySaveListText;

    // 선택 저장을 이어할 때 표시할 안내
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Travel|Presentation")
    FText ContinueSaveText;

    // 저장 목록 조회 중 표시할 안내
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Travel|Presentation")
    FText LoadingSaveListText;

    // 저장 목록 조회 실패 시 표시할 안내
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Travel|Presentation")
    FText FailedSaveListText;

    // 저장된 진행 일차의 표시 형식
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Travel|Presentation")
    FText DayFormat;

    // 현재 탭의 선택 정보를 표시하는 버튼
    UPROPERTY(BlueprintReadOnly, Category = "MD|TravelTab", meta = (BindWidgetOptional))
    TObjectPtr<UMD_CommonButtonBase> Btn_Travel;

    // 현재 탭의 선택 정보를 표시하는 텍스트
    UPROPERTY(BlueprintReadOnly, Category = "MD|TravelTab", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_DetailName;

    // 현재 탭의 선택 정보를 표시하는 텍스트
    UPROPERTY(BlueprintReadOnly, Category = "MD|TravelTab", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_DetailDescription;

    // 현재 탭의 선택 정보를 표시하는 텍스트
    UPROPERTY(BlueprintReadOnly, Category = "MD|TravelTab", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Value_Credit;

    // 현재 탭의 선택 정보를 표시하는 텍스트
    UPROPERTY(BlueprintReadOnly, Category = "MD|TravelTab", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Value_Difficulty;

    // 현재 탭의 선택 정보를 표시하는 텍스트
    UPROPERTY(BlueprintReadOnly, Category = "MD|TravelTab", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Value_PlayTime;

    // 현재 탭의 선택 정보를 표시하는 텍스트
    UPROPERTY(BlueprintReadOnly, Category = "MD|TravelTab", meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Value_Score;

    // 저장 카드를 배치할 스크롤 영역
    UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MD|LoadGame")
    TObjectPtr<UScrollBox> SB_DataList;

    // 선택 슬롯의 저장 시점 장면을 표시하는 미리보기
    UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MD|LoadGame")
    TObjectPtr<UImage> Img_DetailPreview;

    // 디자이너에서 지정하는 저장 카드 클래스
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|LoadGame")
    TSubclassOf<UMD_CommonButtonBase> SaveCardWidgetClass;

    // 월드 전체 데이터 대신 목록 조회로 받은 저장 요약 보관
    UPROPERTY(Transient, BlueprintReadOnly, Category = "MD|LoadGame")
    TArray<FMD_GameData> SaveSlots;

    // 현재 선택한 저장 목록 인덱스
    UPROPERTY(Transient, BlueprintReadOnly, Category = "MD|LoadGame")
    int32 SelectedSlotIndex = INDEX_NONE;

    // 비동기 조회 중 중복 요청과 이전 목록 사용 방지
    UPROPERTY(Transient, BlueprintReadOnly, Category = "MD|LoadGame")
    bool bIsRefreshing = false;

    // 저장 요약과 같은 순서의 런타임 카드
    UPROPERTY(Transient)
    TArray<TObjectPtr<UMD_CommonButtonBase>> SaveCards;

    // 정렬된 저장 목록과 같은 순서로 재사용하는 장면 텍스처
    UPROPERTY(Transient)
    TArray<TObjectPtr<UTexture2D>> SaveThumbnails;

public:
    // 선택 변경 이벤트에서 호출하는 기본 레이아웃 표시 갱신
    UFUNCTION(BlueprintCallable, Category = "MD|TravelTab")
    void RefreshSelectionDisplay();

    // 탭 활성화 또는 새로고침 입력에서 호출하는 비동기 목록 조회
    UFUNCTION(BlueprintCallable, Category = "MD|LoadGame")
    bool RefreshSaveSlots();

    // 실제 목록에 존재하는 저장 항목 선택
    UFUNCTION(BlueprintCallable, Category = "MD|LoadGame")
    bool SelectSaveSlot(int32 SlotIndex);

    // 상세 패널에 선택한 저장 요약 전달
    UFUNCTION(BlueprintPure, Category = "MD|LoadGame")
    bool GetSelectedGameData(FMD_GameData& GameData);

    // 저장된 난이도와 점수를 상세 패널에 전달
    UFUNCTION(BlueprintPure, Category = "MD|LoadGame")
    bool GetSelectedGameStateData(FMD_GameStateData& GameStateData);

    // 저장된 경제 정보를 상세 패널에 전달
    UFUNCTION(BlueprintPure, Category = "MD|LoadGame")
    bool GetSelectedMerchantData(FMD_MerchantData& MerchantData);

    // 불러오기 버튼의 활성 여부를 판단하는 로컬 사전 검증
    UFUNCTION(BlueprintPure, Category = "MD|LoadGame")
    bool CanRequestLoad();

    // 기존 세션 관리자에 복원 요청 전달 여부 반환
    UFUNCTION(BlueprintCallable, Category = "MD|LoadGame")
    bool RequestLoadGame();

    // 선택된 저장 항목 인덱스 조회
    UFUNCTION(BlueprintPure, Category = "MD|LoadGame")
    FORCEINLINE int32 GetSelectedSlotIndex() { return SelectedSlotIndex; }

    // 로딩 표시와 버튼 상태의 전환 이벤트
    UFUNCTION(BlueprintImplementableEvent, Category = "MD|LoadGame")
    void OnSaveListLoading();

    // 빈 목록과 조회 실패를 구분하는 완료 이벤트
    UFUNCTION(BlueprintImplementableEvent, Category = "MD|LoadGame")
    void OnSaveListUpdated(bool bWasSuccessful);

    // 날짜와 이미지를 저장 카드에 적용하는 확장 이벤트
    UFUNCTION(BlueprintImplementableEvent, Category = "MD|LoadGame")
    void OnSaveCardCreated(UMD_CommonButtonBase* Card, int32 SlotIndex, FDateTime SaveTime);

    // 선택 변경 또는 빈 선택을 상세 패널에 전달하는 이벤트
    UFUNCTION(BlueprintImplementableEvent, Category = "MD|LoadGame")
    void OnSelectionChanged();

protected:
    // 저장 관리자와 동일한 동적 델리게이트 시그니처로 조회 결과 수신
    UFUNCTION()
    void HandleSaveSlotsLoaded(const TArray<FMD_GameData>& GameDataList, bool bWasSuccessful);

    // 동적 저장 카드의 클릭 전달
    void HandleSaveCardClicked(int32 SlotIndex);

    // 저장 요약 목록에 맞춘 카드 재구성
    void RebuildSaveCards();

    // 저장 카드와 상세 패널의 선택 상태 동기화
    void RefreshSelection();

    // 로컬 호스트만 저장 목록과 복원 요청 사용 허용
    bool IsLocalHost();
};
