// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/MD_CommonButtonBase.h"
#include "CommonTextBlock.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"

void UMD_CommonButtonBase::NativePreConstruct()
{
	// 부모 클래스의 디자인 타임과 런타임 사전 구성을 먼저 수행한다.
	Super::NativePreConstruct();

	// 편집한 Text_ButtonText 값을 바인딩된 버튼 TextBlock의 Content에 반영한다.
	SetContentText(Text_ButtonText);

	// 편집한 Brush_ButtonImage 값을 바인딩된 버튼 Image의 Brush에 반영한다.
	SetContentImage(Brush_ButtonImage);
}

void UMD_CommonButtonBase::NativeOnCurrentTextStyleChanged()
{
	Super::NativeOnCurrentTextStyleChanged();

	if (IsValid(Txt_ButtonText) == true)
	{
		UCommonTextBlock* CommonTextBlock = Cast<UCommonTextBlock>(Txt_ButtonText);
		if (IsValid(CommonTextBlock) == true)
		{
			CommonTextBlock->SetStyle(GetCurrentTextStyleClass());
		}
		else
		{
			// 버튼 상태에 해당하는 CommonUI 텍스트 스타일을 가져온다.
			UCommonTextStyle* TextStyle = GetCurrentTextStyle();
			ApplyCommonTextStyleToTextBlock(TextStyle, Txt_ButtonText);
		}
	}
}

void UMD_CommonButtonBase::HandleFocusReceived()
{
	Super::HandleFocusReceived();

	// 키보드나 게임패드 포커스도 마우스 Hover와 같은 이벤트 경로로 전달한다.
	NativeOnHovered();
}

void UMD_CommonButtonBase::HandleFocusLost()
{
	Super::HandleFocusLost();

	// 키보드나 게임패드 포커스도 마우스 UnHover와 같은 이벤트 경로로 전달한다.
	NativeOnUnhovered();
}

void UMD_CommonButtonBase::SetContentText(FText InContentText)
{
	// PreConstruct와 이후 TextBlock 재구성에서도 유지할 Content 값을 갱신한다.
	Text_ButtonText = InContentText;

	// TextBlock이 바인딩되지 않은 버튼은 Content를 변경하지 않는다.
	if (IsValid(Txt_ButtonText) == false)
	{
		return;
	}

	// 버튼에 표시되는 TextBlock의 Content를 갱신한다.
	Txt_ButtonText->SetText(InContentText);
}

void UMD_CommonButtonBase::SetContentImage(FSlateBrush InContentImage)
{
	// PreConstruct와 이후 Image 재구성에서도 유지할 Brush 값을 갱신한다.
	Brush_ButtonImage = InContentImage;

	// 갱신한 Brush를 바인딩된 Image에 적용한다.
	ApplyContentImageBrush();
}

void UMD_CommonButtonBase::SetContentImageColor(FLinearColor InContentImageColor)
{
	// PreConstruct와 이후 Image 재구성에서도 유지하도록 Brush의 색상을 갱신한다.
	Brush_ButtonImage.TintColor = FSlateColor(InContentImageColor);

	// 갱신한 Brush를 바인딩된 Image에 적용한다.
	ApplyContentImageBrush();
}

void UMD_CommonButtonBase::SetContentImageSize(FVector2D InContentImageSize)
{
	// Brush가 레이아웃에 제공할 이미지 크기를 갱신한다.
	Brush_ButtonImage.SetImageSize(InContentImageSize);

	// 갱신한 Brush를 바인딩된 Image에 적용한다.
	ApplyContentImageBrush();
}

void UMD_CommonButtonBase::SetContentImageResourceObject(UObject* InContentImageResourceObject)
{
	// Texture 또는 Material과 같은 Brush 리소스를 갱신한다.
	Brush_ButtonImage.SetResourceObject(InContentImageResourceObject);

	// 갱신한 Brush를 바인딩된 Image에 적용한다.
	ApplyContentImageBrush();
}

void UMD_CommonButtonBase::ApplyCommonTextStyleToTextBlock(UCommonTextStyle* TextStyle, UTextBlock* TextBlock)
{
	// CommonUI 텍스트 스타일의 유효성을 확인한다.
	// 스타일을 적용할 TextBlock의 유효성을 확인한다.
	if (IsValid(TextStyle) == false || IsValid(TextBlock) == false)
	{
		return;
	}

	// 일반 TextBlock에도 현재 스타일의 글꼴을 적용한다.
	TextBlock->SetFont(TextStyle->Font);

	// 일반 TextBlock에도 현재 스타일의 글자 색상을 적용한다.
	TextBlock->SetColorAndOpacity(TextStyle->Color);

	// 일반 TextBlock에도 현재 스타일의 내부 여백을 적용한다.
	TextBlock->SetMargin(TextStyle->Margin);

	// 일반 TextBlock에도 현재 스타일의 취소선 브러시를 적용한다.
	TextBlock->SetStrikeBrush(TextStyle->StrikeBrush);

	// 일반 TextBlock에도 현재 스타일의 행 높이 설정을 적용한다.
	TextBlock->SetLineHeightPercentage(TextStyle->LineHeightPercentage);
	TextBlock->SetApplyLineHeightToBottomLine(TextStyle->ApplyLineHeightToBottomLine);

	// 그림자를 사용하는 스타일이면 그림자 속성을 적용한다.
	if (TextStyle->bUsesDropShadow == true)
	{
		TextBlock->SetShadowOffset(TextStyle->ShadowOffset);
		TextBlock->SetShadowColorAndOpacity(TextStyle->ShadowColor);
	}
	else
	{
		// 이전 상태의 그림자가 남지 않도록 그림자 값을 초기화한다.
		TextBlock->SetShadowOffset(FVector2D::ZeroVector);
		TextBlock->SetShadowColorAndOpacity(FLinearColor::Transparent);
	}
}

void UMD_CommonButtonBase::ApplyContentImageBrush()
{
	// Image가 바인딩되지 않은 버튼은 저장된 Brush 값만 유지한다.
	if (IsValid(Img_ButtonImage) == false)
	{
		return;
	}

	// 저장된 Brush의 모든 변경 사항을 바인딩된 Image에 적용한다.
	Img_ButtonImage->SetBrush(Brush_ButtonImage);
}
