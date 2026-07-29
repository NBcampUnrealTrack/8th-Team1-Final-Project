// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MD_Interactable.h"
#include "GameFramework/Actor.h"
#include "MD_TravelConsole.generated.h"

class UMD_TravelConsoleWidget;

UCLASS()
class MARINEDEMOLITION_API AMD_TravelConsole : public AActor, public IMD_Interactable
{
	GENERATED_BODY()

public:
	AMD_TravelConsole();

	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;

	const TSoftObjectPtr<UWorld>& GetTargetLevel() const {return TargetLevel; }
	
protected:
	UPROPERTY(VisibleAnywhere, Category="Console")
	TObjectPtr<UStaticMeshComponent> Mesh;
	
	// 출발 시 이동할 레벨
	UPROPERTY(EditAnywhere, Category="Console")
	TSoftObjectPtr<UWorld> TargetLevel;
	
	UPROPERTY(EditDefaultsOnly, Category="Console")
	TSubclassOf<UMD_TravelConsoleWidget> ConsoleWidgetClass;
};
