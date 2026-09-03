#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MD_InteractionPromptWidget.generated.h"

class APawn;
class UMD_InteractionComponent;
class UTextBlock;

/** 일반 E 상호작용과 현재 장착 도구의 GA 조준 안내를 같은 HUD 위치에 표시한다. */
UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_InteractionPromptWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	UFUNCTION()
	void HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn);

	UFUNCTION()
	void HandleInteractionPromptChanged(FText PromptText);

	void BindToPawn(APawn* NewPawn);
	void UnbindInteractionComponent();
	void ApplyPromptText(const FText& PromptText);
	void SetInfoText(const FText& NewText);
	void SetActionText(const FText& NewKeyText, const FText& NewActionText, bool bVisible);

	/** 기존 WBP_HUD_Interact 디자이너의 대상 이름 본문. */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Info;

	/** 본문과 같은 글자를 표시하는 그림자. */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Info_Shadow;

	/** 검은 행동 상자 안의 입력 키. 대괄호는 별도 TextBlock이다. */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Key;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Key_Shadow;

	/** 검은 행동 상자 안의 행동 이름. */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Action;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Action_Shadow;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Sign_Left;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Sign_Left_Shadow;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Sign_Right;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Sign_Right_Shadow;

	UPROPERTY(Transient)
	TObjectPtr<UMD_InteractionComponent> BoundInteractionComponent;
};
