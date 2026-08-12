// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "MD_Merchant.generated.h"

class UMD_MerchantMenuWidget;
class UMD_ShopCatalog;
class UMD_ShopWidget;
class UMD_QuestWidget;
class UMD_MerchantComponent;
class AMD_PlayerController;
class UStaticMeshComponent;

/**
 * 모선 상인 노라 브릭스 (팀 호칭 "브릭스").
 *
 * 역할 (`02` §7): 장비 판매 · 착수금 지급 · 상급 샘플 퀘스트 부여 · 대출 · 부상 치료
 *
 * 캐릭터 에셋이 아직 없어 큐브 스태틱 메시로 대체한다. 실제 캐릭터가 나오면
 * MeshAsset만 바꾸거나 스켈레탈 메시 컴포넌트로 교체하면 되고,
 * IMD_Interactable 계약과 위젯 흐름은 그대로 유지된다.
 */
UCLASS()
class MARINEDEMOLITION_API AMD_Merchant : public AActor, public IMD_Interactable
{
	GENERATED_BODY()

public:
	AMD_Merchant();

	virtual bool RequiresServerInteraction_Implementation() const override { return false; }
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;

	/** WBP가 표시할 상인 이름 (`06` §4: 노라 브릭스) */
	UFUNCTION(BlueprintCallable, Category = "MD|Merchant")
	FText GetMerchantDisplayName() const { return MerchantDisplayName; }

protected:
	/**
	 * 트랜스폼 기준점. Mesh를 루트로 쓰면 Mesh의 상대 트랜스폼이 곧 액터 트랜스폼이 되어
	 * 레벨 배치 값에 덮어써진다 — 플레이스홀더 크기를 코드로 고정할 수 없다.
	 * 그래서 빈 씬 컴포넌트를 루트로 두고 Mesh를 자식으로 붙인다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Merchant")
	TObjectPtr<USceneComponent> Root;

	/** 큐브 플레이스홀더. 캐릭터 에셋 확정 시 교체 대상. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Merchant")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Merchant")
	FText MerchantDisplayName = FText::FromString(TEXT("노라 브릭스"));

	/** 인터랙트 시 열리는 루트 메뉴 (대화/상점/퀘스트/대출/나가기) */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Merchant")
	TSubclassOf<UMD_MerchantMenuWidget> MenuWidgetClass;

	/** 메뉴에서 '상점'을 눌렀을 때 열리는 화면 */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Merchant")
	TSubclassOf<UMD_ShopWidget> ShopWidgetClass;

	/** 가격·해금 데이터 (`07` §4). 없으면 상점이 빈 목록으로 열린다. */
	UPROPERTY(EditAnywhere, Category = "MD|Merchant")
	TObjectPtr<UMD_ShopCatalog> ShopCatalog;

	/** 메뉴에서 '퀘스트'를 눌렀을 때 열리는 화면 (메인/반복/로어 3탭, `04` §5.5) */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Merchant")
	TSubclassOf<UMD_QuestWidget> QuestWidgetClass;

	/**
	 * 이미 가득 찬 품목을 사려 할 때 브릭스가 하는 말.
	 *
	 * 기획서에 이 상황의 대사가 없어서 `06` §4 톤(캐주얼한 장사꾼, 감정 협박 대신
	 * 손해 계산으로 압박)에 맞춰 임시로 적었다. 확정 문구가 나오면 여기서 교체한다.
	 */
	UPROPERTY(EditAnywhere, Category = "MD|Merchant")
	FText FullStockDialogue = FText::FromString(TEXT("그건 이미 꽉 찼어. 돈 아껴."));

	/**
	 * 아직 화면이 없는 메뉴(퀘스트·대출)를 눌렀을 때의 대사.
	 *
	 * 기획서에 이 상황 대사가 없어 `06` §4 톤에 맞춰 임시로 적었다.
	 * 해당 화면이 생기면 이 대사 대신 화면을 띄우도록 바꾼다.
	 */
	UPROPERTY(EditAnywhere, Category = "MD|Merchant")
	FText NotReadyDialogue = FText::FromString(TEXT("그건 아직 안 열었어. 목록이나 봐."));

	/** 열려 있는 위젯 (중복 생성 방지용) */
	TWeakObjectPtr<UMD_MerchantMenuWidget> ActiveMenuWidget;

	TWeakObjectPtr<UMD_ShopWidget> ActiveShopWidget;

	TWeakObjectPtr<UMD_QuestWidget> ActiveQuestWidget;

private:
	/** 메뉴에서 '나가기'를 눌렀을 때 */
	UFUNCTION()
	void HandleMenuExitRequested();

	/** 메뉴에서 '상점'을 눌렀을 때 */
	UFUNCTION()
	void HandleShopRequested();

	/** 상점에서 '뒤로'를 눌렀을 때 — 메뉴는 아래에 그대로 살아 있다. */
	UFUNCTION()
	void HandleShopBackRequested();

	/** 가득 찬 품목 구매를 막았을 때 — 메뉴의 대사창으로 이유를 알린다. */
	UFUNCTION()
	void HandleShopPurchaseRejected(FName ItemId);

	/** 메뉴에서 '퀘스트'를 눌렀을 때 */
	UFUNCTION()
	void HandleQuestRequested();

	/** 퀘스트 화면에서 '뒤로'를 눌렀을 때 */
	UFUNCTION()
	void HandleQuestBackRequested();

	/** 반복 퀘스트 카드에서 '수락'을 눌렀을 때 — 서버 판정은 PlayerController RPC가 맡는다. */
	UFUNCTION()
	void HandleRepeatAcceptRequested(int32 QuestIndex);

	UFUNCTION()
	void HandleRepeatDeclineRequested(int32 QuestIndex);

	/** 백엔드(UMD_MerchantSubsystem)에 수락 이후 파기 처리가 아직 없어 안내만 띄운다. */
	UFUNCTION()
	void HandleRepeatAbandonRequested(int32 QuestIndex);

	/** 상인 상태(퀘스트 등)가 바뀔 때마다 열려 있는 퀘스트 화면을 다시 그린다. */
	UFUNCTION()
	void HandleMerchantStateChangedForQuest();

	void RefreshQuestWidgetFromComponent();

	UMD_MerchantComponent* GetMerchantComponent() const;

	AMD_PlayerController* GetInteractingPlayerController() const;

	/**
	 * 대출은 아직 화면이 없다. 버튼을 숨기면 돌아올 길이 없으니
	 * 숨기지 않고 대사로만 알린다.
	 */
	UFUNCTION()
	void HandleNotReadyRequested();
};
