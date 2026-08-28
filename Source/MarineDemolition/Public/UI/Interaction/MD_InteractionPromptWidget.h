#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MD_InteractionPromptWidget.generated.h"

class AActor;
class APawn;
class UMD_InteractionComponent;
class UTextBlock;

/** 현재 조준 중인 상호작용 대상의 데이터 기반 이름을 표시한다. */
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
	void HandleInteractionTargetChanged(AActor* NewTarget);

	void BindToPawn(APawn* NewPawn);
	void UnbindInteractionComponent();
	void SetInfoText(const FText& NewText);

	/** 기존 WBP_HUD_Interact 디자이너의 대상 이름 본문. */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Info;

	/** 본문과 같은 글자를 표시하는 그림자. */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Info_Shadow;

	UPROPERTY(Transient)
	TObjectPtr<UMD_InteractionComponent> BoundInteractionComponent;
};
