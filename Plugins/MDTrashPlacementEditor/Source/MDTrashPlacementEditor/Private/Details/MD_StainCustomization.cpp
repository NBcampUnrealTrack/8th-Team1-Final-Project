#include "Details/MD_StainCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Tools/MD_StainRefit.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "World/Contamination/MD_SurfaceStain.h"

#define LOCTEXT_NAMESPACE "MDStainCustomization"

TSharedRef<IDetailCustomization> FMD_StainCustomization::MakeInstance()
{ return MakeShared<FMD_StainCustomization>(); }

void FMD_StainCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	const TWeakObjectPtr<AMD_SurfaceStain> Stain = Objects.Num() == 1 ? Cast<AMD_SurfaceStain>(Objects[0].Get()) : nullptr;
	auto& Category = DetailBuilder.EditCategory("MD|Stain|Surface Fit");
	Category.AddCustomRow(LOCTEXT("Search", "Refit to Surface"))
	.WholeRowContent()
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("Help",
				"오염물 하나를 선택하고 위치·회전·Initial Size를 조절한 뒤 Refit to Surface를 누르세요. 파란 Z축이 표면 밖을 향해야 합니다.\n예전 배치에 원본 정보가 없으면 Refit Source를 한 번 지정하세요."))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton).Text(LOCTEXT("Refit", "Refit to Surface"))
				.IsEnabled_Lambda([Stain] { return Stain.IsValid() && !FMD_StainRefit::Get().IsRunning(); })
				.OnClicked_Lambda([Stain] { FMD_StainRefit::Get().Start(Stain.Get()); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0)
			[
				SNew(SButton).Text(LOCTEXT("Cancel", "Cancel"))
				.IsEnabled_Lambda([] { return FMD_StainRefit::Get().IsRunning(); })
				.OnClicked_Lambda([] { FMD_StainRefit::Get().Cancel(); return FReply::Handled(); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(STextBlock).AutoWrapText(true).Text_Lambda([] { return FMD_StainRefit::Get().GetStatus(); })
		]
	];
}

#undef LOCTEXT_NAMESPACE
