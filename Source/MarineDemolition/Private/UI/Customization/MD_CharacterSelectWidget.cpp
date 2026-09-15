#include "UI/Customization/MD_CharacterSelectWidget.h"

#include "Character/MD_Character.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/UniformGridPanel.h"
#include "Data/Customization/MD_CharacterRosterData.h"
#include "Engine/Texture2D.h"
#include "Gameplay/Customization/MD_CharacterAppearanceLibrary.h"
#include "Player/MD_PlayerController.h"
#include "Player/MD_PlayerState.h"
#include "UI/MD_CommonButtonBase.h"
#include "World/Interaction/MD_CustomizationConsole.h"

UMD_CharacterSelectWidget::UMD_CharacterSelectWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// ESC(Back 액션)로 닫히게 한다. 닫힘은 Cancel과 같은 경로다
	bIsBackHandler = true;
}

void UMD_CharacterSelectWidget::NativeOnActivated()
{
	Super::NativeOnActivated();

	Roster = UMD_CharacterAppearanceLibrary::GetCharacterRoster();

	// 현재 확정된 외형에서 시작한다. PlayerState가 아직 없으면 캐릭터에 적용된 값을 쓴다
	APlayerController* PC = GetOwningPlayer();
	AMD_PlayerState* MDPlayerState = IsValid(PC) == true ? PC->GetPlayerState<AMD_PlayerState>() : nullptr;
	if (IsValid(MDPlayerState) == true)
	{
		Pending = MDPlayerState->GetAppearance();
	}
	else if (AMD_Character* Character = GetOwningCharacter())
	{
		Pending = Character->GetAppliedAppearance();
	}
	if (IsValid(Roster) == true)
	{
		Pending = Roster->Sanitize(Pending);
	}

	if (AMD_PlayerController* MDPC = Cast<AMD_PlayerController>(PC))
	{
		MDPC->SetGameplayHUDVisible(false);
		bGameplayHUDHidden = true;
	}
	if (IsValid(Btn_Close) == true && CloseClickedHandle.IsValid() == false)
	{
		CloseClickedHandle = Btn_Close->OnClicked().AddWeakLambda(this, [this]() { Cancel(); });
	}

	BuildButtons();
	RefreshSelection();
}

void UMD_CharacterSelectWidget::NativeOnDeactivated()
{
	AMD_Character* Character = GetOwningCharacter();

	if (IsValid(OwningConsole) == true)
	{
		OwningConsole->EndCustomization(Character);
	}

	if (IsValid(Btn_Close) == true && CloseClickedHandle.IsValid() == true)
	{
		Btn_Close->OnClicked().Remove(CloseClickedHandle);
		CloseClickedHandle.Reset();
	}

	ClearButtons();
	RestoreGameplayHUD();

	Super::NativeOnDeactivated();
}

void UMD_CharacterSelectWidget::NativeDestruct()
{
	// 레이어 제거/레벨 이동으로 비활성화 경로를 건너뛰어도 HUD를 복구한다.
	RestoreGameplayHUD();
	Super::NativeDestruct();
}

void UMD_CharacterSelectWidget::RestoreGameplayHUD()
{
	if (bGameplayHUDHidden)
	{
		if (AMD_PlayerController* MDPC = Cast<AMD_PlayerController>(GetOwningPlayer()))
		{
			MDPC->SetGameplayHUDVisible(true);
		}
		bGameplayHUDHidden = false;
	}
}

void UMD_CharacterSelectWidget::SelectCharacter(int32 CharacterIndex)
{
	if (IsValid(Roster) == false || Roster->IsValidCharacterIndex(CharacterIndex) == false)
	{
		return;
	}

	Pending.CharacterIndex = CharacterIndex;
	ApplyPreview();
	CommitSelection();
	RefreshSelection();
	OnSelectionChanged(Pending);
}

void UMD_CharacterSelectWidget::SelectColor(int32 ColorIndex)
{
	if (IsValid(Roster) == false || Roster->IsValidColorIndex(ColorIndex) == false)
	{
		return;
	}

	Pending.ColorIndex = ColorIndex;
	ApplyPreview();
	CommitSelection();
	RefreshSelection();
	OnSelectionChanged(Pending);
}

void UMD_CharacterSelectWidget::Confirm()
{
	CommitSelection();
	DeactivateWidget();
}

void UMD_CharacterSelectWidget::CommitSelection()
{
	UMD_CharacterAppearanceLibrary::SaveLocalAppearance(Pending);

	// 서버 PlayerState에 실리면 복제로 모든 클라이언트 캐릭터에 적용된다
	AMD_PlayerController* MDPC = Cast<AMD_PlayerController>(GetOwningPlayer());
	if (IsValid(MDPC) == true)
	{
		MDPC->ServerApplyAppearance(Pending);
	}
}

void UMD_CharacterSelectWidget::Cancel()
{
	DeactivateWidget();
}

AMD_Character* UMD_CharacterSelectWidget::GetOwningCharacter() const
{
	return Cast<AMD_Character>(GetOwningPlayerPawn());
}

void UMD_CharacterSelectWidget::BuildButtons()
{
	ClearButtons();

	if (IsValid(Roster) == false)
	{
		return;
	}

	// 캐릭터 수가 적을 때는 선택 줄 자체를 중앙에 맞추고, 길면 스크롤한다.
	if (UWidget* CharacterScroll = GetWidgetFromName(TEXT("SC_Characters")))
	{
		if (UCanvasPanelSlot* CharacterScrollSlot = Cast<UCanvasPanelSlot>(CharacterScroll->Slot))
		{
			CharacterScrollSlot->SetSize(FVector2D(FMath::Min(640.f, Roster->Characters.Num() * 116.f), 116.f));
		}
	}

	if (IsValid(Panel_Characters) == true && IsValid(CharacterButtonClass) == true)
	{
		for (int32 Index = 0; Index < Roster->Characters.Num(); ++Index)
		{
			const FMD_CharacterRosterEntry& Entry = Roster->Characters[Index];
			UMD_CommonButtonBase* Button = CreateWidget<UMD_CommonButtonBase>(this, CharacterButtonClass);
			if (IsValid(Button) == false)
			{
				continue;
			}

			// 초상화가 있으면 이미지, 없으면 이름으로 구분한다
			UTexture2D* Portrait = Entry.Portrait.LoadSynchronous();
			if (UWidget* PortraitImage = Button->GetWidgetFromName(TEXT("Img_ButtonImage")))
			{
				PortraitImage->SetVisibility(IsValid(Portrait)
					? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
			}
			if (UWidget* CharacterLabel = Button->GetWidgetFromName(TEXT("Txt_ButtonText")))
			{
				CharacterLabel->SetVisibility(IsValid(Portrait)
					? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
			}
			if (IsValid(Portrait) == true)
			{
				FSlateBrush PortraitBrush = Button->GetContentImage();
				PortraitBrush.DrawAs = ESlateBrushDrawType::Image;
				Button->SetContentImage(PortraitBrush);
				Button->SetContentImageResourceObject(Portrait);
			}
			Button->SetContentText(Entry.DisplayName);
			Button->SetToolTipText(Entry.DisplayName);
			Button->SetIsSelectable(true);
			Button->OnClicked().AddWeakLambda(this, [this, Index]() { SelectCharacter(Index); });

			if (UHorizontalBoxSlot* CharacterSlot = Cast<UHorizontalBoxSlot>(Panel_Characters->AddChild(Button)))
			{
				CharacterSlot->SetPadding(FMargin(6.f, 0.f));
			}
			CharacterButtons.Add(Button);
		}
	}

	if (IsValid(Panel_Colors) == true && IsValid(ColorButtonClass) == true)
	{
		for (int32 Index = 0; Index < Roster->Palette.Num(); ++Index)
		{
			const FMD_CharacterPaletteEntry& Entry = Roster->Palette[Index];
			UMD_CommonButtonBase* Button = CreateWidget<UMD_CommonButtonBase>(this, ColorButtonClass);
			if (IsValid(Button) == false)
			{
				continue;
			}

			Button->SetContentImageColor(Entry.Color);
			Button->SetContentText(Entry.DisplayName);
			Button->SetToolTipText(Entry.DisplayName);
			Button->SetIsSelectable(true);
			Button->OnClicked().AddWeakLambda(this, [this, Index]() { SelectColor(Index); });

			// 우측 팔레트는 2열로 제한한다. 색이 늘어나면 세로 스크롤로 수용한다.
			if (UUniformGridPanel* Grid = Cast<UUniformGridPanel>(Panel_Colors))
			{
				Grid->AddChildToUniformGrid(Button, Index / 2, Index % 2);
			}
			else
			{
				Panel_Colors->AddChild(Button);
			}
			ColorButtons.Add(Button);
		}
	}
}

void UMD_CharacterSelectWidget::ClearButtons()
{
	for (UMD_CommonButtonBase* Button : CharacterButtons)
	{
		if (IsValid(Button) == true)
		{
			Button->RemoveFromParent();
		}
	}
	for (UMD_CommonButtonBase* Button : ColorButtons)
	{
		if (IsValid(Button) == true)
		{
			Button->RemoveFromParent();
		}
	}
	CharacterButtons.Reset();
	ColorButtons.Reset();
}

void UMD_CharacterSelectWidget::RefreshSelection()
{
	for (int32 Index = 0; Index < CharacterButtons.Num(); ++Index)
	{
		if (IsValid(CharacterButtons[Index]) == true)
		{
			CharacterButtons[Index]->SetIsSelected(Index == Pending.CharacterIndex, false);
		}
	}
	for (int32 Index = 0; Index < ColorButtons.Num(); ++Index)
	{
		if (IsValid(ColorButtons[Index]) == true)
		{
			ColorButtons[Index]->SetIsSelected(Index == Pending.ColorIndex, false);
		}
	}

	if (IsValid(Txt_CharacterName) == true && IsValid(Roster) == true
		&& Roster->IsValidCharacterIndex(Pending.CharacterIndex) == true)
	{
		Txt_CharacterName->SetText(Roster->Characters[Pending.CharacterIndex].DisplayName);
	}
}

void UMD_CharacterSelectWidget::ApplyPreview()
{
	// 서버 왕복을 기다리지 않고 로컬 외형을 먼저 반영한다.
	AMD_Character* Character = GetOwningCharacter();
	if (IsValid(Character) == true)
	{
		Character->ApplyAppearance(Pending);
	}
}
