// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MD_PlayerController.generated.h"

class AMD_TravelConsole;
class AMD_DayEndConsole;
class AMD_SectorCloseoutMarker;
class UMD_HUDWidget;
class UMD_EquipHotbarWidget;
class UMD_SiteDefinition;
class UGameplayEffect;

UCLASS()
class MARINEDEMOLITION_API AMD_PlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	// 콘솔을 통째로 넘겨 서버가 자기 사본에서 TargetLevel을 읽음 (클라 값 신뢰 안 함)
	UFUNCTION(Server, Reliable)
	void ServerRequestTravel(AMD_TravelConsole* Console);

	// 모선 하루 마감 콘솔 요청. 서버가 거리 검증 후 GameMode 마감 실행
	UFUNCTION(Server, Reliable)
	void ServerRequestEndDay(AMD_DayEndConsole* Console);

	// 구역 마감 제출 요청. 서버가 거리 검증 후 마커 마감 실행
	UFUNCTION(Server, Reliable)
	void ServerRequestSectorCloseout(AMD_SectorCloseoutMarker* Marker);


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
};
