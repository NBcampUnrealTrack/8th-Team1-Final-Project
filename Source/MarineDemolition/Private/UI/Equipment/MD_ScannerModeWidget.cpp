#include "UI/Equipment/MD_ScannerModeWidget.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<SWidget> UMD_ScannerModeWidget::RebuildWidget()
{
	return SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(FMargin(0,0,0,145))
		[
			SAssignNew(Label, STextBlock)
				.Text(ModeText)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 16))
				.ColorAndOpacity(FLinearColor::White)
				.ShadowColorAndOpacity(FLinearColor::Black)
				.ShadowOffset(FVector2D(1,1))
		];
}

void UMD_ScannerModeWidget::SetModeText(const FText& Mode)
{
	ModeText = FText::Format(NSLOCTEXT("MDScanner", "ModeIndicator", "스캐너 · {0}"), Mode);
	if (Label.IsValid()) Label->SetText(ModeText);
}

void UMD_ScannerModeWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	Label.Reset();
}
