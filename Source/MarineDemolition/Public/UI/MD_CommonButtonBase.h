// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "CommonButtonBase.h"
#include "Styling/SlateBrush.h"
#include "MD_CommonButtonBase.generated.h"

class UImage;
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
	virtual void HandleFocusReceived() override;

	virtual void HandleFocusLost() override;

protected:
	// PreConstruct에서 버튼 Content로 적용할 편집 가능한 텍스트를 보관한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FText Text_ButtonText;

	// Common TextBlock 또는 TextBlock
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MD|UI")
	TObjectPtr<UTextBlock> Txt_ButtonText;

	// 활성화하면 버튼 상태별 TextStyle에 정의된 폰트 크기도 TextBlock에 적용한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	bool bApplyTextStyleFontSize = false;

	// Blueprint에서 값이 변경되면 전용 Getter와 Setter를 통해 Image에 반영할 Brush를 보관한다.
	UPROPERTY(EditAnywhere, BlueprintGetter = GetContentImage, BlueprintSetter = SetContentImage, Category = "Appearance")
	FSlateBrush Brush_ButtonImage;

	// 버튼 Blueprint의 선택적인 Image 위젯과 연결된다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MD|UI")
	TObjectPtr<UImage> Img_ButtonImage;

public:
	// 버튼 TextBlock에 표시된 Content를 반환한다.
	UFUNCTION(BlueprintPure, Category = "MD|UI")
	FORCEINLINE FText GetContentText() { return Text_ButtonText; }

	// 버튼 TextBlock에 표시할 Content를 설정한다.
	UFUNCTION(BlueprintCallable, Category = "MD|UI")
	void SetContentText(FText InContentText);

	// 버튼 Image에 표시된 Brush를 반환한다.
	UFUNCTION(BlueprintPure, Category = "MD|UI")
	FORCEINLINE FSlateBrush GetContentImage() { return Brush_ButtonImage; }

	// 버튼 Image에 표시할 Brush를 설정한다.
	UFUNCTION(BlueprintCallable, Category = "MD|UI")
	void SetContentImage(FSlateBrush InContentImage);

	// 버튼 Image Brush의 색상을 설정한다.
	UFUNCTION(BlueprintCallable, Category = "MD|UI")
	void SetContentImageColor(FLinearColor InContentImageColor);

	// 버튼 Image Brush의 크기를 설정한다.
	UFUNCTION(BlueprintCallable, Category = "MD|UI")
	void SetContentImageSize(FVector2D InContentImageSize);

	// 버튼 Image Brush가 표시할 리소스를 설정한다.
	UFUNCTION(BlueprintCallable, Category = "MD|UI")
	void SetContentImageResourceObject(UObject* InContentImageResourceObject);

protected:
	UFUNCTION(BlueprintCallable)
	void ApplyCommonTextStyleToTextBlock(UCommonTextStyle* TextStyle, UTextBlock* TextBlock);

	// 저장된 Image Brush를 선택적으로 바인딩된 Image에 적용한다.
	void ApplyContentImageBrush();
};
