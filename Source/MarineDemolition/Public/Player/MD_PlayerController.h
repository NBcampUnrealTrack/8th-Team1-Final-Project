// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MD_PlayerController.generated.h"

class AMD_TravelConsole;
class UMD_HUDWidget;
class UMD_EquipHotbarWidget;
class UGameplayEffect;

UCLASS()
class MARINEDEMOLITION_API AMD_PlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	// 콘솔을 통째로 넘겨 서버가 자기 사본에서 TargetLevel을 읽음 (클라 값 신뢰 안 함)
	UFUNCTION(Server, Reliable)
	void ServerRequestTravel(AMD_TravelConsole* Console);


private:

	UPROPERTY(EditDefaultsOnly, Category = "MD|Debug")
	TSubclassOf<UGameplayEffect> TestDamageEffect;

	// GE 테스트용 콘솔 커맨드. 오염수·붕괴 구현 되면 삭제
	UFUNCTION(Exec)
	void MD_ApplyTestDamage();

	//Attrivbute Health 확인용 콘솔 커맨드. UI연결되면 삭제
	UFUNCTION(Exec)
	void MD_ShowHealth();

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
