#include "Presets/SMD_PropPresetPanel.h"

#include "Presets/MD_PropPresetSettings.h"
#include "Palette/MD_PropPaletteSettings.h"
#include "Tools/MD_PropPlacementTool.h"
#include "InputCoreTypes.h"
#include "Misc/MessageDialog.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MDPropPresetPanel"

void SMD_PropPresetPanel::Construct(const FArguments& Args)
{
	Properties = Args._Properties;
	Library = GetMutableDefault<UMD_PropPresetSettings>();
	LibraryChangedHandle = Library->OnChanged.AddSP(SharedThis(this), &SMD_PropPresetPanel::RequestRefresh);
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(STextBlock).AutoWrapText(true)
			.Text(LOCTEXT("Help", "팔레트 나누기\nNew로 빈 팔레트를 만든 뒤 Book, Shelf처럼 F2로 이름을 붙이세요. 목록에서 이름을 선택하면 해당 팔레트로 바로 전환합니다.\n\n저장\nSave와 Save Palette는 현재 팔레트를 저장합니다. Save As는 현재 내용을 별도 팔레트로 복사합니다. 팔레트 전환은 레벨의 소품에 영향을 주지 않습니다."))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SAssignNew(Combo, SComboBox<TSharedPtr<FMD_PropPresetOption>>)
			.OptionsSource(&Options)
			.OnGenerateWidget_Lambda([](TSharedPtr<FMD_PropPresetOption> Option)
			{
				return SNew(STextBlock).Text(FText::FromString(Option->Name));
			})
			.OnSelectionChanged_Lambda([this](TSharedPtr<FMD_PropPresetOption> Option, ESelectInfo::Type Reason)
			{
				if (Properties.IsValid() && Option.IsValid() && Reason != ESelectInfo::Direct)
				{
					SelectPalette(Option->Id);
				}
			})
			[
				SAssignNew(NameWidget, SInlineEditableTextBlock)
				.Text_Lambda([this]
				{
					const FMD_PropPlacementPreset* Preset = HasSelection() ? Library->FindPreset(Properties->SelectedPresetId) : nullptr;
					return Preset ? FText::FromString(Preset->Name) : LOCTEXT("Empty", "Unsaved Palette");
				})
				.IsReadOnly_Lambda([this] { return !HasSelection(); })
				.IsSelected_Lambda([this] { return HasSelection(); })
				.DelayedLeftClickEntersEditMode(false)
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				.ToolTipText(LOCTEXT("NameHelp", "팔레트 선택기에 포커스를 두고 F2로 이름을 바꾸세요.\n\nEnter 또는 포커스 이동으로 저장하고 Esc로 취소합니다. 이름은 1~64자이며 중복 이름은 사용할 수 없습니다."))
				.OnEnterEditingMode_Lambda([this] { RenameId = Properties.IsValid() ? Properties->SelectedPresetId : FGuid(); })
				.OnVerifyTextChanged(this, &SMD_PropPresetPanel::VerifyName)
				.OnTextCommitted(this, &SMD_PropPresetPanel::CommitName)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4, 4))
			+ SWrapBox::Slot()
			[SNew(SButton).Text(LOCTEXT("New", "New"))
				.ToolTipText(LOCTEXT("NewHelp", "빈 팔레트를 만듭니다. Book이나 Shelf처럼 용도별로 이름을 붙이고 소품을 등록하세요."))
				.OnClicked(this, &SMD_PropPresetPanel::NewPalette)]
			+ SWrapBox::Slot()
			[SNew(SButton).Text(LOCTEXT("Load", "Reload")).IsEnabled(this, &SMD_PropPresetPanel::HasSelection)
				.ToolTipText(LOCTEXT("LoadHelp", "현재 팔레트의 마지막 저장 내용을 다시 불러옵니다. 미저장 변경이 있으면 먼저 확인합니다."))
				.OnClicked(this, &SMD_PropPresetPanel::Load)]
			+ SWrapBox::Slot()
			[SNew(SButton).Text(LOCTEXT("Save", "Save"))
				.ToolTipText(LOCTEXT("SaveHelp", "현재 팔레트의 목록과 소품별 설정을 저장합니다. 기존 내용도 남기려면 Save As를 사용하세요."))
				.OnClicked(this, &SMD_PropPresetPanel::Save)]
			+ SWrapBox::Slot()
			[SNew(SButton).Text(LOCTEXT("SaveAs", "Save As"))
				.ToolTipText(LOCTEXT("SaveAsHelp", "현재 목록과 소품별 설정을 새 팔레트로 저장합니다. 자동 이름으로 먼저 저장하고 바로 이름을 편집합니다. Esc를 누르면 자동 이름이 유지됩니다."))
				.OnClicked(this, &SMD_PropPresetPanel::SaveAs)]
			+ SWrapBox::Slot()
			[SNew(SButton).Text(LOCTEXT("Duplicate", "Duplicate")).IsEnabled(this, &SMD_PropPresetPanel::HasSelection)
				.ToolTipText(LOCTEXT("DuplicateHelp", "현재 팔레트의 편집 내용을 포함해 독립 복사하고 새 팔레트로 전환합니다."))
				.OnClicked(this, &SMD_PropPresetPanel::Duplicate)]
			+ SWrapBox::Slot()
			[SNew(SButton).Text(LOCTEXT("Delete", "Delete")).IsEnabled(this, &SMD_PropPresetPanel::HasSelection)
				.ToolTipText(LOCTEXT("DeleteHelp", "선택한 팔레트의 저장 기록을 삭제합니다. 현재 팔레트·레벨 소품·원본 에셋은 유지됩니다."))
				.OnClicked(this, &SMD_PropPresetPanel::Delete)]
			+ SWrapBox::Slot()
			[SNew(SButton).Text(LOCTEXT("SaveLibrary", "Save Library"))
				.Visibility_Lambda([this] { return Library.IsValid() && Library->HasUnsavedChanges() ? EVisibility::Visible : EVisibility::Collapsed; })
				.ToolTipText(LOCTEXT("SaveLibraryHelp", "에셋 이름 변경으로 갱신된 팔레트 참조를 저장합니다. 팔레트를 현재 작업 구성으로 덮어쓰지 않습니다."))
				.OnClicked(this, &SMD_PropPresetPanel::SaveLibrary)]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]
			{
				return Library.IsValid() && Library->HasUnsavedChanges()
					? LOCTEXT("ReferencesChanged", "에셋 참조 변경이 저장되지 않았습니다. Save Library를 누르세요.") : FText::GetEmpty();
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this] { return StatusMessage; })]
	];
	RefreshOptions();
}

SMD_PropPresetPanel::~SMD_PropPresetPanel()
{
	if (Library.IsValid())
	{
		Library->OnChanged.Remove(LibraryChangedHandle);
	}
}

void SMD_PropPresetPanel::Tick(const FGeometry& Geometry, const double Time, const float DeltaTime)
{
	SCompoundWidget::Tick(Geometry, Time, DeltaTime);
	if (bRefreshPending)
	{
		RefreshOptions();
	}
	if (bRenamePending)
	{
		bRenamePending = false;
		BeginRename();
	}
}

void SMD_PropPresetPanel::RefreshOptions()
{
	bRefreshPending = false;
	Options.Reset();
	if (!Library.IsValid() || !Properties.IsValid())
	{
		return;
	}
	for (const FMD_PropPlacementPreset& Preset : Library->GetPresets())
	{
		TSharedPtr<FMD_PropPresetOption> Option = MakeShared<FMD_PropPresetOption>();
		Option->Id = Preset.Id;
		Option->Name = Preset.Name;
		Options.Add(MoveTemp(Option));
	}
	if (!Library->FindPreset(Properties->SelectedPresetId))
	{
		Properties->SetActivePaletteId(FGuid());
	}
	Combo->RefreshOptions();
	Combo->ClearSelection();
	for (const TSharedPtr<FMD_PropPresetOption>& Option : Options)
	{
		if (Option->Id == Properties->SelectedPresetId)
		{
			Combo->SetSelectedItem(Option);
			break;
		}
	}
}

bool SMD_PropPresetPanel::HasSelection() const
{
	return Properties.IsValid() && Library.IsValid() && Library->FindPreset(Properties->SelectedPresetId) != nullptr;
}

FReply SMD_PropPresetPanel::OnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	if (KeyEvent.GetKey() == EKeys::F2 && !KeyEvent.IsControlDown() && !KeyEvent.IsAltDown()
		&& !KeyEvent.IsShiftDown() && !KeyEvent.IsCommandDown() && HasSelection())
	{
		BeginRename();
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(Geometry, KeyEvent);
}

void SMD_PropPresetPanel::BeginRename()
{
	if (HasSelection() && NameWidget.IsValid())
	{
		NameWidget->EnterEditingMode();
	}
}

bool SMD_PropPresetPanel::VerifyName(const FText& Text, FText& OutError) const
{
	return Library.IsValid() && Library->ValidateName(Text.ToString(), RenameId, OutError);
}

void SMD_PropPresetPanel::CommitName(const FText& Text, ETextCommit::Type Reason)
{
	const FGuid Id = RenameId;
	RenameId.Invalidate();
	if (!Library.IsValid() || Reason == ETextCommit::OnCleared)
	{
		return;
	}
	const FMD_PropPlacementPreset* Preset = Library->FindPreset(Id);
	if (Preset && Preset->Name != Text.ToString().TrimStartAndEnd())
	{
		if (Library->RenamePreset(Id, Text.ToString(), StatusMessage))
		{
			StatusMessage = LOCTEXT("Renamed", "팔레트 이름을 저장했습니다.");
		}
	}
}

bool SMD_PropPresetPanel::ConfirmSwitch()
{
	if (!Properties.IsValid() || !Library.IsValid())
	{
		return false;
	}
	Properties->StoreActivePaletteSettings();
	if (!Properties->HasUnsavedPresetChanges())
	{
		return true;
	}
	const EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::YesNoCancel,
		EAppReturnType::Cancel,
		LOCTEXT("SwitchQuestion", "현재 팔레트의 변경을 저장하고 전환할까요?\n\nYes: 현재 팔레트를 저장한 뒤 전환\nNo: 변경을 버리고 전환\nCancel: 현재 팔레트에서 계속 작업"),
		LOCTEXT("SwitchTitle", "Switch Palette"));
	return Answer == EAppReturnType::No
		|| (Answer == EAppReturnType::Yes && Properties.IsValid() && Properties->SaveActivePalette(StatusMessage));
}

void SMD_PropPresetPanel::SelectPalette(const FGuid& Id)
{
	if (!Properties.IsValid() || !Library.IsValid() || Id == Properties->SelectedPresetId)
	{
		return;
	}
	const FMD_PropPlacementPreset* Found = Library->FindPreset(Id);
	if (!Found)
	{
		return;
	}
	// Saving in the dialog may reallocate the library; retain a value snapshot.
	const FMD_PropPlacementPreset Palette = *Found;
	if (ConfirmSwitch() && Properties.IsValid() && Library.IsValid() && Library->SetActivePreset(Palette.Id, StatusMessage))
	{
		Properties->SetActivePaletteId(Palette.Id);
		Properties->ApplyPreset(Palette.Data, StatusMessage);
	}
	RequestRefresh();
}

FReply SMD_PropPresetPanel::NewPalette()
{
	if (ConfirmSwitch() && Properties.IsValid() && Library.IsValid())
	{
		const FMD_PropPlacementPresetData Empty;
		const FGuid Id = Library->AddPreset(Library->MakeUniqueName(TEXT("Palette")), Empty, StatusMessage);
		if (Id.IsValid() && Library->SetActivePreset(Id, StatusMessage))
		{
			Properties->SetActivePaletteId(Id);
			Properties->ApplyPreset(Empty, StatusMessage);
			RequestRefresh();
			bRenamePending = true;
		}
	}
	return FReply::Handled();
}

FReply SMD_PropPresetPanel::Load()
{
	if (!HasSelection())
	{
		return FReply::Handled();
	}
	const FMD_PropPlacementPreset Palette = *Library->FindPreset(Properties->SelectedPresetId);
	if (Properties->HasUnsavedPresetChanges()
		&& FMessageDialog::Open(EAppMsgType::YesNo, EAppReturnType::No,
			LOCTEXT("ReloadQuestion", "현재 변경을 버리고 이 팔레트의 마지막 저장 내용을 다시 불러올까요?"),
			LOCTEXT("ReloadTitle", "Reload Palette")) != EAppReturnType::Yes)
	{
		return FReply::Handled();
	}
	Properties->ApplyPreset(Palette.Data, StatusMessage);
	return FReply::Handled();
}

FReply SMD_PropPresetPanel::Save()
{
	if (Properties.IsValid() && Properties->SaveActivePalette(StatusMessage))
	{
		StatusMessage = LOCTEXT("Saved", "현재 팔레트를 저장했습니다.");
		RequestRefresh();
	}
	return FReply::Handled();
}

FReply SMD_PropPresetPanel::SaveAs()
{
	if (Properties.IsValid() && Library.IsValid())
	{
		Properties->StoreActivePaletteSettings();
		const FGuid Id = Library->AddPreset(Library->MakeUniqueName(TEXT("Palette")), Properties->CapturePreset(), StatusMessage);
		if (Id.IsValid() && Library->SetActivePreset(Id, StatusMessage))
		{
			Properties->SetActivePaletteId(Id);
			GetMutableDefault<UMD_PropPaletteSettings>()->MarkEntriesSaved();
			RequestRefresh();
			bRenamePending = true;
			StatusMessage = LOCTEXT("Created", "별도 팔레트로 저장했습니다. 이름을 입력하고 Enter를 누르세요.");
		}
	}
	return FReply::Handled();
}

FReply SMD_PropPresetPanel::Duplicate()
{
	// Duplicate the working collection, including edits, so the displayed name always matches its contents.
	return SaveAs();
}

FReply SMD_PropPresetPanel::Delete()
{
	if (!HasSelection())
	{
		return FReply::Handled();
	}
	const FMD_PropPlacementPreset Palette = *Library->FindPreset(Properties->SelectedPresetId);
	const FText Question = FText::Format(LOCTEXT("DeleteQuestion", "{0} 팔레트의 저장 기록을 삭제할까요?\n\n현재 목록은 이름 없는 팔레트로 유지됩니다. 레벨의 소품과 원본 에셋은 유지됩니다. 저장 기록 삭제는 레벨 Undo로 복원되지 않습니다."), FText::FromString(Palette.Name));
	if (FMessageDialog::Open(EAppMsgType::YesNo, EAppReturnType::No, Question, LOCTEXT("DeleteTitle", "Delete Palette")) == EAppReturnType::Yes
		&& Library.IsValid() && Library->DeletePreset(Palette.Id, StatusMessage))
	{
		Properties->SetActivePaletteId(FGuid());
		RequestRefresh();
		StatusMessage = LOCTEXT("Deleted", "팔레트 저장 기록을 삭제했습니다. 현재 목록은 Save As로 다시 저장할 수 있습니다.");
	}
	return FReply::Handled();
}

FReply SMD_PropPresetPanel::SaveLibrary()
{
	if (Library.IsValid() && Library->SaveLibrary(StatusMessage))
	{
		StatusMessage = LOCTEXT("LibrarySaved", "변경된 팔레트 에셋 참조를 저장했습니다.");
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
