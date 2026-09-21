#pragma once

#include "CoreMinimal.h"
#include "Tools/MD_PropPlacementPreview.h"
#include "Tools/MD_PropPlacementSettings.h"
#include "Templates/SubclassOf.h"
#include "UObject/SoftObjectPtr.h"
#include "MD_PropPlacementCandidate.generated.h"

class AActor;
class UStaticMesh;
struct FHitResult;

/** Resolved source and geometry shared by preview and actual placement. No gameplay actor is spawned here. */
USTRUCT()
struct FMD_PropPlacementCandidate
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UStaticMesh> Mesh = nullptr;

	UPROPERTY()
	TSubclassOf<AActor> ActorClass;

	UPROPERTY()
	TArray<FMD_PropPlacementPreviewPart> Parts;

	FMD_PropPlacementSettings Settings;
	FVector ActorScale = FVector::OneVector;
	FGuid EntryId;
	FString Label;

	bool Resolve(UStaticMesh* InMesh, UClass* InActorClass, TSoftObjectPtr<UStaticMesh> PreviewMesh, FText& OutError);
	FTransform BuildTransform(const FHitResult& Hit, uint32 CandidateIndex) const;
};
