#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Types/Customization/MD_CharacterCustomizationTypes.h"
#include "MD_CharacterCustomizationSubsystem.generated.h"

class UTextureRenderTarget2D;
class UTexture2D;
class UMD_CharacterCustomizationComponent;
class UMD_CharacterCustomizationSaveGame;

UCLASS()
class MARINEDEMOLITION_API UMD_CharacterCustomizationSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintPure, Category = "MarineDemolition|Customization")
	static UMD_CharacterCustomizationSubsystem* Get(const UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "MarineDemolition|Customization")
	int32 GetMaxSaveSlots() const;

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization|Face")
	UTextureRenderTarget2D* CreateFaceRenderTarget();

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization|Face")
	void ClearFaceRenderTarget(UTextureRenderTarget2D* RenderTarget, FLinearColor ClearColor);

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization|Face")
	void FillFaceRenderTarget(UTextureRenderTarget2D* RenderTarget, FLinearColor FillColor);

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization|Face")
	void DrawFaceLine(
		UTextureRenderTarget2D* RenderTarget,
		FVector2D Start,
		FVector2D End,
		FLinearColor LineColor,
		float Thickness = 4.f);

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization|Face")
	void DrawFacePoint(
		UTextureRenderTarget2D* RenderTarget,
		FVector2D Position,
		FLinearColor PointColor,
		float Radius = 6.f);

	/**
	 * Stamp along a UV-space segment. If Start→End jumps farther than MaxSeamPixels,
	 * only the end stamp is drawn (avoids bridging UV island cracks).
	 */
	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization|Face")
	void DrawFaceStampStroke(
		UTextureRenderTarget2D* RenderTarget,
		FVector2D Start,
		FVector2D End,
		FLinearColor StampColor,
		float Radius,
		float MaxSeamPixels = 16.f);

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization|Face")
	bool ExportFaceRenderTargetToPNG(UTextureRenderTarget2D* RenderTarget, TArray<uint8>& OutPNGBytes) const;

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization|Face")
	UTexture2D* CreateFaceTextureFromPNG(const TArray<uint8>& PNGBytes, const FString& TextureName = TEXT("MD_FaceTexture"));

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization")
	FMDCharacterCustomizationAppearance BuildAppearanceFromRenderTarget(
		UTextureRenderTarget2D* ExpressionRenderTarget,
		const FMDCharacterCustomizationAppearance& PartColors) const;

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization")
	bool SaveCustomizationSlot(int32 SlotIndex, const FMDCharacterCustomizationAppearance& Appearance, const FString& SlotLabel = FString());

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization")
	bool LoadCustomizationSlot(int32 SlotIndex, FMDCharacterCustomizationAppearance& OutAppearance, FString& OutSlotLabel) const;

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization")
	bool DeleteCustomizationSlot(int32 SlotIndex);

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization")
	bool IsCustomizationSlotOccupied(int32 SlotIndex) const;

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization")
	TArray<FMDCharacterCustomizationSlotSummary> GetCustomizationSlotSummaries() const;

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization")
	bool ApplyAppearanceToComponent(UMD_CharacterCustomizationComponent* Component, const FMDCharacterCustomizationAppearance& Appearance);

private:
	bool IsValidSlotIndex(int32 SlotIndex) const;
	bool EnsureSaveLoaded() const;
	bool PersistSave() const;
	FMDCharacterCustomizationSlotData* FindMutableSlot(int32 SlotIndex);
	const FMDCharacterCustomizationSlotData* FindSlot(int32 SlotIndex) const;

	UPROPERTY()
	mutable TObjectPtr<UMD_CharacterCustomizationSaveGame> SaveData;

	mutable bool bSaveLoaded = false;
};
