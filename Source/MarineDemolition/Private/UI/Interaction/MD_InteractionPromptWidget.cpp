#include "UI/Interaction/MD_InteractionPromptWidget.h"

#include "Component/Interaction/MD_InteractionComponent.h"
#include "Components/TextBlock.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

namespace
{
	struct FInteractionPromptParts
	{
		FText DisplayName;
		FText Key;
		FText Action;
		bool bHasAction = false;
	};

	FInteractionPromptParts ParseInteractionPrompt(const FText& PromptText)
	{
		FInteractionPromptParts Parts;
		const FString PromptString = PromptText.ToString();
		FString DisplayNameString;
		FString ActionPromptString;

		if (!PromptString.Split(
				TEXT("\n"),
				&DisplayNameString,
				&ActionPromptString,
				ESearchCase::CaseSensitive,
				ESearchDir::FromStart))
		{
			if (PromptString.StartsWith(TEXT("[")))
			{
				ActionPromptString = PromptString;
			}
			else
			{
				DisplayNameString = PromptString;
			}
		}

		Parts.DisplayName = FText::FromString(DisplayNameString);
		ActionPromptString.TrimStartAndEndInline();
		Parts.bHasAction = !ActionPromptString.IsEmpty();

		// A closing-bracket key produces "[]] action"; skip the key character.
		int32 ClosingBracketIndex = INDEX_NONE;
		if (ActionPromptString.StartsWith(TEXT("[]]"))) ClosingBracketIndex = 2;
		else ActionPromptString.FindChar(TEXT(']'), ClosingBracketIndex);
		if (ActionPromptString.StartsWith(TEXT("[")) && ClosingBracketIndex > 1)
		{
			Parts.Key = FText::FromString(
				ActionPromptString.Mid(1, ClosingBracketIndex - 1));
			Parts.Action = FText::FromString(
				ActionPromptString.Mid(ClosingBracketIndex + 1).TrimStartAndEnd());
		}
		else
		{
			Parts.Action = FText::FromString(ActionPromptString);
		}

		return Parts;
	}
}

void UMD_InteractionPromptWidget::NativeConstruct()
{
	Super::NativeConstruct();

	ApplyPromptText(FText::GetEmpty());
	SetVisibility(ESlateVisibility::Collapsed);

	APlayerController* OwningController = GetOwningPlayer();
	if (!IsValid(OwningController))
	{
		return;
	}

	OwningController->OnPossessedPawnChanged.AddUniqueDynamic(
		this,
		&UMD_InteractionPromptWidget::HandlePossessedPawnChanged);
	BindToPawn(OwningController->GetPawn());
}

void UMD_InteractionPromptWidget::NativeDestruct()
{
	if (APlayerController* OwningController = GetOwningPlayer())
	{
		OwningController->OnPossessedPawnChanged.RemoveDynamic(
			this,
			&UMD_InteractionPromptWidget::HandlePossessedPawnChanged);
	}

	UnbindInteractionComponent();
	Super::NativeDestruct();
}

void UMD_InteractionPromptWidget::HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	static_cast<void>(OldPawn);
	BindToPawn(NewPawn);
}

void UMD_InteractionPromptWidget::HandleInteractionPromptChanged(FText PromptText)
{
	ApplyPromptText(PromptText);
	SetVisibility(PromptText.IsEmpty()
		? ESlateVisibility::Collapsed
		: ESlateVisibility::HitTestInvisible);
}

void UMD_InteractionPromptWidget::BindToPawn(APawn* NewPawn)
{
	UnbindInteractionComponent();

	if (IsValid(NewPawn))
	{
		BoundInteractionComponent =
			NewPawn->FindComponentByClass<UMD_InteractionComponent>();
	}

	if (IsValid(BoundInteractionComponent))
	{
		BoundInteractionComponent->OnInteractionPromptChanged.AddUniqueDynamic(
			this,
			&UMD_InteractionPromptWidget::HandleInteractionPromptChanged);
		HandleInteractionPromptChanged(
			BoundInteractionComponent->GetCurrentInteractionPrompt());
		return;
	}

	HandleInteractionPromptChanged(FText::GetEmpty());
}

void UMD_InteractionPromptWidget::UnbindInteractionComponent()
{
	if (IsValid(BoundInteractionComponent))
	{
		BoundInteractionComponent->OnInteractionPromptChanged.RemoveDynamic(
			this,
			&UMD_InteractionPromptWidget::HandleInteractionPromptChanged);
	}

	BoundInteractionComponent = nullptr;
}

void UMD_InteractionPromptWidget::ApplyPromptText(const FText& PromptText)
{
	const FInteractionPromptParts Parts = ParseInteractionPrompt(PromptText);
	SetInfoText(Parts.DisplayName);
	SetActionText(Parts.Key, Parts.Action, Parts.bHasAction);
}

void UMD_InteractionPromptWidget::SetInfoText(const FText& NewText)
{
	if (IsValid(Txt_Info))
	{
		Txt_Info->SetText(NewText);
	}

	if (IsValid(Txt_Info_Shadow))
	{
		Txt_Info_Shadow->SetText(NewText);
	}
}

void UMD_InteractionPromptWidget::SetActionText(
	const FText& NewKeyText,
	const FText& NewActionText,
	const bool bVisible)
{
	const ESlateVisibility ActionVisibility = bVisible
		? ESlateVisibility::HitTestInvisible
		: ESlateVisibility::Collapsed;

	const auto UpdateActionTextBlock = [ActionVisibility](
		UTextBlock* TextBlock,
		const FText& NewText)
	{
		if (IsValid(TextBlock))
		{
			TextBlock->SetText(NewText);
			TextBlock->SetVisibility(ActionVisibility);
		}
	};

	UpdateActionTextBlock(Txt_Key, NewKeyText);
	UpdateActionTextBlock(Txt_Key_Shadow, NewKeyText);
	UpdateActionTextBlock(Txt_Action, NewActionText);
	UpdateActionTextBlock(Txt_Action_Shadow, NewActionText);
	UpdateActionTextBlock(Txt_Sign_Left, FText::FromString(TEXT("[")));
	UpdateActionTextBlock(Txt_Sign_Left_Shadow, FText::FromString(TEXT("[")));
	UpdateActionTextBlock(Txt_Sign_Right, FText::FromString(TEXT("]")));
	UpdateActionTextBlock(Txt_Sign_Right_Shadow, FText::FromString(TEXT("]")));
}
