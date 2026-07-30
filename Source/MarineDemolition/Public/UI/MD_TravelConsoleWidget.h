// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MD_TravelConsoleWidget.generated.h"

class UButton;
class AMD_TravelConsole;

UCLASS()
class MARINEDEMOLITION_API UMD_TravelConsoleWidget : public UUserWidget
{
	GENERATED_BODY()
	
	
public:
	void SetOwningConsole(AMD_TravelConsole* InConsole) { OwningConsole = InConsole; }
	
protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	
	UFUNCTION()
	void HandleDepartClicked();

	UFUNCTION()
	void HandleCloseClicked();
	
	void CloseWidget();
	
	// WBP에서 같은 이름의 버튼과 자동 바인딩
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> DepartButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> CloseButton;

	UPROPERTY()
	TObjectPtr<AMD_TravelConsole> OwningConsole;
};
