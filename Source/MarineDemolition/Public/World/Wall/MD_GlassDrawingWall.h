#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
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

public:
	// 상호작용 중 표시할 화면의 프로젝트 UI 등록 태그
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|UI", meta = (Categories = "UI.Layer"))
	FGameplayTag DrawingWidgetTag;

	// 현재 벽에 지정된 그리기 화면 태그 반환
	UFUNCTION(BlueprintPure, Category = "Glass|UI")
	FORCEINLINE FGameplayTag GetDrawingWidgetTag() { return DrawingWidgetTag; }

private:
	void ConfigureOwnGlass();
	bool IsViewingFrontGlass(const AMD_Character* Character) const;
};
