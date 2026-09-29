#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MD_SampleProtectionZone.generated.h"

class UBoxComponent;

/** Place around a ladder approach/landing. Queries at damage time avoid overlap-event ordering gaps. */
UCLASS(Blueprintable)
class MARINEDEMOLITION_API AMD_SampleProtectionZone : public AActor
{
	GENERATED_BODY()
public:
	AMD_SampleProtectionZone();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Sample Protection")
	TObjectPtr<UBoxComponent> ProtectionBounds;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sample Protection")
	bool bProtectionEnabled = true;
	UFUNCTION(BlueprintPure, Category="Sample Protection")
	bool ContainsPosition(FVector Position) const;
	static bool ProtectsActor(const AActor* Actor);
};
