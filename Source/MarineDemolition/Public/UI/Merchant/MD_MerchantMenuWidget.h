// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/Foundation/MD_CommonGameMenuWidget.h"

#include "UI/Merchant/MD_MerchantUITypes.h"

#include "MD_MerchantMenuWidget.generated.h"

class UScrollBox;
class UTextBlock;
class AMD_Merchant;
class UMD_MerchantComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMD_MerchantMenuRequest);

/**
 * 상인 인터랙트 시 열리는 루트 메뉴: 대화 / 상점 / 퀘스트 / 대출 / 나가기
 *
 * WBP_MD_MerchantMenu의 상인 정보와 대사창을 이름이 같은 위젯에 바인딩한다.
 */
UCLASS()
class MARINEDEMOLITION_API UMD_MerchantMenuWidget : public UMD_CommonGameMenuWidget
{
	GENERATED_BODY()

public:
	// 재사용된 메뉴의 소유 상인 변경 시 대화 진행 상태 초기화
	void SetOwningMerchant(AMD_Merchant* InMerchant);

	/** Blueprint가 Push한 상점을 초기화할 소유 상인을 반환한다. */
	UFUNCTION(BlueprintPure, Category = "MD|Merchant")
	FORCEINLINE AMD_Merchant* GetOwningMerchant() { return OwningMerchant; }

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

	/** 화면 대사창에 한 줄을 띄운다. 대사창이 배치돼 있지 않으면 아무 일도 하지 않는다. */
	UFUNCTION(BlueprintCallable, Category = "MD|Merchant")
	void ShowDialogueLine(const FText& Line);

	/** 각 메뉴 진입 요청. 실제 화면은 소유 액터나 BP가 붙인다. */
	UPROPERTY(BlueprintAssignable, Category = "MD|Merchant")
	FMD_MerchantMenuRequest OnTalkRequested;

	UPROPERTY(BlueprintAssignable, Category = "MD|Merchant")
	FMD_MerchantMenuRequest OnExitRequested;

protected:
	virtual void NativeOnActivated() override;
	virtual void NativeDestruct() override;

	UFUNCTION(BlueprintCallable, Category = "MD|Merchant")
	void HandleTalkClicked();

	UFUNCTION(BlueprintCallable, Category = "MD|Merchant")
	void HandleExitClicked();

	// 복제된 상인 상태를 대화 진행 상태와 함께 화면에 반영
	UFUNCTION()
	void HandleMerchantStateChanged();

	/** 디자이너가 에디터에서 미리보기할 수 있도록 기본값을 노출한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Merchant")
	FMD_MerchantMenuInfo MenuInfo;

	UPROPERTY()
	TObjectPtr<AMD_Merchant> OwningMerchant;

	// 하위 화면 전환 중에도 유지하고 메뉴 제거 시 해제할 상태 알림 연결
	TWeakObjectPtr<UMD_MerchantComponent> BoundMerchantComponent;

	// ───────────────────────── 상인 신원 ─────────────────────────

	UPROPERTY(BlueprintReadOnly, Category = "MD|Merchant", meta = (BindWidgetOptional))
	// 초상화 아래에 표시할 현재 상인 이름
	TObjectPtr<UTextBlock> Txt_NpcName;

	/** 화면 중앙 대사창의 본문 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Merchant", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Dialogue;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Merchant", meta = (BindWidgetOptional))
	// 대사 교체 시 읽기 위치를 처음으로 되돌릴 스크롤 영역
	TObjectPtr<UScrollBox> SB_Dialogue;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "MD|Merchant")
	// 하위 화면에서 복귀하거나 정보가 갱신되어도 유지할 현재 대사
	FText CurrentDialogueLine;

	// 빈 대사도 표시 완료 상태로 구분할 초기화 여부
	bool bHasDialogueLine = false;

private:
	/** TalkLines에서 마지막으로 보여준 줄. -1이면 아직 Greeting만 띄운 상태다. */
	int32 TalkLineIndex = INDEX_NONE;
};
