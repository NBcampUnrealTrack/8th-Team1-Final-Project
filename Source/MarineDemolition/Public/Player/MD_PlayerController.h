// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Data/Shop/MD_ShopCatalog.h"
#include "GameFramework/PlayerController.h"
#include "Types/Score/MD_ScoreTypes.h"
#include "Types/Travel/MD_TravelTypes.h"
#include "MD_PlayerController.generated.h"

class AMD_TravelConsole;
class AMD_DayEndConsole;
class AMD_SectorCloseoutMarker;
class AMD_Merchant;
class UMD_HUDWidget;
class UMD_DailyReportWidget;
class UMD_EquipHotbarWidget;
class UMD_ScrapeFocusReticleWidget;
class UMD_SiteDefinition;
class UMD_MerchantSubsystem;
class UGameplayEffect;

UCLASS()
class MARINEDEMOLITION_API AMD_PlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AMD_PlayerController();

	/**
	 * 입력 모드를 바꾸기 전에 진행 중인 헤라 Focus와 Ability를 함께 정리한다.
	 *
	 * Blueprint UI도 같은 진입점을 사용할 수 있도록 공개한다. 호출은 중복되어도 안전하다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MD|Scrape")
	void CancelScrapeInteraction();

	/**
	 * 모든 C++/Blueprint 입력 모드 전환을 한곳에서 가로채 RMB release 유실 전에
	 * 헤라 상호작용을 정리한다. GameOnly 복귀에서도 호출되지만 정리는 멱등이다.
	 */
	virtual void SetInputMode(const FInputModeDataBase& InData) override;
	virtual void SetViewTarget(
		AActor* NewViewTarget,
		FViewTargetTransitionParams TransitionParams = FViewTargetTransitionParams()) override;

	// 콘솔을 통째로 넘겨 서버가 자기 사본 Sites[SiteIndex]에서 레벨을 읽음 (클라 값 신뢰 안 함)
	UFUNCTION(Server, Reliable)
	void ServerRequestTravel(AMD_TravelConsole* Console, int32 SiteIndex, EMD_Difficulty Difficulty);

	// 모선 하루 마감 콘솔 요청. 서버가 거리 검증 후 GameMode 마감 실행
	UFUNCTION(Server, Reliable)
	void ServerRequestEndDay(AMD_DayEndConsole* Console);

	// 구역 마감 제출 요청. 서버가 거리 검증 후 마커 마감 실행
	UFUNCTION(Server, Reliable)
	void ServerRequestSectorCloseout(AMD_SectorCloseoutMarker* Marker);

	/** GameMode가 원정 계약을 시작할 때 쓴다. DebugSite와 동일 에셋을 넣으면 된다. */
	UMD_SiteDefinition* GetExpeditionSite() const { return DebugSite; }

	/** 상인 UI에서 반복 퀘스트를 수락할 때 호출한다. */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "MD|Merchant")
	void ServerAcceptRepeat(int32 Index);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "MD|Merchant")
	void ServerDeclineRepeat(int32 Index);

	/** 상점 액터와 품목을 서버에서 다시 검증하도록 요청하는 함수 */
	UFUNCTION(Server, Reliable)
	void ServerRequestShopPurchase(AMD_Merchant* Merchant, FName ItemId);

	/** 서버 구매 결과를 요청한 로컬 상점 화면에 전달하는 응답 진입점 */
	UFUNCTION(Client, Reliable)
	void ClientReceiveShopPurchaseResult(AMD_Merchant* Merchant, FName ItemId, EMD_ShopPurchaseResult Result);

	// 서버가 확정한 Day 결과를 로컬 WBP_DailyReport에 표시하는 응답 진입점
	UFUNCTION(Client, Reliable)
	void ClientShowDailyReport(FMD_InspectionReport Report);

	// 서버가 전환을 승인한 뒤 모든 로컬 보고서와 전용 입력 상태를 정리하는 응답
	UFUNCTION(Client, Reliable)
	void ClientCloseDailyReport();

	/** 서버 이동 전에 각 소유 클라이언트에서 실제 로딩 화면을 시작한다. */
	UFUNCTION(Client, Reliable)
	void ClientPrepareLoadingTransition(const FText& Subtitle);

	/** 로딩 화면에 조작 주체 준비 완료를 알린다(하드 트래블/seamless travel 공용). */
	void NotifyLoadingScreenPlayerReady();

	// 일일 보고서 확인 버튼 입력을 서버의 보류 중인 Day 전환에 전달하는 요청
	UFUNCTION(Server, Reliable)
	void ServerConfirmDayTransition();

	// 서버에서 반복 퀘스트 포기와 관련 페널티를 처리하도록 요청하는 함수
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "MD|Merchant")
	void ServerAbandonRepeatQuest(int32 Index);

	// 모선 상인과의 거리 및 대출 한도를 서버에서 검증한 뒤 대출을 실행하는 함수
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "MD|Merchant")
	void ServerBorrowCredits(AMD_Merchant* MerchantActor, int32 Amount);

	// 모선 상인과의 거리 및 보유 크레딧을 서버에서 검증한 뒤 상환을 실행하는 함수
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "MD|Merchant")
	void ServerRepayLoan(AMD_Merchant* MerchantActor, int32 Amount);

private:

	UPROPERTY(EditDefaultsOnly, Category = "MD|Debug")
	TSubclassOf<UGameplayEffect> TestDamageEffect;

	// GE 테스트용 콘솔 커맨드. 오염수·붕괴 구현 되면 삭제
	UFUNCTION(Exec)
	void MD_ApplyTestDamage();

	//Attrivbute Health 확인용 콘솔 커맨드. UI연결되면 삭제
	UFUNCTION(Exec)
	void MD_ShowHealth();

	UFUNCTION(Server,Reliable)
	void ServerApplyTestDamage();

	//상태 변경 확인 콘솔 커맨드. UI연결되면 삭제
	UFUNCTION(Exec)
	void MD_ShowHealthState();

	//상인 크레딧 확인용 콘솔 커맨드. UI 연결되면 삭제
	UFUNCTION(Exec)
	void MD_AddCredits(int32 Amount);

	UFUNCTION(Exec)
	void MD_ShowCredits();

	UFUNCTION(Exec)
	void MD_AddAffinity(int32 DeltaAffinity);

	//서버에서 크레딧 변경 처리
	UFUNCTION(Server, Reliable)
	void ServerAddCredits(int32 Amount);

	/** 테스트용 현장 애셋. BP_MD_PlayerController에서 DA_Site_X7을 지정한다. */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Debug")
	TObjectPtr<UMD_SiteDefinition> DebugSite;

	/** 표준 난이도 착수금 (07 §2.1) */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Debug")
	int32 DebugAdvancePayment = 500;

	UFUNCTION(Exec)
	void MD_BeginContract();

	UFUNCTION(Exec)
	void MD_DumpQuests();

public:
	/**
	 * 게임플레이 HUD(상태창·핫바)를 숨기거나 다시 보여준다.
	 *
	 * 상인 메뉴처럼 화면을 덮는 UI가 열릴 때 HUD가 겹쳐 보이는 것을 막는다.
	 * 위젯을 파괴하지 않고 가시성만 바꾸므로 닫으면 이전 상태가 그대로 돌아온다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MD|UI")
	void SetGameplayHUDVisible(bool bVisible);

protected:
	// 컨트롤러 시작 이후 이미 소유된 Pawn을 기준으로 HUD를 초기화하는 진입점
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 플레이 상태 전환 이후 Pawn 기반 HUD를 복구하는 진입점
	virtual void BeginPlayingState() override;

	// Seamless Travel 완료 이후 새 뷰포트에 HUD를 복구하는 진입점
	virtual void PostSeamlessTravel() override;

	// 서버 소유 및 클라이언트 복제로 Pawn이 확정될 때 HUD를 초기화하는 진입점
	virtual void SetPawn(APawn* InPawn) override;

private:
	/** Alt-Tab/창 비활성화로 입력 release를 잃기 전에 헤라 상호작용을 정리한다. */
	void HandleApplicationWillDeactivate();

	FDelegateHandle ApplicationWillDeactivateHandle;

	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	TSubclassOf<UMD_HUDWidget> HUDWidgetClass;

	UPROPERTY()
	TObjectPtr<UMD_HUDWidget> HUDWidgetInstance;

	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	TSubclassOf<UMD_EquipHotbarWidget> EquipHotbarWidgetClass;

	UPROPERTY()
	TObjectPtr<UMD_EquipHotbarWidget> EquipHotbarWidgetInstance;

	/** Blueprint 자산이 없어도 동작하는 Focus 가상 포인터. */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Scrape")
	TSubclassOf<UMD_ScrapeFocusReticleWidget> ScrapeFocusReticleWidgetClass;

	UPROPERTY()
	TObjectPtr<UMD_ScrapeFocusReticleWidget> ScrapeFocusReticleWidgetInstance;

	// EndDay 결과를 표시하고 다음 전환 확인 입력을 받는 기존 WBP_DailyReport 클래스
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Daily Report")
	TSubclassOf<UMD_DailyReportWidget> DailyReportWidgetClass;

	// 같은 EndDay 알림이 중복 도착해도 하나의 보고서 화면만 유지하기 위한 로컬 인스턴스
	UPROPERTY()
	TObjectPtr<UMD_DailyReportWidget> DailyReportWidgetInstance;

	// 로컬 플레이어의 메인 HUD와 장비 핫바를 생성하거나 화면에 복구하는 처리
	void InitializeGameplayHUD();

	// 로컬 플레이어의 헤라 Focus 포인터를 생성하거나 화면에 복구하는 처리
	void InitializeScrapeFocusReticle();

	// WBP_DailyReport의 확인 버튼을 서버 전환 요청으로 연결하는 로컬 처리
	UFUNCTION()
	void HandleDailyReportContinueClicked();

	// 맵 전환 전 보고서와 전용 입력 상태를 원래 게임플레이 상태로 복구하는 처리
	void CloseDailyReport();

	// 보고서 인스턴스와 외부 버튼 이벤트 연결을 정리하고 실제 제거 여부를 반환하는 공통 처리
	bool RemoveDailyReportWidget();

	/* 아래부터는 콘솔 커맨드 입력으로 디버깅용 ServerRPC래핑입니다. 미션선택 UI연결되면 삭제 */
private:
	
	UMD_MerchantSubsystem* GetMerchantSubsystem() const;

	UFUNCTION(Server, Reliable)
	void ServerBeginContract();

	UFUNCTION(Exec)
	void MD_BeginDay(int32 Day);

	UFUNCTION(Server, Reliable)
	void ServerBeginDay(int32 Day);

	UFUNCTION(Exec)
	void MD_AcceptRepeat(int32 Index);

	UFUNCTION(Exec)
	void MD_DeclineRepeat(int32 Index);

	UFUNCTION(Exec)
	void MD_Deliver(const FString& GradeName, bool bIntact);

	UFUNCTION(Server, Reliable)
	void ServerDeliver(FGameplayTag GradeTag, bool bIntact);

	UFUNCTION(Exec)
	void MD_MarkSiteCleared();

	UFUNCTION(Server, Reliable)
	void ServerMarkSiteCleared();

	UFUNCTION(Exec)
	void MD_FinalizeContract();

	UFUNCTION(Server, Reliable)
	void ServerFinalizeContract();
};
