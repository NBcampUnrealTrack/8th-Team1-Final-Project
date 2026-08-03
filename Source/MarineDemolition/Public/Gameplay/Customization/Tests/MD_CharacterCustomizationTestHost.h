#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MD_CharacterCustomizationTestHost.generated.h"

class UMD_CharacterCustomizationComponent;
class UMD_CharacterCustomizationWidget;
class USkeletalMesh;
class USkeletalMeshComponent;
class UMaterialInterface;
class UPointLightComponent;

/**
 * Drop-in host for /Game/Maps/L_MD_CustomizationTest.
 * Shows WBP_MD_CharacterCustomization and drives a visible world mesh.
 */
UCLASS()
class MARINEDEMOLITION_API AMD_CharacterCustomizationTestHost : public AActor
{
	GENERATED_BODY()

public:
	AMD_CharacterCustomizationTestHost();

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization|Test")
	void OpenCustomizationUI();

	UFUNCTION(BlueprintPure, Category = "MarineDemolition|Customization|Test")
	UMD_CharacterCustomizationComponent* GetCustomizationComponent() const { return CustomizationComponent; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USkeletalMeshComponent> CharacterMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMD_CharacterCustomizationComponent> CustomizationComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPointLightComponent> KeyLight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization|Test")
	TSoftClassPtr<UMD_CharacterCustomizationWidget> CustomizationWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization|Test")
	TSoftObjectPtr<USkeletalMesh> CharacterMeshAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization|Test")
	TSoftObjectPtr<UMaterialInterface> CharacterMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization|Test")
	bool bOpenUIOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization|Test")
	bool bUseUIOnlyInput = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization|Test")
	int32 WidgetZOrder = 10;

	UPROPERTY(Transient)
	TObjectPtr<UMD_CharacterCustomizationWidget> ActiveWidget;

private:
	void ApplyDefaultAssets();
};
