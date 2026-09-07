// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/MD_CommonButtonBase.h"
#include "Audio/MD_AudioSubsystem.h"
#include "CommonTextBlock.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"

UMD_CommonButtonBase::UMD_CommonButtonBase()
{
	// 클릭음은 모든 버튼 공통이라 기본값을 넣어 배치 즉시 동작하게 한다.
	ClickSoundTag = FGameplayTag::RequestGameplayTag(
		TEXT("SFX.UI.Click"), /*ErrorIfNotFound*/ false);

	// Hover 음은 기본값을 넣지 않는다. 타이틀 화면 전용이라 그 버튼 인스턴스에서만 지정한다.
}

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
			// 스타일의 폰트 크기를 제외할 때 복원할 현재 TextBlock 폰트 정보를 저장한다.
			FSlateFontInfo CurrentFont = CommonTextBlock->GetFont();

			// 현재 버튼 상태의 CommonUI 텍스트 스타일을 모든 폰트 속성과 함께 적용한다.
			CommonTextBlock->SetStyle(GetCurrentTextStyleClass());

			// 스타일 폰트 크기를 사용하지 않으면 TextBlock에 지정된 기존 크기를 복원한다.
			if (bApplyTextStyleFontSize == false)
			{
				// 스타일의 나머지 폰트 속성은 유지하면서 기존 크기만 교체한다.
				FSlateFontInfo AppliedFont = CommonTextBlock->GetFont();
				AppliedFont.Size = CurrentFont.Size;
				CommonTextBlock->SetFont(AppliedFont);
			}
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

void UMD_CommonButtonBase::NativeOnHovered()
{
	Super::NativeOnHovered();

	// 마우스 Hover와 포커스 획득이 같은 경로로 들어오므로 이미 울린 상태에서는 넘긴다.
	if (bHoverSoundPlaying == true)
	{
		return;
	}

	// 다음 UnHover 까지 Hover 음을 다시 내지 않도록 표시한다.
	bHoverSoundPlaying = true;

	PlayButtonSound(HoverSoundTag);
}

void UMD_CommonButtonBase::NativeOnUnhovered()
{
	Super::NativeOnUnhovered();

	// 다시 올려놓으면 Hover 음이 울려야 하므로 표시를 지운다.
	bHoverSoundPlaying = false;
}

void UMD_CommonButtonBase::NativeOnClicked()
{
	Super::NativeOnClicked();

	PlayButtonSound(ClickSoundTag);
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

	// 스타일의 폰트 크기를 제외할 때 사용할 현재 TextBlock 폰트 정보를 저장한다.
	FSlateFontInfo CurrentFont = TextBlock->GetFont();

	// 일반 TextBlock에 적용할 TextStyle의 전체 폰트 정보를 준비한다.
	FSlateFontInfo AppliedFont = TextStyle->Font;

	// 스타일 폰트 크기를 사용하지 않으면 TextBlock에 지정된 기존 크기로 교체한다.
	if (bApplyTextStyleFontSize == false)
	{
		// 다른 스타일 속성은 유지하고 폰트 크기만 기존 값으로 보존한다.
		AppliedFont.Size = CurrentFont.Size;
	}

	// 선택한 폰트 크기 정책이 반영된 현재 스타일의 글꼴을 적용한다.
	TextBlock->SetFont(AppliedFont);

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

void UMD_CommonButtonBase::PlayButtonSound(const FGameplayTag& EventTag) const
{
	// 태그를 비워 둔 버튼은 소리를 내지 않는다.
	if (EventTag.IsValid() == false)
	{
		return;
	}

	// 에디터 미리보기에서 마우스를 올릴 때 소리가 나지 않게 한다.
	if (IsDesignTime() == true)
	{
		return;
	}

	// 소리 재생은 태그만 넘기고 소재 선택은 데이터가 맡는다.
	const UWorld* World = GetWorld();
	UMD_AudioSubsystem* AudioSubsystem = World ? World->GetSubsystem<UMD_AudioSubsystem>() : nullptr;
	if (IsValid(AudioSubsystem) == true)
	{
		AudioSubsystem->PlayEvent2D(EventTag);
	}
}
