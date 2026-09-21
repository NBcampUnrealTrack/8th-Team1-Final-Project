#include "Tools/MD_PropPlacementTool.h"

#include "Palette/MD_PropPaletteSettings.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"

#define LOCTEXT_NAMESPACE "MDPropToolPresets"

FMD_PropPlacementPresetData UMD_PropPlacementProperties::CapturePreset() const
{
	FMD_PropPlacementPresetData Data;
	Data.Entries = GetDefault<UMD_PropPaletteSettings>()->GetEntries();
	Data.SelectedEntryId = ActivePaletteEntryId;
	if (!Data.Entries.ContainsByPredicate([this](const FMD_PropPaletteEntry& Entry) { return Entry.Id == ActivePaletteEntryId; }))
	{
		Data.SelectedEntryId.Invalidate();
		UObject* SourceAsset = Source == EMD_PropPlacementSource::StaticMesh ? static_cast<UObject*>(Mesh.Get())
			: ActorClass ? ActorClass->ClassGeneratedBy.Get() : nullptr;
		Data.DirectEntry.AssetPath = IsValid(SourceAsset) ? FSoftObjectPath(SourceAsset) : UnavailablePresetSource;
		Data.DirectEntry.Placement = Placement;
		Data.DirectEntry.BlueprintPreviewMesh = Source == EMD_PropPlacementSource::Blueprint
			? BlueprintPreviewMesh : TSoftObjectPtr<UStaticMesh>();
	}
	Data.bBlueprintSource = Source == EMD_PropPlacementSource::Blueprint;
	Data.bPaletteMix = SelectionMode == EMD_PropBrushSelection::PaletteMix;
	Data.bContinuous = bContinuous;
	Data.Spacing = Spacing;
	Data.MaxPerStroke = MaxPerStroke;
	Data.MixSeed = MixSeed;
	Data.RotationAxis = RotationAxis;
	Data.RotationStep = RotationStep;
	Data.Sanitize();
	return Data;
}

void UMD_PropPlacementProperties::ApplyPreset(const FMD_PropPlacementPresetData& Data, FText& OutWarning)
{
	FMD_PropPlacementPresetData Snapshot = Data;
	Snapshot.Sanitize();
	OutWarning = FText::GetEmpty();
	GetMutableDefault<UMD_PropPaletteSettings>()->ReplaceEntries(Snapshot.Entries);
	const FMD_PropPaletteEntry* Selected = Snapshot.Entries.FindByPredicate([&Snapshot](const FMD_PropPaletteEntry& Entry)
		{ return Entry.Id == Snapshot.SelectedEntryId; });
	ActivePaletteEntryId = Selected ? Selected->Id : FGuid();
	PaletteSelectedEntryId = ActivePaletteEntryId;
	const FMD_PropPaletteEntry& SourceEntry = Selected ? *Selected : Snapshot.DirectEntry;
	Placement = SourceEntry.Placement;
	BlueprintPreviewMesh = SourceEntry.BlueprintPreviewMesh;
	Source = Snapshot.bBlueprintSource ? EMD_PropPlacementSource::Blueprint : EMD_PropPlacementSource::StaticMesh;
	Mesh = nullptr;
	ActorClass = nullptr;
	UnavailablePresetSource = Selected ? FSoftObjectPath() : SourceEntry.AssetPath;
	if (!SourceEntry.AssetPath.IsNull())
	{
		UStaticMesh* ResolvedMesh = nullptr;
		UClass* ResolvedClass = nullptr;
		if (MDResolvePropPaletteAsset(SourceEntry.AssetPath.TryLoad(), ResolvedMesh, ResolvedClass))
		{
			Mesh = ResolvedMesh;
			ActorClass = ResolvedClass;
			Source = ResolvedMesh ? EMD_PropPlacementSource::StaticMesh : EMD_PropPlacementSource::Blueprint;
			UnavailablePresetSource.Reset();
		}
		else
		{
			OutWarning = LOCTEXT("SourceMissing", "프리셋은 불러왔지만 선택된 소품을 사용할 수 없습니다.\n\n등록과 설정은 유지했습니다. 에셋 참조를 확인하거나 다른 항목을 선택하세요.");
		}
	}
	// A preset configures placement; loading it never starts an erase action or modifies level actors.
	Action = EMD_PropBrushAction::Place;
	SelectionMode = Snapshot.bPaletteMix ? EMD_PropBrushSelection::PaletteMix : EMD_PropBrushSelection::SelectedItem;
	bContinuous = Snapshot.bContinuous;
	Spacing = Snapshot.Spacing;
	MaxPerStroke = Snapshot.MaxPerStroke;
	MixSeed = Snapshot.MixSeed;
	RotationAxis = Snapshot.RotationAxis;
	RotationStep = Snapshot.RotationStep;
	PaletteSearchText.Reset();
	PaletteCategoryFilter = NAME_None;
	RememberPresetBaseline(true);
	OnPresetApplied.ExecuteIfBound();
	OnPaletteSelectionChanged.Broadcast();
}

void UMD_PropPlacementProperties::RememberPresetBaseline(bool bSaved)
{
	PresetBaseline = CapturePreset();
	bPresetBaselineSaved = bSaved;
}

bool UMD_PropPlacementProperties::HasUnsavedPresetChanges() const
{
	return !bPresetBaselineSaved || !(CapturePreset() == PresetBaseline);
}

void UMD_PropPlacementTool::HandlePresetApplied()
{
	FinishStroke();
	PreviewCandidateIndex = 0;
	InvalidatePreview();
	NotifyOfPropertyChangeByTool(Properties);
}

#undef LOCTEXT_NAMESPACE
