#pragma once

#include "CoreMinimal.h"
#include "Presets/MD_PropPlacementPreset.h"
#include "UObject/Object.h"
#include "MD_PropPresetSettings.generated.h"

struct FAssetData;

/** Named snapshot library. Mutations succeed only after this config section has been saved. */
UCLASS(config = Editor, defaultconfig)
class UMD_PropPresetSettings : public UObject
{
	GENERATED_BODY()

public:
	const TArray<FMD_PropPlacementPreset>& GetPresets() const { return Presets; }
	const FMD_PropPlacementPreset* FindPreset(const FGuid& Id) const;
	FString MakeUniqueName(const FString& BaseName) const;
	bool ValidateName(const FString& Name, const FGuid& ExceptId, FText& OutError) const;
	FGuid AddPreset(const FString& Name, const FMD_PropPlacementPresetData& Data, FText& OutError);
	bool UpdatePreset(const FGuid& Id, const FMD_PropPlacementPresetData& Data, FText& OutError);
	FGuid DuplicatePreset(const FGuid& Id, FText& OutError);
	bool RenamePreset(const FGuid& Id, const FString& Name, FText& OutError);
	bool DeletePreset(const FGuid& Id, FText& OutError);
	bool SaveLibrary(FText& OutError);
	bool HasUnsavedChanges() const { return bUnsavedChanges; }
	void HandleAssetRenamed(const FAssetData& Asset, const FString& OldObjectPath);

	FSimpleMulticastDelegate OnChanged;

private:
	bool Commit(TArray<FMD_PropPlacementPreset>&& Updated, FText& OutError);
	bool MatchesSavedLibrary() const;

	UPROPERTY(config)
	TArray<FMD_PropPlacementPreset> Presets;

	bool bUnsavedChanges = false;
};
