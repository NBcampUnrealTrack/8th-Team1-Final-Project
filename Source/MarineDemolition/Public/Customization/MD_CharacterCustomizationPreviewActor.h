#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MD_CharacterCustomizationPreviewActor.generated.h"

class UMD_CharacterCustomizationComponent;
class USceneCaptureComponent2D;
class USkeletalMesh;
class USkeletalMeshComponent;
class UTextureRenderTarget2D;
class UMaterialInterface;

/**
 * Offscreen 3D character for the customization UI.
 * Body capture → left preview image.
 * Face capture → right expression panel (live head close-up).
 */
UCLASS()
class MARINEDEMOLITION_API AMD_CharacterCustomizationPreviewActor : public AActor
{
	GENERATED_BODY()

public:
	AMD_CharacterCustomizationPreviewActor();

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization|Preview")
	void ConfigurePreview(
		USkeletalMesh* InMesh,
		UMaterialInterface* InMaterial,
		UTextureRenderTarget2D* InBodyRenderTarget,
		UTextureRenderTarget2D* InFaceRenderTarget = nullptr);

	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization|Preview")
	void SetOrbitEnabled(bool bEnabled);

	/**
	 * Map a point in the face capture view (0..1, top-left origin) to mesh UV0.
	 * Used so drawing lands on the visor glass instead of a flat canvas.
	 */
	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Customization|Preview")
	bool ProjectFaceViewToMeshUV(FVector2D ViewUV01, FVector2D& OutMeshUV) const;

	UFUNCTION(BlueprintPure, Category = "MarineDemolition|Customization|Preview")
	UMD_CharacterCustomizationComponent* GetCustomizationComponent() const { return CustomizationComponent; }

	UFUNCTION(BlueprintPure, Category = "MarineDemolition|Customization|Preview")
	USkeletalMeshComponent* GetPreviewMesh() const { return PreviewMesh; }

protected:
	virtual void Tick(float DeltaSeconds) override;

	void ConfigureCapture(
		USceneCaptureComponent2D* Capture,
		UTextureRenderTarget2D* RenderTarget,
		const FVector& RelativeLocation,
		const FVector& FocusPoint) const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USkeletalMeshComponent> PreviewMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneCaptureComponent2D> SceneCapture;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneCaptureComponent2D> FaceSceneCapture;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UMD_CharacterCustomizationComponent> CustomizationComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<class UPointLightComponent> PreviewLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<class UPointLightComponent> FillLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<class UDirectionalLightComponent> KeyLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<class USkyLightComponent> SkyLight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview")
	float OrbitYawSpeed = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview")
	FVector CaptureRelativeLocation = FVector(-220.f, 0.f, 100.f);

	/** Close-up of helmet / face plate for the expression draw panel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview")
	FVector FaceCaptureRelativeLocation = FVector(-55.f, 0.f, 155.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview")
	bool bOrbitEnabled = true;
};
