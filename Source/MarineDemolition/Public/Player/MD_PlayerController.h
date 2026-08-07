// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Types/Travel/MD_TravelTypes.h"
#include "MD_PlayerController.generated.h"

class AMD_TravelConsole;
class AMD_DayEndConsole;
class UMD_HUDWidget;
class UMD_EquipHotbarWidget;
class UMD_SiteDefinition;
class UMD_MerchantSubsystem;
class UGameplayEffect;

UCLASS()
class MARINEDEMOLITION_API AMD_PlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	// 콘솔을 통째로 넘겨 서버가 자기 사본 Sites[SiteIndex]에서 레벨을 읽음 (클라 값 신뢰 안 함)
	UFUNCTION(Server, Reliable)
	void ServerRequestTravel(AMD_TravelConsole* Console, int32 SiteIndex, EMD_Difficulty Difficulty);

	// 모선 하루 마감 콘솔 요청. 서버가 거리 검증 후 GameMode 마감 실행
	UFUNCTION(Server, Reliable)
	void ServerRequestEndDay(AMD_DayEndConsole* Console);


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

	/** 표준 난이도 착수금 (07 §2.1) */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Debug")
	int32 DebugAdvancePayment = 500;

	UFUNCTION(Exec)
	void MD_BeginContract();

	UFUNCTION(Exec)
	void MD_DumpQuests();

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	TSubclassOf<UMD_HUDWidget> HUDWidgetClass;

	UPROPERTY()
	TObjectPtr<UMD_HUDWidget> HUDWidgetInstance;

	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	TSubclassOf<UMD_EquipHotbarWidget> EquipHotbarWidgetClass;

	UPROPERTY()
	TObjectPtr<UMD_EquipHotbarWidget> EquipHotbarWidgetInstance;

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

	public:
	
	/** 상인 UI에서 반복 퀘스트를 수락할 때 호출한다. */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "MD|Merchant")
	void ServerAcceptRepeat(int32 Index);
	
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "MD|Merchant")
	void ServerDeclineRepeat(int32 Index);
};
