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
	virtual void NativePreConstruct() override;

	virtual void NativeOnCurrentTextStyleChanged() override;

protected:
	// PreConstruct에서 버튼 Content로 적용할 편집 가능한 텍스트를 보관한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|UI")
	FText Text;

	// Common TextBlock 또는 TextBlock
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MD|UI")
	TObjectPtr<UTextBlock> Txt_ButtonText;

public:
	// 버튼 TextBlock에 표시된 Content를 반환한다.
	UFUNCTION(BlueprintPure, Category = "MD|UI")
	FText GetContentText();

	// 버튼 TextBlock에 표시할 Content를 설정한다.
	UFUNCTION(BlueprintCallable, Category = "MD|UI")
	void SetContentText(FText InContentText);

protected:
	void ApplyCommonTextStyleToTextBlock(UCommonTextStyle* TextStyle, UTextBlock* TextBlock);
};
