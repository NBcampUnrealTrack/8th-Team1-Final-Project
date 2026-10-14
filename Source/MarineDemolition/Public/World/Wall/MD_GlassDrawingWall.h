#pragma once

#include "CoreMinimal.h"
#include "World/Interaction/MD_GlassDrawingDesk.h"
#include "MD_GlassDrawingWall.generated.h"

/** Interact with this wall's own glass; reuse the existing drawing camera and pen session. */
UCLASS(Blueprintable, meta = (DisplayName = "Glass Drawing Wall"))
class MARINEDEMOLITION_API AMD_GlassDrawingWall : public AMD_GlassDrawingDesk
{
	GENERATED_BODY()

public:
	AMD_GlassDrawingWall();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual bool ResolveFocusView(const AMD_Character* Character, float Aspect, FVector& OutLocation,
		FRotator& OutRotation, float& OutFOV, float& OutTraceDistance) const override;

protected:
	virtual void RefreshPenDisplay() override;

private:
	void ConfigureOwnGlass();
	bool IsViewingFrontGlass(const AMD_Character* Character) const;
};
