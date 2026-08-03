#include "Gameplay/Customization/MD_CharacterCustomizationSubsystem.h"

#include "Component/Customization/MD_CharacterCustomizationComponent.h"
#include "Gameplay/Customization/MD_CharacterCustomizationSaveGame.h"
#include "Gameplay/Customization/MD_CharacterCustomizationSettings.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Modules/ModuleManager.h"
#include "TextureResource.h"

void UMD_CharacterCustomizationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	EnsureSaveLoaded();
}

UMD_CharacterCustomizationSubsystem* UMD_CharacterCustomizationSubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

	const UWorld* World = WorldContextObject->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	UGameInstance* GameInstance = World->GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UMD_CharacterCustomizationSubsystem>() : nullptr;
}

int32 UMD_CharacterCustomizationSubsystem::GetMaxSaveSlots() const
{
	if (const UMD_CharacterCustomizationSettings* Settings = UMD_CharacterCustomizationSettings::Get())
	{
		return FMath::Clamp(Settings->MaxSaveSlots, 1, 32);
	}
	return MDCharacterCustomization::MaxSaveSlots;
}

UTextureRenderTarget2D* UMD_CharacterCustomizationSubsystem::CreateFaceRenderTarget()
{
	const UMD_CharacterCustomizationSettings* Settings = UMD_CharacterCustomizationSettings::Get();
	const int32 Size = Settings
		? FMath::Clamp(Settings->ExpressionCanvasSize, 32, 1024)
		: MDCharacterCustomization::FaceTextureSize;

	UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(this);
	RenderTarget->RenderTargetFormat = RTF_RGBA8;
	// Transparent clear — opaque white was covering the live face panel as soon as drawing started.
	RenderTarget->ClearColor = FLinearColor(0.f, 0.f, 0.f, 0.f);
	RenderTarget->bAutoGenerateMips = false;
	RenderTarget->InitAutoFormat(Size, Size);
	// Do not ClearRenderTarget2D / UpdateResourceImmediate here — both have fatalled D3D12 on this project.
	return RenderTarget;
}

void UMD_CharacterCustomizationSubsystem::ClearFaceRenderTarget(UTextureRenderTarget2D* RenderTarget, FLinearColor ClearColor)
{
	if (!RenderTarget)
	{
		return;
	}

	// Avoid UKismetRenderingLibrary::ClearRenderTarget2D — it has fatalled D3D12 (E_INVALIDARG /
	// device hung) on this project, especially when nested inside another canvas RT draw (Load).
	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, RenderTarget, Canvas, CanvasSize, Context);
	if (Canvas)
	{
		FCanvasTileItem TileItem(FVector2D::ZeroVector, CanvasSize, ClearColor);
		TileItem.BlendMode = SE_BLEND_Opaque;
		Canvas->DrawItem(TileItem);
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
}

void UMD_CharacterCustomizationSubsystem::FillFaceRenderTarget(UTextureRenderTarget2D* RenderTarget, FLinearColor FillColor)
{
	ClearFaceRenderTarget(RenderTarget, FillColor);
}

void UMD_CharacterCustomizationSubsystem::DrawFaceLine(
	UTextureRenderTarget2D* RenderTarget,
	FVector2D Start,
	FVector2D End,
	FLinearColor LineColor,
	float Thickness)
{
	if (!RenderTarget)
	{
		return;
	}

	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, RenderTarget, Canvas, CanvasSize, Context);
	if (Canvas)
	{
		Canvas->K2_DrawLine(Start, End, Thickness, LineColor);
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
}

void UMD_CharacterCustomizationSubsystem::DrawFacePoint(
	UTextureRenderTarget2D* RenderTarget,
	FVector2D Position,
	FLinearColor PointColor,
	float Radius)
{
	if (!RenderTarget || Radius <= 0.f)
	{
		return;
	}

	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, RenderTarget, Canvas, CanvasSize, Context);
	if (Canvas)
	{
		// Soft filled stamp via radial spokes (single canvas pass).
		const float SpokeThickness = FMath::Max(1.f, Radius * 0.65f);
		const int32 Steps = 16;
		for (int32 StepIndex = 0; StepIndex < Steps; ++StepIndex)
		{
			const float Angle = (2.f * PI * StepIndex) / Steps;
			const FVector2D Tip(
				Position.X + FMath::Cos(Angle) * Radius,
				Position.Y + FMath::Sin(Angle) * Radius);
			Canvas->K2_DrawLine(Position, Tip, SpokeThickness, PointColor);
		}
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
}

void UMD_CharacterCustomizationSubsystem::DrawFaceStampStroke(
	UTextureRenderTarget2D* RenderTarget,
	FVector2D Start,
	FVector2D End,
	FLinearColor StampColor,
	float Radius,
	float MaxSeamPixels)
{
	if (!RenderTarget || Radius <= 0.f)
	{
		return;
	}

	const float Dist = FVector2D::Distance(Start, End);
	if (Dist > MaxSeamPixels)
	{
		// UV island discontinuity — never bridge cracks with a texture-space line.
		DrawFacePoint(RenderTarget, End, StampColor, Radius);
		return;
	}

	const float Spacing = FMath::Max(1.25f, Radius * 0.35f);
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(Dist / Spacing), 1, 48);

	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, RenderTarget, Canvas, CanvasSize, Context);
	if (Canvas)
	{
		const float SpokeThickness = FMath::Max(1.f, Radius * 0.65f);
		constexpr int32 SpokeCount = 12;
		for (int32 Step = 1; Step <= Steps; ++Step)
		{
			const FVector2D Position = FMath::Lerp(Start, End, static_cast<float>(Step) / static_cast<float>(Steps));
			for (int32 Spoke = 0; Spoke < SpokeCount; ++Spoke)
			{
				const float Angle = (2.f * PI * Spoke) / SpokeCount;
				const FVector2D Tip(
					Position.X + FMath::Cos(Angle) * Radius,
					Position.Y + FMath::Sin(Angle) * Radius);
				Canvas->K2_DrawLine(Position, Tip, SpokeThickness, StampColor);
			}
		}
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
}

bool UMD_CharacterCustomizationSubsystem::ExportFaceRenderTargetToPNG(
	UTextureRenderTarget2D* RenderTarget,
	TArray<uint8>& OutPNGBytes) const
{
	OutPNGBytes.Reset();
	if (!RenderTarget)
	{
		return false;
	}

	FTextureRenderTargetResource* RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RenderTargetResource)
	{
		return false;
	}

	TArray<FColor> PixelData;
	if (!RenderTargetResource->ReadPixels(PixelData))
	{
		return false;
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (!ImageWrapper.IsValid())
	{
		return false;
	}

	const int32 Width = RenderTarget->SizeX;
	const int32 Height = RenderTarget->SizeY;
	if (!ImageWrapper->SetRaw(PixelData.GetData(), PixelData.Num() * sizeof(FColor), Width, Height, ERGBFormat::BGRA, 8))
	{
		return false;
	}

	OutPNGBytes = ImageWrapper->GetCompressed();
	return OutPNGBytes.Num() > 0;
}

UTexture2D* UMD_CharacterCustomizationSubsystem::CreateFaceTextureFromPNG(
	const TArray<uint8>& PNGBytes,
	const FString& TextureName)
{
	if (PNGBytes.Num() == 0)
	{
		return nullptr;
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(PNGBytes.GetData(), PNGBytes.Num()))
	{
		return nullptr;
	}

	TArray<uint8> RawData;
	if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, RawData))
	{
		return nullptr;
	}

	const int32 Width = ImageWrapper->GetWidth();
	const int32 Height = ImageWrapper->GetHeight();
	if (Width <= 0 || Height <= 0)
	{
		return nullptr;
	}

	const FName UniqueName = MakeUniqueObjectName(this, UTexture2D::StaticClass(), FName(*TextureName));
	UTexture2D* Texture = UTexture2D::CreateTransient(
		Width,
		Height,
		PF_B8G8R8A8,
		UniqueName,
		TConstArrayView64<uint8>(RawData.GetData(), RawData.Num()));
	if (!Texture)
	{
		return nullptr;
	}

	// Keep alive under the subsystem so Load→draw→apply cannot GC mid-frame.
	Texture->Rename(nullptr, this);
	Texture->NeverStream = true;
	Texture->SRGB = true;
	Texture->MipGenSettings = TMGS_NoMipmaps;
	Texture->CompressionSettings = TC_EditorIcon;
	Texture->Filter = TF_Bilinear;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	Texture->UpdateResource();

	return Texture;
}

FMDCharacterCustomizationAppearance UMD_CharacterCustomizationSubsystem::BuildAppearanceFromRenderTarget(
	UTextureRenderTarget2D* ExpressionRenderTarget,
	const FMDCharacterCustomizationAppearance& PartColors) const
{
	FMDCharacterCustomizationAppearance Appearance = PartColors;
	ExportFaceRenderTargetToPNG(ExpressionRenderTarget, Appearance.ExpressionTexturePNG);
	return Appearance;
}

bool UMD_CharacterCustomizationSubsystem::SaveCustomizationSlot(
	int32 SlotIndex,
	const FMDCharacterCustomizationAppearance& Appearance,
	const FString& SlotLabel)
{
	if (!IsValidSlotIndex(SlotIndex) || !EnsureSaveLoaded())
	{
		return false;
	}

	FMDCharacterCustomizationSlotData* Slot = FindMutableSlot(SlotIndex);
	if (!Slot)
	{
		return false;
	}

	Slot->SlotIndex = SlotIndex;
	Slot->Appearance = Appearance;
	Slot->LastModified = FDateTime::UtcNow();
	if (!SlotLabel.IsEmpty())
	{
		Slot->SlotLabel = SlotLabel;
	}
	else if (Slot->SlotLabel.IsEmpty())
	{
		Slot->SlotLabel = FString::Printf(TEXT("Slot %d"), SlotIndex + 1);
	}

	return PersistSave();
}

bool UMD_CharacterCustomizationSubsystem::LoadCustomizationSlot(
	int32 SlotIndex,
	FMDCharacterCustomizationAppearance& OutAppearance,
	FString& OutSlotLabel) const
{
	OutAppearance = FMDCharacterCustomizationAppearance();
	OutSlotLabel.Reset();

	if (!IsValidSlotIndex(SlotIndex))
	{
		return false;
	}

	const UMD_CharacterCustomizationSubsystem* MutableThis = this;
	if (!MutableThis->EnsureSaveLoaded())
	{
		return false;
	}

	const FMDCharacterCustomizationSlotData* Slot = FindSlot(SlotIndex);
	if (!Slot || Slot->LastModified == FDateTime::MinValue())
	{
		return false;
	}

	OutAppearance = Slot->Appearance;
	OutAppearance.MigrateLegacyFieldsIfNeeded();
	OutSlotLabel = Slot->SlotLabel;
	return true;
}

bool UMD_CharacterCustomizationSubsystem::DeleteCustomizationSlot(int32 SlotIndex)
{
	if (!IsValidSlotIndex(SlotIndex) || !EnsureSaveLoaded())
	{
		return false;
	}

	FMDCharacterCustomizationSlotData* Slot = FindMutableSlot(SlotIndex);
	if (!Slot)
	{
		return false;
	}

	*Slot = FMDCharacterCustomizationSlotData();
	Slot->SlotIndex = SlotIndex;
	Slot->LastModified = FDateTime::MinValue();
	return PersistSave();
}

bool UMD_CharacterCustomizationSubsystem::IsCustomizationSlotOccupied(int32 SlotIndex) const
{
	FMDCharacterCustomizationAppearance Appearance;
	FString Label;
	return LoadCustomizationSlot(SlotIndex, Appearance, Label);
}

TArray<FMDCharacterCustomizationSlotSummary> UMD_CharacterCustomizationSubsystem::GetCustomizationSlotSummaries() const
{
	TArray<FMDCharacterCustomizationSlotSummary> Summaries;
	const int32 MaxSlots = GetMaxSaveSlots();
	Summaries.Reserve(MaxSlots);

	UMD_CharacterCustomizationSubsystem* MutableThis = const_cast<UMD_CharacterCustomizationSubsystem*>(this);
	if (!MutableThis->EnsureSaveLoaded())
	{
		return Summaries;
	}

	for (int32 SlotIndex = 0; SlotIndex < MaxSlots; ++SlotIndex)
	{
		FMDCharacterCustomizationSlotSummary Summary;
		Summary.SlotIndex = SlotIndex;

		if (const FMDCharacterCustomizationSlotData* Slot = FindSlot(SlotIndex))
		{
			Summary.bOccupied = Slot->LastModified != FDateTime::MinValue();
			Summary.SlotLabel = Slot->SlotLabel;
			Summary.LastModified = Slot->LastModified;
		}

		Summaries.Add(Summary);
	}

	return Summaries;
}

bool UMD_CharacterCustomizationSubsystem::ApplyAppearanceToComponent(
	UMD_CharacterCustomizationComponent* Component,
	const FMDCharacterCustomizationAppearance& Appearance)
{
	return IsValid(Component) && Component->ApplyAppearance(Appearance);
}

bool UMD_CharacterCustomizationSubsystem::IsValidSlotIndex(int32 SlotIndex) const
{
	return SlotIndex >= 0 && SlotIndex < GetMaxSaveSlots();
}

bool UMD_CharacterCustomizationSubsystem::EnsureSaveLoaded() const
{
	if (bSaveLoaded && SaveData)
	{
		return true;
	}

	const UMD_CharacterCustomizationSettings* Settings = UMD_CharacterCustomizationSettings::Get();
	const FString SlotName = (Settings && !Settings->SaveSlotName.IsEmpty())
		? Settings->SaveSlotName
		: FString(MDCharacterCustomization::SaveSlotName);
	const int32 MaxSlots = Settings
		? FMath::Clamp(Settings->MaxSaveSlots, 1, 32)
		: MDCharacterCustomization::MaxSaveSlots;

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UGameplayStatics::DoesSaveGameExist(SlotName, 0))
		{
			SaveData = Cast<UMD_CharacterCustomizationSaveGame>(
				UGameplayStatics::LoadGameFromSlot(SlotName, 0));
		}
	}

	if (!SaveData)
	{
		SaveData = Cast<UMD_CharacterCustomizationSaveGame>(
			UGameplayStatics::CreateSaveGameObject(UMD_CharacterCustomizationSaveGame::StaticClass()));
	}

	if (!SaveData)
	{
		return false;
	}

	SaveData->Slots.SetNum(MaxSlots);
	for (int32 SlotIndex = 0; SlotIndex < MaxSlots; ++SlotIndex)
	{
		SaveData->Slots[SlotIndex].SlotIndex = SlotIndex;
	}

	bSaveLoaded = true;
	return true;
}

bool UMD_CharacterCustomizationSubsystem::PersistSave() const
{
	if (!SaveData)
	{
		return false;
	}

	const UMD_CharacterCustomizationSettings* Settings = UMD_CharacterCustomizationSettings::Get();
	const FString SlotName = (Settings && !Settings->SaveSlotName.IsEmpty())
		? Settings->SaveSlotName
		: FString(MDCharacterCustomization::SaveSlotName);

	return UGameplayStatics::SaveGameToSlot(SaveData, SlotName, 0);
}

const FMDCharacterCustomizationSlotData* UMD_CharacterCustomizationSubsystem::FindSlot(int32 SlotIndex) const
{
	if (!SaveData || !IsValidSlotIndex(SlotIndex))
	{
		return nullptr;
	}

	return &SaveData->Slots[SlotIndex];
}

FMDCharacterCustomizationSlotData* UMD_CharacterCustomizationSubsystem::FindMutableSlot(int32 SlotIndex)
{
	if (!SaveData || !IsValidSlotIndex(SlotIndex))
	{
		return nullptr;
	}

	return &SaveData->Slots[SlotIndex];
}
