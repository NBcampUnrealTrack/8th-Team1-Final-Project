#include "UI/Interaction/MD_InteractionPromptWidget.h"

#include "Component/Interaction/MD_InteractionComponent.h"
#include "Components/TextBlock.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/Interaction/MD_InteractionPresentationLibrary.h"

void UMD_InteractionPromptWidget::NativeConstruct()
{
	Super::NativeConstruct();

	SetInfoText(FText::GetEmpty());
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

void UMD_InteractionPromptWidget::HandleInteractionTargetChanged(AActor* NewTarget)
{
	const FText DisplayName =
		UMD_InteractionPresentationLibrary::ResolveInteractionDisplayName(NewTarget);
	SetInfoText(DisplayName);
	SetVisibility(DisplayName.IsEmpty()
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
		BoundInteractionComponent->OnInteractionTargetChanged.AddUniqueDynamic(
			this,
			&UMD_InteractionPromptWidget::HandleInteractionTargetChanged);
		HandleInteractionTargetChanged(BoundInteractionComponent->GetCurrentTarget());
		return;
	}

	HandleInteractionTargetChanged(nullptr);
}

void UMD_InteractionPromptWidget::UnbindInteractionComponent()
{
	if (IsValid(BoundInteractionComponent))
	{
		BoundInteractionComponent->OnInteractionTargetChanged.RemoveDynamic(
			this,
			&UMD_InteractionPromptWidget::HandleInteractionTargetChanged);
	}

	BoundInteractionComponent = nullptr;
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
