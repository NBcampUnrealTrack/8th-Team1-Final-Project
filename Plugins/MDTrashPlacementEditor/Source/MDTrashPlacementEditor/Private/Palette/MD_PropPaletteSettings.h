#pragma once

#include "CoreMinimal.h"
#include "Tools/MD_PropPlacementSettings.h"
#include "UObject/Object.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/SoftObjectPath.h"
#include "MD_PropPaletteSettings.generated.h"

class UStaticMesh;
struct FAssetData;

USTRUCT()
struct FMD_PropPaletteEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid Id;

	/** Explicitly registered asset; Blueprint entries reference the Blueprint asset, not a guessed generated-class path. */
	UPROPERTY()
	FSoftObjectPath AssetPath;

	/** Optional palette-only name. Empty labels follow the source asset name. */
	UPROPERTY()
	FString Label;

	UPROPERTY()
	FName Category;

	/** Explicit membership in the mixed placement brush; independent of list filtering. */
	UPROPERTY()
	bool bUseInMix = false;

	UPROPERTY()
	FMD_PropPlacementSettings Placement;

	UPROPERTY()
	TSoftObjectPtr<UStaticMesh> BlueprintPreviewMesh;

	FString GetDisplayName() const;
	bool operator==(const FMD_PropPaletteEntry& Other) const;
};

/** Project palette collection. Edits stay in memory until Save Palette writes this config section. */
UCLASS(config = Editor, defaultconfig)
class UMD_PropPaletteSettings : public UObject
{
	GENERATED_BODY()

public:
	const TArray<FMD_PropPaletteEntry>& GetEntries() const { return Entries; }
	const FMD_PropPaletteEntry* FindEntry(const FGuid& Id) const;
	int32 AddAssets(const TArray<FAssetData>& Assets, int32& OutDuplicates, int32& OutRejected);
	FGuid DuplicateEntry(const FGuid& Id);
	bool RemoveEntry(const FGuid& Id);
	void SetEntryName(const FGuid& Id, const FString& Name);
	void SetUseInMix(const FGuid& Id, bool bEnabled);
	void SetCategory(const FGuid& Id, const FString& Category);
	void SetPlacementSettings(const FGuid& Id, const FMD_PropPlacementSettings& Placement, TSoftObjectPtr<UStaticMesh> PreviewMesh);
	bool SavePalette(FText& OutError);
	void ReplaceEntries(const TArray<FMD_PropPaletteEntry>& NewEntries);
	bool HasUnsavedChanges() const { return bUnsavedChanges; }
	void HandleAssetRenamed(const FAssetData& Asset, const FString& OldObjectPath);

	/** List metadata changed; pose-only edits update the dirty flag without rebuilding thumbnails. */
	FSimpleMulticastDelegate OnChanged;
	FSimpleMulticastDelegate OnPlacementChanged;

private:
	void MarkChanged(bool bListChanged = true);

	UPROPERTY(config)
	TArray<FMD_PropPaletteEntry> Entries;

	bool bUnsavedChanges = false;
};

/** Shared type check for registration and selection. Does not create an actor. */
bool MDResolvePropPaletteAsset(UObject* Asset, UStaticMesh*& OutMesh, UClass*& OutActorClass);
