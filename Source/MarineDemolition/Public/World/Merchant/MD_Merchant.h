// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "UObject/PrimaryAssetId.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "MD_Merchant.generated.h"

struct FStreamableHandle;

class UMD_MerchantMenuWidget;
class UMD_ShopCatalog;
class UMD_ShopWidget;
class UMD_QuestScreenWidget;
class UMD_LoanWidget;
class UMD_LoreComponent;
class UMD_MerchantComponent;
class UMD_MerchantSubsystem;
class AMD_PlayerController;
class AMD_PlayerState;
class AMD_Character;
class UStaticMeshComponent;

enum class EMD_ShopPurchaseResult : uint8;

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
	virtual FText GetInteractionDisplayName_Implementation() const override;

	/** WBP가 표시할 상인 이름 (`06` §4: 노라 브릭스) */
	UFUNCTION(BlueprintCallable, Category = "MD|Merchant")
	FText GetMerchantDisplayName() const { return MerchantDisplayName; }

	/** 서버 카탈로그와 경제 원장을 사용해 구매자에게 품목을 지급하고 결제를 확정하는 함수 */
	EMD_ShopPurchaseResult TryPurchaseItem(AMD_Character* PurchasingCharacter, FPrimaryAssetId ItemAssetId);

	/** 서버 구매 결과를 현재 로컬 상점과 대사창에 반영하는 함수 */
	void ReceiveShopPurchaseResult(FPrimaryAssetId ItemAssetId, EMD_ShopPurchaseResult Result);

	/** Blueprint가 Push하기 전에 전달한 상점 화면에 상인 데이터와 도메인 이벤트를 연결한다. */
	UFUNCTION(BlueprintCallable, Category = "MD|Merchant")
	void InitializePushedShop(UMD_ShopWidget* ShopWidget, UMD_MerchantMenuWidget* MenuWidget);

	/** Blueprint가 Push한 퀘스트 화면에 상인 데이터와 도메인 이벤트를 연결한다. */
	UFUNCTION(BlueprintCallable, Category = "MD|Merchant")
	void InitializePushedQuest(UMD_QuestScreenWidget* QuestWidget, UMD_MerchantMenuWidget* MenuWidget);

	/** Blueprint가 Push한 대출 화면에 상인 데이터와 도메인 이벤트를 연결하는 함수 */
	UFUNCTION(BlueprintCallable, Category = "MD|Merchant")
	void InitializePushedLoan(UMD_LoanWidget* LoanWidget, UMD_MerchantMenuWidget* MenuWidget);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

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

	/** UIManager가 인터랙트 시 Push할 상인 메뉴 화면 요청 태그 */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Merchant", meta = (Categories = "UI.Layer.GameMenu"))
	FGameplayTag MenuWidgetTag;

	/** 가격·해금 데이터 (`07` §4). 없으면 상점이 빈 목록으로 열린다. */
	UPROPERTY(EditAnywhere, Category = "MD|Merchant")
	TObjectPtr<UMD_ShopCatalog> ShopCatalog;

	// 서버 검증 및 로컬 상점에서 사용할 정의의 로드 수명 유지
	TSharedPtr<FStreamableHandle> DefinitionLoadHandle;

	/**
	 * 이미 가득 찬 품목을 사려 할 때 브릭스가 하는 말.
	 *
	 * 기획서에 이 상황의 대사가 없어서 `06` §4 톤(캐주얼한 장사꾼, 감정 협박 대신
	 * 손해 계산으로 압박)에 맞춰 임시로 적었다. 확정 문구가 나오면 여기서 교체한다.
	 */
	UPROPERTY(EditAnywhere, Category = "MD|Merchant")
	FText FullStockDialogue = FText::FromString(TEXT("그건 이미 꽉 찼어. 돈 아껴."));

	/** 크레딧이 부족한 구매 요청에 표시할 상인 대사 */
	UPROPERTY(EditAnywhere, Category = "MD|Merchant")
	FText InsufficientCreditsDialogue = FText::FromString(TEXT("잔액부터 확인해. 외상은 안 받아."));

	/** 해금 조건을 충족하지 못한 구매 요청에 표시할 상인 대사 */
	UPROPERTY(EditAnywhere, Category = "MD|Merchant")
	FText LockedItemDialogue = FText::FromString(TEXT("아직 네게 팔 물건은 아니야."));

	/** 잘못된 품목이나 만료된 요청에 표시할 상인 대사 */
	UPROPERTY(EditAnywhere, Category = "MD|Merchant")
	FText InvalidPurchaseDialogue = FText::FromString(TEXT("그 거래는 처리할 수 없어."));

	/** 열려 있는 위젯 (중복 생성 방지용) */
	TWeakObjectPtr<UMD_MerchantMenuWidget> ActiveMenuWidget;

	TWeakObjectPtr<UMD_ShopWidget> ActiveShopWidget;

	TWeakObjectPtr<UMD_QuestScreenWidget> ActiveQuestWidget;

	// 중복 생성 방지와 복제 상태 갱신에 사용하는 현재 대출 화면 참조
	TWeakObjectPtr<UMD_LoanWidget> ActiveLoanWidget;

private:
	/** 메뉴에서 '나가기'를 눌렀을 때 */
	UFUNCTION()
	void HandleMenuExitRequested();

	/** Common UI Stack에서 상점이 비활성화되면 상태 구독과 활성 화면 참조를 정리한다. */
	void HandleShopDeactivated();

	/** 가득 찬 품목 구매를 막았을 때 — 메뉴의 대사창으로 이유를 알린다. */
	UFUNCTION()
	void HandleShopPurchaseRejected();

	/** 로컬 상점 구매 요청을 소유 PlayerController의 서버 RPC로 전달하는 처리 */
	UFUNCTION()
	void HandleShopItemPurchaseRequested(FPrimaryAssetId ItemAssetId);

	/** 상인 복제 미러가 바뀔 때 열려 있는 상점 정보와 구매 가능 상태를 갱신하는 처리 */
	UFUNCTION()
	void HandleMerchantStateChangedForShop();

	/** 현재 플레이어의 상점 소유권이 복제될 때 구매 가능 상태를 갱신하는 처리 */
	UFUNCTION()
	void HandlePlayerShopOwnershipChanged();

	/** Common UI Stack에서 퀘스트 화면이 비활성화되면 상태 구독과 활성 화면 참조를 정리한다. */
	void HandleQuestBackRequested();

	/** Common UI Stack에서 대출 화면이 비활성화되면 상태 구독과 활성 화면 참조를 정리하는 함수 */
	void HandleLoanDeactivated();

	// 대출 화면의 금액 요청을 서버 권위 PlayerController로 전달하는 함수
	UFUNCTION()
	void HandleBorrowRequested(int32 Amount);

	// 상환 화면의 금액 요청을 서버 권위 PlayerController로 전달하는 함수
	UFUNCTION()
	void HandleRepayRequested(int32 Amount);

	/** 반복 퀘스트 카드에서 '수락'을 눌렀을 때 — 서버 판정은 PlayerController RPC가 맡는다. */
	UFUNCTION()
	void HandleRepeatAcceptRequested(int32 QuestIndex);

	UFUNCTION()
	void HandleRepeatDeclineRequested(int32 QuestIndex);

	/** 반복 퀘스트 카드의 포기 요청을 서버 권위 PlayerController로 전달하는 함수 */
	UFUNCTION()
	void HandleRepeatAbandonRequested(int32 QuestIndex);

	/** 상인 상태 변경 시 열려 있는 UI의 호감도 정보를 갱신하는 함수 */
	UFUNCTION()
	void HandleMerchantStateChanged();

	/** 로어 전용 복제 정보가 바뀔 때 열려 있는 코덱스 탭만 갱신하는 함수 */
	UFUNCTION()
	void HandleLoreProgressUpdated();

	void RefreshQuestWidgetFromComponent();

	/** UI 개방 또는 로어 수집 변경 시 코덱스 탭에 복제 원장을 적용하는 함수 */
	void RefreshLoreWidgetFromComponent();

	/** 복제된 재화·해금·소유권을 현재 상점 위젯에 한 번에 반영하는 처리 */
	void RefreshShopWidgetFromComponent();

	// 복제된 크레딧과 부채를 현재 대출 화면에 다시 적용하는 함수
	void RefreshLoanWidgetFromComponent();

	/** 활성 메인 퀘스트 중 Interaction 목표를 찾아 서버 페이즈 전환을 요청하는 함수 */
	void TryAdvanceInteractionQuests(APlayerController* PC);

	UMD_MerchantComponent* GetMerchantComponent() const;

	/** 메인·반복 퀘스트와 독립된 로어 수집 복제 컴포넌트 조회 함수 */
	UMD_LoreComponent* GetLoreComponent() const;

	/** 서버 구매 원장을 제공하는 GameInstance 상인 서브시스템 조회 함수 */
	UMD_MerchantSubsystem* GetMerchantSubsystem() const;

	AMD_PlayerController* GetInteractingPlayerController() const;

	/** 현재 상점 화면을 소유한 플레이어의 개별 구매 원장을 제공하는 함수 */
	AMD_PlayerState* GetInteractingPlayerState() const;


};
