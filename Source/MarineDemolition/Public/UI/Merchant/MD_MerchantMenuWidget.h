// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "UI/Merchant/MD_MerchantTypes.h"

#include "MD_MerchantMenuWidget.generated.h"

class UButton;
class UImage;
class UProgressBar;
class UTextBlock;
class AMD_Merchant;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMD_MerchantMenuRequest);

/**
 * 상인 인터랙트 시 열리는 루트 메뉴: 대화 / 상점 / 퀘스트 / 대출 / 나가기
 *
 * 모든 위젯 바인딩은 BindWidgetOptional이다. WBP 쪽에서 아직 배치하지 않은 요소가 있어도
 * 컴파일이 깨지지 않아야 UI를 단계적으로 붙여 나갈 수 있고, 여기 노출된 목록 자체가
 * "이 화면에 띄울 수 있는 정보 전량"의 명세 역할을 한다 (`07` §8 참고).
 * 대응하는 WBP 위젯 이름은 아래 프로퍼티 이름과 동일하게 두면 자동 바인딩된다.
 */
UCLASS()
class MARINEDEMOLITION_API UMD_MerchantMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetOwningMerchant(AMD_Merchant* InMerchant) { OwningMerchant = InMerchant; }

	/** 정보를 갈아끼우고 즉시 화면에 반영한다. */
	UFUNCTION(BlueprintCallable, Category = "MD|Merchant")
	void SetMenuInfo(const FMD_MerchantMenuInfo& InInfo);

	UFUNCTION(BlueprintPure, Category = "MD|Merchant")
	const FMD_MerchantMenuInfo& GetMenuInfo() const { return MenuInfo; }

	/** MenuInfo 값을 바인딩된 위젯들에 밀어 넣는다. 바인딩 안 된 요소는 조용히 건너뛴다. */
	UFUNCTION(BlueprintCallable, Category = "MD|Merchant")
	void RefreshDisplay();

	UFUNCTION(BlueprintCallable, Category = "MD|Merchant")
	void CloseMenu();

	/** 화면 하단 대사창에 한 줄을 띄운다. 대사창이 배치돼 있지 않으면 아무 일도 하지 않는다. */
	UFUNCTION(BlueprintCallable, Category = "MD|Merchant")
	void ShowDialogueLine(const FText& Line);

	/**
	 * 상점·퀘스트·대출 화면이 열릴 때 메뉴 패널을 숨긴다.
	 * 메뉴 패널 전체(카테고리 탭들)가 상점과 겹치므로 숨긴다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MD|Merchant")
	void SetMenuPanelVisible(bool bVisible);

	/** 각 메뉴 진입 요청. 실제 화면은 소유 액터나 BP가 붙인다. */
	UPROPERTY(BlueprintAssignable, Category = "MD|Merchant")
	FMD_MerchantMenuRequest OnTalkRequested;

	UPROPERTY(BlueprintAssignable, Category = "MD|Merchant")
	FMD_MerchantMenuRequest OnShopRequested;

	UPROPERTY(BlueprintAssignable, Category = "MD|Merchant")
	FMD_MerchantMenuRequest OnQuestRequested;

	UPROPERTY(BlueprintAssignable, Category = "MD|Merchant")
	FMD_MerchantMenuRequest OnLoanRequested;

	UPROPERTY(BlueprintAssignable, Category = "MD|Merchant")
	FMD_MerchantMenuRequest OnExitRequested;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UFUNCTION()
	void HandleTalkClicked();

	UFUNCTION()
	void HandleShopClicked();

	UFUNCTION()
	void HandleQuestClicked();

	UFUNCTION()
	void HandleLoanClicked();

	UFUNCTION()
	void HandleExitClicked();

	/** 디자이너가 에디터에서 미리보기할 수 있도록 기본값을 노출한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Merchant")
	FMD_MerchantMenuInfo MenuInfo;

	UPROPERTY()
	TObjectPtr<AMD_Merchant> OwningMerchant;

	// ───────────────────────── 메뉴 버튼 5종 ─────────────────────────

	/**
	 * 메뉴 패널 전체(카테고리 탭 + 버튼들). 상점 화면 열릴 때 숨긴다.
	 * 컨테이너 종류를 묶지 않아 디자이너가 바꿔도 바인딩이 유지된다.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Merchant", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> BDR_MenuPanel;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Merchant", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Talk;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Merchant", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Shop;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Merchant", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Quest;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Merchant", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Loan;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Merchant", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Exit;

	// ───────────────────────── 상인 신원 ─────────────────────────

	UPROPERTY(BlueprintReadOnly, Category = "MD|Merchant", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_MerchantName;

	/** 화면 중앙 하단 대사창의 본문 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Merchant", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_Greeting;

	/**
	 * 대사창 컨테이너. 보이기/숨기기 대상이라 구체 타입을 묶지 않는다 —
	 * 디자이너가 Border를 SizeBox나 Overlay로 바꿔도 그대로 바인딩된다.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Merchant", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> BDR_Dialogue;

	/** 캐릭터 에셋 확정 전까지는 비워둔다 (현재 월드 모델도 큐브 대체) */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Merchant", meta = (BindWidgetOptional))
	TObjectPtr<UImage> IMG_MerchantPortrait;

	// ───────────────────────── 호감도 ─────────────────────────

	/** `+2.0` / `-1.5` 처럼 부호 포함 소수점 1자리 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Merchant", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_Affinity;

	/** -4.0~+5.0을 0~1로 정규화한 값 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Merchant", meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> PB_Affinity;

	/** `다음 해금까지 0.6` (`04` §3) */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Merchant", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_AffinityToNextUnlock;

	// ───────────────────────── 크레딧 ─────────────────────────

	UPROPERTY(BlueprintReadOnly, Category = "MD|Merchant", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_Credits;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Merchant", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_DailyAdvance;

	// ───────────────────────── 대출 ─────────────────────────

	UPROPERTY(BlueprintReadOnly, Category = "MD|Merchant", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_DebtBalance;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Merchant", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_LoanLimit;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Merchant", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_DailyInterestRate;

	/** 잔액 × 일이자율. `04` §4: "예상 다음 이자를 숫자(₡)로 미리 보여줌" */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Merchant", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_NextInterestPreview;

	/** `04` §4 안내 문구: "패널티는 이자뿐입니다" */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Merchant", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_LoanPenaltyNotice;

	// ───────────────────────── 계약 / 퀘스트 ─────────────────────────

	/** `Day 1 / 3` */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Merchant", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_ContractDay;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Merchant", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_ActiveQuestReward;

	/** `07` §3.2: 퀘스트 실패는 호감도만 하락, 크레딧 위약금 없음 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Merchant", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_QuestPenaltyNotice;

private:
	/** 화폐 표기는 항상 `₡` 접미사 — `07`·`04` 전체가 이 표기를 쓴다. */
	static FText FormatCredits(int32 Amount);

	/** TalkLines에서 마지막으로 보여준 줄. -1이면 아직 Greeting만 띄운 상태다. */
	int32 TalkLineIndex = INDEX_NONE;
};
