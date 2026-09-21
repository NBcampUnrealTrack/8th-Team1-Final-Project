#include "Tools/MD_PropPlacementTool.h"

#include "MD_TrashPlacementSettings.h"
#include "Palette/MD_PropPaletteSettings.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "BaseBehaviors/MouseWheelBehavior.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "Math/RandomStream.h"
#include "Templates/TypeHash.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/HitResult.h"
#include "Engine/Level.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "InteractiveToolManager.h"
#include "LevelUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ToolContextInterfaces.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "MDPropPlacementTool"

namespace
{
	constexpr int32 ShiftEraseModifierId = 1;
}

void UMD_PropPlacementProperties::RefreshPreview()
{
	OnRefreshPreview.ExecuteIfBound();
}

void UMD_PropPlacementProperties::RotateBackward()
{
	RotationStep = FMath::IsFinite(RotationStep) ? FMath::Clamp(RotationStep, 1.0, 180.0) : 15.0;
	RotateSelectedAxis(-RotationStep);
}

void UMD_PropPlacementProperties::RotateForward()
{
	RotationStep = FMath::IsFinite(RotationStep) ? FMath::Clamp(RotationStep, 1.0, 180.0) : 15.0;
	RotateSelectedAxis(RotationStep);
}

void UMD_PropPlacementProperties::FlipSelectedAxis()
{
	RotateSelectedAxis(180.0);
}

void UMD_PropPlacementProperties::RotateSelectedAxis(double DeltaDegrees)
{
	if (!IsSelectedPlacement())
	{
		OnPoseEditUnavailable.ExecuteIfBound();
		return;
	}
	Placement.GetRotationAxis(RotationAxis).Rotate(DeltaDegrees);
	StoreActivePaletteSettings();
	OnPlacementSettingsChanged.ExecuteIfBound();
}

void UMD_PropPlacementProperties::ResetPlacementSettings()
{
	if (!IsSelectedPlacement())
	{
		OnPoseEditUnavailable.ExecuteIfBound();
		return;
	}
	Placement = FMD_PropPlacementSettings();
	StoreActivePaletteSettings();
	OnPlacementSettingsChanged.ExecuteIfBound();
}

void UMD_PropPlacementProperties::RerollPreview()
{
	OnRerollPreview.ExecuteIfBound();
}

void UMD_PropPlacementProperties::FreezeCurrentPose()
{
	OnFreezePreview.ExecuteIfBound();
}

void UMD_PropPlacementProperties::SanitizeBrushSettings()
{
	Spacing = FMath::IsFinite(Spacing) ? FMath::Clamp(Spacing, 1.0, 100000.0) : 100.0;
	MaxPerStroke = FMath::Clamp(MaxPerStroke, 1, 5000);
}

void UMD_PropPlacementProperties::StoreActivePaletteSettings()
{
	Placement.Sanitize();
	if (ActivePaletteEntryId.IsValid())
	{
		GetMutableDefault<UMD_PropPaletteSettings>()->SetPlacementSettings(ActivePaletteEntryId, Placement,
			Source == EMD_PropPlacementSource::Blueprint ? BlueprintPreviewMesh : TSoftObjectPtr<UStaticMesh>());
	}
}

bool UMD_PropPlacementProperties::SelectPaletteEntry(
	const FMD_PropPaletteEntry& Entry, FText& OutError, bool bPreservePreviewPose)
{
	UStaticMesh* SelectedMesh = nullptr;
	UClass* SelectedClass = nullptr;
	if (!MDResolvePropPaletteAsset(Entry.AssetPath.TryLoad(), SelectedMesh, SelectedClass))
	{
		OutError = LOCTEXT("PaletteAssetUnavailable", "에셋을 찾을 수 없거나 배치할 수 없는 항목입니다.\n\n메시 또는 컴파일된 액터 블루프린트를 선택하세요.\n계속 사용할 수 없는 항목은 팔레트에서 제거할 수 있습니다.");
		return false;
	}
	const TSoftObjectPtr<UStaticMesh> SelectedPreviewMesh = SelectedClass ? Entry.BlueprintPreviewMesh : TSoftObjectPtr<UStaticMesh>();
	// Only a copy of the same source and settings can continue the current random candidate.
	const bool bKeepCandidate = bPreservePreviewPose && IsSelectedPlacement() && SelectedMesh == Mesh.Get() && SelectedClass == ActorClass.Get()
		&& Placement == Entry.Placement && BlueprintPreviewMesh == SelectedPreviewMesh;
	UnavailablePresetSource.Reset();
	Action = EMD_PropBrushAction::Place;
	SelectionMode = EMD_PropBrushSelection::SelectedItem;
	Placement = Entry.Placement;
	Placement.Sanitize();
	BlueprintPreviewMesh = SelectedPreviewMesh;
	Source = SelectedMesh ? EMD_PropPlacementSource::StaticMesh : EMD_PropPlacementSource::Blueprint;
	Mesh = SelectedMesh;
	ActorClass = SelectedClass;
	ActivePaletteEntryId = Entry.Id;
	PaletteSelectedEntryId = Entry.Id;
	OutError = FText::GetEmpty();
	OnPaletteAssetSelected.ExecuteIfBound(bKeepCandidate);
	OnPaletteSelectionChanged.Broadcast();
	return true;
}

void UMD_PropPlacementProperties::ClearPaletteSelection()
{
	UnavailablePresetSource.Reset();
	ActivePaletteEntryId.Invalidate();
	Mesh = nullptr;
	ActorClass = nullptr;
	BlueprintPreviewMesh.Reset();
	OnPaletteAssetSelected.ExecuteIfBound(false);
	OnPaletteSelectionChanged.Broadcast();
}

bool UMD_PropPlacementToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return SceneState.World && SceneState.World->WorldType == EWorldType::Editor && GEditor && !GEditor->PlayWorld;
}

UInteractiveTool* UMD_PropPlacementToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UMD_PropPlacementTool* Tool = NewObject<UMD_PropPlacementTool>(SceneState.ToolManager);
	Tool->SetTargetWorld(SceneState.World);
	return Tool;
}

void UMD_PropPlacementTool::SetTargetWorld(UWorld* World)
{
	TargetWorld = World;
}

void UMD_PropPlacementTool::Setup()
{
	Super::Setup();
	Properties = NewObject<UMD_PropPlacementProperties>(this);
	Properties->RestoreProperties(this);
	Properties->SanitizeBrushSettings();
	Properties->Placement.Sanitize();
	Properties->OnPaletteAssetSelected.BindUObject(this, &UMD_PropPlacementTool::HandlePaletteAssetSelected);
	Properties->OnRefreshPreview.BindUObject(this, &UMD_PropPlacementTool::InvalidatePreview);
	Properties->OnPlacementSettingsChanged.BindUObject(this, &UMD_PropPlacementTool::HandlePlacementSettingsChanged);
	Properties->OnRerollPreview.BindUObject(this, &UMD_PropPlacementTool::AdvancePreviewCandidate);
	Properties->OnFreezePreview.BindUObject(this, &UMD_PropPlacementTool::FreezePreviewPose);
	Properties->OnPoseEditUnavailable.BindUObject(this, &UMD_PropPlacementTool::ShowPoseEditUnavailable);
	Properties->OnPresetApplied.BindUObject(this, &UMD_PropPlacementTool::HandlePresetApplied);
	UMD_PropPaletteSettings* Palette = GetMutableDefault<UMD_PropPaletteSettings>();
	PaletteChangedHandle = Palette->OnChanged.AddUObject(this, &UMD_PropPlacementTool::HandlePaletteChanged);
	PalettePlacementChangedHandle = Palette->OnPlacementChanged.AddUObject(this, &UMD_PropPlacementTool::HandlePaletteChanged);
	Properties->RememberPresetBaseline(!Palette->HasUnsavedChanges());
	AddToolPropertySource(Properties);

	UClickDragInputBehavior* Click = NewObject<UClickDragInputBehavior>(this);
	Click->Initialize(this);
	Click->Modifiers.RegisterModifier(ShiftEraseModifierId, FInputDeviceState::IsShiftKeyDown);
	Click->bUpdateModifiersDuringDrag = true;
	Click->ModifierCheckFunc = [](const FInputDeviceState& Input)
	{
		return !FInputDeviceState::IsAltKeyDown(Input)
			&& !FInputDeviceState::IsCtrlKeyDown(Input);
	};
	AddInputBehavior(Click);
	UMouseHoverBehavior* Hover = NewObject<UMouseHoverBehavior>(this);
	Hover->Initialize(this);
	Hover->Modifiers.RegisterModifier(ShiftEraseModifierId, FInputDeviceState::IsShiftKeyDown);
	AddInputBehavior(Hover);
	UMouseWheelInputBehavior* Wheel = NewObject<UMouseWheelInputBehavior>(this);
	Wheel->Initialize(this);
	Wheel->ModifierCheckFunc = [](const FInputDeviceState& Input)
	{
		return FInputDeviceState::IsShiftKeyDown(Input)
			&& !FInputDeviceState::IsAltKeyDown(Input) && !FInputDeviceState::IsCtrlKeyDown(Input)
			&& !Input.Mouse.Left.bDown && !Input.Mouse.Middle.bDown && !Input.Mouse.Right.bDown;
	};
	AddInputBehavior(Wheel);
	if (GEditor)
	{
		BlueprintCompiledHandle = GEditor->OnBlueprintCompiled().AddUObject(this, &UMD_PropPlacementTool::InvalidatePreview);
	}
	RebuildPreview();
}

void UMD_PropPlacementTool::Shutdown(EToolShutdownType ShutdownType)
{
	FinishStroke();
	UMD_PropPaletteSettings* Palette = GetMutableDefault<UMD_PropPaletteSettings>();
	Palette->OnChanged.Remove(PaletteChangedHandle);
	Palette->OnPlacementChanged.Remove(PalettePlacementChangedHandle);
	if (GEditor)
	{
		GEditor->OnBlueprintCompiled().Remove(BlueprintCompiledHandle);
	}
	if (Properties)
	{
		Properties->OnPaletteAssetSelected.Unbind();
		Properties->StoreActivePaletteSettings();
		Properties->OnRefreshPreview.Unbind();
		Properties->OnPlacementSettingsChanged.Unbind();
		Properties->OnRerollPreview.Unbind();
		Properties->OnFreezePreview.Unbind();
		Properties->OnPoseEditUnavailable.Unbind();
		Properties->OnPresetApplied.Unbind();
		Properties->SaveProperties(this);
	}
	DestroyPreview();
	PreviewCandidate = FMD_PropPlacementCandidate();
	MixCandidates.Reset();
	EraseHoverActor.Reset();
	TargetWorld.Reset();
	Super::Shutdown(ShutdownType);
}

void UMD_PropPlacementTool::OnTick(float DeltaTime)
{
	Super::OnTick(DeltaTime);
	if (!GEditor || GEditor->PlayWorld || !TargetWorld.IsValid())
	{
		FinishStroke();
		HidePreview();
		EraseHoverActor.Reset();
		return;
	}
	if (bStrokeActive && (!CanEditLevel() || StrokeLevel.Get() != TargetWorld->GetCurrentLevel()))
	{
		FinishStroke();
	}
	if (bPreviewDirty)
	{
		RebuildPreview();
		if (bHasHoverRay)
		{
			OnUpdateHover(LastHoverRay);
		}
	}
}

void UMD_PropPlacementTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	Super::OnPropertyModified(PropertySet, Property);
	if (PropertySet != Properties)
	{
		return;
	}
	FinishStroke();
	const FName Name = Property ? Property->GetFName() : NAME_None;
	if (Name == GET_MEMBER_NAME_CHECKED(UMD_PropPlacementProperties, Action)
		|| Name == GET_MEMBER_NAME_CHECKED(UMD_PropPlacementProperties, SelectionMode)
		|| Name == GET_MEMBER_NAME_CHECKED(UMD_PropPlacementProperties, MixSeed)
		|| Name == GET_MEMBER_NAME_CHECKED(UMD_PropPlacementProperties, Spacing)
		|| Name == GET_MEMBER_NAME_CHECKED(UMD_PropPlacementProperties, bContinuous)
		|| Name == GET_MEMBER_NAME_CHECKED(UMD_PropPlacementProperties, MaxPerStroke))
	{
		Properties->SanitizeBrushSettings();
		if (Name == GET_MEMBER_NAME_CHECKED(UMD_PropPlacementProperties, SelectionMode)
			|| Name == GET_MEMBER_NAME_CHECKED(UMD_PropPlacementProperties, MixSeed))
		{
			PreviewCandidateIndex = 0;
		}
		InvalidatePreview();
		return;
	}
	if (Name == GET_MEMBER_NAME_CHECKED(UMD_PropPlacementProperties, RotationAxis)
		|| Name == GET_MEMBER_NAME_CHECKED(UMD_PropPlacementProperties, RotationStep))
	{
		Properties->RotationStep = FMath::IsFinite(Properties->RotationStep)
			? FMath::Clamp(Properties->RotationStep, 1.0, 180.0) : 15.0;
		return;
	}
	// Direct source/pose edits belong to the editing selection, never a randomly previewed entry.
	if (!Properties->IsSelectedPlacement())
	{
		Properties->Action = EMD_PropBrushAction::Place;
		Properties->SelectionMode = EMD_PropBrushSelection::SelectedItem;
		PreviewCandidateIndex = 0;
		InvalidatePreview();
		NotifyOfPropertyChangeByTool(Properties);
	}
	const bool bSourceChanged = Name == GET_MEMBER_NAME_CHECKED(UMD_PropPlacementProperties, Source)
		|| Name == GET_MEMBER_NAME_CHECKED(UMD_PropPlacementProperties, Mesh)
		|| Name == GET_MEMBER_NAME_CHECKED(UMD_PropPlacementProperties, ActorClass);
	if (bSourceChanged || !Property)
	{
		Properties->ClearUnavailablePresetSource();
		Properties->ActivePaletteEntryId.Invalidate();
		Properties->PaletteSelectedEntryId.Invalidate();
		if (bSourceChanged)
		{
			Properties->BlueprintPreviewMesh.Reset();
		}
		Properties->OnPaletteSelectionChanged.Broadcast();
		PreviewCandidateIndex = 0;
		InvalidatePreview();
		return;
	}
	Properties->StoreActivePaletteSettings();
	if (Name == GET_MEMBER_NAME_CHECKED(UMD_PropPlacementProperties, BlueprintPreviewMesh))
	{
		InvalidatePreview();
	}
	else
	{
		HandlePlacementSettingsChanged();
	}
}

EMD_PropBrushAction UMD_PropPlacementTool::GetEffectiveAction() const
{
	if (bStrokeActive)
	{
		return StrokeAction;
	}
	return bShiftErase ? EMD_PropBrushAction::Erase : Properties->Action;
}

void UMD_PropPlacementTool::OnUpdateModifierState(int ModifierID, bool bIsOn)
{
	if (ModifierID == ShiftEraseModifierId && bShiftErase != bIsOn)
	{
		bShiftErase = bIsOn;
		// The action is fixed at press time; Shift changes affect only the next stroke.
		if (!bStrokeActive && bHasHoverRay)
		{
			OnUpdateHover(LastHoverRay);
		}
	}
}

void UMD_PropPlacementTool::HandlePaletteAssetSelected(bool bPreservePreviewPose)
{
	FinishStroke();
	if (!bPreservePreviewPose)
	{
		PreviewCandidateIndex = 0;
		InvalidatePreview();
	}
	else
	{
		// Keep the random candidate and components, but refresh the copied entry's identity.
		PreviewCandidate.EntryId = Properties->ActivePaletteEntryId;
		if (const FMD_PropPaletteEntry* Entry = GetDefault<UMD_PropPaletteSettings>()->FindEntry(PreviewCandidate.EntryId))
		{
			PreviewCandidate.Label = Entry->GetDisplayName();
			Properties->PreviewItem = FText::FromString(PreviewCandidate.Label);
		}
	}
	NotifyOfPropertyChangeByTool(Properties);
}

void UMD_PropPlacementTool::HandlePaletteChanged()
{
	bMixCandidatesDirty = true;
	if (Properties && Properties->SelectionMode == EMD_PropBrushSelection::PaletteMix)
	{
		FinishStroke();
		InvalidatePreview();
	}
	else if (Properties)
	{
		if (const FMD_PropPaletteEntry* Entry = GetDefault<UMD_PropPaletteSettings>()->FindEntry(PreviewCandidate.EntryId))
		{
			PreviewCandidate.Label = Entry->GetDisplayName();
			Properties->PreviewItem = FText::FromString(PreviewCandidate.Label);
		}
	}
}

void UMD_PropPlacementTool::ShowPoseEditUnavailable()
{
	GetToolManager()->DisplayMessage(LOCTEXT("SelectPoseItem", "자세를 조절하거나 고정하려면 팔레트 항목을 클릭해 Selected Item으로 전환하세요.\n\nPalette Mix에서는 Reroll Preview로 다음 항목과 자세를 고를 수 있습니다."), EToolMessageLevel::UserWarning);
}

void UMD_PropPlacementTool::HandlePlacementSettingsChanged()
{
	PreviewCandidate.Settings = Properties->Placement;
	if (Properties->SelectionMode == EMD_PropBrushSelection::PaletteMix)
	{
		InvalidatePreview();
	}
	if (bHasHoverRay)
	{
		OnUpdateHover(LastHoverRay);
	}
	NotifyOfPropertyChangeByTool(Properties);
}

void UMD_PropPlacementTool::RefreshCandidatePreview()
{
	if (Properties->SelectionMode == EMD_PropBrushSelection::PaletteMix)
	{
		bPreviewDirty = true;
		RebuildPreview();
	}
	if (bHasHoverRay)
	{
		OnUpdateHover(LastHoverRay);
	}
}

void UMD_PropPlacementTool::AdvancePreviewCandidate()
{
	if (Properties->Action != EMD_PropBrushAction::Place || bStrokeActive)
	{
		return;
	}
	++PreviewCandidateIndex;
	RefreshCandidatePreview();
}

void UMD_PropPlacementTool::FreezePreviewPose()
{
	if (!Properties)
	{
		return;
	}
	if (!Properties->IsSelectedPlacement())
	{
		ShowPoseEditUnavailable();
		return;
	}
	if (bPreviewDirty)
	{
		RebuildPreview();
	}
	if (PreviewCandidate.Parts.IsEmpty() || !PreviewError.IsEmpty())
	{
		GetToolManager()->DisplayMessage(LOCTEXT("FreezeUnavailable", "고정할 소품의 미리보기를 먼저 준비하세요.\n\n메시 또는 액터 블루프린트를 선택하고 미리보기 안내를 확인하세요."), EToolMessageLevel::UserWarning);
		return;
	}
	if (!Properties->Placement.HasRandomizedValues())
	{
		GetToolManager()->DisplayMessage(LOCTEXT("PoseAlreadyFixed", "회전과 크기가 이미 고정값을 사용하고 있습니다."), EToolMessageLevel::UserNotification);
		return;
	}
	// The candidate remains available while the cursor is over the Details panel.
	Properties->Placement.FreezeRandomValues(PreviewCandidateIndex);
	Properties->StoreActivePaletteSettings();
	HandlePlacementSettingsChanged();
	GetToolManager()->DisplayMessage(LOCTEXT("PoseFrozen", "현재 회전과 크기를 고정했습니다.\n\n표면 위치와 정렬은 계속 적용됩니다. 랜덤 범위는 보존되므로 원하는 랜덤 옵션을 다시 켤 수 있습니다.\n\n팔레트 항목으로 사용 중이라면 Save Palette를 눌러 이 설정을 보존하세요."), EToolMessageLevel::UserNotification);
}

FInputRayHit UMD_PropPlacementTool::ShouldRespondToMouseWheel(const FInputDeviceRay& CurrentPos)
{
	if (!Properties || !Properties->IsSelectedPlacement() || bStrokeActive || !CanEditLevel())
	{
		return FInputRayHit();
	}
	if (bPreviewDirty)
	{
		RebuildPreview();
	}
	FHitResult Hit;
	return !PreviewCandidate.Parts.IsEmpty() && PreviewError.IsEmpty() && TraceSurface(CurrentPos, Hit)
		? FInputRayHit(Hit.Distance) : FInputRayHit();
}

void UMD_PropPlacementTool::OnMouseWheelScrollUp(const FInputDeviceRay& CurrentPos)
{
	LastHoverRay = CurrentPos;
	bHasHoverRay = true;
	Properties->RotateForward();
	ShowWheelPreview();
}

void UMD_PropPlacementTool::OnMouseWheelScrollDown(const FInputDeviceRay& CurrentPos)
{
	LastHoverRay = CurrentPos;
	bHasHoverRay = true;
	Properties->RotateBackward();
	ShowWheelPreview();
}

void UMD_PropPlacementTool::ShowWheelPreview()
{
	// Show the adjusted pose immediately. The next pointer move resumes the Shift erase highlight.
	EraseHoverActor.Reset();
	FHitResult Hit;
	if (TraceSurface(LastHoverRay, Hit))
	{
		UpdatePreview(Hit);
	}
}

void UMD_PropPlacementTool::InvalidatePreview()
{
	bPreviewDirty = true;
	bMixCandidatesDirty = true;
	GhostMaterial = nullptr;
	EraseHoverActor.Reset();
	HidePreview();
}

void UMD_PropPlacementTool::RebuildMixCandidates()
{
	bMixCandidatesDirty = false;
	MixCandidates.Reset();
	MixWarning = FText::GetEmpty();
	int32 Rejected = 0;
	// Asset loading may broadcast editor changes, so iterate a value snapshot.
	const TArray<FMD_PropPaletteEntry> Entries = GetDefault<UMD_PropPaletteSettings>()->GetEntries();
	for (const FMD_PropPaletteEntry& Entry : Entries)
	{
		if (!Entry.bUseInMix)
		{
			continue;
		}
		UStaticMesh* Mesh = nullptr;
		UClass* ActorClass = nullptr;
		FMD_PropPlacementCandidate Candidate;
		Candidate.Settings = Entry.Placement;
		Candidate.EntryId = Entry.Id;
		Candidate.Label = Entry.GetDisplayName();
		FText Error;
		if (MDResolvePropPaletteAsset(Entry.AssetPath.TryLoad(), Mesh, ActorClass)
			&& Candidate.Resolve(Mesh, ActorClass, Entry.BlueprintPreviewMesh, Error))
		{
			MixCandidates.Add(MoveTemp(Candidate));
		}
		else
		{
			++Rejected;
		}
	}
	if (Rejected > 0)
	{
		MixWarning = FText::Format(LOCTEXT("InvalidMixItems", "혼합 대상 중 {0}개는 에셋 또는 미리보기가 유효하지 않아 제외했습니다.\n\n해당 항목을 선택해 참조를 수정한 뒤 Refresh Preview를 누르세요."), FText::AsNumber(Rejected));
	}
}

void UMD_PropPlacementTool::RebuildPreview()
{
	bPreviewDirty = false;
	PreviewError = FText::GetEmpty();
	HidePreview();
	if (!Properties || !TargetWorld.IsValid())
	{
		return;
	}
	if (Properties->Action == EMD_PropBrushAction::Erase)
	{
		Properties->PreviewItem = LOCTEXT("EraseItem", "Erase");
		GetToolManager()->DisplayMessage(LOCTEXT("EraseInstructions", "삭제 방법\n빨간 표시의 소품을 클릭하거나 드래그해 삭제합니다. 이 도구로 배치 표시를 남긴 현재 레벨의 소품만 대상입니다.\n\n한 번의 드래그는 Undo 한 번으로 복원합니다. 기존 수동 배치와 이전 버전의 소품은 일반 선택 도구로 관리하세요."), EToolMessageLevel::UserNotification);
		GetToolManager()->DisplayMessage(FText::GetEmpty(), EToolMessageLevel::UserWarning);
		return;
	}
	if (Properties->SelectionMode == EMD_PropBrushSelection::PaletteMix)
	{
		if (bMixCandidatesDirty)
		{
			RebuildMixCandidates();
		}
		if (MixCandidates.IsEmpty())
		{
			PreviewCandidate = FMD_PropPlacementCandidate();
			PreviewError = LOCTEXT("EmptyMix", "사용할 수 있는 혼합 대상이 없습니다.\n\n팔레트에서 사용할 항목의 Mix를 체크하고 에셋·미리보기 참조를 확인하세요.");
		}
		else
		{
			const FRandomStream Random(static_cast<int32>(HashCombineFast(GetTypeHash(Properties->MixSeed), PreviewCandidateIndex)));
			PreviewCandidate = MixCandidates[Random.RandHelper(MixCandidates.Num())];
		}
	}
	else
	{
		PreviewCandidate = FMD_PropPlacementCandidate();
		PreviewCandidate.Settings = Properties->Placement;
		PreviewCandidate.EntryId = Properties->ActivePaletteEntryId;
		const bool bMesh = Properties->Source == EMD_PropPlacementSource::StaticMesh;
		PreviewCandidate.Resolve(bMesh ? Properties->Mesh.Get() : nullptr,
			bMesh ? nullptr : Properties->ActorClass.Get(), Properties->BlueprintPreviewMesh, PreviewError);
		const FMD_PropPaletteEntry* Entry = GetDefault<UMD_PropPaletteSettings>()->FindEntry(PreviewCandidate.EntryId);
		PreviewCandidate.Label = Entry ? Entry->GetDisplayName()
			: bMesh ? GetNameSafe(Properties->Mesh.Get()) : GetNameSafe(Properties->ActorClass.Get());
	}
	Properties->PreviewItem = PreviewError.IsEmpty() ? FText::FromString(PreviewCandidate.Label) : LOCTEXT("NoPreview", "None");
	if (!PreviewError.IsEmpty())
	{
		GetToolManager()->DisplayMessage(PreviewError, EToolMessageLevel::UserWarning);
		return;
	}
	if (!GhostMaterial)
	{
		UMaterialInterface* Material = GetDefault<UMD_TrashPlacementSettings>()->GhostPreviewMaterial.LoadSynchronous();
		if (!IsValid(Material))
		{
			PreviewError = LOCTEXT("MissingMaterial", "Ghost Preview Material을 프로젝트 설정에서 지정하고 Refresh Preview를 누르세요.");
			GetToolManager()->DisplayMessage(PreviewError, EToolMessageLevel::UserWarning);
			return;
		}
		GhostMaterial = UMaterialInstanceDynamic::Create(Material, this);
		GhostMaterial->SetScalarParameterValue(TEXT("OpacityAmount"), 0.35f);
	}
	while (GhostComponents.Num() > PreviewCandidate.Parts.Num())
	{
		UStaticMeshComponent* Ghost = GhostComponents.Pop();
		if (IsValid(Ghost))
		{
			Ghost->DestroyComponent();
		}
	}
	for (int32 PartIndex = 0; PartIndex < PreviewCandidate.Parts.Num(); ++PartIndex)
	{
		const FMD_PropPlacementPreviewPart& Part = PreviewCandidate.Parts[PartIndex];
		if (!GhostComponents.IsValidIndex(PartIndex))
		{
			UStaticMeshComponent* Ghost = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
			Ghost->SetMobility(EComponentMobility::Movable);
			Ghost->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Ghost->SetGenerateOverlapEvents(false);
			Ghost->SetCanEverAffectNavigation(false);
			Ghost->SetCastShadow(false);
			Ghost->bSelectable = false;
			Ghost->SetVisibility(false);
			Ghost->RegisterComponentWithWorld(TargetWorld.Get());
			GhostComponents.Add(Ghost);
		}
		UStaticMeshComponent* Ghost = GhostComponents[PartIndex];
		if (Ghost->GetStaticMesh() != Part.Mesh)
		{
			Ghost->SetStaticMesh(Part.Mesh);
		}
		for (int32 MaterialIndex = 0; MaterialIndex < FMath::Max(Ghost->GetNumMaterials(), 1); ++MaterialIndex)
		{
			Ghost->SetMaterial(MaterialIndex, GhostMaterial);
		}
	}
	GetToolManager()->DisplayMessage(LOCTEXT("PlaceInstructions", "배치 방법\n클릭하면 한 개, Continuous를 켜고 드래그하면 Spacing 간격으로 배치합니다. 한 번의 드래그는 Undo 한 번으로 되돌립니다.\n\n임시 삭제\nShift를 먼저 누르고 클릭·드래그하면 해당 드래그만 Erase로 처리합니다. Shift+휠은 Selected Item의 회전을 유지합니다.\n\n소품과 자세\nPalette Mix는 Mix를 체크한 항목만 사용합니다. Preview Item에서 다음 소품을 확인하세요. 항목을 클릭하면 Selected Item으로 돌아가며 Shift+휠로 회전할 수 있습니다.\n\nBP 미리보기는 기본 메시 또는 지정한 Blueprint Preview Mesh를 사용하며 실제 생성 외형과 다를 수 있습니다."), EToolMessageLevel::UserNotification);
	GetToolManager()->DisplayMessage(Properties->SelectionMode == EMD_PropBrushSelection::PaletteMix ? MixWarning : FText::GetEmpty(), EToolMessageLevel::UserWarning);
}

void UMD_PropPlacementTool::DestroyPreview()
{
	for (UStaticMeshComponent* Ghost : GhostComponents)
	{
		if (IsValid(Ghost))
		{
			Ghost->DestroyComponent();
		}
	}
	GhostComponents.Reset();
	GhostMaterial = nullptr;
}

void UMD_PropPlacementTool::HidePreview()
{
	for (UStaticMeshComponent* Ghost : GhostComponents)
	{
		if (IsValid(Ghost))
		{
			Ghost->SetVisibility(false);
		}
	}
}

bool UMD_PropPlacementTool::CanEditLevel() const
{
	const UWorld* World = TargetWorld.Get();
	ULevel* Level = World ? World->GetCurrentLevel() : nullptr;
	return GEditor && !GEditor->PlayWorld && World && World->WorldType == EWorldType::Editor
		&& IsValid(Level) && !FLevelUtils::IsLevelLocked(Level) && FLevelUtils::IsLevelVisible(Level);
}

bool UMD_PropPlacementTool::TraceSurface(const FInputDeviceRay& DevicePos, FHitResult& Hit) const
{
	UWorld* World = TargetWorld.Get();
	if (!World || World->WorldType != EWorldType::Editor || !GEditor || GEditor->PlayWorld)
	{
		return false;
	}
	FCollisionQueryParams Query(SCENE_QUERY_STAT(MDPropPlacementSurface), true);
	for (const TWeakObjectPtr<AActor>& Actor : StrokePlacedActors)
	{
		if (Actor.IsValid())
		{
			Query.AddIgnoredActor(Actor.Get());
		}
	}
	return World->LineTraceSingleByChannel(Hit, DevicePos.WorldRay.Origin,
		DevicePos.WorldRay.PointAt(10000000.0), ECC_Visibility, Query);
}

FTransform UMD_PropPlacementTool::BuildPlacementTransform(const FHitResult& Hit) const
{
	return PreviewCandidate.BuildTransform(Hit, PreviewCandidateIndex);
}

bool UMD_PropPlacementTool::IsPlacementSafe(const FTransform& Transform) const
{
	if (!CanEditLevel() || !Transform.IsValid() || PreviewCandidate.Parts.IsEmpty() || !PreviewError.IsEmpty())
	{
		return false;
	}
	if (!PreviewCandidate.Settings.bPreventOverlaps)
	{
		return true;
	}
	FCollisionQueryParams Query(SCENE_QUERY_STAT(MDPropPlacementOverlap), false);
	Query.bIgnoreTouches = true;
	const double Tolerance = FMath::Max(PreviewCandidate.Settings.OverlapTolerance, 0.0);
	for (const FMD_PropPlacementPreviewPart& Part : PreviewCandidate.Parts)
	{
		const FTransform PartTransform = Part.LocalTransform * Transform;
		const FBox Bounds = Part.Mesh->GetBoundingBox();
		const FVector Extent = (Bounds.GetExtent() * PartTransform.GetScale3D().GetAbs()
			- FVector(Tolerance)).ComponentMax(FVector(0.1));
		TArray<FOverlapResult> Overlaps;
		if (TargetWorld->OverlapMultiByChannel(Overlaps, PartTransform.TransformPosition(Bounds.GetCenter()),
			PartTransform.GetRotation(), ECC_Pawn, FCollisionShape::MakeBox(Extent), Query))
		{
			return false;
		}
	}
	return true;
}

void UMD_PropPlacementTool::UpdatePreview(const FHitResult& Hit)
{
	if (GhostComponents.IsEmpty() || PreviewCandidate.Parts.IsEmpty() || !GhostMaterial || !PreviewError.IsEmpty())
	{
		HidePreview();
		return;
	}
	const FTransform Transform = BuildPlacementTransform(Hit);
	if (!Transform.IsValid())
	{
		HidePreview();
		return;
	}
	GhostMaterial->SetVectorParameterValue(TEXT("HighlightColor"), IsPlacementSafe(Transform)
		? FLinearColor(0.05f, 1.0f, 0.1f) : FLinearColor(1.0f, 0.05f, 0.05f));
	for (int32 Index = 0; Index < GhostComponents.Num(); ++Index)
	{
		GhostComponents[Index]->SetWorldTransform(PreviewCandidate.Parts[Index].LocalTransform * Transform);
		GhostComponents[Index]->SetVisibility(true);
	}
}

FInputRayHit UMD_PropPlacementTool::BeginHoverSequenceHitTest(const FInputDeviceRay& DevicePos)
{
	FHitResult Hit;
	const bool bHit = TraceSurface(DevicePos, Hit);
	return bHit ? FInputRayHit(Hit.Distance) : FInputRayHit();
}

void UMD_PropPlacementTool::OnBeginHover(const FInputDeviceRay& DevicePos)
{
	OnUpdateHover(DevicePos);
}

bool UMD_PropPlacementTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	LastHoverRay = DevicePos;
	bHasHoverRay = true;
	if (bPreviewDirty)
	{
		RebuildPreview();
	}
	FHitResult Hit;
	if (GetEffectiveAction() == EMD_PropBrushAction::Erase)
	{
		HidePreview();
		EraseHoverActor = TraceEraseTarget(DevicePos, Hit) ? Hit.GetActor() : nullptr;
		return true;
	}
	EraseHoverActor.Reset();
	if (TraceSurface(DevicePos, Hit))
	{
		UpdatePreview(Hit);
	}
	else
	{
		HidePreview();
	}
	return true;
}

void UMD_PropPlacementTool::OnEndHover()
{
	bHasHoverRay = false;
	EraseHoverActor.Reset();
	HidePreview();
}

#undef LOCTEXT_NAMESPACE
