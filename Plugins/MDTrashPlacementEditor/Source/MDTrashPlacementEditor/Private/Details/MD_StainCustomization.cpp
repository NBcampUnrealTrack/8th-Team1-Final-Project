#include "Details/MD_StainCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "Gameplay/Contamination/MD_StainSurfaceFit.h"
#include "Gameplay/Contamination/MD_SurfaceStainSize.h"
#include "Gameplay/Contamination/MD_SurfaceStainDefinition.h"
#include "LevelUtils.h"
#include "Tools/MD_StainRefit.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "World/Contamination/MD_SurfaceStain.h"

#define LOCTEXT_NAMESPACE "MDStainCustomization"

TSharedRef<IDetailCustomization> FMD_StainCustomization::MakeInstance()
{ return MakeShared<FMD_StainCustomization>(); }

static FText DimensionsText(const FVector& Dimensions)
{
	FNumberFormattingOptions Format;
	Format.SetMaximumFractionalDigits(2);
	return FText::Format(LOCTEXT("Dimensions", "{0} × {1} × {2} cm"),
		FText::AsNumber(Dimensions.X, &Format), FText::AsNumber(Dimensions.Y, &Format), FText::AsNumber(Dimensions.Z, &Format));
}

bool FMD_StainCustomization::CanEdit() const
{
	const auto* Actor = Stain.Get();
	return IsValid(Actor) && !Actor->IsActorBeingDestroyed() && !Actor->IsTemplate()
		&& Actor->UsesSharedVolume()
		&& GEditor && !GEditor->PlayWorld && Actor->GetWorld()
		&& Actor->GetWorld() == GEditor->GetEditorWorldContext().World()
		&& Actor->GetWorld()->WorldType == EWorldType::Editor && Actor->GetLevel()
		&& !FLevelUtils::IsLevelLocked(Actor->GetLevel())
		&& Actor->GetActorScale3D().Equals(FVector::OneVector, 0.001)
		&& GEditor->GetSelectedActors() && GEditor->GetSelectedActors()->Num() == 1
		&& GEditor->GetSelectedActors()->IsSelected(Actor);
}

void FMD_StainCustomization::RefreshSizing() const
{
	const auto* Actor = Stain.Get();
	if (!Actor) return;
	const auto Reference = Actor->GetRefitSource();
	const bool bSourceChanged = SourceReference != Reference || Reference.Get() != Source.Get();
	if (bSourceChanged)
	{
		SourceReference = Reference;
		Source.Reset(Reference.LoadSynchronous());
		SourceRevision = MAX_uint32;
	}
	const auto* Original = Source.Get();
	const uint32 Revision = Original ? Original->GetAuthoringRevision() : 0;
	const bool bRevisionChanged = Revision != SourceRevision;
	if (bSourceChanged || bRevisionChanged || CurrentSize == INDEX_NONE)
	{
		SourceRevision = Revision;
		SizingError.Reset();
		bHasDimensions = Original && Original->IsAsset()
			&& FMD_StainSurfaceFit::GetSourceDimensions(*Original, OriginalDimensions)
			&& FMD_StainSurfaceFit::GetAllowedWidthRange(*Original, true, AllowedWidth, SizingError);
		if (!bHasDimensions && SizingError.IsEmpty())
			SizingError = TEXT("Refit Source에 배치에 사용한 원본 DA_Slime 에셋을 지정하세요.");
	}
	const auto* Current = Actor->GetDefinition();
	const uint32 CurrentRevision = Current ? Current->GetAuthoringRevision() : 0;
	if (bSourceChanged || bRevisionChanged || CurrentSize != Actor->GetInitialSize()
		|| ActiveDefinition.Get() != Current || ActiveRevision != CurrentRevision)
	{
		CurrentSize = Actor->GetInitialSize();
		ActiveDefinition = Actor->GetDefinition();
		ActiveRevision = CurrentRevision;
		CurrentDimensions = FVector::ZeroVector;
		if (Current && Current->UsesSharedVolume())
			if (const auto* Stage = Current->GetVolumeStage(CurrentSize, CurrentSize))
				CurrentDimensions = Stage->QueryBounds.GetSize() * Current->GetVolumeScale(CurrentSize);
		FVector Nominal;
		DraftWidth = bHasDimensions && FMD_StainSurfaceFit::GetSizeDimensions(*Original, CurrentSize, Nominal)
			? TOptional<double>(OriginalDimensions.X * FMath::Sqrt(double(CurrentSize) / FMD_SurfaceStainSize::Reference))
			: TOptional<double>();
	}
}

bool FMD_StainCustomization::ResolveDraft(int32& OutSize, FVector& OutDimensions, FString& Error) const
{
	RefreshSizing();
	if (!bHasDimensions || !Source.IsValid() || !DraftWidth.IsSet())
	{
		Error = SizingError;
		return false;
	}
	FVector2D Range;
	return FMD_StainSurfaceFit::ResolveWidth(*Source.Get(), DraftWidth.GetValue(), true,
		OutSize, OutDimensions, Range, Error);
}

FText FMD_StainCustomization::GetDimensionsText() const
{
	RefreshSizing();
	if (!Stain.IsValid()) return LOCTEXT("SelectOne", "오염물 하나를 선택하세요.");
	if (!bHasDimensions) return FText::FromString(SizingError);
	FNumberFormattingOptions Format;
	Format.SetMaximumFractionalDigits(2);
	FText Result = FText::Format(LOCTEXT("Sizing", "원본 300 기준 가로 × 세로 × 두께: {0}\n현재 배치축 X × Y × Z 범위: {1}\n입력 가능한 가로 폭: {2}–{3} cm · 현재 오염량: {4}"),
		DimensionsText(OriginalDimensions), DimensionsText(CurrentDimensions),
		FText::AsNumber(FMath::CeilToDouble(AllowedWidth.X * 100.0) / 100.0, &Format),
		FText::AsNumber(FMath::FloorToDouble(AllowedWidth.Y * 100.0) / 100.0, &Format), FText::AsNumber(CurrentSize));
	if (CurrentSize > FMD_SurfaceStainSize::MaximumPlacement)
		Result = FText::Format(LOCTEXT("LegacySize", "{0}\n기존 10,000 초과 오염량은 그대로 표시합니다. 새 폭을 적용할 때만 현재 허용 범위를 사용합니다."), Result);
	return Result;
}

FText FMD_StainCustomization::GetDraftText() const
{
	int32 Size = 0; FVector Dimensions; FString Error;
	if (!ResolveDraft(Size, Dimensions, Error)) return FText::FromString(Error);
	return FText::Format(LOCTEXT("Draft", "적용 예상 가로 × 세로 × 두께: {0} · 오염량: {1}\n원본 비율은 유지합니다. 정수 오염량으로 환산한 폭이며, 감싼 뒤의 배치축 범위는 달라질 수 있습니다."),
		DimensionsText(Dimensions), FText::AsNumber(Size));
}

FReply FMD_StainCustomization::RefitDraft()
{
	int32 Size = 0; FVector Dimensions; FString Error;
	if (CanEdit() && ResolveDraft(Size, Dimensions, Error)) FMD_StainRefit::Get().Start(Stain.Get(), Size, true);
	return FReply::Handled();
}

void FMD_StainCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	Stain = Objects.Num() == 1 ? Cast<AMD_SurfaceStain>(Objects[0].Get()) : nullptr;
	SourceReference.Reset(); Source.Reset(); ActiveDefinition.Reset();
	SourceRevision = ActiveRevision = MAX_uint32; CurrentSize = INDEX_NONE;
	DraftWidth.Reset(); bHasDimensions = false;
	RefreshSizing();
	const TWeakPtr<FMD_StainCustomization> WeakThis = StaticCastSharedRef<FMD_StainCustomization>(AsShared());
	auto& Category = DetailBuilder.EditCategory("MD|Stain|Surface Fit");
	Category.AddCustomRow(LOCTEXT("Search", "Refit to Surface"))
	.WholeRowContent()
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("Help",
				"오염물 하나를 선택하고 위치·회전을 조절하세요. 파란 Z축이 표면 밖을 향해야 하며 Transform Scale은 1을 유지합니다.\n가로 폭은 원형을 펼쳤을 때의 X축 기준입니다. cm 입력은 임시값이며 Refit 성공 시 크기와 형상을 함께 적용합니다. 취소·실패·선택 변경 시 기존 결과를 유지합니다.\n예전 배치에 원본 정보가 없으면 Refit Source를 한 번 지정하세요."))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(STextBlock).AutoWrapText(true).Text_Lambda([WeakThis]
			{ const auto Self = WeakThis.Pin(); return Self ? Self->GetDimensionsText() : FText::GetEmpty(); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
			[ SNew(STextBlock).Text(LOCTEXT("Width", "적용할 원형 가로 폭 (cm)")) ]
			+ SHorizontalBox::Slot().FillWidth(1)
			[
				SNew(SNumericEntryBox<double>).AllowSpin(false).MinDesiredValueWidth(100).MaxFractionalDigits(2)
				.MinValue(TOptional<double>()).MaxValue(TOptional<double>())
				.IsEnabled_Lambda([WeakThis]
				{ const auto Self = WeakThis.Pin(); if (!Self) return false; Self->RefreshSizing(); return Self->CanEdit() && Self->bHasDimensions && !FMD_StainRefit::Get().IsRunning(); })
				.Value_Lambda([WeakThis]
				{ const auto Self = WeakThis.Pin(); if (!Self) return TOptional<double>(); Self->RefreshSizing(); return Self->DraftWidth; })
				.OnValueCommitted_Lambda([WeakThis](double Value, ETextCommit::Type)
				{ if (const auto Self = WeakThis.Pin()) { Self->RefreshSizing(); Self->DraftWidth = Value; } })
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(STextBlock).AutoWrapText(true).Text_Lambda([WeakThis]
			{ const auto Self = WeakThis.Pin(); return Self ? Self->GetDraftText() : FText::GetEmpty(); })
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton).Text(LOCTEXT("Refit", "Refit to Surface"))
				.IsEnabled_Lambda([WeakThis]
				{ const auto Self = WeakThis.Pin(); int32 Size = 0; FVector Dimensions; FString Error; return Self && Self->CanEdit() && !FMD_StainRefit::Get().IsRunning() && Self->ResolveDraft(Size, Dimensions, Error); })
				.OnClicked_Lambda([WeakThis]
				{ const auto Self = WeakThis.Pin(); return Self ? Self->RefitDraft() : FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0)
			[
				SNew(SButton).Text(LOCTEXT("Cancel", "Cancel"))
				.IsEnabled_Lambda([WeakThis]
				{ const auto Self = WeakThis.Pin(); return Self && FMD_StainRefit::Get().IsRunningFor(Self->Stain.Get()); })
				.OnClicked_Lambda([WeakThis]
				{ if (const auto Self = WeakThis.Pin()) if (FMD_StainRefit::Get().IsRunningFor(Self->Stain.Get())) FMD_StainRefit::Get().Cancel(); return FReply::Handled(); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(STextBlock).AutoWrapText(true).Text_Lambda([] { return FMD_StainRefit::Get().GetStatus(); })
		]
	];
}

#undef LOCTEXT_NAMESPACE
