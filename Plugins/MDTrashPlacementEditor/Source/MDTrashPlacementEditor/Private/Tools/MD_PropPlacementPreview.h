#pragma once

#include "CoreMinimal.h"
#include "MD_PropPlacementPreview.generated.h"

class AActor;
class UStaticMesh;

/** Actor-local visual geometry; never owns or executes a gameplay actor. */
USTRUCT()
struct FMD_PropPlacementPreviewPart
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UStaticMesh> Mesh = nullptr;

	UPROPERTY()
	FTransform LocalTransform = FTransform::Identity;
};

/** Reads native/SCS defaults, including inherited component overrides. */
bool MDCollectPropPreviewParts(
	UClass* ActorClass, TArray<FMD_PropPlacementPreviewPart>& OutParts,
	FVector& OutActorScale, FText& OutError);
