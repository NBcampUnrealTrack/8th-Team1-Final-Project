// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/PrimaryAssetId.h"
#include "UI/Foundation/MD_CommonGameMenuWidget.h"
#include "MD_EncyclopediaWidget.generated.h"

class UCommonButtonGroupBase;
class UMD_CommonButtonBase;
class UMD_LoreCatalog;
class UMD_LoreComponent;
class UImage;
class UPanelWidget;
class UScrollBox;
class USlider;
class UTextBlock;
class UWidgetSwitcher;

// 도감 탭별 표시 이름과 기록물 목록 설정
USTRUCT(BlueprintType)
struct FMD_EncyclopediaCategory
{
	GENERATED_BODY()

public:
	// 카탈로그가 없는 탭에도 표시할 제목
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Encyclopedia")
	FText DisplayName;

	// 탭의 표시 순서와 해독 조건을 제공하는 카탈로그
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Encyclopedia")
	TObjectPtr<UMD_LoreCatalog> LoreCatalog;
};

// 복제된 로어 진행으로 목록과 선택 기록의 상세 내용을 표시하는 도감 화면
UCLASS(Abstract, Blueprintable)
class MARINEDEMOLITION_API UMD_EncyclopediaWidget : public UMD_CommonGameMenuWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	virtual void NativeDestruct() override;

protected:
	// 디자이너의 카테고리 순서에 대응하는 데이터 설정
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Encyclopedia")
	TArray<FMD_EncyclopediaCategory> Categories;

	// 로어 카탈로그 대신 분류 설명서를 표시할 카테고리 버튼 순서
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Encyclopedia|Categorizing")
	int32 CategorizingCategoryIndex = 3;

	// 분류 설명서에서 현재 표시하는 품목 분류 순서
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MD|Encyclopedia|Categorizing")
	int32 SelectedCategorizingIndex = 0;

	// 디자이너의 분류 페이지 순서에 대응하는 설명서 버튼
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMD_CommonButtonBase>> CategorizingButtons;

	// 설명서의 품목 분류 중 하나의 선택을 유지할 버튼 그룹
	UPROPERTY(Transient)
	TObjectPtr<UCommonButtonGroupBase> CategorizingButtonGroup;

	// 각 기록물의 번호와 발견 여부를 표시할 공통 버튼 클래스
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Encyclopedia")
	TSubclassOf<UMD_CommonButtonBase> EntryWidgetClass;

	// 현재 선택한 카테고리 인덱스
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MD|Encyclopedia")
	int32 SelectedCategoryIndex = 0;

	// 현재 선택한 기록물의 카탈로그 인덱스
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MD|Encyclopedia")
	int32 SelectedEntryIndex = INDEX_NONE;

	// 목록 재구성 이후에도 동일한 기록물 선택을 유지할 식별자
	UPROPERTY(Transient)
	FPrimaryAssetId SelectedLoreId;

	// 현재 탭의 상세 조회와 진행률 계산에 사용할 카탈로그
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MD|Encyclopedia")
	TObjectPtr<UMD_LoreCatalog> CurrentCatalog;

	// 서버 원장을 수정하지 않는 표시 전용 발견 목록
	UPROPERTY(Transient, BlueprintReadOnly, Category = "MD|Encyclopedia")
	TArray<FPrimaryAssetId> DiscoveredLoreIds;

	// 카탈로그 인덱스와 일대일로 대응하는 동적 버튼
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMD_CommonButtonBase>> EntryWidgets;

	// 패널 순서에 따라 입력과 선택 표시를 연결한 카테고리 버튼
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMD_CommonButtonBase>> CategoryButtons;

	// 비활성화 시 정확한 수집 알림 연결을 해제할 약한 참조
	UPROPERTY(Transient)
	TWeakObjectPtr<UMD_LoreComponent> BoundLoreSource;

	// 늦게 복제되거나 교체되는 GameState를 추적할 월드
	UPROPERTY(Transient)
	TWeakObjectPtr<UWorld> BoundWorld;

	// 월드 교체 알림의 중복 구독 방지 및 해제 식별자
	FDelegateHandle GameStateChangedHandle;

	// 카드 표시 이벤트에서 목록 재생성이 중첩되는 상황 방지
	bool bIsRefreshingEntries = false;

	// 미발견 기록물의 제목 대체 문구
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Encyclopedia|Text")
	FText UndiscoveredTitle;

	// 미발견 기록물의 본문 대체 문구
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Encyclopedia|Text")
	FText UndiscoveredBody;

	// 선행 기록 부족 시 실제 본문을 가리는 안내
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Encyclopedia|Text")
	FText EncryptedBody;

	// 빈 카테고리의 상세 제목
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Encyclopedia|Text")
	FText EmptyTitle;

	// 빈 카테고리의 본문 안내
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Encyclopedia|Text")
	FText EmptyBody;

	// 미발견 상태 표시 문구
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Encyclopedia|Text")
	FText UndiscoveredStateText;

	// 암호화 상태 표시 문구
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Encyclopedia|Text")
	FText EncryptedStateText;

	// 해독 완료 상태 표시 문구
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Encyclopedia|Text")
	FText DecryptedStateText;

	// 발견 수량과 전체 수량 표시 형식
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Encyclopedia|Text")
	FText ProgressTextFormat;

	// 선택한 기록 번호와 전체 수량 표시 형식
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Encyclopedia|Text")
	FText PageTextFormat;

	// 동적 기록 카드 목록
	UPROPERTY(BlueprintReadOnly, Category = "MD|Encyclopedia", meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> SB_Entries;

	// 기록 변경 시 본문을 처음부터 표시할 스크롤 영역
	UPROPERTY(BlueprintReadOnly, Category = "MD|Encyclopedia", meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> DetailScroll;

	// 발견 비율 표시용 슬라이더
	UPROPERTY(BlueprintReadOnly, Category = "MD|Encyclopedia", meta = (BindWidgetOptional))
	TObjectPtr<USlider> Slider_LoreProgress;

	// 선택 카테고리의 제목
	UPROPERTY(BlueprintReadOnly, Category = "MD|Encyclopedia", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_SiteTitle;

	// 선택 카테고리의 수집 수량
	UPROPERTY(BlueprintReadOnly, Category = "MD|Encyclopedia", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Progress;

	// 선택 기록의 공개 가능한 제목
	UPROPERTY(BlueprintReadOnly, Category = "MD|Encyclopedia", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_DetailTitle;

	// 선택 기록의 발견 및 해독 상태
	UPROPERTY(BlueprintReadOnly, Category = "MD|Encyclopedia", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_DetailMeta;

	// 선택 기록의 공개 가능한 본문
	UPROPERTY(BlueprintReadOnly, Category = "MD|Encyclopedia", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_DetailBody;

	// 선택 기록의 순번
	UPROPERTY(BlueprintReadOnly, Category = "MD|Encyclopedia", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PageNumber;

	// 이전 기록으로 이동하는 버튼
	UPROPERTY(BlueprintReadOnly, Category = "MD|Encyclopedia", meta = (BindWidgetOptional))
	TObjectPtr<UMD_CommonButtonBase> Btn_Previous;

	// 다음 기록으로 이동하는 버튼
	UPROPERTY(BlueprintReadOnly, Category = "MD|Encyclopedia", meta = (BindWidgetOptional))
	TObjectPtr<UMD_CommonButtonBase> Btn_Next;

	// 개수와 이름을 고정하지 않는 디자이너 카테고리 버튼 영역
	UPROPERTY(BlueprintReadOnly, Category = "MD|Encyclopedia", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> VB_Categories;

	// 로어 기록과 분류 설명서 사이의 페이지 전환 영역
	UPROPERTY(BlueprintReadOnly, Category = "MD|Encyclopedia", meta = (BindWidgetOptional))
	TObjectPtr<UWidgetSwitcher> WS_Page;

	// 기존 로어 기록의 목록과 상세 페이지
	UPROPERTY(BlueprintReadOnly, Category = "MD|Encyclopedia", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> HB_LoreCodex;

	// 분류 버튼과 품목 안내를 포함하는 설명서 페이지
	UPROPERTY(BlueprintReadOnly, Category = "MD|Encyclopedia", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> HB_Categorizing;

	// 설명서의 품목 분류 버튼 목록
	UPROPERTY(BlueprintReadOnly, Category = "MD|Encyclopedia", meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> SB_CategorizingList;

	// 금속 안내 페이지를 선택할 설명서 버튼
	UPROPERTY(BlueprintReadOnly, Category = "MD|Encyclopedia", meta = (BindWidgetOptional))
	TObjectPtr<UMD_CommonButtonBase> Btn_Categorizing_Metal;

	// 플라스틱 안내 페이지를 선택할 설명서 버튼
	UPROPERTY(BlueprintReadOnly, Category = "MD|Encyclopedia", meta = (BindWidgetOptional))
	TObjectPtr<UMD_CommonButtonBase> Btn_Categorizing_Plastic;

	// 일반 쓰레기 안내 페이지를 선택할 설명서 버튼
	UPROPERTY(BlueprintReadOnly, Category = "MD|Encyclopedia", meta = (BindWidgetOptional))
	TObjectPtr<UMD_CommonButtonBase> Btn_Categorizing_General;

	// 오염물 안내 페이지를 선택할 설명서 버튼
	UPROPERTY(BlueprintReadOnly, Category = "MD|Encyclopedia", meta = (BindWidgetOptional))
	TObjectPtr<UMD_CommonButtonBase> Btn_Categorizing_Pollution;

	// 비가연성 안내 페이지를 선택할 설명서 버튼
	UPROPERTY(BlueprintReadOnly, Category = "MD|Encyclopedia", meta = (BindWidgetOptional))
	TObjectPtr<UMD_CommonButtonBase> Btn_Categorizing_Scrap;

	// 분류 버튼 순서대로 배치된 품목 안내 페이지
	UPROPERTY(BlueprintReadOnly, Category = "MD|Encyclopedia", meta = (BindWidgetOptional))
	TObjectPtr<UWidgetSwitcher> WS_Categorizing;

	// 현재 품목 분류의 표시 이름
	UPROPERTY(BlueprintReadOnly, Category = "MD|Encyclopedia", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Categorizing_Name;

	// 현재 품목 분류의 식별 색상
	UPROPERTY(BlueprintReadOnly, Category = "MD|Encyclopedia", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_Categorizing_Color;

public:
	// 현재 카테고리 인덱스 반환
	UFUNCTION(BlueprintPure, Category = "MD|Encyclopedia")
	FORCEINLINE int32 GetSelectedCategoryIndex() { return SelectedCategoryIndex; }

	// 현재 설명서의 품목 분류 인덱스 반환
	UFUNCTION(BlueprintPure, Category = "MD|Encyclopedia|Categorizing")
	FORCEINLINE int32 GetSelectedCategorizingIndex() { return SelectedCategorizingIndex; }

	// 현재 기록물 인덱스 반환
	UFUNCTION(BlueprintPure, Category = "MD|Encyclopedia")
	FORCEINLINE int32 GetSelectedEntryIndex() { return SelectedEntryIndex; }

	// 현재 선택된 카탈로그 반환
	UFUNCTION(BlueprintPure, Category = "MD|Encyclopedia")
	FORCEINLINE UMD_LoreCatalog* GetCurrentCatalog() { return CurrentCatalog; }

	// 현재 월드의 로어 원장과 표시 목록 동기화
	UFUNCTION(BlueprintCallable, Category = "MD|Encyclopedia")
	void RefreshLore();

	// 표시 전용 발견 목록 적용 및 공개 상태 갱신
	UFUNCTION(BlueprintCallable, Category = "MD|Encyclopedia")
	void ApplyLoreProgress(TArray<FPrimaryAssetId> InDiscoveredLoreIds);

	// 유효한 카테고리 선택 및 기록 목록 전환
	UFUNCTION(BlueprintCallable, Category = "MD|Encyclopedia")
	void SelectCategory(int32 CategoryIndex);

	// 설명서의 품목 분류 선택과 제목 및 색상 동기화
	UFUNCTION(BlueprintCallable, Category = "MD|Encyclopedia|Categorizing")
	void SelectCategorizingEntry(int32 CategorizingIndex);

	// 유효한 기록 선택 및 상세 내용 전환
	UFUNCTION(BlueprintCallable, Category = "MD|Encyclopedia")
	void SelectEntry(int32 EntryIndex);

	// 첫 기록에서 멈추는 이전 기록 이동
	UFUNCTION(BlueprintCallable, Category = "MD|Encyclopedia")
	void SelectPreviousEntry();

	// 마지막 기록에서 멈추는 다음 기록 이동
	UFUNCTION(BlueprintCallable, Category = "MD|Encyclopedia")
	void SelectNextEntry();

protected:
	// 기존 카드 Blueprint의 번호와 발견 상태 표시 연결
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|Encyclopedia")
	void OnEntryUpdated(UMD_CommonButtonBase* EntryWidget, int32 DisplayNumber, const FText& EntryTitle, bool bDiscovered);

	// 카테고리 클릭 처리 이후 Blueprint의 페이지 전환 연출 연결
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|Encyclopedia")
	void OnCategoryClicked(int32 CategoryIndex);

	// 실제 사용자 선택 변경에 따른 Blueprint 연출 연결
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|Encyclopedia")
	void OnEntrySelectionChanged();

	// 현재 카탈로그의 동적 카드 구성
	void RebuildEntries();

	// 기존 카드 인스턴스에 변경된 발견 상태 반영
	void RefreshEntryDisplay();

	// 선택과 진행률 및 상세 본문 동기화
	void RefreshSelectionDisplay();

	// 분류 설명서에 연결된 유효한 카테고리 여부 확인
	bool IsCategorizingCategory(int32 CategoryIndex);

	// 현재 메뉴와 품목 분류의 페이지 및 선택 표시 동기화
	void RefreshCategorizingDisplay();

	// 유효한 카테고리 클릭의 데이터 갱신과 Blueprint 알림 전달
	UFUNCTION(BlueprintCallable, Category = "MD|Encyclopedia")
	void HandleCategoryClicked(int32 CategoryIndex);

	// 패널의 버튼 순서와 데이터 순서에 따른 입력 연결
	void RebuildCategoryBindings();

	// 화면에 연결한 카테고리 클릭 구독과 참조 정리
	void ReleaseCategoryBindings();

	// 바인딩된 개별 카테고리 버튼의 선택 상태 반영
	void RefreshCategoryButton(UMD_CommonButtonBase* Button, int32 CategoryIndex);

	// 카드 클릭 구독과 인스턴스 참조 정리
	void ReleaseEntryWidgets();

	// 월드와 복제 원장의 알림 연결 해제
	void UnbindLoreSource();

	// 늦게 생성되거나 교체된 GameState의 로어 원장 재연결
	void HandleGameStateSet(AGameStateBase* GameState);

	// 복제된 원장 변경을 표시 전용 데이터에 적용
	UFUNCTION()
	void HandleLoreProgressUpdated();
};
