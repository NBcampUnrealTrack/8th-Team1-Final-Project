#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/Merchant/MD_MerchantTypes.h"
#include "MD_HUDQuestWidget.generated.h"

class UMD_HUDQuestCard;
class UMD_MerchantComponent;
class UScrollBox;

/** 현재 메인 퀘스트와 수락한 반복 퀘스트를 카드 목록으로 표시하는 HUD 위젯 */
UCLASS()
class MARINEDEMOLITION_API UMD_HUDQuestWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	// 위젯 생성 시 퀘스트 카드 템플릿과 복제 컴포넌트 연결
	virtual void NativeConstruct() override;

	// 위젯 제거 시 상태 변경 이벤트와 재연결 타이머 정리
	virtual void NativeDestruct() override;

protected:
	// 메인 퀘스트와 수락한 반복 퀘스트 카드를 나열하는 스크롤 목록
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> SB_QuestList;

	// 목록에 동적으로 생성할 퀘스트 카드 Blueprint 클래스
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest")
	TSubclassOf<UMD_HUDQuestCard> QuestCardClass;

	// Seamless Travel로 교체되는 GameState 컴포넌트를 안전하게 추적하기 위한 약한 참조
	UPROPERTY()
	TWeakObjectPtr<UMD_MerchantComponent> BoundMerchantComponent;

	// 맵 전환 이후 새 GameState에 다시 연결하기 위한 주기적 확인 타이머
	FTimerHandle MerchantBindingTimerHandle;

protected:
	// 현재 GameState의 상인 복제 컴포넌트와 위젯을 연결하는 처리
	void TryBindMerchantComponent();

	// 현재 일차의 메인 퀘스트 카드를 목록에 추가하는 처리
	void AddMainQuestCard(const FMD_MainQuestState& QuestState);

	// 사용자가 수락한 반복 퀘스트 카드를 목록에 추가하는 처리
	void AddRepeatQuestCard(const FMD_RepeatQuestState& QuestState);

	// 복제된 메인 퀘스트와 수락한 반복 퀘스트로 카드 목록을 다시 만드는 처리
	UFUNCTION()
	void RefreshQuestDisplay();
};
