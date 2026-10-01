#include "Details/MD_TrashSpawnVariantChanceCustomization.h"

#include "Gameplay/Trash/MD_TrashLibrary.h"
#include "World/Trash/MD_TrashSpawnVolume.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "UObject/UnrealType.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MDTrashSpawnVariantChance"

TSharedRef<IPropertyTypeCustomization> FMD_TrashSpawnVariantChanceCustomization::MakeInstance()
{
	return MakeShared<FMD_TrashSpawnVariantChanceCustomization>();
}

void FMD_TrashSpawnVariantChanceCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	HeaderRow.NameContent()[StructPropertyHandle->CreatePropertyNameWidget()]
		.ValueContent()[SNew(STextBlock).Text(this, &FMD_TrashSpawnVariantChanceCustomization::GetVariantLabel)];
}

void FMD_TrashSpawnVariantChanceCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	VariantIdHandle = StructPropertyHandle->GetChildHandle(
		GET_MEMBER_NAME_CHECKED(FMD_TrashSpawnVariantChance, VariantId));
	TSharedPtr<IPropertyHandle> ProbabilityHandle = StructPropertyHandle->GetChildHandle(
		GET_MEMBER_NAME_CHECKED(FMD_TrashSpawnVariantChance, ProbabilityPercent));

	// Every VariantChances element has its own KindChance parent. Neighbouring
	// array elements can have different kinds, so use this element's handle.
	for (TSharedPtr<IPropertyHandle> Parent = StructPropertyHandle->GetParentHandle();
		Parent.IsValid(); Parent = Parent->GetParentHandle())
	{
		const FStructProperty* ParentStruct = CastField<FStructProperty>(Parent->GetProperty());
		if (ParentStruct && ParentStruct->Struct == FMD_TrashSpawnKindChance::StaticStruct())
		{
			KindIdHandle = Parent->GetChildHandle(
				GET_MEMBER_NAME_CHECKED(FMD_TrashSpawnKindChance, TrashKindId));
			break;
		}
	}

	if (!VariantIdHandle.IsValid() || !ProbabilityHandle.IsValid())
	{
		return;
	}

	RefreshVariantOptions();
	ChildBuilder.AddCustomRow(LOCTEXT("VariantIdFilter", "Variant"))
		.NameContent()[VariantIdHandle->CreatePropertyNameWidget()]
		.ValueContent().MinDesiredWidth(180.0f)
		[
			SAssignNew(VariantCombo, SComboBox<TSharedPtr<FName>>)
			.OptionsSource(&VariantOptions)
			.OnComboBoxOpening(this, &FMD_TrashSpawnVariantChanceCustomization::RefreshVariantOptions)
			.OnGenerateWidget(this, &FMD_TrashSpawnVariantChanceCustomization::GenerateVariantOption)
			.OnSelectionChanged(this, &FMD_TrashSpawnVariantChanceCustomization::SelectVariant)
			.IsEnabled(this, &FMD_TrashSpawnVariantChanceCustomization::CanChooseVariant)
			[
				SNew(STextBlock).Text(this, &FMD_TrashSpawnVariantChanceCustomization::GetVariantLabel)
			]
		];
	ChildBuilder.AddProperty(ProbabilityHandle.ToSharedRef());
}

FName FMD_TrashSpawnVariantChanceCustomization::GetKindId() const
{
	FName KindId;
	return KindIdHandle.IsValid() && KindIdHandle->GetValue(KindId) == FPropertyAccess::Success
		? KindId : NAME_None;
}

FName FMD_TrashSpawnVariantChanceCustomization::GetVariantId() const
{
	FName VariantId;
	return VariantIdHandle.IsValid() && VariantIdHandle->GetValue(VariantId) == FPropertyAccess::Success
		? VariantId : NAME_None;
}

void FMD_TrashSpawnVariantChanceCustomization::RefreshVariantOptions()
{
	CachedKindId = GetKindId();
	VariantOptions.Reset();
	if (!CachedKindId.IsNone())
	{
		for (const FName VariantId : UMD_TrashLibrary::GetTrashVariantIds(CachedKindId))
		{
			VariantOptions.Add(MakeShared<FName>(VariantId));
		}
	}
	if (VariantCombo.IsValid())
	{
		VariantCombo->RefreshOptions();
	}
}

FText FMD_TrashSpawnVariantChanceCustomization::GetVariantLabel() const
{
	const FName KindId = GetKindId();
	if (KindId.IsNone())
	{
		return LOCTEXT("SelectKindFirst", "Select a trash kind first");
	}
	if (CachedKindId != KindId)
	{
		return LOCTEXT("KindChanged", "Choose a variant for this kind");
	}
	if (VariantOptions.IsEmpty())
	{
		return LOCTEXT("NoValidVariants", "No variants with meshes");
	}
	const FName VariantId = GetVariantId();
	if (VariantId.IsNone())
	{
		return LOCTEXT("SelectVariant", "Select variant");
	}
	const bool bValid = VariantOptions.ContainsByPredicate(
		[VariantId](const TSharedPtr<FName>& Option) { return Option.IsValid() && *Option == VariantId; });
	return bValid ? FText::FromName(VariantId)
		: FText::Format(LOCTEXT("InvalidVariant", "Invalid: {0}"), FText::FromName(VariantId));
}

bool FMD_TrashSpawnVariantChanceCustomization::CanChooseVariant() const
{
	return VariantIdHandle.IsValid() && VariantIdHandle->IsEditable() && !GetKindId().IsNone();
}

TSharedRef<SWidget> FMD_TrashSpawnVariantChanceCustomization::GenerateVariantOption(TSharedPtr<FName> Option) const
{
	return SNew(STextBlock).Text(Option.IsValid() ? FText::FromName(*Option) : FText::GetEmpty());
}

void FMD_TrashSpawnVariantChanceCustomization::SelectVariant(
	TSharedPtr<FName> Option, ESelectInfo::Type SelectionReason)
{
	if (SelectionReason == ESelectInfo::Direct || !Option.IsValid() || !VariantIdHandle.IsValid())
	{
		return;
	}
	const FName KindId = GetKindId();
	if (KindId.IsNone() || !UMD_TrashLibrary::GetTrashVariantIds(KindId).Contains(*Option))
	{
		return;
	}
	VariantIdHandle->SetValue(*Option);
}

#undef LOCTEXT_NAMESPACE
