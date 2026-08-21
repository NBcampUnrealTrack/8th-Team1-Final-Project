// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "CommonButtonBase.h"
#include "MD_CommonButtonBase.generated.h"

class UTextBlock;

/** 프로젝트 버튼의 공통 스타일과 입력 정책을 확장하기 위한 Blueprint 기반 클래스다. */
UCLASS(Abstract, Blueprintable)
class MARINEDEMOLITION_API UMD_CommonButtonBase : public UCommonButtonBase
{
	GENERATED_BODY()

public:
	virtual void NativeOnCurrentTextStyleChanged() override;

protected:
	// Common TextBlock 또는 TextBlock
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MD|UI")
	TObjectPtr<UTextBlock> Txt_ButtonText;

protected:
	void ApplyCommonTextStyleToTextBlock(UCommonTextStyle* TextStyle, UTextBlock* TextBlock);
};
