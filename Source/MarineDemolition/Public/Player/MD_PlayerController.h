// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Data/Shop/MD_ShopCatalog.h"
#include "GameFramework/PlayerController.h"
#include "Types/Customization/MD_CharacterAppearanceTypes.h"
#include "Types/Score/MD_ScoreTypes.h"
#include "Types/Travel/MD_TravelTypes.h"
#include "Types/UI/MD_LoadingScreenTypes.h"
#include "MD_PlayerController.generated.h"

class AMD_GameStateBase;
class AMD_TravelConsole;
class AMD_DayEndConsole;
class AMD_SectorCloseoutMarker;
class AMD_Merchant;
class UMD_HUDWidget;
class UMD_SiteDefinition;
class UMD_MerchantSubsystem;
class UGameplayEffect;
class AMD_CuttableWall;
class AMD_WallUtility;

// 서버에서 수신한 점수와 크레딧 기록을 로컬 UI에 전달하는 이벤트
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMD_OnDailyLedgerReceived, FMD_InspectionReport, Report);

UCLASS()
class MARINEDEMOLITION_API AMD_PlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AMD_PlayerController();

    // 응답 RPC를 구독하는 로컬 UI 이벤트
    UPROPERTY(BlueprintAssignable, Category = "MD|Score")
    FMD_OnDailyLedgerReceived OnDailyLedgerReceived;

    // 늦게 도착한 이전 화면 응답을 구분하는 로컬 요청 번호
    int32 DailyLedgerRequestId = 0;

    // 현재 서버에서 구독 중인 요청 번호
    int32 ServerDailyLedgerRequestId = 0;

    // RPC 응답에 필요한 서버 원장 구독 대상
    TWeakObjectPtr<AMD_GameStateBase> DailyLedgerSource;

    // 로컬 UI의 점수와 크레딧 기록 요청 시작
    UFUNCTION(BlueprintCallable, Category = "MD|Score")
    void RequestDailyLedger();

    // 화면 종료 시 서버 원장 구독과 이전 응답 무효화
    UFUNCTION(BlueprintCallable, Category = "MD|Score")
    void StopDailyLedger();

    // 소유 클라이언트가 요청한 서버 원장 조회
    UFUNCTION(Server, Reliable)
    void ServerRequestDailyLedger(int32 RequestId);

    // 닫힌 화면의 원장 변경 전송 중단
    UFUNCTION(Server, Reliable)
    void ServerStopDailyLedger(int32 RequestId);

    // 요청자에게만 점수와 크레딧 기록 반환
    UFUNCTION(Client, Reliable)
    void ClientReceiveDailyLedger(int32 RequestId, FMD_InspectionReport Report);

    // 서버 확정 보고서를 각 소유 클라이언트의 UI Manager에 전달
    UFUNCTION(Client, Reliable)
    void ClientShowDayEndReport(FGameplayTag ReportWidgetTag, FMD_InspectionReport Report, double EndServerTime);

    // 서버의 5초 표시 종료를 각 소유 클라이언트에 전달
    UFUNCTION(Client, Reliable)
    void ClientCloseDayEndReport();

    // 변경된 서버 원장을 현재 요청자에게 전송
    void SendDailyLedger();

    // 서버 원장 이벤트 연결 정리
    void UnbindDailyLedger();


	/**
	 * 입력 모드를 바꾸기 전에 진행 중인 오염물 타격을 취소한다.
	 *
	 * Blueprint UI도 같은 진입점을 사용할 수 있도록 공개한다. 호출은 중복되어도 안전하다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MD|Stain")
	void CancelScrapeInteraction();

	/**
	 * 모든 C++/Blueprint 입력 모드 전환을 한곳에서 가로채 RMB release 유실 전에
	 * 헤라 상호작용을 정리한다. GameOnly 복귀에서도 호출되지만 정리는 멱등이다.
	 */
	virtual void SetInputMode(const FInputModeDataBase& InData) override;
	/** 입력 초기화가 키 해제로 처리되기 전에 충전 던지기를 취소한다. */
	virtual void FlushPressedKeys() override;
	virtual void SetViewTarget(
		AActor* NewViewTarget,
		FViewTargetTransitionParams TransitionParams = FViewTargetTransitionParams()) override;

	virtual void ClientWasKicked_Implementation(const FText& KickReason) override;

	// 콘솔을 통째로 넘겨 서버가 자기 사본 Sites[SiteIndex]에서 레벨을 읽음 (클라 값 신뢰 안 함)
	UFUNCTION(Server, Reliable)
	void ServerRequestTravel(AMD_TravelConsole* Console, int32 SiteIndex, EMD_Difficulty Difficulty);

	// 리슨 서버 호스트가 선택한 원격 플레이어의 강퇴 요청
	UFUNCTION(Server, Reliable)
	void ServerRequestKickPlayer(APlayerState* TargetPlayerState);

	// 모선 하루 마감 콘솔 요청. 서버가 거리 검증 후 GameMode 마감 실행
	UFUNCTION(Server, Reliable)
	void ServerRequestEndDay(AMD_DayEndConsole* Console);

	// 구역 마감 제출 요청. 서버가 거리 검증 후 마커 마감 실행
	UFUNCTION(Server, Reliable)
	void ServerRequestSectorCloseout(AMD_SectorCloseoutMarker* Marker);

	/** GameMode가 원정 계약을 시작할 때 쓴다. DebugSite와 동일 에셋을 넣으면 된다. */
	UMD_SiteDefinition* GetExpeditionSite() const { return DebugSite; }


	/** 상점 액터와 품목을 서버에서 다시 검증하도록 요청하는 함수 */
	UFUNCTION(Server, Reliable)
	void ServerRequestShopPurchase(AMD_Merchant* Merchant, FPrimaryAssetId ItemAssetId);

	// 소유 클라이언트의 판매 품목과 예상 슬롯 및 수량을 서버에서 검증
	UFUNCTION(Server, Reliable)
	void ServerRequestShopSale(AMD_Merchant* Merchant, FPrimaryAssetId ItemAssetId, int32 SlotIndex, int32 Quantity);

	// 해당 상인의 현재 화면에 판매 결과 전달
	UFUNCTION(Client, Reliable)
	void ClientReceiveShopSaleResult(AMD_Merchant* Merchant, EMD_ShopSaleResult Result);

	/** 서버 구매 결과를 요청한 로컬 상점 화면에 전달하는 응답 진입점 */
	UFUNCTION(Client, Reliable)
	void ClientReceiveShopPurchaseResult(AMD_Merchant* Merchant, FPrimaryAssetId ItemAssetId, EMD_ShopPurchaseResult Result);

	/** 서버 이동 전에 각 소유 클라이언트에서 실제 로딩 화면을 시작한다. */
	UFUNCTION(Client, Reliable)
	void ClientPrepareLoadingTransition(EMD_LoadingContext Context);

	// 서버의 참여 및 퇴장 알림을 소유 클라이언트 HUD에 전달
	UFUNCTION(Client, Reliable)
	void ClientNotifyPlayerJoinAndExit(const FText& PlayerName, bool bIsJoin);

	// 서버 판정 결과(슬롯 가득 참·전원 없음 등) 한 줄을 소유 클라이언트 HUD 알림에 전달
	UFUNCTION(Client, Reliable)
	void ClientShowHUDMessage(const FText& Message);

	/** 로딩 화면에 조작 주체 준비 완료를 알린다(하드 트래블/seamless travel 공용). */
	void NotifyLoadingScreenPlayerReady();


	// 모선 상인과의 거리 및 대출 한도를 서버에서 검증한 뒤 대출을 실행하는 함수
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "MD|Merchant")
	void ServerBorrowCredits(AMD_Merchant* MerchantActor, int32 Amount);

	// 모선 상인과의 거리 및 보유 크레딧을 서버에서 검증한 뒤 상환을 실행하는 함수
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "MD|Merchant")
	void ServerRepayLoan(AMD_Merchant* MerchantActor, int32 Amount);

	/** 클라이언트가 확정한 캐릭터 외형을 서버 PlayerState에 적용한다. */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "MD|Customization")
	void ServerApplyAppearance(FMD_CharacterAppearance NewAppearance);

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

	//서버에서 크레딧 변경 처리
	UFUNCTION(Server, Reliable)
	void ServerAddCredits(int32 Amount);

	/** 테스트용 현장 애셋. BP_MD_PlayerController에서 DA_Site_X7을 지정한다. */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Debug")
	TObjectPtr<UMD_SiteDefinition> DebugSite;

	UFUNCTION(Exec)
	void MD_BeginContract();


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
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void BeginPlayingState() override;

	virtual void PostSeamlessTravel() override;

	virtual void SetPawn(APawn* InPawn) override;

private:
	/** UI 전환과 창 비활성화에서 현재 캐릭터의 충전 던지기를 중단한다. */
	void CancelThrowInteraction();

	/** 로컬 커스터마이징 세이브에서 가장 최근 외형을 읽어 서버에 전달한다. */
	void RestoreAppearanceFromLocalSave();

	/** Alt-Tab/창 비활성화로 입력 release를 잃기 전에 헤라 상호작용을 정리한다. */
	void HandleApplicationWillDeactivate();

	/** 로그인된 로컬 플랫폼 사용자의 닉네임을 서버 PlayerState에 제출한다. */
	void SubmitPlatformNickname();

	/** 소유 클라이언트가 확인한 플랫폼 닉네임을 서버의 PlayerState에 저장한다. */
	UFUNCTION(Server, Reliable)
	void ServerSubmitPlatformNickname(const FString& PlatformNickname);

	FDelegateHandle ApplicationWillDeactivateHandle;

	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	TSubclassOf<UMD_HUDWidget> HUDWidgetClass;

	UPROPERTY()
	TObjectPtr<UMD_HUDWidget> HUDWidgetInstance;

	// 로컬 플레이어의 메인 HUD를 생성하거나 화면에 복구하는 처리
	void InitializeGameplayHUD();

	/* 아래부터는 콘솔 커맨드 입력으로 디버깅용 ServerRPC래핑입니다. 미션선택 UI연결되면 삭제 */
private:
	
	/** 치트가 조준선·주변에서 대상을 찾는 기본 반경 (20m) */
	static constexpr float DefaultCheatSearchRadiusCm = 2000.0f;

	UMD_MerchantSubsystem* GetMerchantSubsystem() const;

	UFUNCTION(Server, Reliable)
	void ServerBeginContract();

	/**
	 * 조준한 인프라(없으면 가장 가까운 파손된 인프라) 하나를 수리 상태로 만든다.
	 * 명령 1회당 1개. 배관벽은 배관을 4개 품으므로 여러 번 불러야 할 수 있다.
	 */
	UFUNCTION(Exec)
	void MD_RepairUtility();

	/** Day3 회수 샘플을 배치된 스포너에 생성한다. 기존 샘플이 있으면 중복 생성하지 않는다. */
	UFUNCTION(Exec)
	void MD_SpawnDay3Sample();

	UFUNCTION(Server, Reliable)
	void ServerSpawnDay3Sample();

	UFUNCTION(Server, Reliable)
	void ServerRepairUtility(AMD_WallUtility* Utility);

	/** 조준선 우선, 실패하면 반경 내 가장 가까운 파손된 인프라를 찾는다 */
	AMD_WallUtility* FindUtilityToRepair(float SearchRadiusCm = DefaultCheatSearchRadiusCm) const;


	UFUNCTION(Exec)
	void MD_Deliver(const FString& GradeName, bool bIntact);

	UFUNCTION(Server, Reliable)
	void ServerDeliver(FGameplayTag GradeTag, bool bIntact);

	UFUNCTION(Exec)
	void MD_FinalizeContract();

	UFUNCTION(Server, Reliable)
	void ServerFinalizeContract();
};
