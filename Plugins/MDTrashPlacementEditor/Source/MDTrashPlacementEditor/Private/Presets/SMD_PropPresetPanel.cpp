#include "Presets/SMD_PropPresetPanel.h"

#include "Presets/MD_PropPresetSettings.h"
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
			.Text(LOCTEXT("Help", "저장과 불러오기\nSave As로 현재 구성을 새로 저장하고 F2로 이름을 바꾸세요. Save는 선택한 프리셋을 현재 구성으로 덮어씁니다.\n\n목록 선택 후 Load를 누르면 팔레트와 배치 설정을 불러옵니다. 레벨의 소품은 그대로 유지됩니다."))
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
					Properties->SelectedPresetId = Option->Id;
					StatusMessage = LOCTEXT("SelectionHelp", "Load로 불러오거나 Save로 현재 구성을 덮어쓸 수 있습니다.");
				}
			})
			[
				SAssignNew(NameWidget, SInlineEditableTextBlock)
				.Text_Lambda([this]
				{
					const FMD_PropPlacementPreset* Preset = HasSelection() ? Library->FindPreset(Properties->SelectedPresetId) : nullptr;
					return Preset ? FText::FromString(Preset->Name) : LOCTEXT("Empty", "No Presets");
				})
				.IsReadOnly_Lambda([this] { return !HasSelection(); })
				.IsSelected_Lambda([this] { return HasSelection(); })
				.DelayedLeftClickEntersEditMode(false)
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				.ToolTipText(LOCTEXT("NameHelp", "프리셋 선택기에 포커스를 두고 F2로 이름을 바꾸세요.\n\nEnter 또는 포커스 이동으로 저장하고 Esc로 취소합니다. 이름은 1~64자이며 중복 이름은 사용할 수 없습니다."))
				.OnEnterEditingMode_Lambda([this] { RenameId = Properties.IsValid() ? Properties->SelectedPresetId : FGuid(); })
				.OnVerifyTextChanged(this, &SMD_PropPresetPanel::VerifyName)
				.OnTextCommitted(this, &SMD_PropPresetPanel::CommitName)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4, 4))
			+ SWrapBox::Slot()
			[SNew(SButton).Text(LOCTEXT("Load", "Load")).IsEnabled(this, &SMD_PropPresetPanel::HasSelection)
				.ToolTipText(LOCTEXT("LoadHelp", "선택한 프리셋의 팔레트·Mix 체크·배치 설정을 불러옵니다. 현재 팔레트를 교체하며 미저장 변경이 있으면 먼저 확인합니다."))
				.OnClicked(this, &SMD_PropPresetPanel::Load)]
			+ SWrapBox::Slot()
			[SNew(SButton).Text(LOCTEXT("Save", "Save")).IsEnabled(this, &SMD_PropPresetPanel::HasSelection)
				.ToolTipText(LOCTEXT("SaveHelp", "선택한 프리셋을 현재 팔레트와 배치 설정으로 덮어써 저장합니다. 기존 내용도 남기려면 Save As를 사용하세요."))
				.OnClicked(this, &SMD_PropPresetPanel::Save)]
			+ SWrapBox::Slot()
			[SNew(SButton).Text(LOCTEXT("SaveAs", "Save As"))
				.ToolTipText(LOCTEXT("SaveAsHelp", "현재 구성을 새 프리셋으로 저장합니다. 자동 이름으로 먼저 저장하고 바로 이름을 편집합니다. Esc를 누르면 자동 이름이 유지됩니다."))
				.OnClicked(this, &SMD_PropPresetPanel::SaveAs)]
			+ SWrapBox::Slot()
			[SNew(SButton).Text(LOCTEXT("Duplicate", "Duplicate")).IsEnabled(this, &SMD_PropPresetPanel::HasSelection)
				.ToolTipText(LOCTEXT("DuplicateHelp", "선택한 프리셋에 저장된 내용을 독립 복사합니다. 현재 편집 내용으로 별도 저장하려면 Save As를 사용하세요."))
				.OnClicked(this, &SMD_PropPresetPanel::Duplicate)]
			+ SWrapBox::Slot()
			[SNew(SButton).Text(LOCTEXT("Delete", "Delete")).IsEnabled(this, &SMD_PropPresetPanel::HasSelection)
				.ToolTipText(LOCTEXT("DeleteHelp", "선택한 프리셋의 저장 기록을 삭제합니다. 현재 팔레트·레벨 소품·원본 에셋은 유지됩니다."))
				.OnClicked(this, &SMD_PropPresetPanel::Delete)]
			+ SWrapBox::Slot()
			[SNew(SButton).Text(LOCTEXT("SaveLibrary", "Save Library"))
				.Visibility_Lambda([this] { return Library.IsValid() && Library->HasUnsavedChanges() ? EVisibility::Visible : EVisibility::Collapsed; })
				.ToolTipText(LOCTEXT("SaveLibraryHelp", "에셋 이름 변경으로 갱신된 프리셋 참조를 저장합니다. 프리셋을 현재 작업 구성으로 덮어쓰지 않습니다."))
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
		Properties->SelectedPresetId = Options.IsEmpty() ? FGuid() : Options[0]->Id;
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
			StatusMessage = LOCTEXT("Renamed", "프리셋 이름을 저장했습니다.");
		}
	}
}

FReply SMD_PropPresetPanel::Load()
{
	if (!HasSelection())
	{
		return FReply::Handled();
	}
	// A dialog or asset load may invalidate the library array; work with a value snapshot.
	const FMD_PropPlacementPreset Preset = *Library->FindPreset(Properties->SelectedPresetId);
	if (Properties->HasUnsavedPresetChanges() && !(Properties->CapturePreset() == Preset.Data))
	{
		const FText Question = FText::Format(LOCTEXT("DiscardQuestion", "현재 작업 구성이 변경되었습니다. {0} 프리셋으로 교체할까요?\n\n변경을 프리셋으로 남기려면 No를 누른 뒤 Save 또는 Save As를 사용하세요."), FText::FromString(Preset.Name));
		if (FMessageDialog::Open(EAppMsgType::YesNo, EAppReturnType::No, Question, LOCTEXT("LoadTitle", "Load Preset")) != EAppReturnType::Yes)
		{
			return FReply::Handled();
		}
	}
	if (Properties.IsValid())
	{
		Properties->ApplyPreset(Preset.Data, StatusMessage);
		if (StatusMessage.IsEmpty())
		{
			StatusMessage = FText::Format(LOCTEXT("Loaded", "{0} 프리셋을 불러왔습니다."), FText::FromString(Preset.Name));
		}
	}
	return FReply::Handled();
}

FReply SMD_PropPresetPanel::Save()
{
	if (HasSelection())
	{
		Properties->StoreActivePaletteSettings();
		if (Library->UpdatePreset(Properties->SelectedPresetId, Properties->CapturePreset(), StatusMessage))
		{
			Properties->RememberPresetBaseline(true);
			StatusMessage = LOCTEXT("Saved", "현재 구성을 선택한 프리셋에 저장했습니다.");
		}
	}
	return FReply::Handled();
}

FReply SMD_PropPresetPanel::SaveAs()
{
	if (Properties.IsValid() && Library.IsValid())
	{
		Properties->StoreActivePaletteSettings();
		const FGuid NewId = Library->AddPreset(Library->MakeUniqueName(TEXT("Preset")), Properties->CapturePreset(), StatusMessage);
		if (NewId.IsValid())
		{
			Properties->SelectedPresetId = NewId;
			Properties->RememberPresetBaseline(true);
			bRenamePending = true;
			StatusMessage = LOCTEXT("Created", "새 프리셋을 저장했습니다. 이름을 입력하고 Enter를 누르세요.");
		}
	}
	return FReply::Handled();
}

FReply SMD_PropPresetPanel::Duplicate()
{
	if (HasSelection())
	{
		const FGuid NewId = Library->DuplicatePreset(Properties->SelectedPresetId, StatusMessage);
		if (NewId.IsValid())
		{
			Properties->SelectedPresetId = NewId;
			bRenamePending = true;
			StatusMessage = LOCTEXT("Duplicated", "저장된 프리셋을 복제했습니다. 이름을 바꾸고 Load로 불러올 수 있습니다.");
		}
	}
	return FReply::Handled();
}

FReply SMD_PropPresetPanel::Delete()
{
	if (!HasSelection())
	{
		return FReply::Handled();
	}
	const FMD_PropPlacementPreset Preset = *Library->FindPreset(Properties->SelectedPresetId);
	const FText Question = FText::Format(LOCTEXT("DeleteQuestion", "{0} 프리셋의 저장 기록을 삭제할까요?\n\n프리셋 삭제는 레벨 Undo로 복원되지 않습니다. 현재 팔레트와 레벨에 배치한 소품은 유지됩니다."), FText::FromString(Preset.Name));
	if (FMessageDialog::Open(EAppMsgType::YesNo, EAppReturnType::No, Question, LOCTEXT("DeleteTitle", "Delete Preset")) == EAppReturnType::Yes
		&& Library.IsValid() && Library->DeletePreset(Preset.Id, StatusMessage))
	{
		if (Properties.IsValid() && Properties->CapturePreset() == Preset.Data)
		{
			const bool bAnotherCopy = Library->GetPresets().ContainsByPredicate([&Preset](const FMD_PropPlacementPreset& Item)
				{ return Item.Data == Preset.Data; });
			if (!bAnotherCopy)
			{
				Properties->RememberPresetBaseline(false);
			}
		}
		StatusMessage = LOCTEXT("Deleted", "프리셋을 삭제했습니다. 현재 팔레트와 배치 결과는 유지됩니다.");
	}
	return FReply::Handled();
}

FReply SMD_PropPresetPanel::SaveLibrary()
{
	if (Library.IsValid() && Library->SaveLibrary(StatusMessage))
	{
		StatusMessage = LOCTEXT("LibrarySaved", "변경된 프리셋 에셋 참조를 저장했습니다.");
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
