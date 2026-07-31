#include "UI/MD_CharacterCustomizationWidget.h"

#include "Customization/MD_CharacterCustomizationComponent.h"
#include "Customization/MD_CharacterCustomizationPreviewActor.h"
#include "Customization/MD_CharacterCustomizationSettings.h"
#include "Customization/MD_CharacterCustomizationSubsystem.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/Image.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Input/Reply.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInterface.h"
#include "Styling/SlateBrush.h"
#include "UObject/SoftObjectPath.h"

namespace MDCharacterCustomizationWidgetPrivate
{
	static int32 GetExpressionCanvasSize()
	{
		if (const UMD_CharacterCustomizationSettings* Settings = UMD_CharacterCustomizationSettings::Get())
		{
			return FMath::Clamp(Settings->ExpressionCanvasSize, 32, 1024);
		}
		return MDCharacterCustomization::FaceTextureSize;
	}

	static int32 GetSVPickerSize()
	{
		if (const UMD_CharacterCustomizationSettings* Settings = UMD_CharacterCustomizationSettings::Get())
		{
			return FMath::Clamp(Settings->SVPickerSize, 8, 64);
		}
		return 32;
	}

	static FLinearColor HSVToLinearColor(float Hue, float Saturation, float Value)
	{
		Hue = FMath::Fmod(Hue, 360.f);
		if (Hue < 0.f)
		{
			Hue += 360.f;
		}

		const float Chroma = Value * Saturation;
		const float ChannelMax = Chroma * (1.f - FMath::Abs(FMath::Fmod(Hue / 60.f, 2.f) - 1.f));
		const float Match = Value - Chroma;

		float Red = 0.f;
		float Green = 0.f;
		float Blue = 0.f;

		if (Hue < 60.f)
		{
			Red = Chroma;
			Green = ChannelMax;
		}
		else if (Hue < 120.f)
		{
			Red = ChannelMax;
			Green = Chroma;
		}
		else if (Hue < 180.f)
		{
			Green = Chroma;
			Blue = ChannelMax;
		}
		else if (Hue < 240.f)
		{
			Green = ChannelMax;
			Blue = Chroma;
		}
		else if (Hue < 300.f)
		{
			Red = ChannelMax;
			Blue = Chroma;
		}
		else
		{
			Red = Chroma;
			Blue = ChannelMax;
		}

		return FLinearColor(Red + Match, Green + Match, Blue + Match, 1.f);
	}

	static void LinearColorToHSV(const FLinearColor& Color, float& OutHue, float& OutSaturation, float& OutValue)
	{
		const float MaxChannel = FMath::Max3(Color.R, Color.G, Color.B);
		const float MinChannel = FMath::Min3(Color.R, Color.G, Color.B);
		const float Delta = MaxChannel - MinChannel;

		OutValue = MaxChannel;
		OutSaturation = MaxChannel <= KINDA_SMALL_NUMBER ? 0.f : Delta / MaxChannel;

		if (Delta <= KINDA_SMALL_NUMBER)
		{
			OutHue = 0.f;
			return;
		}

		if (MaxChannel == Color.R)
		{
			OutHue = 60.f * FMath::Fmod((Color.G - Color.B) / Delta, 6.f);
		}
		else if (MaxChannel == Color.G)
		{
			OutHue = 60.f * (((Color.B - Color.R) / Delta) + 2.f);
		}
		else
		{
			OutHue = 60.f * (((Color.R - Color.G) / Delta) + 4.f);
		}

		if (OutHue < 0.f)
		{
			OutHue += 360.f;
		}
	}

	static FString PartDisplayName(EMDCharacterCustomizationPart Part)
	{
		switch (Part)
		{
		case EMDCharacterCustomizationPart::Visor: return TEXT("Helmet");
		case EMDCharacterCustomizationPart::Face: return TEXT("Face Glass");
		case EMDCharacterCustomizationPart::Torso: return TEXT("Torso");
		case EMDCharacterCustomizationPart::Hands: return TEXT("Hands");
		case EMDCharacterCustomizationPart::Rim: return TEXT("Rim");
		default: return TEXT("Part");
		}
	}
}

void UMDCharacterCustomizationSwatchHandler::Initialize(
	UMD_CharacterCustomizationWidget* InWidget,
	FLinearColor InColor)
{
	OwnerWidget = InWidget;
	SwatchColor = InColor;
}

void UMDCharacterCustomizationSwatchHandler::HandleClicked()
{
	if (OwnerWidget)
	{
		// Only update the color picker field; fill applies via BTN_ApplyFill.
		OwnerWidget->SetPickerColor(SwatchColor);
	}
}

UMD_CharacterCustomizationWidget::UMD_CharacterCustomizationWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
}

void UMD_CharacterCustomizationWidget::ApplyEngineSettings()
{
	const UMD_CharacterCustomizationSettings* Settings = UMD_CharacterCustomizationSettings::Get();
	if (!Settings)
	{
		return;
	}

	PreviewSkeletalMesh = Settings->PreviewSkeletalMesh;
	PreviewMaterial = Settings->PreviewMaterial;
	PreviewRenderSize = Settings->PreviewRenderSize;
	bAutoSpawnPreviewActor = Settings->bAutoSpawnPreviewActor;
	bOrbitPreview = Settings->bOrbitPreview;
	PreviewSpawnOffset = Settings->PreviewSpawnOffset;
	ExpressionBrushColor = Settings->ExpressionBrushColor;
	ExpressionBrushThickness = Settings->ExpressionBrushThickness;
	SelectedPart = Settings->DefaultSelectedPart;
	CurrentAppearance = Settings->DefaultAppearance;
	CurrentAppearance.MigrateLegacyFieldsIfNeeded();

	// Config/LiveCoding can leave SoftObjectPtrs empty even when constructor defaults exist.
	if (PreviewSkeletalMesh.IsNull())
	{
		PreviewSkeletalMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(
			TEXT("/Game/Characters/Customization/SK_MD_TripoCharacter.SK_MD_TripoCharacter")));
	}
	if (PreviewMaterial.IsNull())
	{
		PreviewMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(
			TEXT("/Game/Characters/Customization/MI_MD_CharacterCustom.MI_MD_CharacterCustom")));
	}
}

void UMD_CharacterCustomizationWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	ApplyEngineSettings();
}

void UMD_CharacterCustomizationWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ApplyEngineSettings();

	// Do NOT create render targets / ClearRenderTarget2D here — that path fatalled D3D12 E_INVALIDARG on PIE.
	SVPickerRenderTarget = nullptr;
	ExpressionRenderTarget = nullptr;

	SetPickerColorInternal(CurrentAppearance.GetPartColor(SelectedPart), true, true, false);
	UpdateSVPickerTexture();
	BindUiCallbacks();
	BuildPresetSwatchButtons();
	RebuildCustomSwatchButtons();
	UpdatePreviewHint();
	UpdateSelectedPartLabel();
	RefreshSlotStatus();
	UpdateBrushUI();

	// Host may call InitializeCustomization later; still spawn preview when settings request it.
	if ((IMG_Preview || bAutoSpawnPreviewActor) && !PreviewActor)
	{
		EnsurePreviewActor();
	}
}

void UMD_CharacterCustomizationWidget::NativeDestruct()
{
	for (UButton* Button : PresetSwatchButtons)
	{
		if (Button)
		{
			Button->OnClicked.Clear();
		}
	}
	PresetSwatchButtons.Reset();
	SwatchHandlers.Reset();
	DestroyPreviewActor();

	Super::NativeDestruct();
}

void UMD_CharacterCustomizationWidget::NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	Super::NativeOnMouseCaptureLost(CaptureLostEvent);

	// Paint / SV-pick capture can be lost without ButtonUp (focus change, PIE pause).
	// Leaving these true blocks subsequent UI clicks from feeling right.
	const bool bWasCapturing = bIsPaintingExpression || bIsPickingSV;
	bIsPaintingExpression = false;
	bIsPickingSV = false;
	bHasLastExpressionCanvasPixel = false;
	bHasLastExpressionViewUV = false;
	if (bWasCapturing && PreviewActor)
	{
		PreviewActor->SetOrbitEnabled(bOrbitPreview);
	}
}

FReply UMD_CharacterCustomizationWidget::NativeOnMouseButtonDown(
	const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent)
{
	FVector2D SaturationValue;
	if (TryPickSVFromPointer(InMouseEvent, SaturationValue))
	{
		bIsPickingSV = true;
		PickerSaturation = SaturationValue.X;
		PickerValue = SaturationValue.Y;
		SyncPickerFromHSV();
		return FReply::Handled().CaptureMouse(TakeWidget());
	}

	// Always capture when over the face panel so the viewport camera cannot steal the drag.
	if (IsPointerOverExpressionCanvas(InMouseEvent))
	{
		bIsPaintingExpression = true;
		if (PreviewActor)
		{
			PreviewActor->SetOrbitEnabled(false);
		}

		FVector2D ViewUV;
		FVector2D CanvasPixel;
		if (TryGetExpressionPaintSample(InMouseEvent, ViewUV, CanvasPixel))
		{
			LastExpressionCanvasPixel = CanvasPixel;
			LastExpressionViewUV = ViewUV;
			bHasLastExpressionCanvasPixel = true;
			bHasLastExpressionViewUV = true;
			PaintExpressionDot(CanvasPixel, GetActiveDrawColor(), ExpressionBrushThickness * 0.5f);
			RefreshPreview();
		}
		else
		{
			bHasLastExpressionCanvasPixel = false;
			bHasLastExpressionViewUV = false;
		}

		return FReply::Handled().CaptureMouse(TakeWidget());
	}

	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UMD_CharacterCustomizationWidget::NativeOnMouseButtonUp(
	const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent)
{
	if (bIsPaintingExpression || bIsPickingSV)
	{
		bIsPaintingExpression = false;
		bIsPickingSV = false;
		bHasLastExpressionCanvasPixel = false;
		bHasLastExpressionViewUV = false;
		if (PreviewActor)
		{
			PreviewActor->SetOrbitEnabled(bOrbitPreview);
		}
		return FReply::Handled().ReleaseMouseCapture();
	}

	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

FReply UMD_CharacterCustomizationWidget::NativeOnMouseMove(
	const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent)
{
	if (bIsPickingSV)
	{
		FVector2D SaturationValue;
		if (TryPickSVFromPointer(InMouseEvent, SaturationValue))
		{
			PickerSaturation = SaturationValue.X;
			PickerValue = SaturationValue.Y;
			SyncPickerFromHSV();
		}
		return FReply::Handled();
	}

	if (bIsPaintingExpression)
	{
		FVector2D ViewUV;
		FVector2D CanvasPixel;
		if (TryGetExpressionPaintSample(InMouseEvent, ViewUV, CanvasPixel))
		{
			const int32 CanvasSize = MDCharacterCustomizationWidgetPrivate::GetExpressionCanvasSize();
			const float MaxSeamPixels = FMath::Max(14.f, ExpressionBrushThickness * 2.5f);
			const float StampRadius = ExpressionBrushThickness * 0.5f;
			const FLinearColor DrawColor = GetActiveDrawColor();
			UMD_CharacterCustomizationSubsystem* Subsystem = GetCustomizationSubsystem();
			UTextureRenderTarget2D* ExpressionRT = GetExpressionRenderTarget();

			if (bHasLastExpressionViewUV && PreviewActor && Subsystem && ExpressionRT)
			{
				// Resample in face-view space, then project each sample to mesh UV.
				// Never lerp long distances in UV/canvas space (that bridges UV cracks).
				const float ViewDist = FVector2D::Distance(LastExpressionViewUV, ViewUV);
				const int32 Steps = FMath::Clamp(
					FMath::CeilToInt(ViewDist * static_cast<float>(CanvasSize) / FMath::Max(1.5f, StampRadius * 0.75f)),
					1,
					32);

				FVector2D PrevCanvas = LastExpressionCanvasPixel;
				bool bHavePrev = bHasLastExpressionCanvasPixel;
				for (int32 Step = 1; Step <= Steps; ++Step)
				{
					const float Alpha = static_cast<float>(Step) / static_cast<float>(Steps);
					const FVector2D SampleView = FMath::Lerp(LastExpressionViewUV, ViewUV, Alpha);
					FVector2D SampleMeshUV;
					if (!PreviewActor->ProjectFaceViewToMeshUV(SampleView, SampleMeshUV))
					{
						bHavePrev = false;
						continue;
					}

					const FVector2D SampleCanvas(SampleMeshUV.X * CanvasSize, SampleMeshUV.Y * CanvasSize);
					if (bHavePrev)
					{
						Subsystem->DrawFaceStampStroke(
							ExpressionRT, PrevCanvas, SampleCanvas, DrawColor, StampRadius, MaxSeamPixels);
					}
					else
					{
						Subsystem->DrawFacePoint(ExpressionRT, SampleCanvas, DrawColor, StampRadius);
					}
					PrevCanvas = SampleCanvas;
					bHavePrev = true;
				}

				LastExpressionCanvasPixel = PrevCanvas;
				bHasLastExpressionCanvasPixel = bHavePrev;
				SyncExpressionToMeshes();
			}
			else if (bHasLastExpressionCanvasPixel)
			{
				PaintExpressionStroke(LastExpressionCanvasPixel, CanvasPixel, DrawColor, ExpressionBrushThickness);
				LastExpressionCanvasPixel = CanvasPixel;
				bHasLastExpressionCanvasPixel = true;
			}
			else
			{
				PaintExpressionDot(CanvasPixel, DrawColor, StampRadius);
				LastExpressionCanvasPixel = CanvasPixel;
				bHasLastExpressionCanvasPixel = true;
			}

			LastExpressionViewUV = ViewUV;
			bHasLastExpressionViewUV = true;
			UpdateExpressionCanvasBrush();
			RefreshPreview();
		}

		return FReply::Handled();
	}

	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

void UMD_CharacterCustomizationWidget::InitializeCustomization(UMD_CharacterCustomizationComponent* InPreviewComponent)
{
	ExternalCustomizationComponent = InPreviewComponent;

	// Always spawn / reconfigure offscreen capture when preview is wanted.
	if (IMG_Preview || bAutoSpawnPreviewActor)
	{
		EnsurePreviewActor();
	}
	else if (InPreviewComponent)
	{
		PreviewComponent = InPreviewComponent;
	}

	UpdatePreviewHint();
	RefreshSlotStatus();
	OnCustomizationInitialized();

	if (PreviewComponent)
	{
		PreviewComponent->ApplyPartColors(CurrentAppearance);
	}
	SyncAppearanceToExternalComponent();

	UpdateFacePreviewBrush();
}

UTextureRenderTarget2D* UMD_CharacterCustomizationWidget::GetExpressionRenderTarget()
{
	if (!ExpressionRenderTarget)
	{
		if (UMD_CharacterCustomizationSubsystem* Subsystem = GetCustomizationSubsystem())
		{
			ExpressionRenderTarget = Subsystem->CreateFaceRenderTarget();
			UpdateExpressionCanvasBrush();
		}
	}

	return ExpressionRenderTarget;
}

void UMD_CharacterCustomizationWidget::SelectPart(EMDCharacterCustomizationPart Part)
{
	SelectedPart = Part;
	SetPickerColorInternal(CurrentAppearance.GetPartColor(Part), true, true, true);
	UpdateSelectedPartLabel();
	OnPartSelected(Part);
}

void UMD_CharacterCustomizationWidget::SetPickerColor(FLinearColor NewColor)
{
	SetPickerColorInternal(NewColor, true, true, true);
}

void UMD_CharacterCustomizationWidget::ApplyPickerColorToSelectedPart()
{
	SetPartColor(SelectedPart, PickerColor);
	RefreshPreview();
}

void UMD_CharacterCustomizationWidget::SetPartColor(EMDCharacterCustomizationPart Part, FLinearColor NewColor)
{
	CurrentAppearance.SetPartColor(Part, NewColor);
	if (PreviewComponent)
	{
		PreviewComponent->ApplyPartColor(Part, NewColor);
	}
	if (ExternalCustomizationComponent && ExternalCustomizationComponent != PreviewComponent)
	{
		ExternalCustomizationComponent->ApplyPartColor(Part, NewColor);
	}
}

FLinearColor UMD_CharacterCustomizationWidget::GetPartColor(EMDCharacterCustomizationPart Part) const
{
	return CurrentAppearance.GetPartColor(Part);
}

void UMD_CharacterCustomizationWidget::SelectSlot(int32 SlotIndex)
{
	if (SlotIndex < 0 || SlotIndex >= (UMD_CharacterCustomizationSettings::Get()
		? UMD_CharacterCustomizationSettings::Get()->MaxSaveSlots
		: MDCharacterCustomization::MaxSaveSlots))
	{
		return;
	}

	SelectedSlotIndex = SlotIndex;
	RefreshSlotStatus();
}

void UMD_CharacterCustomizationWidget::ClearExpressionCanvas()
{
	// Recreate the RT instead of ClearRenderTarget2D (D3D12 fatals on this project).
	if (UMD_CharacterCustomizationSubsystem* Subsystem = GetCustomizationSubsystem())
	{
		ExpressionRenderTarget = Subsystem->CreateFaceRenderTarget();
	}
	else if (!ExpressionRenderTarget)
	{
		return;
	}

	ImportedExpressionScratch = nullptr;

	if (PreviewComponent)
	{
		PreviewComponent->ApplyExpressionTexture(nullptr);
	}
	if (ExternalCustomizationComponent && ExternalCustomizationComponent != PreviewComponent)
	{
		ExternalCustomizationComponent->ApplyExpressionTexture(nullptr);
	}
	UpdateExpressionCanvasBrush();
	UpdateFacePreviewBrush();
}

void UMD_CharacterCustomizationWidget::PaintExpressionStroke(
	FVector2D Start,
	FVector2D End,
	FLinearColor StrokeColor,
	float Thickness)
{
	if (UMD_CharacterCustomizationSubsystem* Subsystem = GetCustomizationSubsystem())
	{
		const float Radius = FMath::Max(1.f, Thickness * 0.5f);
		const float MaxSeamPixels = FMath::Max(14.f, Thickness * 2.5f);
		Subsystem->DrawFaceStampStroke(GetExpressionRenderTarget(), Start, End, StrokeColor, Radius, MaxSeamPixels);
		SyncExpressionToMeshes();
	}
}

void UMD_CharacterCustomizationWidget::PaintExpressionDot(FVector2D Position, FLinearColor DotColor, float Radius)
{
	if (UMD_CharacterCustomizationSubsystem* Subsystem = GetCustomizationSubsystem())
	{
		Subsystem->DrawFacePoint(GetExpressionRenderTarget(), Position, DotColor, Radius);
		SyncExpressionToMeshes();
	}
}

bool UMD_CharacterCustomizationWidget::SaveSelectedSlot()
{
	return SaveToSlot(SelectedSlotIndex, FString::Printf(TEXT("Slot %d"), SelectedSlotIndex + 1));
}

bool UMD_CharacterCustomizationWidget::LoadSelectedSlot()
{
	return LoadFromSlot(SelectedSlotIndex);
}

bool UMD_CharacterCustomizationWidget::DeleteSelectedSlot()
{
	const bool bDeleted = DeleteSlot(SelectedSlotIndex);
	RefreshSlotStatus();
	return bDeleted;
}

bool UMD_CharacterCustomizationWidget::SaveToSlot(int32 SlotIndex, const FString& SlotLabel)
{
	UMD_CharacterCustomizationSubsystem* Subsystem = GetCustomizationSubsystem();
	if (!Subsystem)
	{
		return false;
	}

	const FMDCharacterCustomizationAppearance Appearance = BuildCurrentAppearance();
	const bool bSaved = Subsystem->SaveCustomizationSlot(SlotIndex, Appearance, SlotLabel);
	if (bSaved)
	{
		SelectedSlotIndex = SlotIndex;
		RefreshSlotStatus();
		OnSlotSaved(SlotIndex);
	}
	return bSaved;
}

bool UMD_CharacterCustomizationWidget::LoadFromSlot(int32 SlotIndex)
{
	UMD_CharacterCustomizationSubsystem* Subsystem = GetCustomizationSubsystem();
	if (!Subsystem)
	{
		return false;
	}

	FMDCharacterCustomizationAppearance LoadedAppearance;
	FString LoadedLabel;
	if (!Subsystem->LoadCustomizationSlot(SlotIndex, LoadedAppearance, LoadedLabel))
	{
		return false;
	}

	CurrentAppearance = LoadedAppearance;
	ImportExpressionPNGToCanvas(LoadedAppearance.ExpressionTexturePNG);
	SetPickerColorInternal(CurrentAppearance.GetPartColor(SelectedPart), true, true, true);
	UpdateExpressionCanvasBrush();
	ApplyAppearanceToPreview(CurrentAppearance);

	SelectedSlotIndex = SlotIndex;
	RefreshSlotStatus();
	OnSlotLoaded(SlotIndex);
	return true;
}

bool UMD_CharacterCustomizationWidget::DeleteSlot(int32 SlotIndex)
{
	if (UMD_CharacterCustomizationSubsystem* Subsystem = GetCustomizationSubsystem())
	{
		return Subsystem->DeleteCustomizationSlot(SlotIndex);
	}

	return false;
}

TArray<FMDCharacterCustomizationSlotSummary> UMD_CharacterCustomizationWidget::GetSlotSummaries() const
{
	if (UMD_CharacterCustomizationSubsystem* Subsystem = GetCustomizationSubsystem())
	{
		return Subsystem->GetCustomizationSlotSummaries();
	}

	return {};
}

void UMD_CharacterCustomizationWidget::RefreshPreview()
{
	// Avoid BuildCurrentAppearance() here — it ReadPixels() from the expression RT and crashed D3D12.
	if (bAutoSpawnPreviewActor && !PreviewComponent)
	{
		EnsurePreviewActor();
	}
	ApplyAppearanceToPreview(CurrentAppearance);
	UpdatePreviewBrush();
	UpdateFacePreviewBrush();
}

void UMD_CharacterCustomizationWidget::RefreshSlotStatus()
{
	if (!TXT_SlotStatus)
	{
		return;
	}

	FString SlotLabel;
	FMDCharacterCustomizationAppearance DummyAppearance;
	const UMD_CharacterCustomizationSubsystem* Subsystem = GetCustomizationSubsystem();
	const bool bOccupied = Subsystem
		&& Subsystem->LoadCustomizationSlot(SelectedSlotIndex, DummyAppearance, SlotLabel);

	TXT_SlotStatus->SetText(FText::FromString(FString::Printf(
		TEXT("Selected Slot: %d  |  %s"),
		SelectedSlotIndex + 1,
		bOccupied ? *FString::Printf(TEXT("Saved (%s)"), *SlotLabel) : TEXT("Empty"))));
}

void UMD_CharacterCustomizationWidget::AddPickerColorToCustomSwatches()
{
	const int32 MaxCustom = UMD_CharacterCustomizationSettings::Get()
		? UMD_CharacterCustomizationSettings::Get()->MaxCustomSwatches
		: MDCharacterCustomization::MaxCustomSwatches;
	if (CustomSwatchColors.Num() >= MaxCustom)
	{
		CustomSwatchColors.RemoveAt(0);
	}

	CustomSwatchColors.Add(PickerColor);
	RebuildCustomSwatchButtons();
}

UMD_CharacterCustomizationSubsystem* UMD_CharacterCustomizationWidget::GetCustomizationSubsystem() const
{
	return UMD_CharacterCustomizationSubsystem::Get(this);
}

UImage* UMD_CharacterCustomizationWidget::GetExpressionCanvasImage() const
{
	return IMG_ExpressionCanvas ? IMG_ExpressionCanvas.Get() : IMG_FaceCanvas.Get();
}

bool UMD_CharacterCustomizationWidget::ImportExpressionPNGToCanvas(const TArray<uint8>& PNGBytes)
{
	UMD_CharacterCustomizationSubsystem* Subsystem = GetCustomizationSubsystem();
	if (!Subsystem || PNGBytes.Num() == 0)
	{
		ClearExpressionCanvas();
		return false;
	}

	ImportedExpressionScratch = Subsystem->CreateFaceTextureFromPNG(PNGBytes, TEXT("MD_ExpressionImportPreview"));
	if (!ImportedExpressionScratch)
	{
		ClearExpressionCanvas();
		return false;
	}

	UTextureRenderTarget2D* RenderTarget = GetExpressionRenderTarget();
	if (!RenderTarget)
	{
		return false;
	}

	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, RenderTarget, Canvas, CanvasSize, Context);
	if (Canvas)
	{
		// Full-cover opaque draw replaces previous strokes — do not nest ClearRenderTarget2D here.
		Canvas->K2_DrawTexture(
			ImportedExpressionScratch,
			FVector2D::ZeroVector,
			CanvasSize,
			FVector2D::ZeroVector,
			FVector2D::UnitVector,
			FLinearColor::White,
			BLEND_Opaque);
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);

	SyncExpressionToMeshes();
	UpdateExpressionCanvasBrush();
	UpdateFacePreviewBrush();
	return true;
}

void UMD_CharacterCustomizationWidget::BindUiCallbacks()
{
	if (BTN_ClearExpression) BTN_ClearExpression->OnClicked.AddDynamic(this, &UMD_CharacterCustomizationWidget::HandleClearExpressionClicked);
	if (BTN_ApplyFill) BTN_ApplyFill->OnClicked.AddDynamic(this, &UMD_CharacterCustomizationWidget::HandleApplyFillClicked);
	if (BTN_SetBrushColor) BTN_SetBrushColor->OnClicked.AddDynamic(this, &UMD_CharacterCustomizationWidget::HandleSetBrushColorClicked);
	if (BTN_AddCustomColor) BTN_AddCustomColor->OnClicked.AddDynamic(this, &UMD_CharacterCustomizationWidget::HandleAddCustomColorClicked);
	if (BTN_SaveSlot) BTN_SaveSlot->OnClicked.AddDynamic(this, &UMD_CharacterCustomizationWidget::HandleSaveSlotClicked);
	if (BTN_LoadSlot) BTN_LoadSlot->OnClicked.AddDynamic(this, &UMD_CharacterCustomizationWidget::HandleLoadSlotClicked);
	if (BTN_DeleteSlot) BTN_DeleteSlot->OnClicked.AddDynamic(this, &UMD_CharacterCustomizationWidget::HandleDeleteSlotClicked);
	if (BTN_Slot0) BTN_Slot0->OnClicked.AddDynamic(this, &UMD_CharacterCustomizationWidget::HandleSlot0Clicked);
	if (BTN_Slot1) BTN_Slot1->OnClicked.AddDynamic(this, &UMD_CharacterCustomizationWidget::HandleSlot1Clicked);
	if (BTN_Slot2) BTN_Slot2->OnClicked.AddDynamic(this, &UMD_CharacterCustomizationWidget::HandleSlot2Clicked);
	if (BTN_Slot3) BTN_Slot3->OnClicked.AddDynamic(this, &UMD_CharacterCustomizationWidget::HandleSlot3Clicked);
	if (BTN_Slot4) BTN_Slot4->OnClicked.AddDynamic(this, &UMD_CharacterCustomizationWidget::HandleSlot4Clicked);
	if (BTN_Slot5) BTN_Slot5->OnClicked.AddDynamic(this, &UMD_CharacterCustomizationWidget::HandleSlot5Clicked);
	if (BTN_Slot6) BTN_Slot6->OnClicked.AddDynamic(this, &UMD_CharacterCustomizationWidget::HandleSlot6Clicked);
	if (BTN_Slot7) BTN_Slot7->OnClicked.AddDynamic(this, &UMD_CharacterCustomizationWidget::HandleSlot7Clicked);
	if (BTN_PartVisor) BTN_PartVisor->OnClicked.AddDynamic(this, &UMD_CharacterCustomizationWidget::HandlePartVisorClicked);
	if (BTN_PartFace) BTN_PartFace->OnClicked.AddDynamic(this, &UMD_CharacterCustomizationWidget::HandlePartFaceClicked);
	if (BTN_PartTorso) BTN_PartTorso->OnClicked.AddDynamic(this, &UMD_CharacterCustomizationWidget::HandlePartTorsoClicked);
	if (BTN_PartHands) BTN_PartHands->OnClicked.AddDynamic(this, &UMD_CharacterCustomizationWidget::HandlePartHandsClicked);
	if (BTN_PartRim) BTN_PartRim->OnClicked.AddDynamic(this, &UMD_CharacterCustomizationWidget::HandlePartRimClicked);
	if (SL_Hue) SL_Hue->OnValueChanged.AddDynamic(this, &UMD_CharacterCustomizationWidget::HandleHueChanged);
	if (SL_Value) SL_Value->OnValueChanged.AddDynamic(this, &UMD_CharacterCustomizationWidget::HandleValueChanged);
	if (SL_BrushThickness) SL_BrushThickness->OnValueChanged.AddDynamic(this, &UMD_CharacterCustomizationWidget::HandleBrushThicknessChanged);
	if (TXT_Hex) TXT_Hex->OnTextCommitted.AddDynamic(this, &UMD_CharacterCustomizationWidget::HandleHexCommitted);
	if (TXT_R) TXT_R->OnTextCommitted.AddDynamic(this, &UMD_CharacterCustomizationWidget::HandleRCommitted);
	if (TXT_G) TXT_G->OnTextCommitted.AddDynamic(this, &UMD_CharacterCustomizationWidget::HandleGCommitted);
	if (TXT_B) TXT_B->OnTextCommitted.AddDynamic(this, &UMD_CharacterCustomizationWidget::HandleBCommitted);
}

void UMD_CharacterCustomizationWidget::BuildPresetSwatchButtons()
{
	if (!WRAP_PresetColors)
	{
		return;
	}

	for (UButton* Button : PresetSwatchButtons)
	{
		if (Button)
		{
			Button->OnClicked.Clear();
		}
	}
	PresetSwatchButtons.Reset();
	SwatchHandlers.Reset();

	const UMD_CharacterCustomizationSettings* Settings = UMD_CharacterCustomizationSettings::Get();
	if (!Settings)
	{
		return;
	}

	const TArray<FLinearColor>& Presets = Settings->GetPresetSwatchColorsOrDefault();
	const int32 ChildCount = WRAP_PresetColors->GetChildrenCount();
	const int32 SwatchCount = FMath::Min(ChildCount, Presets.Num());

	for (int32 Index = 0; Index < SwatchCount; ++Index)
	{
		UButton* SwatchButton = Cast<UButton>(WRAP_PresetColors->GetChildAt(Index));
		if (!SwatchButton)
		{
			continue;
		}

		SwatchButton->SetBackgroundColor(Presets[Index]);
		SwatchButton->OnClicked.Clear();
		SwatchButton->SetIsEnabled(true);
		SwatchButton->SetVisibility(ESlateVisibility::Visible);

		UMDCharacterCustomizationSwatchHandler* Handler = NewObject<UMDCharacterCustomizationSwatchHandler>(this);
		Handler->Initialize(this, Presets[Index]);
		SwatchButton->OnClicked.AddDynamic(Handler, &UMDCharacterCustomizationSwatchHandler::HandleClicked);
		SwatchHandlers.Add(Handler);
		PresetSwatchButtons.Add(SwatchButton);
	}
}

void UMD_CharacterCustomizationWidget::RebuildCustomSwatchButtons()
{
	if (!WRAP_CustomColors)
	{
		return;
	}

	WRAP_CustomColors->ClearChildren();

	// Keep only preset handlers; drop stale custom handlers after ClearChildren.
	const int32 PresetCount = PresetSwatchButtons.Num();
	if (SwatchHandlers.Num() > PresetCount)
	{
		SwatchHandlers.SetNum(PresetCount);
	}
	else if (SwatchHandlers.Num() < PresetCount)
	{
		// Preset clicks were GC'd or never rebound — rebuild presets first.
		BuildPresetSwatchButtons();
	}

	for (int32 Index = 0; Index < CustomSwatchColors.Num(); ++Index)
	{
		if (!WidgetTree)
		{
			break;
		}

		UButton* SwatchButton = WidgetTree->ConstructWidget<UButton>(
			UButton::StaticClass(),
			*FString::Printf(TEXT("CustomSwatch_%d"), Index));
		if (!SwatchButton)
		{
			continue;
		}

		SwatchButton->SetBackgroundColor(CustomSwatchColors[Index]);
		if (UWrapBoxSlot* WrapSlot = WRAP_CustomColors->AddChildToWrapBox(SwatchButton))
		{
			WrapSlot->SetPadding(FMargin(2.f));
		}

		UMDCharacterCustomizationSwatchHandler* Handler = NewObject<UMDCharacterCustomizationSwatchHandler>(this);
		Handler->Initialize(this, CustomSwatchColors[Index]);
		SwatchButton->OnClicked.AddDynamic(Handler, &UMDCharacterCustomizationSwatchHandler::HandleClicked);
		SwatchHandlers.Add(Handler);
	}
}

void UMD_CharacterCustomizationWidget::UpdateExpressionCanvasBrush()
{
	UpdateFacePreviewBrush();
}

void UMD_CharacterCustomizationWidget::UpdateFacePreviewBrush()
{
	const int32 CanvasSize = MDCharacterCustomizationWidgetPrivate::GetExpressionCanvasSize();
	const FVector2D ExpressionSize(CanvasSize, CanvasSize);

	auto ApplyBrush = [](UImage* Image, UTexture* Texture, const FVector2D& Size, ESlateVisibility InVisibility)
	{
		if (!Image || !Texture)
		{
			return;
		}

		FSlateBrush Brush;
		Brush.SetResourceObject(Texture);
		Brush.ImageSize = Size;
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Image->SetBrush(Brush);
		Image->SetVisibility(InVisibility);
	};

	// Prefer a single live face image for display. Strokes land in mesh UV and show via material.
	if (IMG_FaceCanvas && IMG_ExpressionCanvas && IMG_FaceCanvas != IMG_ExpressionCanvas)
	{
		if (FacePreviewRenderTarget)
		{
			ApplyBrush(
				IMG_FaceCanvas,
				FacePreviewRenderTarget,
				FVector2D(FacePreviewRenderTarget->SizeX, FacePreviewRenderTarget->SizeY),
				ESlateVisibility::HitTestInvisible);
			ApplyBrush(
				IMG_ExpressionCanvas,
				FacePreviewRenderTarget,
				FVector2D(FacePreviewRenderTarget->SizeX, FacePreviewRenderTarget->SizeY),
				ESlateVisibility::Visible);
		}
		return;
	}

	UImage* CanvasImage = GetExpressionCanvasImage();
	if (!CanvasImage)
	{
		return;
	}

	// Always show the live face capture. Strokes are written into mesh UVs and appear
	// on the 3D surface via the material — do not overlay the flat expression RT here.
	if (FacePreviewRenderTarget)
	{
		ApplyBrush(
			CanvasImage,
			FacePreviewRenderTarget,
			FVector2D(FacePreviewRenderTarget->SizeX, FacePreviewRenderTarget->SizeY),
			ESlateVisibility::Visible);
		return;
	}

	if (ExpressionRenderTarget)
	{
		ApplyBrush(CanvasImage, ExpressionRenderTarget, ExpressionSize, ESlateVisibility::Visible);
	}
}

void UMD_CharacterCustomizationWidget::UpdateColorPreview()
{
	if (IMG_ColorPreview)
	{
		IMG_ColorPreview->SetColorAndOpacity(PickerColor);
	}
}

void UMD_CharacterCustomizationWidget::UpdateSVPickerTexture()
{
	if (!IMG_SVPicker)
	{
		return;
	}

	// Slate tint only — no render-target clear/draw (D3D12 E_INVALIDARG on this project).
	const FLinearColor Fill = MDCharacterCustomizationWidgetPrivate::HSVToLinearColor(PickerHue, 1.f, 1.f);
	IMG_SVPicker->SetColorAndOpacity(Fill);
}

void UMD_CharacterCustomizationWidget::UpdatePickerTextFields()
{
	if (!TXT_Hex && !TXT_R && !TXT_G && !TXT_B)
	{
		return;
	}

	bSuppressPickerCallbacks = true;

	const FColor SRGB = PickerColor.ToFColor(true);
	if (TXT_Hex)
	{
		TXT_Hex->SetText(FText::FromString(FString::Printf(TEXT("#%02X%02X%02X"), SRGB.R, SRGB.G, SRGB.B)));
	}
	if (TXT_R) TXT_R->SetText(FText::FromString(FString::FromInt(SRGB.R)));
	if (TXT_G) TXT_G->SetText(FText::FromString(FString::FromInt(SRGB.G)));
	if (TXT_B) TXT_B->SetText(FText::FromString(FString::FromInt(SRGB.B)));

	bSuppressPickerCallbacks = false;
}

void UMD_CharacterCustomizationWidget::SyncPickerFromHSV()
{
	PickerColor = MDCharacterCustomizationWidgetPrivate::HSVToLinearColor(PickerHue, PickerSaturation, PickerValue);
	UpdateColorPreview();
	UpdatePickerTextFields();

	if (SL_Value && !bSuppressPickerCallbacks)
	{
		bSuppressPickerCallbacks = true;
		SL_Value->SetValue(PickerValue);
		bSuppressPickerCallbacks = false;
	}

	ExpressionBrushColor = PickerColor;
	UpdateBrushUI();
}

void UMD_CharacterCustomizationWidget::SyncHSVFromPicker()
{
	MDCharacterCustomizationWidgetPrivate::LinearColorToHSV(PickerColor, PickerHue, PickerSaturation, PickerValue);

	if (SL_Hue && !bSuppressPickerCallbacks)
	{
		bSuppressPickerCallbacks = true;
		SL_Hue->SetValue(PickerHue / 360.f);
		bSuppressPickerCallbacks = false;
	}

	if (SL_Value && !bSuppressPickerCallbacks)
	{
		bSuppressPickerCallbacks = true;
		SL_Value->SetValue(PickerValue);
		bSuppressPickerCallbacks = false;
	}

	UpdateSVPickerTexture();
	UpdateColorPreview();
	UpdatePickerTextFields();
}

bool UMD_CharacterCustomizationWidget::IsPointerOverExpressionCanvas(const FPointerEvent& InMouseEvent) const
{
	UImage* CanvasImage = GetExpressionCanvasImage();
	if (!CanvasImage)
	{
		return false;
	}

	const FGeometry FaceGeometry = CanvasImage->GetCachedGeometry();
	const FVector2D LocalPosition = FaceGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
	const FVector2D FaceSize = FaceGeometry.GetLocalSize();
	if (FaceSize.X <= KINDA_SMALL_NUMBER || FaceSize.Y <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	return LocalPosition.X >= 0.f && LocalPosition.Y >= 0.f
		&& LocalPosition.X <= FaceSize.X && LocalPosition.Y <= FaceSize.Y;
}

bool UMD_CharacterCustomizationWidget::TryGetExpressionCanvasPixel(
	const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent,
	FVector2D& OutCanvasPixel) const
{
	FVector2D ViewUV;
	return TryGetExpressionPaintSample(InMouseEvent, ViewUV, OutCanvasPixel);
}

bool UMD_CharacterCustomizationWidget::TryGetExpressionPaintSample(
	const FPointerEvent& InMouseEvent,
	FVector2D& OutViewUV,
	FVector2D& OutCanvasPixel) const
{
	UImage* CanvasImage = GetExpressionCanvasImage();
	if (!CanvasImage || !PreviewActor)
	{
		return false;
	}

	const FGeometry FaceGeometry = CanvasImage->GetCachedGeometry();
	const FVector2D LocalPosition = FaceGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
	const FVector2D FaceSize = FaceGeometry.GetLocalSize();
	if (FaceSize.X <= KINDA_SMALL_NUMBER || FaceSize.Y <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	if (LocalPosition.X < 0.f || LocalPosition.Y < 0.f || LocalPosition.X > FaceSize.X || LocalPosition.Y > FaceSize.Y)
	{
		return false;
	}

	OutViewUV.X = LocalPosition.X / FaceSize.X;
	OutViewUV.Y = LocalPosition.Y / FaceSize.Y;

	FVector2D MeshUV;
	if (!PreviewActor->ProjectFaceViewToMeshUV(OutViewUV, MeshUV))
	{
		return false;
	}

	const int32 CanvasSize = MDCharacterCustomizationWidgetPrivate::GetExpressionCanvasSize();
	OutCanvasPixel.X = MeshUV.X * CanvasSize;
	OutCanvasPixel.Y = MeshUV.Y * CanvasSize;
	return true;
}

bool UMD_CharacterCustomizationWidget::TryPickSVFromPointer(
	const FPointerEvent& InMouseEvent,
	FVector2D& OutSaturationValue) const
{
	if (!IMG_SVPicker)
	{
		return false;
	}

	const FGeometry PickerGeometry = IMG_SVPicker->GetCachedGeometry();
	const FVector2D LocalPosition = PickerGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
	const FVector2D PickerSize = PickerGeometry.GetLocalSize();
	if (PickerSize.X <= KINDA_SMALL_NUMBER || PickerSize.Y <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	if (LocalPosition.X < 0.f || LocalPosition.Y < 0.f || LocalPosition.X > PickerSize.X || LocalPosition.Y > PickerSize.Y)
	{
		return false;
	}

	OutSaturationValue.X = FMath::Clamp(LocalPosition.X / PickerSize.X, 0.f, 1.f);
	OutSaturationValue.Y = FMath::Clamp(1.f - (LocalPosition.Y / PickerSize.Y), 0.f, 1.f);
	return true;
}

void UMD_CharacterCustomizationWidget::UpdatePreviewHint()
{
	const bool bHasLivePreview = PreviewActor != nullptr || PreviewComponent != nullptr;
	if (TXT_PreviewHint)
	{
		TXT_PreviewHint->SetVisibility(bHasLivePreview ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		TXT_PreviewHint->SetText(FText::FromString(
			bHasLivePreview
				? TEXT("3D preview active")
				: TEXT("Character model pending\nPart colors + expression apply when mesh is ready")));
	}
}

void UMD_CharacterCustomizationWidget::EnsurePreviewActor()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	ApplyEngineSettings();

	if (!PreviewRenderTarget)
	{
		const int32 SizeX = FMath::Max(64, PreviewRenderSize.X);
		const int32 SizeY = FMath::Max(64, PreviewRenderSize.Y);
		PreviewRenderTarget = UKismetRenderingLibrary::CreateRenderTarget2D(
			this,
			SizeX,
			SizeY,
			ETextureRenderTargetFormat::RTF_RGBA8,
			FLinearColor(0.04f, 0.06f, 0.09f, 1.f));
	}

	if (!FacePreviewRenderTarget)
	{
		const int32 FaceSize = MDCharacterCustomizationWidgetPrivate::GetExpressionCanvasSize();
		FacePreviewRenderTarget = UKismetRenderingLibrary::CreateRenderTarget2D(
			this,
			FaceSize,
			FaceSize,
			ETextureRenderTargetFormat::RTF_RGBA8,
			FLinearColor(0.08f, 0.09f, 0.12f, 1.f));
	}

	if (!PreviewActor)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.ObjectFlags |= RF_Transient;

		const FVector SpawnLocation = PreviewSpawnOffset.IsNearlyZero()
			? FVector(-2500.f, 0.f, 100.f)
			: FVector(PreviewSpawnOffset);
		PreviewActor = World->SpawnActor<AMD_CharacterCustomizationPreviewActor>(
			AMD_CharacterCustomizationPreviewActor::StaticClass(),
			SpawnLocation,
			FRotator::ZeroRotator,
			SpawnParams);

		if (!PreviewActor)
		{
			UE_LOG(LogTemp, Error, TEXT("MD Customization: failed to spawn PreviewActor"));
			UpdatePreviewHint();
			return;
		}
	}

	USkeletalMesh* Mesh = PreviewSkeletalMesh.LoadSynchronous();
	UMaterialInterface* Material = PreviewMaterial.LoadSynchronous();

	// Prefer the live TestHost / character mesh when soft paths fail or are empty.
	if (!Mesh && ExternalCustomizationComponent)
	{
		if (AActor* Host = ExternalCustomizationComponent->GetOwner())
		{
			if (USkeletalMeshComponent* HostMesh = Host->FindComponentByClass<USkeletalMeshComponent>())
			{
				Mesh = HostMesh->GetSkeletalMeshAsset();
				if (!Material)
				{
					Material = HostMesh->GetMaterial(0);
				}
				UE_LOG(LogTemp, Log, TEXT("MD Customization: using host mesh %s"),
					Mesh ? *Mesh->GetName() : TEXT("null"));
			}
		}
	}

	if (!Mesh)
	{
		UE_LOG(LogTemp, Error, TEXT("MD Customization: PreviewSkeletalMesh missing (%s)"),
			*PreviewSkeletalMesh.ToSoftObjectPath().ToString());
	}
	if (!Material)
	{
		UE_LOG(LogTemp, Warning, TEXT("MD Customization: PreviewMaterial missing (%s)"),
			*PreviewMaterial.ToSoftObjectPath().ToString());
	}

	PreviewActor->ConfigurePreview(Mesh, Material, PreviewRenderTarget, FacePreviewRenderTarget);
	PreviewActor->SetOrbitEnabled(bOrbitPreview);
	PreviewComponent = PreviewActor->GetCustomizationComponent();
	if (PreviewComponent)
	{
		PreviewComponent->ApplyPartColors(CurrentAppearance);
	}

	GetExpressionRenderTarget();
	SyncExpressionToMeshes();

	UpdatePreviewBrush();
	UpdateFacePreviewBrush();
	UpdatePreviewHint();
}

void UMD_CharacterCustomizationWidget::DestroyPreviewActor()
{
	if (PreviewActor)
	{
		if (PreviewComponent && PreviewComponent == PreviewActor->GetCustomizationComponent())
		{
			PreviewComponent = ExternalCustomizationComponent;
		}
		PreviewActor->Destroy();
		PreviewActor = nullptr;
	}

	PreviewRenderTarget = nullptr;
	FacePreviewRenderTarget = nullptr;
	FaceDisplayRenderTarget = nullptr;
	if (IMG_Preview)
	{
		IMG_Preview->SetBrush(FSlateBrush());
	}
}

void UMD_CharacterCustomizationWidget::UpdatePreviewBrush()
{
	if (!PreviewRenderTarget)
	{
		return;
	}

	FSlateBrush Brush;
	Brush.SetResourceObject(PreviewRenderTarget);
	Brush.ImageSize = FVector2D(PreviewRenderSize.X, PreviewRenderSize.Y);
	Brush.DrawAs = ESlateBrushDrawType::Image;

	if (IMG_Preview)
	{
		IMG_Preview->SetBrush(Brush);
		IMG_Preview->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else if (BDR_Preview)
	{
		BDR_Preview->SetBrush(Brush);
	}
}

void UMD_CharacterCustomizationWidget::SyncExpressionToMeshes()
{
	if (!ExpressionRenderTarget)
	{
		return;
	}

	if (PreviewComponent)
	{
		PreviewComponent->ApplyExpressionTexture(ExpressionRenderTarget);
	}
	if (ExternalCustomizationComponent && ExternalCustomizationComponent != PreviewComponent)
	{
		ExternalCustomizationComponent->ApplyExpressionTexture(ExpressionRenderTarget);
	}
}

void UMD_CharacterCustomizationWidget::SyncAppearanceToExternalComponent()
{
	if (!ExternalCustomizationComponent || ExternalCustomizationComponent == PreviewComponent)
	{
		return;
	}

	ExternalCustomizationComponent->ApplyPartColors(CurrentAppearance);
	if (ExpressionRenderTarget)
	{
		ExternalCustomizationComponent->ApplyExpressionTexture(ExpressionRenderTarget);
	}
}

void UMD_CharacterCustomizationWidget::UpdateSelectedPartLabel()
{
	if (TXT_SelectedPart)
	{
		TXT_SelectedPart->SetText(FText::FromString(FString::Printf(
			TEXT("Fill target: %s"),
			*MDCharacterCustomizationWidgetPrivate::PartDisplayName(SelectedPart))));
	}
}

FMDCharacterCustomizationAppearance UMD_CharacterCustomizationWidget::BuildCurrentAppearance() const
{
	if (UMD_CharacterCustomizationSubsystem* Subsystem = GetCustomizationSubsystem())
	{
		return Subsystem->BuildAppearanceFromRenderTarget(GetExpressionRenderTargetConst(), CurrentAppearance);
	}

	return CurrentAppearance;
}

void UMD_CharacterCustomizationWidget::ApplyAppearanceToPreview(const FMDCharacterCustomizationAppearance& Appearance)
{
	if (PreviewComponent)
	{
		PreviewComponent->ApplyPartColors(Appearance);
		if (ExpressionRenderTarget)
		{
			PreviewComponent->ApplyExpressionTexture(ExpressionRenderTarget);
		}
	}
	SyncAppearanceToExternalComponent();
}

void UMD_CharacterCustomizationWidget::SetPickerColorInternal(
	FLinearColor NewColor,
	bool bUpdateHSV,
	bool bUpdateTextFields,
	bool bUpdateSVTexture)
{
	PickerColor = NewColor;
	if (bUpdateHSV)
	{
		MDCharacterCustomizationWidgetPrivate::LinearColorToHSV(PickerColor, PickerHue, PickerSaturation, PickerValue);
		if (SL_Hue)
		{
			bSuppressPickerCallbacks = true;
			SL_Hue->SetValue(PickerHue / 360.f);
			bSuppressPickerCallbacks = false;
		}
		if (SL_Value)
		{
			bSuppressPickerCallbacks = true;
			SL_Value->SetValue(PickerValue);
			bSuppressPickerCallbacks = false;
		}
	}

	UpdateColorPreview();
	if (bUpdateTextFields)
	{
		UpdatePickerTextFields();
	}
	if (bUpdateSVTexture)
	{
		UpdateSVPickerTexture();
	}

	// Keep stored brush color in sync for Blueprint / settings consumers.
	ExpressionBrushColor = PickerColor;
	UpdateBrushUI();
}

void UMD_CharacterCustomizationWidget::SetExpressionBrushColor(FLinearColor NewColor)
{
	ExpressionBrushColor = NewColor;
	ExpressionBrushColor.A = 1.f;
	UpdateBrushUI();
}

void UMD_CharacterCustomizationWidget::SetExpressionBrushThickness(float NewThickness)
{
	ExpressionBrushThickness = FMath::Clamp(NewThickness, 1.f, 48.f);
	UpdateBrushUI();
}

FLinearColor UMD_CharacterCustomizationWidget::GetActiveDrawColor() const
{
	// Face drawing always uses the color picker so thickness + color are both interactive.
	return PickerColor;
}

void UMD_CharacterCustomizationWidget::UpdateBrushUI()
{
	if (IMG_BrushColorPreview)
	{
		IMG_BrushColorPreview->SetColorAndOpacity(GetActiveDrawColor());
	}

	if (TXT_BrushThickness)
	{
		TXT_BrushThickness->SetText(FText::FromString(
			FString::Printf(TEXT("Brush: %.0f"), ExpressionBrushThickness)));
	}

	if (SL_BrushThickness && !bSuppressPickerCallbacks)
	{
		const float Norm = FMath::GetMappedRangeValueClamped(
			FVector2D(2.f, 24.f),
			FVector2D(0.f, 1.f),
			ExpressionBrushThickness);
		bSuppressPickerCallbacks = true;
		SL_BrushThickness->SetValue(Norm);
		bSuppressPickerCallbacks = false;
	}
}

void UMD_CharacterCustomizationWidget::HandleClearExpressionClicked()
{
	ClearExpressionCanvas();
	RefreshPreview();
}

void UMD_CharacterCustomizationWidget::HandleApplyFillClicked()
{
	ApplyPickerColorToSelectedPart();
}

void UMD_CharacterCustomizationWidget::HandleSetBrushColorClicked()
{
	SetExpressionBrushColor(PickerColor);
}

void UMD_CharacterCustomizationWidget::HandleBrushThicknessChanged(float NewValue)
{
	if (bSuppressPickerCallbacks)
	{
		return;
	}

	ExpressionBrushThickness = FMath::GetMappedRangeValueClamped(
		FVector2D(0.f, 1.f),
		FVector2D(2.f, 24.f),
		NewValue);
	if (TXT_BrushThickness)
	{
		TXT_BrushThickness->SetText(FText::FromString(
			FString::Printf(TEXT("Brush: %.0f"), ExpressionBrushThickness)));
	}
	if (IMG_BrushColorPreview)
	{
		IMG_BrushColorPreview->SetColorAndOpacity(GetActiveDrawColor());
	}
}

void UMD_CharacterCustomizationWidget::HandleAddCustomColorClicked()
{
	AddPickerColorToCustomSwatches();
}

void UMD_CharacterCustomizationWidget::HandleSaveSlotClicked() { SaveSelectedSlot(); }
void UMD_CharacterCustomizationWidget::HandleLoadSlotClicked() { LoadSelectedSlot(); }
void UMD_CharacterCustomizationWidget::HandleDeleteSlotClicked() { DeleteSelectedSlot(); }
void UMD_CharacterCustomizationWidget::HandleSlot0Clicked() { SelectSlot(0); }
void UMD_CharacterCustomizationWidget::HandleSlot1Clicked() { SelectSlot(1); }
void UMD_CharacterCustomizationWidget::HandleSlot2Clicked() { SelectSlot(2); }
void UMD_CharacterCustomizationWidget::HandleSlot3Clicked() { SelectSlot(3); }
void UMD_CharacterCustomizationWidget::HandleSlot4Clicked() { SelectSlot(4); }
void UMD_CharacterCustomizationWidget::HandleSlot5Clicked() { SelectSlot(5); }
void UMD_CharacterCustomizationWidget::HandleSlot6Clicked() { SelectSlot(6); }
void UMD_CharacterCustomizationWidget::HandleSlot7Clicked() { SelectSlot(7); }
void UMD_CharacterCustomizationWidget::HandlePartVisorClicked() { SelectPart(EMDCharacterCustomizationPart::Visor); }
void UMD_CharacterCustomizationWidget::HandlePartFaceClicked() { SelectPart(EMDCharacterCustomizationPart::Face); }
void UMD_CharacterCustomizationWidget::HandlePartTorsoClicked() { SelectPart(EMDCharacterCustomizationPart::Torso); }
void UMD_CharacterCustomizationWidget::HandlePartHandsClicked() { SelectPart(EMDCharacterCustomizationPart::Hands); }
void UMD_CharacterCustomizationWidget::HandlePartRimClicked() { SelectPart(EMDCharacterCustomizationPart::Rim); }

void UMD_CharacterCustomizationWidget::HandleHueChanged(float NewValue)
{
	if (bSuppressPickerCallbacks)
	{
		return;
	}

	PickerHue = NewValue * 360.f;
	UpdateSVPickerTexture();
	SyncPickerFromHSV();
}

void UMD_CharacterCustomizationWidget::HandleValueChanged(float NewValue)
{
	if (bSuppressPickerCallbacks)
	{
		return;
	}

	PickerValue = NewValue;
	SyncPickerFromHSV();
}

void UMD_CharacterCustomizationWidget::HandleHexCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (bSuppressPickerCallbacks || CommitMethod == ETextCommit::OnCleared)
	{
		return;
	}

	FString HexString = Text.ToString().TrimStartAndEnd();
	HexString.RemoveFromStart(TEXT("#"));
	if (HexString.Len() != 6)
	{
		UpdatePickerTextFields();
		return;
	}

	const FColor ParsedColor = FColor::FromHex(HexString);
	SetPickerColorInternal(FLinearColor(ParsedColor), true, false, true);
}

void UMD_CharacterCustomizationWidget::HandleRCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (bSuppressPickerCallbacks || CommitMethod == ETextCommit::OnCleared)
	{
		return;
	}

	FColor Color = PickerColor.ToFColor(true);
	Color.R = FMath::Clamp(FCString::Atoi(*Text.ToString()), 0, 255);
	SetPickerColorInternal(FLinearColor(Color), true, true, true);
}

void UMD_CharacterCustomizationWidget::HandleGCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (bSuppressPickerCallbacks || CommitMethod == ETextCommit::OnCleared)
	{
		return;
	}

	FColor Color = PickerColor.ToFColor(true);
	Color.G = FMath::Clamp(FCString::Atoi(*Text.ToString()), 0, 255);
	SetPickerColorInternal(FLinearColor(Color), true, true, true);
}

void UMD_CharacterCustomizationWidget::HandleBCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (bSuppressPickerCallbacks || CommitMethod == ETextCommit::OnCleared)
	{
		return;
	}

	FColor Color = PickerColor.ToFColor(true);
	Color.B = FMath::Clamp(FCString::Atoi(*Text.ToString()), 0, 255);
	SetPickerColorInternal(FLinearColor(Color), true, true, true);
}
