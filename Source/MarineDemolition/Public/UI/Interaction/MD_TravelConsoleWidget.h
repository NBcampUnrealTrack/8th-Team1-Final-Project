// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/Travel/MD_TravelTypes.h"
#include "MD_TravelConsoleWidget.generated.h"

class UButton;
class UPanelWidget;
class UTextBlock;
class UWidget;
class UWidgetSwitcher;
class UMD_TravelDifficultyWidget;
class AMD_TravelConsole;

/**
 * 출발 콘솔 UI의 껍데기. 현장 선택 화면을 직접 갖고, 난이도 화면은 별도 위젯
 * (WBP_TravelDifficulty)을 자식으로 물린다. 두 화면은 서로 배타적으로 보인다 —
 * 현장을 고르면 콘솔 본체(BDR_TravelPanel)가 접히고 난이도 화면만 남으며,
 * 뒤로를 누르면 되돌아온다.
 *
 * 그래서 W_DifficultyPage는 BDR_TravelPanel 바깥(CVS_Root 직속)에 있어야 한다.
 * 패널 안에 들어 있으면 패널을 접는 순간 난이도 화면까지 같이 사라진다.
 *
 * 출발/뒤로 버튼은 난이도 위젯에만 존재한다. 이 위젯에는 닫기 버튼 하나뿐이다.
 */
UCLASS()
class MARINEDEMOLITION_API UMD_TravelConsoleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "MD|Travel")
	void SetOwningConsole(AMD_TravelConsole* InConsole) { OwningConsole = InConsole; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UFUNCTION()
	void HandleSiteX7Clicked();

	UFUNCTION()
	void HandleSiteTR9Clicked();

	UFUNCTION()
	void HandleSiteBelmontClicked();

	UFUNCTION()
	void HandleCloseClicked();

	UFUNCTION()
	void HandleBackRequested();

	UFUNCTION()
	void HandleDepartRequested(EMD_Difficulty Difficulty);

	void CloseWidget();

	void SelectSite(int32 SiteIndex);

	void ShowPage(int32 PageIndex);

	void InitSiteButtons();

	void RefreshHostGate();

	bool IsHost() const;

	static void SetWidgetShown(UWidget* Widget, bool bShown);

	// ───────────────────────── 페이지 ─────────────────────────

	/** 현장 선택 화면을 담은 콘솔 본체. 난이도 화면으로 넘어가면 통째로 접힌다. */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> BDR_TravelPanel;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
	TObjectPtr<UWidgetSwitcher> WS_TravelPages;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
	TObjectPtr<UMD_TravelDifficultyWidget> W_DifficultyPage;

	// ───────────────────────── 현장 ─────────────────────────

	UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Site_X7;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Site_TR9;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Site_Belmont;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_Site_X7;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_Site_TR9;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_Site_Belmont;

	// ───────────────────────── 안내 ─────────────────────────

	UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_HostNotice;

	// ───────────────────────── 푸터 ─────────────────────────

	UPROPERTY(BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
	TObjectPtr<UButton> CloseButton;

	UPROPERTY()
	TObjectPtr<AMD_TravelConsole> OwningConsole;

	int32 SelectedSiteIndex = INDEX_NONE;

	/** 난이도 화면이 아직 콘솔 본체 안에 들어 있으면 본체를 접지 않는다(빈 화면 방지). */
	bool bDifficultyNestedInPanel = false;
};
