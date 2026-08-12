// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "GameFramework/Actor.h"
#include "MD_DayEndConsole.generated.h"

class UMD_DayEndConsoleWidget;

// 모선 하루 마감 콘솔. 한 명이 눌러도 즉시 출발, 미복귀 인원은 마감 시 자동 판정
UCLASS()
class MARINEDEMOLITION_API AMD_DayEndConsole : public AActor, public IMD_Interactable
{
	GENERATED_BODY()

public:
	AMD_DayEndConsole();

	virtual bool RequiresServerInteraction_Implementation() const override { return false; }
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;

protected:
	UPROPERTY(VisibleAnywhere, Category="Console")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(EditDefaultsOnly, Category="Console")
	TSubclassOf<UMD_DayEndConsoleWidget> ConsoleWidgetClass;

	// 열려 있는 위젯 (중복 생성 방지용)
	TWeakObjectPtr<UMD_DayEndConsoleWidget> ActiveWidget;
};
