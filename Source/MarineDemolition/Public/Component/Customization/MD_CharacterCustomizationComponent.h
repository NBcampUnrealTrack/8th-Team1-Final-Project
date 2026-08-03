#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Types/Customization/MD_CharacterCustomizationTypes.h"
#include "MD_CharacterCustomizationComponent.generated.h"

class UMaterialInstanceDynamic;
class UMeshComponent;
class UTexture;
class UTexture2D;

UCLASS(ClassGroup = (MarineDemolition), meta = (BlueprintSpawnableComponent))
class MARINEDEMOLITION_API UMD_CharacterCustomizationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/**
	 * Attach this component on the character Blueprint (e.g. BP_MDPlayerCharacter).
	 * Do not CreateDefaultSubobject it inside AMD_Character C++ — keeps merge conflicts off MD_Character.
	 */
	UMD_CharacterCustomizationComponent();

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization")
	bool ApplyAppearance(const FMDCharacterCustomizationAppearance& Appearance);

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization")
	void ApplyPartColors(const FMDCharacterCustomizationAppearance& Appearance);

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization")
	void ApplyPartColor(EMDCharacterCustomizationPart Part, FLinearColor NewColor);

	/** Accepts Texture2D or TextureRenderTarget2D (live expression canvas). */
	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization")
	void ApplyExpressionTexture(UTexture* ExpressionTexture);

	UFUNCTION(BlueprintPure, Category = "MarineDemolition|Customization")
	const FMDCharacterCustomizationAppearance& GetCurrentAppearance() const { return CurrentAppearance; }

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization")
	void SetCurrentAppearance(const FMDCharacterCustomizationAppearance& Appearance);

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization|Material")
	FName VisorColorParameter = TEXT("VisorColor");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization|Material")
	FName FaceColorParameter = TEXT("FaceColor");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization|Material")
	FName TorsoColorParameter = TEXT("TorsoColor");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization|Material")
	FName HandsColorParameter = TEXT("HandsColor");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization|Material")
	FName RimColorParameter = TEXT("RimColor");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization|Material")
	FName ExpressionTextureParameter = TEXT("ExpressionTexture");

	/** Gates Face expression overlay. Material default is 0 so FaceColor is not replaced by DefaultTexture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization|Material")
	FName ExpressionBlendParameter = TEXT("ExpressionBlend");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization|Material")
	int32 MaterialSlotIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization|Material")
	bool bApplyToAllMeshComponents = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization|Material")
	TArray<TObjectPtr<UMeshComponent>> TargetMeshes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Customization")
	FMDCharacterCustomizationAppearance CurrentAppearance;

private:
	bool ApplyToMesh(UMeshComponent* MeshComponent);
	UMaterialInstanceDynamic* GetOrCreateDynamicMaterial(UMeshComponent* MeshComponent);
	void ApplyParametersToMaterial(UMaterialInstanceDynamic* DynamicMaterial);
	void ReleaseRuntimeExpressionTexture();
	TArray<UMeshComponent*> GatherTargetMeshes() const;
	FName GetParameterNameForPart(EMDCharacterCustomizationPart Part) const;

	UPROPERTY(Transient)
	TMap<TObjectPtr<UMeshComponent>, TObjectPtr<UMaterialInstanceDynamic>> DynamicMaterials;

	UPROPERTY(Transient)
	TObjectPtr<UTexture> RuntimeExpressionTexture;
};
