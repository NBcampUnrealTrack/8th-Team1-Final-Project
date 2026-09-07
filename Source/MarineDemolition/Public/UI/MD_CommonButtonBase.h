// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "CommonButtonBase.h"
#include "GameplayTagContainer.h"
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
	UMD_CommonButtonBase();

	virtual void NativePreConstruct() override;

	virtual void NativeOnCurrentTextStyleChanged() override;

	// 부모와 같은 public 접근을 유지해야 한다. protected 로 좁히면 외부 호출자가 전부 막힌다.
	virtual void SetIsEnabled(bool bInIsEnabled) override;

	virtual void SetVisibility(ESlateVisibility InVisibility) override;

protected:
	virtual void HandleFocusReceived() override;

	virtual void HandleFocusLost() override;

	virtual void NativeOnHovered() override;

	virtual void NativeOnUnhovered() override;

	virtual void NativeOnClicked() override;

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

	/**
	 * 마우스 Hover와 포커스 획득에 1회. 사운드 에셋이 아니라 태그다 — 어떤 소재가 나갈지는
	 * DA_MD_Audio_UI 가 정한다.
	 *
	 * 기본은 비어 있다(무음). Hover 음은 타이틀 화면 전용이라 필요한 버튼에만 넣는다.
	 * 타이틀과 일시정지가 같은 WBP_MenuButton 을 쓰므로 클래스 기본값으로는 갈릴 수 없어,
	 * 위젯 트리의 버튼 인스턴스에서 지정한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|UI|사운드",
		meta = (Categories = "SFX.UI"))
	FGameplayTag HoverSoundTag;

	/** 클릭이 확정될 때 1회. 모든 버튼 공통이며, 비우면 이 버튼은 Click 음을 내지 않는다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|UI|사운드",
		meta = (Categories = "SFX.UI"))
	FGameplayTag ClickSoundTag;

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

private:
	/** 태그가 유효하고 디자인 타임이 아닐 때만 오디오 서브시스템으로 2D 재생한다. */
	void PlayButtonSound(const FGameplayTag& EventTag) const;

	/**
	 * Hover 음이 이미 울린 상태.
	 * 마우스 Hover와 포커스 획득이 둘 다 NativeOnHovered 로 들어와서, 이 표시가 없으면
	 * 버튼을 클릭할 때 Hover 음이 두 번 난다 (HandleFocusReceived 가 다시 부른다).
	 *
	 * NativeOnUnhovered 로만 지우면 실제 Hover 상태와 어긋날 수 있다. 마우스가 올라간 채로
	 * 버튼이 비활성화되거나 숨겨지면 언호버가 오지 않아 표시가 남고, 그러면 다시 켠 뒤에
	 * Hover 음이 영구히 죽는다. 그래서 SetIsEnabled / SetVisibility 에서도 지운다.
	 */
	bool bHoverSoundPlaying = false;
};
