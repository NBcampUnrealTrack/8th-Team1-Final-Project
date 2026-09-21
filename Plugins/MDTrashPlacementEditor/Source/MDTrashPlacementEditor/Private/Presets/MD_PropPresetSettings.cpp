#include "Presets/MD_PropPresetSettings.h"

#include "AssetRegistry/AssetData.h"
#include "Engine/StaticMesh.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/OutputDeviceNull.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "MDPropPresetSettings"

FGuid UMD_PropPresetSettings::GetActivePresetId() const
{
	FGuid Id;
	FGuid::Parse(ActivePaletteId, Id);
	return Id;
}

bool UMD_PropPresetSettings::SetActivePreset(const FGuid& Id, FText& OutError)
{
	if (!FindPreset(Id))
	{
		OutError = LOCTEXT("Missing", "팔레트를 먼저 선택하세요.");
		return false;
	}
	const FString Updated = Id.ToString();
	if (ActivePaletteId == Updated)
	{
		return true;
	}
	const FString Previous = ActivePaletteId;
	ActivePaletteId = Updated;
	if (!SaveLibrary(OutError))
	{
		ActivePaletteId = Previous;
		return false;
	}
	return true;
}

const FMD_PropPlacementPreset* UMD_PropPresetSettings::FindPreset(const FGuid& Id) const
{
	return Presets.FindByPredicate([&Id](const FMD_PropPlacementPreset& Preset) { return Preset.Id == Id; });
}

FString UMD_PropPresetSettings::MakeUniqueName(const FString& BaseName) const
{
	const FString Base = BaseName.TrimStartAndEnd().Left(48);
	FString Name = Base.IsEmpty() ? TEXT("Preset") : Base;
	for (int32 Suffix = 2; Presets.ContainsByPredicate([&Name](const FMD_PropPlacementPreset& Preset)
		{ return Preset.Name.Equals(Name, ESearchCase::IgnoreCase); }); ++Suffix)
	{
		Name = FString::Printf(TEXT("%s %d"), Base.IsEmpty() ? TEXT("Preset") : *Base, Suffix);
	}
	return Name;
}

bool UMD_PropPresetSettings::ValidateName(const FString& Name, const FGuid& ExceptId, FText& OutError) const
{
	OutError = FText::GetEmpty();
	const FString Trimmed = Name.TrimStartAndEnd();
	if (Trimmed.IsEmpty() || Trimmed.Len() > 64)
	{
		OutError = LOCTEXT("NameLength", "팔레트 이름은 1~64자로 입력하세요.");
		return false;
	}
	for (const TCHAR Character : Trimmed)
	{
		if (Character < 32 || Character == 127)
		{
			OutError = LOCTEXT("NameControl", "팔레트 이름은 줄바꿈과 탭 없이 한 줄로 입력하세요.");
			return false;
		}
	}
	if (Presets.ContainsByPredicate([&Trimmed, &ExceptId](const FMD_PropPlacementPreset& Preset)
		{ return Preset.Id != ExceptId && Preset.Name.Equals(Trimmed, ESearchCase::IgnoreCase); }))
	{
		OutError = LOCTEXT("NameExists", "같은 이름의 팔레트가 있습니다. 다른 이름을 입력하세요.");
		return false;
	}
	return true;
}

FGuid UMD_PropPresetSettings::AddPreset(const FString& Name, const FMD_PropPlacementPresetData& Data, FText& OutError)
{
	if (!ValidateName(Name, FGuid(), OutError))
	{
		return FGuid();
	}
	TArray<FMD_PropPlacementPreset> Updated = Presets;
	FMD_PropPlacementPreset& Added = Updated.AddDefaulted_GetRef();
	Added.Id = FGuid::NewGuid();
	Added.Name = Name.TrimStartAndEnd();
	Added.Data = Data;
	Added.Data.Sanitize();
	const FGuid NewId = Added.Id;
	return Commit(MoveTemp(Updated), OutError) ? NewId : FGuid();
}

bool UMD_PropPresetSettings::UpdatePreset(const FGuid& Id, const FMD_PropPlacementPresetData& Data, FText& OutError)
{
	TArray<FMD_PropPlacementPreset> Updated = Presets;
	FMD_PropPlacementPreset* Preset = Updated.FindByPredicate([&Id](const FMD_PropPlacementPreset& Item) { return Item.Id == Id; });
	if (!Preset)
	{
		OutError = LOCTEXT("Missing", "팔레트를 먼저 선택하세요.");
		return false;
	}
	Preset->Data = Data;
	Preset->Data.Sanitize();
	return Commit(MoveTemp(Updated), OutError);
}

FGuid UMD_PropPresetSettings::DuplicatePreset(const FGuid& Id, FText& OutError)
{
	const FMD_PropPlacementPreset* Preset = FindPreset(Id);
	if (!Preset)
	{
		OutError = LOCTEXT("Missing", "팔레트를 먼저 선택하세요.");
		return FGuid();
	}
	// Duplicate the saved snapshot, not the potentially edited working palette.
	const FMD_PropPlacementPresetData Data = Preset->Data;
	return AddPreset(MakeUniqueName(Preset->Name.Left(41) + TEXT(" - Copy")), Data, OutError);
}

bool UMD_PropPresetSettings::RenamePreset(const FGuid& Id, const FString& Name, FText& OutError)
{
	if (!ValidateName(Name, Id, OutError))
	{
		return false;
	}
	TArray<FMD_PropPlacementPreset> Updated = Presets;
	FMD_PropPlacementPreset* Preset = Updated.FindByPredicate([&Id](const FMD_PropPlacementPreset& Item) { return Item.Id == Id; });
	if (!Preset)
	{
		OutError = LOCTEXT("Missing", "팔레트를 먼저 선택하세요.");
		return false;
	}
	Preset->Name = Name.TrimStartAndEnd();
	return Commit(MoveTemp(Updated), OutError);
}

bool UMD_PropPresetSettings::DeletePreset(const FGuid& Id, FText& OutError)
{
	TArray<FMD_PropPlacementPreset> Updated = Presets;
	if (Updated.RemoveAll([&Id](const FMD_PropPlacementPreset& Item) { return Item.Id == Id; }) == 0)
	{
		OutError = LOCTEXT("Missing", "팔레트를 먼저 선택하세요.");
		return false;
	}
	return Commit(MoveTemp(Updated), OutError);
}

bool UMD_PropPresetSettings::Commit(TArray<FMD_PropPlacementPreset>&& Updated, FText& OutError)
{
	TArray<FMD_PropPlacementPreset> Previous = MoveTemp(Presets);
	Presets = MoveTemp(Updated);
	const FString PreviousActive = ActivePaletteId;
	if (!FindPreset(GetActivePresetId()))
	{
		ActivePaletteId.Reset();
	}
	if (!SaveLibrary(OutError))
	{
		Presets = MoveTemp(Previous);
		ActivePaletteId = PreviousActive;
		return false;
	}
	return true;
}

bool UMD_PropPresetSettings::SaveLibrary(FText& OutError)
{
	OutError = FText::GetEmpty();
	if (!TryUpdateDefaultConfigFile() || !MatchesSavedLibrary())
	{
		OutError = FText::Format(LOCTEXT("SaveFailed", "팔레트를 저장하지 못했습니다.\n\n설정 파일의 쓰기 권한을 확인하고 다시 시도하세요.\n{0}"), FText::FromString(GetDefaultConfigFilename()));
		return false;
	}
	bUnsavedChanges = false;
	OnChanged.Broadcast();
	return true;
}

bool UMD_PropPresetSettings::MatchesSavedLibrary() const
{
	// TryUpdateDefaultConfigFile only reports read-only rejection in this engine version.
	FConfigFile SavedFile;
	if (!SavedFile.Combine(GetDefaultConfigFilename()))
	{
		return false;
	}
	FString SavedActive;
	SavedFile.GetString(*GetClass()->GetPathName(), TEXT("ActivePaletteId"), SavedActive);
	if (SavedActive != ActivePaletteId)
	{
		return false;
	}
	TArray<FString> SavedValues;
	SavedFile.GetArray(*GetClass()->GetPathName(), TEXT("Presets"), SavedValues);
	if (SavedValues.Num() != Presets.Num())
	{
		return false;
	}
	const UScriptStruct* Struct = FMD_PropPlacementPreset::StaticStruct();
	FOutputDeviceNull Errors;
	for (int32 Index = 0; Index < Presets.Num(); ++Index)
	{
		FMD_PropPlacementPreset Saved;
		if (!Struct->ImportText(*SavedValues[Index], &Saved, nullptr, PPF_SerializedAsImportText, &Errors, Struct->GetName()))
		{
			return false;
		}
		// Compare canonical serialized values, respecting the engine's numeric text precision.
		FString ActualText;
		FString ExpectedText;
		Struct->ExportText(ActualText, &Saved, nullptr, nullptr, PPF_SerializedAsImportText, nullptr);
		Struct->ExportText(ExpectedText, &Presets[Index], nullptr, nullptr, PPF_SerializedAsImportText, nullptr);
		if (ActualText != ExpectedText)
		{
			return false;
		}
	}
	return true;
}

void UMD_PropPresetSettings::HandleAssetRenamed(const FAssetData& Asset, const FString& OldObjectPath)
{
	const FSoftObjectPath OldPath(OldObjectPath);
	const FSoftObjectPath NewPath = Asset.GetSoftObjectPath();
	bool bChanged = false;
	const auto UpdateEntry = [&OldPath, &NewPath, &bChanged](FMD_PropPaletteEntry& Entry)
	{
		if (Entry.AssetPath == OldPath)
		{
			Entry.AssetPath = NewPath;
			bChanged = true;
		}
		if (Entry.BlueprintPreviewMesh.ToSoftObjectPath() == OldPath)
		{
			Entry.BlueprintPreviewMesh = TSoftObjectPtr<UStaticMesh>(NewPath);
			bChanged = true;
		}
	};
	for (FMD_PropPlacementPreset& Preset : Presets)
	{
		for (FMD_PropPaletteEntry& Entry : Preset.Data.Entries)
		{
			UpdateEntry(Entry);
		}
		UpdateEntry(Preset.Data.DirectEntry);
	}
	if (bChanged)
	{
		bUnsavedChanges = true;
		OnChanged.Broadcast();
	}
}

#undef LOCTEXT_NAMESPACE
