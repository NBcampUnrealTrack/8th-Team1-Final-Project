#include "Palette/MD_PropPaletteCustomization.h"

#include "Palette/MD_PropPaletteSettings.h"
#include "Presets/SMD_PropPresetPanel.h"
#include "Tools/MD_PropPlacementTool.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetThumbnail.h"
#include "ContentBrowserModule.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Engine/StaticMesh.h"
#include "IContentBrowserSingleton.h"
#include "InputCoreTypes.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "MDPropPalette"

struct FMD_PropPaletteListItem
{
	FMD_PropPaletteEntry Entry;
	FAssetData AssetData;
	FString Name;
	TSharedPtr<FAssetThumbnail> Thumbnail;
	TWeakPtr<SInlineEditableTextBlock> NameWidget;
};

class SMD_PropPalette : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMD_PropPalette) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UMD_PropPlacementProperties>, Properties)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);
	virtual ~SMD_PropPalette() override;
	virtual void Tick(const FGeometry& Geometry, const double Time, const float DeltaTime) override;
	virtual FReply OnDragOver(const FGeometry& Geometry, const FDragDropEvent& Event) override;
	virtual FReply OnDrop(const FGeometry& Geometry, const FDragDropEvent& Event) override;

private:
	void RequestRefresh() { bRefreshPending = true; }
	void HandleAssetChanged(const FAssetData& Asset);
	void HandleSourceChanged();
	void RebuildItems();
	TSharedRef<ITableRow> GenerateRow(TSharedPtr<FMD_PropPaletteListItem> Item, const TSharedRef<STableViewBase>& Owner);
	void SelectItem(TSharedPtr<FMD_PropPaletteListItem> Item, ESelectInfo::Type Reason);
	bool HasVisibleSelection() const;
	FReply HandleListKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent);
	void HandleItemScrolledIntoView(TSharedPtr<FMD_PropPaletteListItem> Item, const TSharedPtr<ITableRow>& Row);
	void CommitEntryName(const FText& Text, ETextCommit::Type Reason, FGuid EntryId);
	FReply AddSelected();
	void RegisterAssets(const TArray<FAssetData>& Assets);
	FReply DuplicateSelected();
	FReply RemoveSelected();
	FReply SavePalette();

	TWeakObjectPtr<UMD_PropPlacementProperties> Properties;
	TWeakObjectPtr<UMD_PropPaletteSettings> Palette;
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
	TSharedPtr<SListView<TSharedPtr<FMD_PropPaletteListItem>>> ListView;
	TSharedPtr<SComboBox<TSharedPtr<FName>>> CategoryCombo;
	TSharedPtr<SSearchBox> SearchBox;
	TArray<TSharedPtr<FMD_PropPaletteListItem>> Items;
	TArray<TSharedPtr<FName>> Categories;
	FGuid SelectedId;
	FGuid PendingRenameId;
	FName CategoryFilter;
	FString SearchText;
	FText StatusMessage;
	FDelegateHandle PaletteChangedHandle;
	FDelegateHandle SourceChangedHandle;
	FDelegateHandle AssetAddedHandle;
	FDelegateHandle AssetRemovedHandle;
	int32 MixEntryCount = 0;
	bool bRefreshPending = true;
	bool bApplyingSelection = false;
};

void SMD_PropPalette::Construct(const FArguments& Args)
{
	Properties = Args._Properties;
	Palette = GetMutableDefault<UMD_PropPaletteSettings>();
	ThumbnailPool = MakeShared<FAssetThumbnailPool>(64);
	PaletteChangedHandle = Palette->OnChanged.AddSP(SharedThis(this), &SMD_PropPalette::RequestRefresh);
	if (Properties.IsValid())
	{
		SelectedId = Properties->PaletteSelectedEntryId;
		SearchText = Properties->PaletteSearchText;
		CategoryFilter = Properties->PaletteCategoryFilter;
		SourceChangedHandle = Properties->OnPaletteSelectionChanged.AddSP(SharedThis(this), &SMD_PropPalette::HandleSourceChanged);
	}
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	AssetAddedHandle = Registry.OnAssetAdded().AddSP(SharedThis(this), &SMD_PropPalette::HandleAssetChanged);
	AssetRemovedHandle = Registry.OnAssetRemoved().AddSP(SharedThis(this), &SMD_PropPalette::HandleAssetChanged);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(SBorder).Padding(8)
			[
				SNew(STextBlock).AutoWrapText(true)
				.Text(LOCTEXT("DropHint", "소품 등록\n메시나 액터 BP를 끌어다 놓거나 Add Selected로 등록하세요.\n\n복제와 이름 변경\nDuplicate Entry로 다른 모습을 만드세요. 목록에서 항목을 선택하고 F2로 이름을 바꿉니다.\n\n혼합 배치\n사용할 행의 Mix를 체크하고 Selection Mode를 Palette Mix로 바꾸세요.\n\n설정과 저장\n항목을 선택해 배치 설정을 조절하세요. 다음 실행에도 쓰려면 Save Palette를 누르세요."))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4, 4))
			+ SWrapBox::Slot()
			[SNew(SButton).Text(LOCTEXT("Add", "Add Selected")).OnClicked(this, &SMD_PropPalette::AddSelected)]
			+ SWrapBox::Slot()
			[
				SNew(SButton).Text(LOCTEXT("Duplicate", "Duplicate Entry"))
				.ToolTipText(LOCTEXT("DuplicateHelp", "같은 에셋과 배치 설정으로 독립 항목을 만들고 바로 선택합니다.\n\n현재 항목의 랜덤 자세도 이어받습니다. 마음에 드는 자세를 보존하려면 복제 후 Freeze Current Pose를 누르세요."))
				.IsEnabled_Lambda([this] { return Properties.IsValid() && HasVisibleSelection(); })
				.OnClicked(this, &SMD_PropPalette::DuplicateSelected)
			]
			+ SWrapBox::Slot()
			[SNew(SButton).Text(LOCTEXT("Remove", "Remove Entry")).IsEnabled(this, &SMD_PropPalette::HasVisibleSelection).OnClicked(this, &SMD_PropPalette::RemoveSelected)]
			+ SWrapBox::Slot()
			[
				SNew(SButton).Text(LOCTEXT("Save", "Save Palette"))
				.IsEnabled_Lambda([this] { return Palette.IsValid() && Palette->HasUnsavedChanges(); })
				.OnClicked(this, &SMD_PropPalette::SavePalette)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SAssignNew(SearchBox, SSearchBox).InitialText(FText::FromString(SearchText))
			.HintText(LOCTEXT("SearchHint", "Search name, path or category"))
			.OnTextChanged_Lambda([this](const FText& Text)
			{
				SearchText = Text.ToString();
				if (Properties.IsValid())
				{
					Properties->PaletteSearchText = SearchText;
				}
				RequestRefresh();
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SAssignNew(CategoryCombo, SComboBox<TSharedPtr<FName>>).OptionsSource(&Categories)
			.OnGenerateWidget_Lambda([](TSharedPtr<FName> Category)
			{
				return SNew(STextBlock).Text(Category->IsNone() ? LOCTEXT("AllCategories", "All Categories") : FText::FromName(*Category));
			})
			.OnSelectionChanged_Lambda([this](TSharedPtr<FName> Category, ESelectInfo::Type Reason)
			{
				if (Category.IsValid() && Reason != ESelectInfo::Direct)
				{
					CategoryFilter = *Category;
					if (Properties.IsValid())
					{
						Properties->PaletteCategoryFilter = CategoryFilter;
					}
					RequestRefresh();
				}
			})
			[
				SNew(STextBlock).Text_Lambda([this] { return CategoryFilter.IsNone() ? LOCTEXT("AllCategories", "All Categories") : FText::FromName(CategoryFilter); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(SBox).HeightOverride(250)
			[
				SAssignNew(ListView, SListView<TSharedPtr<FMD_PropPaletteListItem>>)
				.ListItemsSource(&Items).SelectionMode(ESelectionMode::Single)
				.OnGenerateRow(this, &SMD_PropPalette::GenerateRow)
				.OnSelectionChanged(this, &SMD_PropPalette::SelectItem)
				.OnMouseButtonClick_Lambda([this](TSharedPtr<FMD_PropPaletteListItem> Item)
				{
					// Clicking an already-selected row still leaves the brush's Mix or Erase mode.
					if (Properties.IsValid() && !Properties->IsSelectedPlacement())
					{
						SelectItem(Item, ESelectInfo::OnMouseClick);
					}
				})
				.OnKeyDownHandler(this, &SMD_PropPalette::HandleListKeyDown)
				.OnItemScrolledIntoView(this, &SMD_PropPalette::HandleItemScrolledIntoView)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(SEditableTextBox).IsEnabled(this, &SMD_PropPalette::HasVisibleSelection)
			.HintText(LOCTEXT("CategoryHint", "Category (Enter to apply)"))
			.Text_Lambda([this]
			{
				const FMD_PropPaletteEntry* Entry = Palette.IsValid() ? Palette->FindEntry(SelectedId) : nullptr;
				return Entry && !Entry->Category.IsNone() ? FText::FromName(Entry->Category) : FText::GetEmpty();
			})
			.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type Reason)
			{
				if (Palette.IsValid() && HasVisibleSelection() && Reason != ETextCommit::OnCleared)
				{
					Palette->SetCategory(SelectedId, Text.ToString());
				}
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]
			{
				return FText::Format(LOCTEXT("Count", "{0} shown / {1} total | Mix: {2}\n{3}"), FText::AsNumber(Items.Num()),
					FText::AsNumber(Palette.IsValid() ? Palette->GetEntries().Num() : 0), FText::AsNumber(MixEntryCount),
					Palette.IsValid() && Palette->HasUnsavedChanges() ? LOCTEXT("Unsaved", "Unsaved changes") : LOCTEXT("Saved", "Saved"));
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this] { return StatusMessage; })]
	];
	RebuildItems();
}

SMD_PropPalette::~SMD_PropPalette()
{
	if (Palette.IsValid())
	{
		Palette->OnChanged.Remove(PaletteChangedHandle);
	}
	if (Properties.IsValid())
	{
		Properties->OnPaletteSelectionChanged.Remove(SourceChangedHandle);
	}
	if (FAssetRegistryModule* Module = FModuleManager::GetModulePtr<FAssetRegistryModule>(TEXT("AssetRegistry")))
	{
		if (IAssetRegistry* Registry = Module->TryGet())
		{
			Registry->OnAssetAdded().Remove(AssetAddedHandle);
			Registry->OnAssetRemoved().Remove(AssetRemovedHandle);
		}
	}
}

void SMD_PropPalette::Tick(const FGeometry& Geometry, const double Time, const float DeltaTime)
{
	SCompoundWidget::Tick(Geometry, Time, DeltaTime);
	if (bRefreshPending)
	{
		RebuildItems();
	}
}

void SMD_PropPalette::HandleAssetChanged(const FAssetData& Asset)
{
	if (Palette.IsValid() && Palette->GetEntries().ContainsByPredicate([&Asset](const FMD_PropPaletteEntry& Entry)
		{ return Entry.AssetPath == Asset.GetSoftObjectPath(); }))
	{
		RequestRefresh();
	}
}

void SMD_PropPalette::HandleSourceChanged()
{
	if (!bApplyingSelection)
	{
		PendingRenameId.Invalidate();
		SelectedId = Properties.IsValid() ? Properties->PaletteSelectedEntryId : FGuid();
		if (Properties.IsValid())
		{
			CategoryFilter = Properties->PaletteCategoryFilter;
			SearchText = Properties->PaletteSearchText;
			if (SearchBox.IsValid() && SearchBox->GetText().ToString() != SearchText)
			{
				SearchBox->SetText(FText::FromString(SearchText));
			}
		}
		RequestRefresh();
	}
}

void SMD_PropPalette::RebuildItems()
{
	bRefreshPending = false;
	PendingRenameId.Invalidate();
	if (!Palette.IsValid())
	{
		return;
	}
	TArray<FName> Names;
	MixEntryCount = 0;
	for (const FMD_PropPaletteEntry& Entry : Palette->GetEntries())
	{
		MixEntryCount += Entry.bUseInMix ? 1 : 0;
		if (!Entry.Category.IsNone())
		{
			Names.AddUnique(Entry.Category);
		}
	}
	Names.Sort([](FName A, FName B) { return A.LexicalLess(B); });
	if (!CategoryFilter.IsNone() && !Names.Contains(CategoryFilter))
	{
		CategoryFilter = NAME_None;
		if (Properties.IsValid())
		{
			Properties->PaletteCategoryFilter = NAME_None;
		}
	}
	Categories.Reset();
	Categories.Add(MakeShared<FName>(NAME_None));
	for (FName Name : Names)
	{
		Categories.Add(MakeShared<FName>(Name));
	}
	CategoryCombo->RefreshOptions();
	for (const TSharedPtr<FName>& Category : Categories)
	{
		if (*Category == CategoryFilter)
		{
			CategoryCombo->SetSelectedItem(Category);
			break;
		}
	}

	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	Items.Reset();
	for (const FMD_PropPaletteEntry& Entry : Palette->GetEntries())
	{
		if (!CategoryFilter.IsNone() && CategoryFilter != Entry.Category)
		{
			continue;
		}
		const FString Searchable = Entry.GetDisplayName() + TEXT(" ") + Entry.AssetPath.ToString() + TEXT(" ") + Entry.Category.ToString();
		if (!SearchText.IsEmpty() && !Searchable.Contains(SearchText, ESearchCase::IgnoreCase))
		{
			continue;
		}
		TSharedPtr<FMD_PropPaletteListItem> Item = MakeShared<FMD_PropPaletteListItem>();
		Item->Entry = Entry;
		Item->AssetData = Registry.GetAssetByObjectPath(Entry.AssetPath);
		Item->Name = Entry.GetDisplayName();
		Items.Add(Item);
	}
	ListView->RequestListRefresh();
	ListView->ClearSelection();
	for (const TSharedPtr<FMD_PropPaletteListItem>& Item : Items)
	{
		if (Item->Entry.Id == SelectedId)
		{
			ListView->SetSelection(Item, ESelectInfo::Direct);
			break;
		}
	}
}

TSharedRef<ITableRow> SMD_PropPalette::GenerateRow(TSharedPtr<FMD_PropPaletteListItem> Item, const TSharedRef<STableViewBase>& Owner)
{
	Item->Thumbnail = MakeShared<FAssetThumbnail>(Item->AssetData, 48, 48, ThumbnailPool);
	FAssetThumbnailConfig ThumbnailConfig;
	ThumbnailConfig.bAllowRealTimeOnHovered = false;
	ThumbnailConfig.ThumbnailLabel = EThumbnailLabel::NoLabel;
	const FText Type = !Item->AssetData.IsValid() ? LOCTEXT("Missing", "Missing Asset")
		: Item->AssetData.AssetClassPath == UStaticMesh::StaticClass()->GetClassPathName() ? LOCTEXT("Mesh", "Mesh") : LOCTEXT("BP", "Blueprint");
	const FText Category = Item->Entry.Category.IsNone() ? LOCTEXT("Uncategorized", "Uncategorized") : FText::FromName(Item->Entry.Category);
	TSharedPtr<SInlineEditableTextBlock> NameWidget;
	const TSharedRef<STableRow<TSharedPtr<FMD_PropPaletteListItem>>> Row = SNew(STableRow<TSharedPtr<FMD_PropPaletteListItem>>, Owner).Padding(3)
	.ToolTipText(FText::Format(LOCTEXT("EntrySource", "Entry: {0}\n\nSource Asset: {1}\n{2}"), FText::FromString(Item->Name),
		FText::FromString(Item->Entry.AssetPath.GetAssetName()), FText::FromString(Item->Entry.AssetPath.ToString())));
	Row->SetContent(
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0).VAlign(VAlign_Center)
		[
			SNew(SCheckBox)
			.ToolTipText(LOCTEXT("MixHelp", "체크한 항목을 Palette Mix의 혼합 대상으로 사용합니다.\n\n검색·분류로 숨겨진 항목도 체크되어 있으면 포함합니다. 체크 상태는 Save Palette로 저장하세요."))
			.IsChecked_Lambda([WeakPalette = Palette, EntryId = Item->Entry.Id]
			{
				const FMD_PropPaletteEntry* Entry = WeakPalette.IsValid() ? WeakPalette->FindEntry(EntryId) : nullptr;
				return Entry && Entry->bUseInMix ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([WeakPalette = Palette, EntryId = Item->Entry.Id](ECheckBoxState State)
			{
				if (WeakPalette.IsValid())
				{
					WeakPalette->SetUseInMix(EntryId, State == ECheckBoxState::Checked);
				}
			})
			[SNew(STextBlock).Text(LOCTEXT("Mix", "Mix"))]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[SNew(SBox).WidthOverride(48).HeightOverride(48)[Item->Thumbnail->MakeThumbnailWidget(ThumbnailConfig)]]
		+ SHorizontalBox::Slot().FillWidth(1).Padding(8, 0).VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(NameWidget, SInlineEditableTextBlock)
				.Text_Lambda([WeakPalette = Palette, EntryId = Item->Entry.Id]
				{
					const FMD_PropPaletteEntry* Entry = WeakPalette.IsValid() ? WeakPalette->FindEntry(EntryId) : nullptr;
					return Entry ? FText::FromString(Entry->GetDisplayName()) : FText::GetEmpty();
				})
				.IsSelected(Row, &STableRow<TSharedPtr<FMD_PropPaletteListItem>>::IsSelectedExclusively)
				.DelayedLeftClickEntersEditMode(false)
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				.ToolTipText(LOCTEXT("RenameHelp", "항목을 선택하고 F2로 이름을 바꾸세요.\n\nEnter로 적용하고 Esc로 취소합니다. 이름을 비우면 원본 에셋 이름을 표시합니다."))
				.OnTextCommitted(this, &SMD_PropPalette::CommitEntryName, Item->Entry.Id)
			]
			+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::Format(LOCTEXT("EntryMeta", "{0} | {1}"), Type, Category)).OverflowPolicy(ETextOverflowPolicy::Ellipsis)]
		]
	);
	Item->NameWidget = NameWidget;
	return Row;
}

bool SMD_PropPalette::HasVisibleSelection() const
{
	return SelectedId.IsValid() && Items.ContainsByPredicate([this](const TSharedPtr<FMD_PropPaletteListItem>& Item) { return Item->Entry.Id == SelectedId; });
}

FReply SMD_PropPalette::HandleListKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	if (KeyEvent.GetKey() != EKeys::F2 || KeyEvent.IsAltDown() || KeyEvent.IsControlDown()
		|| KeyEvent.IsShiftDown() || KeyEvent.IsCommandDown())
	{
		return FReply::Unhandled();
	}
	const TArray<TSharedPtr<FMD_PropPaletteListItem>> Selection = ListView->GetSelectedItems();
	if (Selection.Num() != 1 || !Palette.IsValid() || !Palette->FindEntry(Selection[0]->Entry.Id))
	{
		return FReply::Unhandled();
	}
	const TSharedPtr<FMD_PropPaletteListItem>& Item = Selection[0];
	if (const TSharedPtr<SInlineEditableTextBlock> NameWidget = Item->NameWidget.Pin())
	{
		NameWidget->EnterEditingMode();
	}
	else
	{
		// Virtualized rows may be off screen; begin editing after the selected row is visible.
		PendingRenameId = Item->Entry.Id;
		ListView->RequestScrollIntoView(Item, KeyEvent.GetUserIndex());
	}
	return FReply::Handled();
}

void SMD_PropPalette::HandleItemScrolledIntoView(TSharedPtr<FMD_PropPaletteListItem> Item, const TSharedPtr<ITableRow>& Row)
{
	if (!Item.IsValid() || Item->Entry.Id != PendingRenameId)
	{
		return;
	}
	PendingRenameId.Invalidate();
	if (Item->Entry.Id == SelectedId && (ListView->HasKeyboardFocus() || ListView->HasFocusedDescendants()))
	{
		if (const TSharedPtr<SInlineEditableTextBlock> NameWidget = Item->NameWidget.Pin())
		{
			NameWidget->EnterEditingMode();
		}
	}
}

void SMD_PropPalette::CommitEntryName(const FText& Text, ETextCommit::Type Reason, FGuid EntryId)
{
	if (!Palette.IsValid() || Reason == ETextCommit::OnCleared)
	{
		return;
	}
	const FMD_PropPaletteEntry* Entry = Palette->FindEntry(EntryId);
	const FString Name = Text.ToString().TrimStartAndEnd();
	// Opening and accepting an unchanged asset name must not create a custom label.
	if (Entry && Name != Entry->GetDisplayName())
	{
		Palette->SetEntryName(EntryId, Name);
	}
}

void SMD_PropPalette::SelectItem(TSharedPtr<FMD_PropPaletteListItem> Item, ESelectInfo::Type Reason)
{
	if (!Item.IsValid() || Reason == ESelectInfo::Direct || !Properties.IsValid())
	{
		return;
	}
	const FMD_PropPaletteEntry* CurrentEntry = Palette.IsValid() ? Palette->FindEntry(Item->Entry.Id) : nullptr;
	if (!CurrentEntry)
	{
		RequestRefresh();
		return;
	}
	PendingRenameId.Invalidate();
	// Rows cache metadata, so read the latest per-prop settings from the model before selecting.
	const FMD_PropPaletteEntry Entry = *CurrentEntry;
	SelectedId = Entry.Id;
	Properties->PaletteSelectedEntryId = SelectedId;
	TGuardValue<bool> Selecting(bApplyingSelection, true);
	FText Error;
	if (!Properties->SelectPaletteEntry(Entry, Error))
	{
		Properties->ClearPaletteSelection();
		StatusMessage = Error;
		return;
	}
	StatusMessage = FText::Format(LOCTEXT("Selected", "Selected: {0}\n\n표면 위에서 위치와 자세를 확인한 뒤 클릭해 배치하세요."), FText::FromString(Item->Name));
}

FReply SMD_PropPalette::OnDragOver(const FGeometry& Geometry, const FDragDropEvent& Event)
{
	const TSharedPtr<FAssetDragDropOp> Operation = Event.GetOperationAs<FAssetDragDropOp>();
	return Operation.IsValid() && Operation->HasAssets() ? FReply::Handled() : FReply::Unhandled();
}

FReply SMD_PropPalette::OnDrop(const FGeometry& Geometry, const FDragDropEvent& Event)
{
	const TSharedPtr<FAssetDragDropOp> Operation = Event.GetOperationAs<FAssetDragDropOp>();
	if (!Operation.IsValid() || !Operation->HasAssets())
	{
		return FReply::Unhandled();
	}
	RegisterAssets(Operation->GetAssets());
	return FReply::Handled();
}

FReply SMD_PropPalette::AddSelected()
{
	TArray<FAssetData> Assets;
	FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().GetSelectedAssets(Assets);
	RegisterAssets(Assets);
	return FReply::Handled();
}

void SMD_PropPalette::RegisterAssets(const TArray<FAssetData>& Assets)
{
	if (!Palette.IsValid())
	{
		return;
	}
	int32 Duplicates = 0;
	int32 Rejected = 0;
	const int32 Added = Palette->AddAssets(Assets, Duplicates, Rejected);
	if (Added > 0)
	{
		CategoryFilter = NAME_None;
		if (Properties.IsValid())
		{
			Properties->PaletteCategoryFilter = NAME_None;
		}
		SearchBox->SetText(FText::GetEmpty());
	}
	StatusMessage = FText::Format(LOCTEXT("Added", "Added: {0} | Duplicates: {1}\nUnsupported or unavailable: {2}"),
		FText::AsNumber(Added), FText::AsNumber(Duplicates), FText::AsNumber(Rejected));
}

FReply SMD_PropPalette::DuplicateSelected()
{
	if (!Palette.IsValid() || !Properties.IsValid() || !HasVisibleSelection())
	{
		return FReply::Handled();
	}
	const bool bWasActive = Properties->ActivePaletteEntryId == SelectedId;
	if (bWasActive)
	{
		Properties->StoreActivePaletteSettings();
	}
	const FGuid NewId = Palette->DuplicateEntry(SelectedId);
	const FMD_PropPaletteEntry* NewEntry = Palette->FindEntry(NewId);
	if (!NewEntry)
	{
		RequestRefresh();
		return FReply::Handled();
	}
	// Selection can load an asset and trigger callbacks; do not retain an array pointer across it.
	const FMD_PropPaletteEntry Entry = *NewEntry;
	TGuardValue<bool> Selecting(bApplyingSelection, true);
	SelectedId = NewId;
	Properties->PaletteSelectedEntryId = NewId;
	CategoryFilter = NAME_None;
	Properties->PaletteCategoryFilter = NAME_None;
	SearchText.Reset();
	Properties->PaletteSearchText.Reset();
	SearchBox->SetText(FText::GetEmpty());
	RequestRefresh();
	FText Error;
	if (!Properties->SelectPaletteEntry(Entry, Error, bWasActive))
	{
		Properties->ClearPaletteSelection();
		StatusMessage = FText::Format(LOCTEXT("CopyUnavailable", "항목은 복제했지만 배치할 수 없습니다.\n\n{0}"), Error);
		return FReply::Handled();
	}
	StatusMessage = FText::Format(LOCTEXT("Duplicated", "Selected: {0}\n\n복제본을 목록에서 선택하고 F2로 이름을 바꾸세요. 배치 설정도 따로 바꿀 수 있습니다. 지금 자세를 반복해서 쓰려면 Freeze Current Pose를 누르세요.\n\n변경을 마치면 팔레트를 저장하세요."), FText::FromString(Entry.GetDisplayName()));
	return FReply::Handled();
}

FReply SMD_PropPalette::RemoveSelected()
{
	if (Palette.IsValid() && HasVisibleSelection())
	{
		const FGuid RemovedId = SelectedId;
		Palette->RemoveEntry(RemovedId);
		SelectedId.Invalidate();
		if (Properties.IsValid())
		{
			Properties->PaletteSelectedEntryId.Invalidate();
		}
		if (Properties.IsValid() && Properties->ActivePaletteEntryId == RemovedId)
		{
			Properties->ClearPaletteSelection();
		}
		StatusMessage = LOCTEXT("Removed", "팔레트에서 항목을 제거했습니다.\n\n이미 배치한 액터와 원본 에셋은 그대로 유지됩니다.");
	}
	return FReply::Handled();
}

FReply SMD_PropPalette::SavePalette()
{
	if (Palette.IsValid())
	{
		FText Error;
		StatusMessage = Palette->SavePalette(Error) ? LOCTEXT("SaveSucceeded", "팔레트와 소품별 배치 설정을 저장했습니다.\n\n다음 실행에서도 같은 항목과 설정을 사용할 수 있습니다.") : Error;
	}
	return FReply::Handled();
}

TSharedRef<IDetailCustomization> FMD_PropPaletteCustomization::MakeInstance()
{
	return MakeShared<FMD_PropPaletteCustomization>();
}

void FMD_PropPaletteCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	if (Objects.Num() != 1)
	{
		return;
	}
	UMD_PropPlacementProperties* Properties = Cast<UMD_PropPlacementProperties>(Objects[0].Get());
	if (!Properties)
	{
		return;
	}
	DetailBuilder.EditCategory(TEXT("Presets"), LOCTEXT("PresetsCategory", "Presets"), ECategoryPriority::Important)
		.AddCustomRow(LOCTEXT("PresetsSearch", "Presets Save Load Duplicate F2"))
		.WholeRowContent().MinDesiredWidth(260)
		[SNew(SMD_PropPresetPanel).Properties(TWeakObjectPtr<UMD_PropPlacementProperties>(Properties))];
	DetailBuilder.EditCategory(TEXT("Brush"), LOCTEXT("BrushCategory", "Brush"), ECategoryPriority::Important)
		.AddCustomRow(LOCTEXT("BrushHelpSearch", "Continuous Spacing Palette Mix Erase Undo"))
		.WholeRowContent()
		[
			SNew(STextBlock).AutoWrapText(true)
			.Text(LOCTEXT("BrushHelp", "연속 배치\nContinuous를 켜고 드래그하세요. Spacing은 표면 위 배치 기준점 사이 간격입니다.\n\n혼합과 편집\nPalette Mix는 체크한 행의 설정을 각각 사용합니다. Preview Item이 다음 소품입니다. 행 선택이나 배치 설정 변경 시 Selected Item으로 돌아갑니다.\n\n삭제와 Undo\nShift를 먼저 누르고 클릭·드래그하면 그 드래그만 삭제합니다. Action을 Erase로 바꿔 계속 삭제할 수도 있습니다. 이 도구로 배치 표시를 남긴 현재 레벨의 소품만 대상입니다. 한 번의 드래그는 Undo 한 번으로 복원합니다."))
		];
	DetailBuilder.EditCategory(TEXT("Prop"), LOCTEXT("PropCategory", "Prop"));
	DetailBuilder.EditCategory(TEXT("Placement"), LOCTEXT("PlacementCategory", "Placement"));
	DetailBuilder.EditCategory(TEXT("Orientation"), LOCTEXT("OrientationCategory", "Orientation"));
	DetailBuilder.EditCategory(TEXT("Rotation"), LOCTEXT("RotationCategory", "Rotation"));
	DetailBuilder.EditCategory(TEXT("Scale"), LOCTEXT("ScaleCategory", "Scale"));
	DetailBuilder.EditCategory(TEXT("Surface"), LOCTEXT("SurfaceCategory", "Surface"));
	DetailBuilder.EditCategory(TEXT("Randomization"), LOCTEXT("RandomizationCategory", "Randomization"));
	DetailBuilder.EditCategory(TEXT("Safety"), LOCTEXT("SafetyCategory", "Safety"));
	DetailBuilder.EditCategory(TEXT("Quick Pose"), LOCTEXT("QuickPose", "Quick Pose"))
		.AddCustomRow(LOCTEXT("QuickPoseHelpSearch", "Rotation Axis Step Shift Wheel Reroll Freeze"))
		.WholeRowContent()
		[
			SNew(STextBlock).AutoWrapText(true)
			.Text(LOCTEXT("QuickPoseHelp", "항목 편집\n회전·고정·초기화는 Selected Item에서 사용합니다. Palette Mix의 Reroll Preview는 다음 항목과 자세를 함께 뽑습니다.\n\n축과 간격\nRotation Axis와 Rotation Step을 고른 뒤 Rotate - / +를 누르세요. Flip 180은 같은 축을 반 바퀴 돌립니다.\n\n뷰포트 조작\n표면 위에서 Shift+휠로 회전합니다. Shift를 놓고 클릭하면 배치됩니다. Shift+클릭·드래그는 임시 삭제입니다.\n\n랜덤 자세 유지\nReroll Preview로 다시 뽑고, 마음에 들면 Freeze Current Pose로 고정하세요. 원래 랜덤 항목도 남기려면 먼저 Duplicate Entry를 누르세요.\n\n초기화\nReset Placement는 현재 항목의 배치 설정을 기본값으로 되돌립니다."))
		];
	DetailBuilder.EditCategory(TEXT("Palette"), LOCTEXT("Palette", "Prop Palette"), ECategoryPriority::Important)
		.AddCustomRow(LOCTEXT("PaletteSearch", "Prop Palette Mesh Blueprint Category F2 Rename"))
		.WholeRowContent().MinDesiredWidth(260)
		[SNew(SMD_PropPalette).Properties(TWeakObjectPtr<UMD_PropPlacementProperties>(Properties))];
}

#undef LOCTEXT_NAMESPACE
