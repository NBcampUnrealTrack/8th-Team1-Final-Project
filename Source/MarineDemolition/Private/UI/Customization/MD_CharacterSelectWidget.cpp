#include "UI/Customization/MD_CharacterSelectWidget.h"

#include "Character/MD_Character.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/UniformGridPanel.h"
#include "Data/Customization/MD_CharacterRosterData.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateUser.h"
#include "Gameplay/Customization/MD_CharacterAppearanceLibrary.h"
#include "Groups/CommonButtonGroupBase.h"
#include "Player/MD_PlayerController.h"
#include "Player/MD_PlayerState.h"
#include "UI/MD_CommonButtonBase.h"
#include "Widgets/SViewport.h"
#include "World/Interaction/MD_CustomizationConsole.h"

UMD_CharacterSelectWidget::UMD_CharacterSelectWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// ESC(Back 액션)로 닫히게 한다. 닫힘은 Cancel과 같은 경로다
	bIsBackHandler = true;
	// 위젯 자체도 포커스를 받을 수 있게 해 Q/E 키 이벤트가 이 위젯에서 끝나게 한다
	SetIsFocusable(true);
}

UWidget* UMD_CharacterSelectWidget::NativeGetDesiredFocusTarget() const
{
	// 액션 라우터는 자기 자신을 포커스 대상으로 받지 않으므로 자식 버튼을 준다
	if (CharacterButtons.IsValidIndex(Pending.CharacterIndex) == true && IsValid(CharacterButtons[Pending.CharacterIndex]) == true)
	{
		return CharacterButtons[Pending.CharacterIndex];
	}
	return Super::NativeGetDesiredFocusTarget();
}

FReply UMD_CharacterSelectWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// 자식 버튼에 포커스가 있어도 키 이벤트는 여기까지 버블링된다. 반복 입력은 무시한다
	const FKey Key = InKeyEvent.GetKey();
	if (InKeyEvent.IsRepeat() == false && (Key == TurnLeftKey || Key == TurnRightKey))
	{
		// 하단 회전 버튼과 동일한 처리 경로 호출
		TurnCharacter(Key == TurnRightKey, true);
		return FReply::Handled();
	}

	// ESC 입력도 BP의 Back 처리와 CommonUI 기본 닫기 경로로 전달
	if (InKeyEvent.IsRepeat() == false && Key == CloseKey && NativeOnHandleBackAction() == true)
	{
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply UMD_CharacterSelectWidget::NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// 해제된 키가 좌우 회전 입력인지 확인할 키 조회
	FKey Key = InKeyEvent.GetKey();
	// 눌렀던 회전 키의 해제를 선택 위젯에서 처리
	if (Key == TurnLeftKey || Key == TurnRightKey)
	{
		// 해당 방향의 키 입력만 해제하고 나머지 입력 유지
		TurnCharacter(Key == TurnRightKey, false);
		// 회전 키 해제의 중복 전달 방지
		return FReply::Handled();
	}
	return Super::NativeOnKeyUp(InGeometry, InKeyEvent);
}

void UMD_CharacterSelectWidget::NativeOnRemovedFromFocusPath(const FFocusEvent& InFocusEvent)
{
	// 포커스 이탈로 키 해제를 놓친 경우에도 회전 중단
	StopCharacterTurn();
	Super::NativeOnRemovedFromFocusPath(InFocusEvent);
}

void UMD_CharacterSelectWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	EnsureKeyboardFocus();
}

void UMD_CharacterSelectWidget::EnsureKeyboardFocus()
{
	if (IsActivated() == false || HasKeyboardFocus() == true || HasFocusedDescendants() == true)
	{
		return;
	}

	// 마우스가 창 밖으로 나갔다 오거나 빈 곳을 클릭하면 포커스가 게임 뷰포트로 넘어가고, UIOnly 모드의 뷰포트는 키를 버린다
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	TSharedPtr<FSlateUser> SlateUser = IsValid(LocalPlayer) == true ? LocalPlayer->GetSlateUser() : nullptr;
	if (SlateUser.IsValid() == false)
	{
		return;
	}

	const TSharedPtr<SWidget> FocusedWidget = SlateUser->GetFocusedWidget();
	const TSharedPtr<SViewport> ViewportWidget =
		IsValid(LocalPlayer->ViewportClient) == true ? LocalPlayer->ViewportClient->GetGameViewportWidget() : nullptr;
	if (FocusedWidget.IsValid() == true && FocusedWidget != ViewportWidget)
	{
		return;
	}

	if (UWidget* FocusTarget = NativeGetDesiredFocusTarget())
	{
		FocusTarget->SetFocus();
	}
}

void UMD_CharacterSelectWidget::NativeOnActivated()
{
	Super::NativeOnActivated();

	Roster = UMD_CharacterAppearanceLibrary::GetCharacterRoster();
	// 에셋의 위젯 이름을 가정하지 않는 BP 참조 초기화
	InitializeLayout();

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
	BuildButtons();
	RefreshSelection();
}

void UMD_CharacterSelectWidget::NativeOnDeactivated()
{
	// 화면 종료 후 회전 입력이 남지 않도록 먼저 정리
	StopCharacterTurn();

	AMD_Character* Character = GetOwningCharacter();

	if (IsValid(OwningConsole) == true)
	{
		OwningConsole->EndCustomization(Character);
	}

	ClearButtons();
	RestoreGameplayHUD();

	Super::NativeOnDeactivated();
}

void UMD_CharacterSelectWidget::NativeDestruct()
{
	// 비활성화가 생략되는 제거 경로에서도 회전 입력 해제
	StopCharacterTurn();
	// 비활성화를 거치지 않은 제거에서도 그룹 구독과 생성 버튼 정리
	ClearButtons();
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

void UMD_CharacterSelectWidget::TurnCharacter(bool bTurnRight, bool bPressed)
{
	// 누름과 해제를 반영할 입력 방향의 유지 횟수 선택
	int32& HoldCount = bTurnRight == true ? TurnRightHoldCount : TurnLeftHoldCount;
	// 중복 해제는 무시하고 같은 방향의 키와 버튼 입력을 함께 관리
	HoldCount = FMath::Max(0, HoldCount + (bPressed == true ? 1 : -1));
	// 화면을 연 콘솔에 현재 눌린 방향만 전달
	if (IsValid(OwningConsole) == true)
	{
		// 양쪽 입력이 모두 눌리면 정지하고 하나를 떼면 남은 방향으로 회전
		OwningConsole->TurnFacing((TurnRightHoldCount > 0 ? 1.f : 0.f) - (TurnLeftHoldCount > 0 ? 1.f : 0.f));
	}
}

void UMD_CharacterSelectWidget::StopCharacterTurn()
{
	// 포커스 이탈과 화면 종료 시 왼쪽 입력 해제
	TurnLeftHoldCount = 0;
	// 포커스 이탈과 화면 종료 시 오른쪽 입력 해제
	TurnRightHoldCount = 0;
	// 연결된 콘솔이 남아 있으면 연속 회전 즉시 중단
	if (IsValid(OwningConsole) == true)
	{
		// 보간 목표 없이 회전 입력과 틱 정지
		OwningConsole->TurnFacing(0.f);
	}
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

void UMD_CharacterSelectWidget::ConfigureLayout(UPanelWidget* InCharacterPanel, UPanelWidget* InColorPanel,
	UWidget* InCharacterListViewport, UTextBlock* InCharacterName)
{
	// 로스터 카드를 배치할 실제 패널 보관
	CharacterPanel = InCharacterPanel;
	// 로스터 팔레트를 배치할 실제 패널 보관
	ColorPanel = InColorPanel;
	// 선택적 Canvas 목록 크기 조절 대상 보관
	CharacterListViewport = InCharacterListViewport;
	// 선택된 캐릭터의 이름 표시 대상 보관
	CharacterName = InCharacterName;
}

void UMD_CharacterSelectWidget::BuildButtons()
{
	// 이전 활성화에서 생성한 항목과 클릭 구독 정리
	ClearButtons();

	// 생성 기준 데이터가 없을 때 목록 구성 생략
	if (IsValid(Roster) == false)
	{
		return;
	}

	// 디자이너 예시 카드를 런타임 로스터 항목으로 교체
	if (IsValid(CharacterPanel) == true)
	{
		// 중복 카드가 남지 않도록 예시 자식 제거
		CharacterPanel->ClearChildren();
	}
	// BP에서 크기 조절 대상을 지정한 경우에만 목록 영역 갱신
	if (IsValid(CharacterListViewport) == true)
	{
		// 기존 선택 줄의 Canvas 슬롯 조회
		UCanvasPanelSlot* CharacterScrollSlot = Cast<UCanvasPanelSlot>(CharacterListViewport->Slot);
		// Canvas 슬롯에만 캐릭터 개수 기준 너비 적용
		if (IsValid(CharacterScrollSlot) == true)
		{
			// 적은 캐릭터는 중앙 배치하고 긴 목록은 스크롤로 수용
			CharacterScrollSlot->SetSize(FVector2D(
				FMath::Min(CharacterListMaxWidth, Roster->Characters.Num() * CharacterListEntrySize.X), CharacterListEntrySize.Y));
		}
	}

	// 패널과 카드 클래스가 준비된 경우 로스터 순서로 카드 생성
	if (IsValid(CharacterPanel) == true && IsValid(CharacterButtonClass) == true)
	{
		// 재활성화 시 기존 그룹을 재사용하고 최초 생성 시에만 준비
		if (IsValid(CharacterButtonGroup) == false)
		{
			// 화면 수명에 맞춰 캐릭터 카드의 단일 선택 그룹 생성
			CharacterButtonGroup = NewObject<UCommonButtonGroupBase>(this);
		}
		// 캐릭터가 있는 목록에서는 하나의 선택 유지
		CharacterButtonGroup->SetSelectionRequired(true);
		// 생성 실패가 있어도 로스터와 선택 배열의 인덱스 일치 유지
		CharacterButtons.SetNum(Roster->Characters.Num());
		// 로스터에 등록된 모든 캐릭터의 선택 카드 구성
		for (int32 Index = 0; Index < Roster->Characters.Num(); ++Index)
		{
			// 카드 표시와 클릭에 사용할 현재 로스터 항목 참조
			FMD_CharacterRosterEntry& Entry = Roster->Characters[Index];
			// 현재 화면이 소유하는 카드 인스턴스 생성
			UMD_CommonButtonBase* Button = CreateWidget<UMD_CommonButtonBase>(this, CharacterButtonClass);
			// 생성 실패 항목의 접근 차단
			if (IsValid(Button) == false)
			{
				// 다른 정상 로스터 항목의 생성 계속
				continue;
			}

			// 로스터에서 지정한 초상화 로드
			UTexture2D* Portrait = Entry.Portrait.LoadSynchronous();
			// 공통 카드의 초상화 표시 영역 조회
			UImage* PortraitImage = Button->GetContentImageWidget();
			// 이미지 영역이 있는 카드에만 초상화 표시 정책 적용
			if (IsValid(PortraitImage) == true)
			{
				// 초상화가 없는 캐릭터는 빈 이미지 영역 숨김
				PortraitImage->SetVisibility(IsValid(Portrait) == true
					? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
			}
			// 공통 카드의 캐릭터 이름 영역 조회
			UTextBlock* CharacterLabel = Button->GetContentTextWidget();
			// 클래스 설정에서 이름 표시를 직접 관리하도록 지정한 경우에만 갱신
			if (IsValid(CharacterLabel) == true && bManageCharacterNameVisibility == true)
			{
				// 기존 아이콘 전용 카드에만 초상화 존재 시 이름 숨김
				CharacterLabel->SetVisibility(IsValid(Portrait) == true
					? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
			}
			// 로드된 초상화가 있는 경우 카드 브러시에 적용
			if (IsValid(Portrait) == true)
			{
				// 크기와 틴트를 보존할 현재 이미지 브러시 조회
				FSlateBrush PortraitBrush = Button->GetContentImage();
				// 로스터 초상화의 이미지 표시 방식 지정
				PortraitBrush.DrawAs = ESlateBrushDrawType::Image;
				// 기존 카드 브러시 설정을 유지한 표시 방식 반영
				Button->SetContentImage(PortraitBrush);
				// 선택한 캐릭터의 초상화 리소스 연결
				Button->SetContentImageResourceObject(Portrait);
			}
			// 카드 이름을 로스터 표시명으로 동기화
			Button->SetContentText(Entry.DisplayName);
			// 초상화만 표시되는 경우에도 이름을 확인할 툴팁 지정
			Button->SetToolTipText(Entry.DisplayName);
			// 기존 CommonButton 선택 연출 활성화
			Button->SetIsSelectable(true);
			// 선택된 캐릭터를 다시 눌러 선택이 해제되는 동작 방지
			Button->SetIsToggleable(false);
			// BP 기본 선택 상태를 제거하여 그룹 초기 선택과 충돌 방지
			Button->ClearSelection();
			// 개별 선택 해제 대신 그룹에서 다른 카드의 선택 정리
			CharacterButtonGroup->AddWidget(Button);

			// 생성한 카드를 실제 목록에 추가
			UHorizontalBoxSlot* CharacterSlot = Cast<UHorizontalBoxSlot>(CharacterPanel->AddChild(Button));
			// 가로 목록에만 해당 화면의 카드 간격 적용
			if (IsValid(CharacterSlot) == true)
			{
				// 화면 클래스에서 지정한 카드 간격 적용
				CharacterSlot->SetPadding(CharacterButtonPadding);
				// 화면 클래스에서 지정한 카드 세로 정렬 적용
				CharacterSlot->SetVerticalAlignment(CharacterButtonVerticalAlignment);
			}
			// 선택 상태 갱신용 로스터 인덱스 보존
			CharacterButtons[Index] = Button;
		}
		// 생성 중 첫 카드의 자동 선택이 저장값을 덮어쓰지 않도록 구성 후 구독
		CharacterButtonGroup->NativeOnSelectedButtonBaseChanged.AddWeakLambda(this, [this](UCommonButtonBase* SelectedButton, int32)
		{
			// 생성 실패로 그룹 순서가 달라져도 원래 로스터 인덱스 조회
			int32 CharacterIndex = CharacterButtons.IndexOfByKey(SelectedButton);
			// 외부 선택 동기화의 재진입과 같은 캐릭터의 중복 저장 방지
			if (CharacterIndex != INDEX_NONE && Pending.CharacterIndex != CharacterIndex)
			{
				// 그룹 선택을 기존 외형 적용과 저장 경로로 전달
				SelectCharacter(CharacterIndex);
			}
		});
	}

	// 새 팔레트 항목 또는 기존 단독 색상 버튼의 생성 가능 여부 확인
	if (IsValid(ColorPanel) == true && (IsValid(ColorPaletteClass) == true || IsValid(ColorButtonClass) == true))
	{
		// 재활성화 시 팔레트의 기존 선택 그룹 재사용
		if (IsValid(ColorButtonGroup) == false)
		{
			// 캐릭터 그룹과 별도로 색상 단일 선택 그룹 생성
			ColorButtonGroup = NewObject<UCommonButtonGroupBase>(this);
		}
		// 팔레트가 있는 목록에서는 하나의 색상 선택 유지
		ColorButtonGroup->SetSelectionRequired(true);
		// 색상 생성 실패가 있어도 로스터 인덱스 유지
		ColorButtons.SetNum(Roster->Palette.Num());
		// 로스터 팔레트 개수만큼 색상 항목 생성
		for (int32 Index = 0; Index < Roster->Palette.Num(); ++Index)
		{
			// 표시 이름과 미리보기 색상에 사용할 로스터 항목 참조
			FMD_CharacterPaletteEntry& Entry = Roster->Palette[Index];
			// 목록에 추가할 외부 항목과 선택 처리용 내부 버튼 분리
			UWidget* ColorEntry = nullptr;
			// 새 팔레트와 기존 버튼에서 공유할 선택 대상 준비
			UMD_CommonButtonBase* Button = nullptr;
			// 이름과 색상을 함께 표시하는 새 팔레트 클래스 우선 사용
			if (IsValid(ColorPaletteClass) == true)
			{
				// 현재 화면이 소유하는 팔레트 항목 생성
				UUserWidget* PaletteWidget = CreateWidget<UUserWidget>(this, ColorPaletteClass);
				// 생성된 팔레트에만 로스터 표시값 적용
				if (IsValid(PaletteWidget) == true)
				{
					// BP의 기존 색상과 이름 표시 흐름 재사용
					InitializeColorPalette(PaletteWidget, Entry.DisplayName, Entry.Color);
					// 클릭과 선택 상태를 담당하는 실제 버튼 조회
					Button = GetPaletteButton(PaletteWidget);
					// 내부 버튼 대신 전체 팔레트 항목을 목록에 배치
					ColorEntry = PaletteWidget;
				}
			}
			else
			{
				// 기존 화면의 단독 색상 버튼 생성 방식 유지
				Button = CreateWidget<UMD_CommonButtonBase>(this, ColorButtonClass);
				// 기존 버튼은 버튼 자체를 목록 항목으로 사용
				ColorEntry = Button;
			}
			// 생성 실패 또는 필수 내부 버튼 누락 항목 제외
			if (IsValid(Button) == false)
			{
				// 나머지 색상 항목의 생성 계속
				continue;
			}

			// BP 초기화가 없는 기존 단독 색상 버튼에만 표시값 적용
			if (IsValid(ColorPaletteClass) == false)
			{
				// 기존 이미지 기반 버튼의 색상 표시 유지
				Button->SetContentImageColor(Entry.Color);
				// 기존 단독 버튼의 표시 이름 유지
				Button->SetContentText(Entry.DisplayName);
			}
			// 색상 이름을 툴팁으로 제공
			Button->SetToolTipText(Entry.DisplayName);
			// CommonButton의 선택 상태와 BP 강조 연출 활성화
			Button->SetIsSelectable(true);
			// 선택된 색상을 다시 눌러도 선택 유지
			Button->SetIsToggleable(false);
			// BP 기본 선택값을 그룹에 등록하기 전에 초기화
			Button->ClearSelection();
			// 실제 내부 색상 버튼을 그룹의 단일 선택 대상으로 등록
			ColorButtonGroup->AddWidget(Button);

			// 기존 2열 그리드의 호환 배치 여부 확인
			UUniformGridPanel* Grid = Cast<UUniformGridPanel>(ColorPanel);
			// 기존 그리드에만 행과 열 지정
			if (IsValid(Grid) == true)
			{
				// 잘못된 설정에서도 나눗셈 오류가 없도록 사용할 열 개수 제한
				int32 ColumnCount = FMath::Max(1, PaletteColumnCount);
				// 화면 클래스에서 지정한 팔레트 열 개수 적용
				Grid->AddChildToUniformGrid(ColorEntry, Index / ColumnCount, Index % ColumnCount);
			}
			else
			{
				// 새 WrapBox의 너비와 간격 설정에 따른 자동 줄바꿈 사용
				ColorPanel->AddChild(ColorEntry);
			}
			// 실제 로스터 인덱스에 해당하는 선택 버튼 보관
			ColorButtons[Index] = Button;
		}
		// 팔레트 구성 중 자동 선택이 기존 외형을 덮어쓰지 않도록 구성 후 구독
		ColorButtonGroup->NativeOnSelectedButtonBaseChanged.AddWeakLambda(this, [this](UCommonButtonBase* SelectedButton, int32)
		{
			// 생성 실패가 있어도 원래 팔레트 인덱스로 색상 적용
			int32 ColorIndex = ColorButtons.IndexOfByKey(SelectedButton);
			// 외부 선택 동기화와 같은 색상의 중복 저장 방지
			if (ColorIndex != INDEX_NONE && Pending.ColorIndex != ColorIndex)
			{
				// 그룹 선택을 기존 색상 적용과 저장 경로로 전달
				SelectColor(ColorIndex);
			}
		});
	}
}

void UMD_CharacterSelectWidget::ClearButtons()
{
	// 제거되는 카드에서 선택 변경이 발생하기 전에 그룹 연결 정리
	if (IsValid(CharacterButtonGroup) == true)
	{
		// 재활성화 시 선택 적용 구독의 중복 방지
		CharacterButtonGroup->NativeOnSelectedButtonBaseChanged.RemoveAll(this);
		// 이전 카드의 선택과 그룹 이벤트 구독을 함께 해제
		CharacterButtonGroup->RemoveAll();
	}
	// 동적으로 생성한 캐릭터 카드 정리
	for (UMD_CommonButtonBase* Button : CharacterButtons)
	{
		// 아직 유효한 카드만 목록에서 제거
		if (IsValid(Button) == true)
		{
			// 이전 활성화에서 생성한 카드 제거
			Button->RemoveFromParent();
		}
	}
	// 팔레트 항목 제거 전에 내부 버튼의 그룹 연결 정리
	if (IsValid(ColorButtonGroup) == true)
	{
		// 재활성화 시 색상 적용 구독의 중복 방지
		ColorButtonGroup->NativeOnSelectedButtonBaseChanged.RemoveAll(this);
		// 내부 색상 버튼의 선택과 그룹 이벤트 구독 해제
		ColorButtonGroup->RemoveAll();
	}
	// 팔레트 항목을 소유하는 패널의 유효성 확인
	if (IsValid(ColorPanel) == true)
	{
		// 별도 참조 배열 없이 이름과 버튼을 포함한 팔레트 항목 제거
		ColorPanel->ClearChildren();
	}
	// 다음 활성화를 위한 캐릭터 버튼 참조 초기화
	CharacterButtons.Reset();
	// 다음 활성화를 위한 색상 버튼 참조 초기화
	ColorButtons.Reset();
}

void UMD_CharacterSelectWidget::RefreshSelection()
{
	// 현재 외형의 캐릭터 버튼이 생성된 경우에만 그룹 선택 동기화
	if (IsValid(CharacterButtonGroup) == true && CharacterButtons.IsValidIndex(Pending.CharacterIndex) == true
		&& IsValid(CharacterButtons[Pending.CharacterIndex]) == true)
	{
		// 선택 음향을 중복 재생하지 않고 그룹을 통해 이전 카드 선택 해제
		CharacterButtonGroup->SelectButtonAtIndex(CharacterButtonGroup->FindButtonIndex(CharacterButtons[Pending.CharacterIndex]), false);
	}
	// 현재 외형의 팔레트 버튼이 생성된 경우에만 색상 그룹 동기화
	if (IsValid(ColorButtonGroup) == true && ColorButtons.IsValidIndex(Pending.ColorIndex) == true
		&& IsValid(ColorButtons[Pending.ColorIndex]) == true)
	{
		// 캐릭터 선택을 유지하며 색상 그룹의 이전 선택만 해제
		ColorButtonGroup->SelectButtonAtIndex(ColorButtonGroup->FindButtonIndex(ColorButtons[Pending.ColorIndex]), false);
	}

	if (IsValid(CharacterName) == true && IsValid(Roster) == true
		&& Roster->IsValidCharacterIndex(Pending.CharacterIndex) == true)
	{
		// BP에서 전달한 텍스트에 선택된 캐릭터 이름 반영
		CharacterName->SetText(Roster->Characters[Pending.CharacterIndex].DisplayName);
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
