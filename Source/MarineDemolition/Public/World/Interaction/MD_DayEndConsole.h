// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "MD_DayEndConsole.generated.h"

class UCommonActivatableWidget;

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

	// UI Manager에 등록된 화면 클래스와 부모 레이어를 선택할 BP 설정 태그
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Console", meta=(Categories="UI.Layer"))
	FGameplayTag ConsoleWidgetTag;

	// 열려 있는 위젯 (중복 생성 방지용)
	TWeakObjectPtr<UCommonActivatableWidget> ActiveWidget;
public:
    // 서버가 콘솔 BP에 설정된 보고서 태그를 조회하는 진입점
    FORCEINLINE FGameplayTag GetConsoleWidgetTag() { return ConsoleWidgetTag; }
};
