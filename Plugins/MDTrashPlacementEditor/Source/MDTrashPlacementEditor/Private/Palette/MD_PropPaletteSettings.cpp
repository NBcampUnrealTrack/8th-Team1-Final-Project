#include "Palette/MD_PropPaletteSettings.h"

#include "AssetRegistry/AssetData.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"

#define LOCTEXT_NAMESPACE "MDPropPaletteSettings"

bool MDResolvePropPaletteAsset(UObject* Asset, UStaticMesh*& OutMesh, UClass*& OutActorClass)
{
	OutMesh = nullptr;
	OutActorClass = nullptr;
	if (!IsValid(Asset))
	{
		return false;
	}
	if (UStaticMesh* Mesh = Cast<UStaticMesh>(Asset))
	{
		OutMesh = Mesh;
		return true;
	}
	const UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
	UClass* Class = Blueprint ? Blueprint->GeneratedClass.Get() : nullptr;
	if (IsValid(Class) && Cast<UBlueprintGeneratedClass>(Class) && Class->IsChildOf(AActor::StaticClass())
		&& !Class->HasAnyClassFlags(CLASS_Abstract | CLASS_NotPlaceable | CLASS_Transient | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		OutActorClass = Class;
		return true;
	}
	return false;
}

FString FMD_PropPaletteEntry::GetDisplayName() const
{
	return Label.IsEmpty() ? AssetPath.GetAssetName() : Label;
}

bool FMD_PropPaletteEntry::operator==(const FMD_PropPaletteEntry& Other) const
{
	return Id == Other.Id && AssetPath == Other.AssetPath && Label == Other.Label && Category == Other.Category
		&& bUseInMix == Other.bUseInMix && Placement == Other.Placement && BlueprintPreviewMesh == Other.BlueprintPreviewMesh;
}

void UMD_PropPaletteSettings::ReplaceEntries(const TArray<FMD_PropPaletteEntry>& NewEntries)
{
	if (Entries != NewEntries)
	{
		Entries = NewEntries;
		MarkChanged();
	}
}

const FMD_PropPaletteEntry* UMD_PropPaletteSettings::FindEntry(const FGuid& Id) const
{
	return Entries.FindByPredicate([&Id](const FMD_PropPaletteEntry& Entry) { return Entry.Id == Id; });
}

int32 UMD_PropPaletteSettings::AddAssets(const TArray<FAssetData>& Assets, int32& OutDuplicates, int32& OutRejected)
{
	int32 Added = 0;
	OutDuplicates = 0;
	OutRejected = 0;
	for (const FAssetData& AssetData : Assets)
	{
		// Loading happens on explicit registration, never while filtering or hovering over a drop target.
		UObject* Asset = AssetData.IsValid() ? AssetData.GetAsset() : nullptr;
		UStaticMesh* Mesh = nullptr;
		UClass* ActorClass = nullptr;
		if (!MDResolvePropPaletteAsset(Asset, Mesh, ActorClass))
		{
			++OutRejected;
			continue;
		}
		const FSoftObjectPath Path(Asset);
		if (Entries.ContainsByPredicate([&Path](const FMD_PropPaletteEntry& Entry) { return Entry.AssetPath == Path; }))
		{
			++OutDuplicates;
			continue;
		}
		FMD_PropPaletteEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.Id = FGuid::NewGuid();
		Entry.AssetPath = Path;
		++Added;
	}
	if (Added > 0)
	{
		MarkChanged();
	}
	return Added;
}

FGuid UMD_PropPaletteSettings::DuplicateEntry(const FGuid& Id)
{
	const FMD_PropPaletteEntry* Source = FindEntry(Id);
	if (!Source)
	{
		return FGuid();
	}
	// Copy before adding to Entries, since growing the array may invalidate Source.
	FMD_PropPaletteEntry Copy = *Source;
	Copy.Id = FGuid::NewGuid();
	const FText SourceName = FText::FromString(Source->GetDisplayName());
	Copy.Label = FText::Format(LOCTEXT("CopyName", "{0} - Copy"), SourceName).ToString();
	for (int32 Suffix = 2; Entries.ContainsByPredicate([&Copy](const FMD_PropPaletteEntry& Entry)
		{ return Entry.GetDisplayName().Equals(Copy.Label, ESearchCase::IgnoreCase); }); ++Suffix)
	{
		Copy.Label = FText::Format(LOCTEXT("NumberedCopyName", "{0} - Copy {1}"), SourceName, FText::AsNumber(Suffix)).ToString();
	}
	const FGuid NewId = Copy.Id;
	Entries.Add(MoveTemp(Copy));
	MarkChanged();
	return NewId;
}

void UMD_PropPaletteSettings::SetEntryName(const FGuid& Id, const FString& Name)
{
	FMD_PropPaletteEntry* Entry = Entries.FindByPredicate([&Id](const FMD_PropPaletteEntry& Item) { return Item.Id == Id; });
	const FString NewLabel = Name.TrimStartAndEnd();
	if (Entry && Entry->Label != NewLabel)
	{
		Entry->Label = NewLabel;
		MarkChanged();
	}
}

bool UMD_PropPaletteSettings::RemoveEntry(const FGuid& Id)
{
	if (Entries.RemoveAll([&Id](const FMD_PropPaletteEntry& Entry) { return Entry.Id == Id; }) > 0)
	{
		MarkChanged();
		return true;
	}
	return false;
}

void UMD_PropPaletteSettings::SetUseInMix(const FGuid& Id, bool bEnabled)
{
	FMD_PropPaletteEntry* Entry = Entries.FindByPredicate([&Id](const FMD_PropPaletteEntry& Item) { return Item.Id == Id; });
	if (Entry && Entry->bUseInMix != bEnabled)
	{
		Entry->bUseInMix = bEnabled;
		MarkChanged();
	}
}

void UMD_PropPaletteSettings::SetCategory(const FGuid& Id, const FString& Category)
{
	FMD_PropPaletteEntry* Entry = Entries.FindByPredicate([&Id](const FMD_PropPaletteEntry& Item) { return Item.Id == Id; });
	const FName NewCategory(*Category.TrimStartAndEnd());
	if (Entry && Entry->Category != NewCategory)
	{
		Entry->Category = NewCategory;
		MarkChanged();
	}
}

void UMD_PropPaletteSettings::SetPlacementSettings(
	const FGuid& Id, const FMD_PropPlacementSettings& Placement, TSoftObjectPtr<UStaticMesh> PreviewMesh)
{
	FMD_PropPaletteEntry* Entry = Entries.FindByPredicate([&Id](const FMD_PropPaletteEntry& Item) { return Item.Id == Id; });
	FMD_PropPlacementSettings Sanitized = Placement;
	Sanitized.Sanitize();
	if (Entry && (!(Entry->Placement == Sanitized) || Entry->BlueprintPreviewMesh != PreviewMesh))
	{
		Entry->Placement = Sanitized;
		Entry->BlueprintPreviewMesh = PreviewMesh;
		MarkChanged(false);
		OnPlacementChanged.Broadcast();
	}
}

bool UMD_PropPaletteSettings::SavePalette(FText& OutError)
{
	OutError = FText::GetEmpty();
	if (!TryUpdateDefaultConfigFile())
	{
		OutError = FText::Format(LOCTEXT("SaveFailed", "팔레트를 저장하지 못했습니다.\n\n아래 설정 파일에 쓸 수 있는지 확인한 뒤 다시 저장하세요.\n{0}"), FText::FromString(GetDefaultConfigFilename()));
		return false;
	}
	bUnsavedChanges = false;
	OnChanged.Broadcast();
	return true;
}

void UMD_PropPaletteSettings::HandleAssetRenamed(const FAssetData& Asset, const FString& OldObjectPath)
{
	const FSoftObjectPath OldPath(OldObjectPath);
	bool bChanged = false;
	for (FMD_PropPaletteEntry& Entry : Entries)
	{
		if (Entry.AssetPath == OldPath)
		{
			Entry.AssetPath = Asset.GetSoftObjectPath();
			bChanged = true;
		}
		if (Entry.BlueprintPreviewMesh.ToSoftObjectPath() == OldPath)
		{
			Entry.BlueprintPreviewMesh = TSoftObjectPtr<UStaticMesh>(Asset.GetSoftObjectPath());
			bChanged = true;
		}
	}
	if (bChanged)
	{
		MarkChanged();
	}
}

void UMD_PropPaletteSettings::MarkChanged(bool bListChanged)
{
	bUnsavedChanges = true;
	if (bListChanged)
	{
		OnChanged.Broadcast();
	}
}

#undef LOCTEXT_NAMESPACE
