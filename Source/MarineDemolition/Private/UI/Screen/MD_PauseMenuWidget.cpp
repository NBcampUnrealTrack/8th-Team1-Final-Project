#include "UI/Screen/MD_PauseMenuWidget.h"

#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Game/MD_GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Online/Session/MD_MultiplayManager.h"
#include "UI/Screen/MD_PlayerInfoWidget.h"

void UMD_PauseMenuWidget::NativeOnActivated()
{
	Super::NativeOnActivated();

	// 현재 세션 참가 코드의 방 코드 텍스트 표시
	RefreshRoomCode();
	// 활성화 중 발생하는 플레이어 접속, 퇴장과 이름 변경 이벤트 구독
	BindPlayerListEvents();
}

void UMD_PauseMenuWidget::NativeOnDeactivated()
{
	// 비활성화된 메뉴의 플레이어 목록 변경 이벤트 구독 해제
	UnbindPlayerListEvents();

	Super::NativeOnDeactivated();
}

void UMD_PauseMenuWidget::BuildPlayerList()
{
	// 플레이어 목록 영역이 없는 변형 위젯의 목록 구성 중단
	if (IsValid(VB_PlayerList) == false)
	{
		return;
	}

	// 플레이어 정보 카드 클래스가 없는 경우 목록 구성 중단
	if (IsValid(PlayerInfoWidgetClass) == false)
	{
		return;
	}

	// 이전 접속 상태에서 생성한 플레이어 카드 목록 초기화
	VB_PlayerList->ClearChildren();

	// 현재 접속 플레이어를 관리하는 GameState 조회
	UWorld* World = GetWorld();
	if (IsValid(World) == false)
	{
		return;
	}

	// 현재 월드의 기본 GameState 조회
	AGameStateBase* GameState = World->GetGameState();
	if (IsValid(GameState) == false)
	{
		return;
	}

	// 생성할 플레이어 정보 카드의 입력 소유자 조회
	APlayerController* OwningPlayer = GetOwningPlayer();
	if (IsValid(OwningPlayer) == false)
	{
		return;
	}

	// 현재 복제된 모든 PlayerState 순회
	for (APlayerState* PlayerState : GameState->PlayerArray)
	{
		// 아직 유효하지 않은 PlayerState의 카드 생성 생략
		if (IsValid(PlayerState) == false)
		{
			continue;
		}

		// 접속 플레이어 한 명을 표시할 정보 카드 생성
		UMD_PlayerInfoWidget* PlayerInfoWidget = CreateWidget<UMD_PlayerInfoWidget>(OwningPlayer, PlayerInfoWidgetClass);
		if (IsValid(PlayerInfoWidget) == false)
		{
			continue;
		}

		// PlayerState에 복제된 플레이어 이름 조회
		FString PlayerName = PlayerState->GetPlayerName();
		// 플레이어 이름을 정보 카드 표시용 텍스트로 변환
		FText PlayerNameText = FText::FromString(PlayerName);
		// 플레이어 이름의 카드 본문과 그림자 동시 적용
		PlayerInfoWidget->SetPlayerName(PlayerNameText);

		// 생성한 플레이어 정보 카드의 세로 목록 추가
		UVerticalBoxSlot* PlayerInfoSlot = VB_PlayerList->AddChildToVerticalBox(PlayerInfoWidget);
		// 목록 슬롯 생성 실패 시 다음 플레이어 처리
		if (IsValid(PlayerInfoSlot) == false)
		{
			continue;
		}
	}
}

void UMD_PauseMenuWidget::RefreshRoomCode()
{
	// 방 코드 텍스트가 없는 변형 위젯의 갱신 생략
	if (IsValid(Txt_RoomCode) == false)
	{
		return;
	}

	// 현재 게임 인스턴스의 멀티플레이 세션 관리자 조회
	UGameInstance* GameInstance = GetGameInstance();
	if (IsValid(GameInstance) == false)
	{
		return;
	}

	// 현재 참가 중인 세션의 방 코드를 제공하는 서브시스템 조회
	UMD_MultiplayManager* MultiplayManager = GameInstance->GetSubsystem<UMD_MultiplayManager>();
	if (IsValid(MultiplayManager) == false)
	{
		return;
	}

	// 세션 관리자가 보관하는 현재 방 코드 문자열 조회
	FString RoomCode = MultiplayManager->GetCurrentRoomCode();
	// TextBlock 적용을 위한 방 코드 문자열의 텍스트 변환
	FText RoomCodeText = FText::FromString(RoomCode);
	// 변환한 방 코드의 일시 정지 메뉴 표시
	Txt_RoomCode->SetText(RoomCodeText);
}

void UMD_PauseMenuWidget::BindPlayerListEvents()
{
	// 재활성화 과정의 이벤트 중복 등록 방지를 위한 기존 구독 해제
	UnbindPlayerListEvents();

	// GameState 설정 이벤트를 제공하는 현재 월드 조회
	UWorld* World = GetWorld();
	if (IsValid(World) == false)
	{
		return;
	}

	// 비활성화 시 월드 이벤트 구독 해제를 위한 약한 참조 저장
	BoundWorld = World;
	// Seamless Travel 등의 GameState 교체에 대응하는 새 목록 연결
	World->GameStateSetEvent.AddUObject(this, &UMD_PauseMenuWidget::HandleGameStateSet);

	// 기존 GameState 즉시 연결과 현재 플레이어 목록 구성
	AGameStateBase* GameState = World->GetGameState();
	HandleGameStateSet(GameState);
}

void UMD_PauseMenuWidget::UnbindPlayerListEvents()
{
	// 현재 GameState의 플레이어 목록 변경 이벤트 구독 해제
	if (BoundGameState.IsValid() == true)
	{
		BoundGameState->OnPlayerListChanged.RemoveAll(this);
	}
	// 이전 GameState의 후속 이벤트 전달 방지를 위한 약한 참조 초기화
	BoundGameState.Reset();

	// 현재 월드의 GameState 설정 이벤트 구독 해제
	if (BoundWorld.IsValid() == true)
	{
		BoundWorld->GameStateSetEvent.RemoveAll(this);
	}
	// 비활성화된 메뉴의 월드 참조 방지를 위한 약한 참조 초기화
	BoundWorld.Reset();
}

void UMD_PauseMenuWidget::HandleGameStateSet(AGameStateBase* GameState)
{
	// 교체 전 GameState의 목록 변경 이벤트 구독 해제
	if (BoundGameState.IsValid() == true)
	{
		BoundGameState->OnPlayerListChanged.RemoveAll(this);
	}
	// 새 GameState 검사 전 이전 약한 참조 초기화
	BoundGameState.Reset();

	// 프로젝트 플레이어 목록 이벤트를 제공하는 GameState로 변환
	AMD_GameStateBase* MDGameState = Cast<AMD_GameStateBase>(GameState);
	if (IsValid(MDGameState) == true)
	{
		// 비활성화 또는 교체 시 구독 해제를 위한 새 GameState 저장
		BoundGameState = MDGameState;
		// 접속, 퇴장과 이름 변경 이벤트의 목록 재구성 함수 직접 연결
		MDGameState->OnPlayerListChanged.AddUObject(this, &UMD_PauseMenuWidget::BuildPlayerList);
	}

	// 새 GameState에 등록된 전체 플레이어 기준 목록 즉시 구성
	BuildPlayerList();
}
