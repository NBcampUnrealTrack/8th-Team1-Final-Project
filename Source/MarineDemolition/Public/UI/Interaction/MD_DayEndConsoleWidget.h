// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MD_DayEndConsoleWidget.generated.h"

class UButton;
class AMD_DayEndConsole;

UCLASS()
class MARINEDEMOLITION_API UMD_DayEndConsoleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetOwningConsole(AMD_DayEndConsole* InConsole) { OwningConsole = InConsole; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UFUNCTION()
	void HandleEndDayClicked();

	UFUNCTION()
	void HandleCloseClicked();

	void CloseWidget();

	// WBP에서 같은 이름의 버튼과 자동 바인딩
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> EndDayButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> CloseButton;

	UPROPERTY()
	TObjectPtr<AMD_DayEndConsole> OwningConsole;
};
