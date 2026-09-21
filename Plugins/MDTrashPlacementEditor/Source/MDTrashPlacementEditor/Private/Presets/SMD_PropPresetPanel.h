#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UMD_PropPlacementProperties;
class UMD_PropPresetSettings;
class SInlineEditableTextBlock;
template<typename OptionType> class SComboBox;

struct FMD_PropPresetOption
{
	FGuid Id;
	FString Name;
};

/** Compact library controls, separate from the palette list and the placement tool. */
class SMD_PropPresetPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMD_PropPresetPanel) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UMD_PropPlacementProperties>, Properties)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);
	virtual ~SMD_PropPresetPanel() override;
	virtual void Tick(const FGeometry& Geometry, const double Time, const float DeltaTime) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;

private:
	void RequestRefresh() { bRefreshPending = true; }
	void RefreshOptions();
	bool HasSelection() const;
	void BeginRename();
	void CommitName(const FText& Text, ETextCommit::Type Reason);
	bool VerifyName(const FText& Text, FText& OutError) const;
	FReply Load();
	FReply Save();
	FReply SaveAs();
	FReply Duplicate();
	FReply Delete();
	FReply SaveLibrary();

	TWeakObjectPtr<UMD_PropPlacementProperties> Properties;
	TWeakObjectPtr<UMD_PropPresetSettings> Library;
	TArray<TSharedPtr<FMD_PropPresetOption>> Options;
	TSharedPtr<SComboBox<TSharedPtr<FMD_PropPresetOption>>> Combo;
	TSharedPtr<SInlineEditableTextBlock> NameWidget;
	FGuid RenameId;
	FText StatusMessage;
	FDelegateHandle LibraryChangedHandle;
	bool bRefreshPending = true;
	bool bRenamePending = false;
};
