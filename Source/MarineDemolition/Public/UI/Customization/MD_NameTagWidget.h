// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MD_NameTagWidget.generated.h"

class UTextBlock;

/**
 * 
 */
UCLASS()
class MARINEDEMOLITION_API UMD_NameTagWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativePreConstruct() override;
	
protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = GetTextContent, BlueprintSetter = SetTextContent, Category = "Appearance")
	FText TextContent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_NameTag;

public:
	UFUNCTION(BlueprintPure)
	FORCEINLINE FText GetTextContent() const { return TextContent; }

	UFUNCTION(BlueprintCallable)
	void SetTextContent(const FText& InText);
};
