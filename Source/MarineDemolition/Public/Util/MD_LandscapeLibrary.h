// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MD_LandscapeLibrary.generated.h"

class ALandscape;
class ULandscapeLayerInfoObject;

/**
 * Editor-only helpers for Lobby landscape create / paint layers.
 */
UCLASS()
class MARINEDEMOLITION_API UMD_LandscapeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Landscape", meta = (DevelopmentOnly))
	static ALandscape* CreateLandscapeFromR16(
		const FString& R16Path,
		int32 SizeX,
		int32 SizeY,
		int32 SectionSizeQuads,
		int32 SectionsPerComponent,
		int32 ComponentsX,
		int32 ComponentsY,
		FVector Scale,
		FVector Location,
		const FString& ActorLabel
	);

	/** Register a paint Target Layer on a Landscape (needed for Landscape Paint mode). */
	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Landscape", meta = (DevelopmentOnly))
	static bool RegisterLandscapeTargetLayer(
		ALandscape* Landscape,
		FName LayerName,
		ULandscapeLayerInfoObject* LayerInfo
	);

	/**
	 * Registers Seabed / Plains / Mountain target layers using LI_* assets under
	 * /Game/Environment/Lobby/Landscape/Layers. Returns how many layers were added/updated.
	 */
	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Landscape", meta = (DevelopmentOnly))
	static int32 RegisterLobbyLandscapePaintLayers(ALandscape* Landscape);
};
