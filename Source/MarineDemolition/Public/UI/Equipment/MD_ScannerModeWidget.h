#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MD_ScannerModeWidget.generated.h"

class STextBlock;

/** Small, non-interactive mode label; no Blueprint or input asset migration required. */
UCLASS()
class MARINEDEMOLITION_API UMD_ScannerModeWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void SetModeText(const FText& Mode);
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
private:
	FText ModeText;
	TSharedPtr<STextBlock> Label;
};
