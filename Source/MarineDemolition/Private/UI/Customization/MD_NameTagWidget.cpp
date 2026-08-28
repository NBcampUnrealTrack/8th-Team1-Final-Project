// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Customization/MD_NameTagWidget.h"
#include "Components/TextBlock.h"

void UMD_NameTagWidget::NativePreConstruct()
{
	Super::NativePreConstruct();

	if (IsValid(Txt_NameTag) == true)
	{
		Txt_NameTag->SetText(TextContent);
	}
}

void UMD_NameTagWidget::SetTextContent(const FText& InText)
{
	TextContent = InText;

	if (IsValid(Txt_NameTag) == true)
	{
		Txt_NameTag->SetText(TextContent);
	}
}
