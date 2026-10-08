// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/Travel/MD_TravelTypes.h"
#include "MD_TravelDifficultyWidget.generated.h"

class UButton;
class UTextBlock;

/** 뒤로 버튼을 눌러 현장 목록으로 돌아가고 싶다 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMD_OnTravelBackRequested);

/** 출발 버튼을 눌렀다. 고른 난이도를 함께 넘긴다 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMD_OnTravelDepartRequested, EMD_Difficulty, Difficulty);

/**
 * 출발 콘솔의 난이도 선택 페이지. WBP_TravelDifficulty 전용 클래스이며
 * WBP_TravelConsole의 WidgetSwitcher 1번 슬롯에 자식 위젯으로 들어간다.
 *
 * 이 위젯은 트래블을 직접 실행하지 않는다. 부모(UMD_TravelConsoleWidget)가
 * 현장 인덱스를 쥐고 있으므로 델리게이트로 의사만 알린다.
 */
UCLASS()
class MARINEDEMOLITION_API UMD_TravelDifficultyWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "MD|Travel")
	FMD_OnTravelBackRequested OnBackRequested;

	UPROPERTY(BlueprintAssignable, Category = "MD|Travel")
	FMD_OnTravelDepartRequested OnDepartRequested;

	/** 부모가 현장을 확정할 때 호출. 선택 현장명을 띄우고 버튼 상태를 다시 잡는다 */
	UFUNCTION(BlueprintCallable, Category = "MD|Travel")
	void SetSelectedSite(const FText& InSiteDisplayName);

	UFUNCTION(BlueprintPure, Category = "MD|Travel")
	EMD_Difficulty GetSelectedDifficulty() const { return SelectedDifficulty; }

	/** 난이도 기본값(표준)으로 되돌린다. 콘솔을 다시 열 때 쓴다 */
	UFUNCTION(BlueprintCallable, Category = "MD|Travel")
	void ResetSelection();

protected:
	virtual void NativeConstruct() override;

	UFUNCTION()
	void HandleDiffSafeClicked();

	UFUNCTION()
	void HandleDiffStandardClicked();

	UFUNCTION()
	void HandleDiffDangerousClicked();

	UFUNCTION()
	void HandleBackClicked();

	UFUNCTION()
	void HandleDepartClicked();

	void SelectDifficulty(EMD_Difficulty NewDifficulty);

	/** 난이도 3버튼 갱신. 선택된 하나만 비활성으로 눌린 상태를 표시한다 */
	void RefreshDifficultyButtons();

	bool IsHost() const;

	// ───────────────────────── 난이도 ─────────────────────────

	UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Diff_Safe;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Diff_Standard;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Diff_Dangerous;

	// ───────────────────────── 안내 ─────────────────────────

	UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_SelectedSite;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_InspectionPreview;

	// ───────────────────────── 푸터 ─────────────────────────

	UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Depart;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Back;

	EMD_Difficulty SelectedDifficulty = EMD_Difficulty::Standard;
};
