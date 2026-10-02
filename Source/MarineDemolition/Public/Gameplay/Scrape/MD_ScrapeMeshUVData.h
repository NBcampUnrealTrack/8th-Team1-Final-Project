#pragma once

#include "Engine/DataAsset.h"
#include "MD_ScrapeMeshUVData.generated.h"

class UStaticMesh;

/** Read-only compatibility for mesh exports referenced by unchanged designer levels. */
UCLASS()
class MARINEDEMOLITION_API UMD_ScrapeMeshUVData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<UStaticMesh> SourceMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> BakedRenderMesh;
};
