#include "Gameplay/Interaction/MD_InteractionPresentationLibrary.h"

#include "Engine/DataTable.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "Gameplay/Interaction/MD_InteractionSettings.h"
#include "Gameplay/Settings/MD_ControlSettingCategory.h"
#include "Gameplay/Settings/MD_SettingsManager.h"
#include "Types/Interaction/MD_InteractionTypes.h"

FName UMD_InteractionPresentationLibrary::MakeShortInteractionId(const UClass* TargetClass)
{
	if (!IsValid(TargetClass))
	{
		return NAME_None;
	}

	FString ClassName = TargetClass->GetName();
	ClassName.RemoveFromStart(TEXT("MD_"));
	return FName(*ClassName);
}

namespace
{
const UDataTable* GetValidatedInteractionPresentationTable()
{
	const UMD_InteractionSettings* Settings = UMD_InteractionSettings::Get();
	const UDataTable* Table = IsValid(Settings)
		? Settings->GetInteractionPresentationTable()
		: nullptr;
	return IsValid(Table)
		&& IsValid(Table->GetRowStruct())
		&& Table->GetRowStruct()->IsChildOf(FMD_InteractionPresentationRow::StaticStruct())
		? Table : nullptr;
}
}

bool UMD_InteractionPresentationLibrary::FindDisplayName(const FName InteractionId, FText& OutDisplayName)
{
	OutDisplayName = FText::GetEmpty();
	if (InteractionId.IsNone())
	{
		return false;
	}

	const UMD_InteractionSettings* Settings = UMD_InteractionSettings::Get();
	const UDataTable* Table = IsValid(Settings)
		? Settings->GetInteractionPresentationTable()
		: nullptr;
	if (!IsValid(Table)
		|| !IsValid(Table->GetRowStruct())
		|| !Table->GetRowStruct()->IsChildOf(FMD_InteractionPresentationRow::StaticStruct()))
	{
		return false;
	}

	static const FString Context(TEXT("MD_InteractionPresentation"));
	const FMD_InteractionPresentationRow* Row =
		Table->FindRow<FMD_InteractionPresentationRow>(InteractionId, Context, false);
	if (Row == nullptr || Row->DisplayName.IsEmpty())
	{
		return false;
	}

	OutDisplayName = Row->DisplayName;
	return true;
}

FText UMD_InteractionPresentationLibrary::FindActionPrompt(
	const FName InteractionId, UObject* WorldContextObject)
{
	if (InteractionId.IsNone()) return FText::GetEmpty();
	const UDataTable* Table = GetValidatedInteractionPresentationTable();
	if (Table == nullptr) return FText::GetEmpty();

	static const FString Context(TEXT("MD_InteractionPresentation"));
	const FMD_InteractionPresentationRow* Row =
		Table->FindRow<FMD_InteractionPresentationRow>(InteractionId, Context, false);
	if (Row == nullptr) return FText::GetEmpty();

	// Keep the text value rather than a row pointer while formatting can invoke external code.
	const FText ActionPrompt = Row->ActionPrompt;
	if (ActionPrompt.IsEmpty()) return FText::GetEmpty();

	static const FKey DefaultInteractionKey = FMD_ControlSettings().InteractKey;
	FKey InteractionKey = DefaultInteractionKey;
	if (UMD_SettingsManager* Settings = UMD_SettingsManager::Get(WorldContextObject); IsValid(Settings))
	{
		if (UMD_ControlSettingCategory* Controls = Settings->GetControlSettings(); IsValid(Controls))
		{
			const FKey AppliedKey = Controls->GetAppliedInteractionKey();
			if (AppliedKey.IsValid()) InteractionKey = AppliedKey;
		}
	}

	return FText::Format(NSLOCTEXT("MDInteraction", "KeyActionPromptFormat", "[{0}] {1}"),
		InteractionKey.GetDisplayName(false), ActionPrompt);
}

FText UMD_InteractionPresentationLibrary::ResolveInteractionDisplayName(const UObject* Target)
{
	if (!IsValid(Target)
		|| !Target->GetClass()->ImplementsInterface(UMD_Interactable::StaticClass()))
	{
		return FText::GetEmpty();
	}

	const FText TargetOwnedName = IMD_Interactable::Execute_GetInteractionDisplayName(Target);
	if (!TargetOwnedName.IsEmpty())
	{
		return TargetOwnedName;
	}

	for (const UClass* TargetClass = Target->GetClass(); IsValid(TargetClass); TargetClass = TargetClass->GetSuperClass())
	{
		FText DisplayName;
		if (FindDisplayName(TargetClass->GetFName(), DisplayName))
		{
			return DisplayName;
		}

		const FName ShortId = MakeShortInteractionId(TargetClass);
		if (ShortId != TargetClass->GetFName() && FindDisplayName(ShortId, DisplayName))
		{
			return DisplayName;
		}
	}

	return FText::GetEmpty();
}
