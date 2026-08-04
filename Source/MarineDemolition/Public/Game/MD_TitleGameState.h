// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "MD_TitleGameState.generated.h"

/**
* 
*/
UCLASS()
class MARINEDEMOLITION_API AMD_TitleGameState : public AGameStateBase
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;

protected:
	// Blueprint UI가 슬롯 순서를 유지한 상태로 로드 결과를 조회할 수 있게 보관한다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Save GameSession")
	TArray<TObjectPtr<class UMD_SaveGameSession>> SaveSlots;

};
