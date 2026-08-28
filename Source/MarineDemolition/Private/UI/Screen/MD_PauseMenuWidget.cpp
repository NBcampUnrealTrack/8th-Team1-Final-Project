#include "UI/Screen/MD_PauseMenuWidget.h"

#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Online/Session/MD_MultiplayManager.h"
#include "UI/Screen/MD_PlayerInfoWidget.h"

void UMD_PauseMenuWidget::NativeOnActivated()
{
	Super::NativeOnActivated();

	// 현재 세션의 참가 코드를 방 코드 텍스트에 표시한다.
	RefreshRoomCode();
	// 현재 접속 중인 플레이어를 기준으로 정보 카드 목록을 구성한다.
	BuildPlayerList();
}

void UMD_PauseMenuWidget::BuildPlayerList()
{
	// 플레이어 목록 또는 생성할 카드 클래스가 없으면 목록 구성을 중단한다.
	if (IsValid(VB_PlayerList) == false || IsValid(PlayerInfoWidgetClass) == false)
	{
		return;
	}

	// 이전 접속 상태에서 생성한 플레이어 카드가 남지 않도록 목록을 비운다.
	VB_PlayerList->ClearChildren();

	// 현재 접속 플레이어를 관리하는 GameState를 가져온다.
	UWorld* World = GetWorld();
	if (IsValid(World) == false)
	{
		return;
	}

	// 현재 월드의 기본 GameState를 가져온다.
	AGameStateBase* GameState = World->GetGameState();
	if (IsValid(GameState) == false)
	{
		return;
	}

	// 생성된 플레이어 정보 카드의 입력 소유자를 가져온다.
	APlayerController* OwningPlayer = GetOwningPlayer();
	if (IsValid(OwningPlayer) == false)
	{
		return;
	}

	// 현재 복제된 모든 PlayerState를 순회한다.
	for (APlayerState* PlayerState : GameState->PlayerArray)
	{
		// 아직 유효하지 않은 PlayerState에는 카드를 생성하지 않는다.
		if (IsValid(PlayerState) == false)
		{
			continue;
		}

		// 접속 플레이어 한 명을 표시할 정보 카드를 생성한다.
		UMD_PlayerInfoWidget* PlayerInfoWidget = CreateWidget<UMD_PlayerInfoWidget>(OwningPlayer, PlayerInfoWidgetClass);
		if (IsValid(PlayerInfoWidget) == false)
		{
			continue;
		}

		// PlayerState에 복제된 플레이어 이름을 가져온다.
		FString PlayerName = PlayerState->GetPlayerName();
		// 플레이어 정보 카드에 표시할 텍스트로 이름을 변환한다.
		FText PlayerNameText = FText::FromString(PlayerName);
		// 플레이어 이름을 카드 본문과 그림자에 함께 적용한다.
		PlayerInfoWidget->SetPlayerName(PlayerNameText);

		// 생성한 플레이어 정보 카드를 세로 목록에 추가한다.
		UVerticalBoxSlot* PlayerInfoSlot = VB_PlayerList->AddChildToVerticalBox(PlayerInfoWidget);
		// 목록 슬롯을 생성하지 못한 경우 다음 플레이어 처리를 계속한다.
		if (IsValid(PlayerInfoSlot) == false)
		{
			continue;
		}
	}
}

void UMD_PauseMenuWidget::RefreshRoomCode()
{
	// 방 코드를 표시할 텍스트가 없는 변형 위젯에서는 갱신을 생략한다.
	if (IsValid(Txt_RoomCode) == false)
	{
		return;
	}

	// 현재 게임 인스턴스에서 멀티플레이 세션 관리자를 조회한다.
	UGameInstance* GameInstance = GetGameInstance();
	if (IsValid(GameInstance) == false)
	{
		return;
	}

	// 현재 참가 중인 세션의 방 코드를 제공하는 서브시스템을 가져온다.
	UMD_MultiplayManager* MultiplayManager = GameInstance->GetSubsystem<UMD_MultiplayManager>();
	if (IsValid(MultiplayManager) == false)
	{
		return;
	}

	// 세션 관리자가 보관하는 현재 방 코드를 문자열로 가져온다.
	FString RoomCode = MultiplayManager->GetCurrentRoomCode();
	// TextBlock에 적용할 수 있도록 방 코드 문자열을 텍스트로 변환한다.
	FText RoomCodeText = FText::FromString(RoomCode);
	// 변환한 방 코드를 일시 정지 메뉴에 표시한다.
	Txt_RoomCode->SetText(RoomCodeText);
}
