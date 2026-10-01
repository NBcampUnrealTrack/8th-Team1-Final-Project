#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"
#include "Widgets/Input/SComboBox.h"

class IPropertyHandle;

/** Shows only the mesh variants belonging to this array element's trash kind. */
class FMD_TrashSpawnVariantChanceCustomization final : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle,
		FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle,
		IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:
	FName GetKindId() const;
	FName GetVariantId() const;
	void RefreshVariantOptions();
	FText GetVariantLabel() const;
	bool CanChooseVariant() const;
	TSharedRef<SWidget> GenerateVariantOption(TSharedPtr<FName> Option) const;
	void SelectVariant(TSharedPtr<FName> Option, ESelectInfo::Type SelectionReason);

	TSharedPtr<IPropertyHandle> KindIdHandle;
	TSharedPtr<IPropertyHandle> VariantIdHandle;
	TSharedPtr<SComboBox<TSharedPtr<FName>>> VariantCombo;
	TArray<TSharedPtr<FName>> VariantOptions;
	FName CachedKindId;
};
