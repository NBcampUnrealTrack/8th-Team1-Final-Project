// Fill out your copyright notice in the Description page of Project Settings.


#include "Online/Voice/MD_CharacterVOIPTalker.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Player/MD_PlayerState.h"
#include "TimerManager.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "UI/MD_LocalUIManager.h"
#include "Components/PanelWidget.h"
#include "GameFramework/PlayerController.h"
#include "UI/Game/MD_HUDWidget.h"
#include "UI/Game/MD_TalkNotificationWidget.h"
#include "Online/Voice/MD_VoiceChatComponent.h"

void UMD_CharacterVOIPTalker::BeginPlay()
{
	Super::BeginPlay();

	TryRegisterWithOwnerPlayerState();
	// 소유권과 음성 컴포넌트의 늦은 준비를 포함한 로컬 송신 상태 확인 등록
	UWorld* VoiceWorld = GetWorld();
	if (IsValid(VoiceWorld) == true)
	{
		VoiceWorld->GetTimerManager().SetTimer(LocalVoiceStateTimer, this, &UMD_CharacterVOIPTalker::UpdateLocalVoiceState, 0.1f, true);
	}
	if (bVoiceRegistrationComplete == false)
	{
		if (UWorld* World = GetWorld())
		{
			// PlayerState 등록 재시도 주기
			float PlayerStateRetryIntervalSeconds = 0.2f;
			World->GetTimerManager().SetTimer(
				PlayerStateRetryTimerHandle,
				this,
				&UMD_CharacterVOIPTalker::TryRegisterWithOwnerPlayerState,
				PlayerStateRetryIntervalSeconds,
				true);
		}
	}
}

void UMD_CharacterVOIPTalker::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 발화 종료 콜백 없이 컴포넌트가 제거된 경우의 알림 정리
	StopTalkNotification();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PlayerStateRetryTimerHandle);
		// 컴포넌트 제거 이후 로컬 송신 상태 확인 중단
		World->GetTimerManager().ClearTimer(LocalVoiceStateTimer);
	}

	Super::EndPlay(EndPlayReason);
}

void UMD_CharacterVOIPTalker::TryRegisterWithOwnerPlayerState()
{
	// 이미 완료된 PlayerState 등록의 중복 처리 방지
	if (bVoiceRegistrationComplete == true)
	{
		return;
	}

	// 컴포넌트 소유 Pawn 준비 여부 확인
	APawn* OwningPawn = Cast<APawn>(GetOwner());
	if (IsValid(OwningPawn) == false)
	{
		return;
	}

	// 복제된 PlayerState 준비 여부 확인
	APlayerState* OwningState = OwningPawn->GetPlayerState();
	if (IsValid(OwningState) == false)
	{
		return;
	}

	// VOIP 등록에 필요한 플레이어 고유 식별자의 복제 완료 대기
	FUniqueNetIdRepl OwningPlayerUniqueId = OwningState->GetUniqueId();
	if (OwningPlayerUniqueId.IsValid() == false)
	{
		return;
	}

	// 고유 식별자가 준비된 PlayerState와 VOIP Talker 연결
	RegisterWithPlayerState(OwningState);
	// 프로젝트 공통 마이크 감도 값
	float VoiceMicThreshold = 0.003f;
	// 프로젝트 공통 마이크 감도 적용
	UVOIPStatics::SetMicThreshold(VoiceMicThreshold);

	// Pawn 재생성 전 PlayerState에 저장된 로컬 음량 조회
	AMD_PlayerState* ProjectPlayerState = Cast<AMD_PlayerState>(OwningState);
	if (IsValid(ProjectPlayerState) == true)
	{
		float StoredVolumeMultiplier = ProjectPlayerState->GetVoiceVolumeMultiplier();
		SetVolumeMuliplier(StoredVolumeMultiplier);
	}

	// PlayerState와 고유 식별자를 확인한 등록 완료 상태 저장
	bVoiceRegistrationComplete = true;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PlayerStateRetryTimerHandle);
	}
}

void UMD_CharacterVOIPTalker::OnTalkingBegin(UAudioComponent* AudioComponent)
{
	Super::OnTalkingBegin(AudioComponent);

	ActiveVoiceAudioComponent = AudioComponent;

	if (IsValid(ActiveVoiceAudioComponent))
	{
		ActiveVoiceAudioComponent->SetVolumeMultiplier(VolumeMultiplier);
	}

	// 수신 음성의 재생 시작을 HUD 알림에 반영
	StartTalkNotification();
}

void UMD_CharacterVOIPTalker::OnTalkingEnd()
{
	// 생성된 알림 검사 후 제거 및 반복 검색 종료
	StopTalkNotification();

	ActiveVoiceAudioComponent = nullptr;

	Super::OnTalkingEnd();
}

void UMD_CharacterVOIPTalker::SetVolumeMuliplier(float NewVolumeMultiplier)
{
	// UI 정규화 음량과 공통으로 사용하는 허용 배율 적용
	float ClampedVolumeMultiplier = FMath::Clamp(NewVolumeMultiplier, 0.0f, MaxVolumeMultiplier);
	VolumeMultiplier = ClampedVolumeMultiplier;

	if (IsValid(ActiveVoiceAudioComponent) == true)
	{
		ActiveVoiceAudioComponent->SetVolumeMultiplier(VolumeMultiplier);
	}
}

void UMD_CharacterVOIPTalker::UpdateLocalVoiceState()
{
	// 현재 소유 Pawn을 기준으로 로컬 플레이어 여부 확인
	APawn* OwningPawn = Cast<APawn>(GetOwner());
	if (IsValid(OwningPawn) == false)
	{
		return;
	}
	// 원격 플레이어는 기존 음성 재생 콜백으로 표시 유지
	bool bLocalPawn = OwningPawn->IsLocallyControlled();
	if (bLocalPawn == false)
	{
		return;
	}
	// 로컬 PlayerController에 부착된 송신 컴포넌트 조회
	APlayerController* PlayerController = Cast<APlayerController>(OwningPawn->GetController());
	if (IsValid(PlayerController) == false)
	{
		return;
	}
	// 컨트롤러의 로컬 음성 송신 컴포넌트 조회
	UMD_VoiceChatComponent* VoiceChat = PlayerController->FindComponentByClass<UMD_VoiceChatComponent>();
	// 송신 컴포넌트가 없거나 비활성인 경우 기본 숨김 상태 적용
	bool bLocalVoiceActive = false;
	if (IsValid(VoiceChat) == true)
	{
		// 실제 로컬 마이크 활성 플래그 조회
		bLocalVoiceActive = VoiceChat->IsVoiceActivated();
	}
	// 변경되지 않은 상태의 반복 생성과 제거 방지
	if (bLocalVoiceActive == bIsTalking)
	{
		return;
	}
	// 자기 자신의 마이크 활성 상태에 따른 표시 시작
	if (bLocalVoiceActive == true)
	{
		StartTalkNotification();
		return;
	}
	// 로컬 송신이 끝난 경우 자신의 알림 제거
	StopTalkNotification();
}

void UMD_CharacterVOIPTalker::StartTalkNotification()
{
	// 진행 중인 발화의 중복 시작 방지
	if (bIsTalking == true)
	{
		return;
	}
	// 로컬 송신과 원격 재생에 공통으로 사용하는 발화 상태 저장
	bIsTalking = true;
	// HUD 준비 지연과 알림 무효화에 대응하는 검색 주기 등록
	UWorld* VoiceWorld = GetWorld();
	if (IsValid(VoiceWorld) == true)
	{
		// 런타임 설정의 잘못된 검색 간격 보정
		float RetryInterval = FMath::Max(TalkNotificationRetryInterval, 0.05f);
		VoiceWorld->GetTimerManager().SetTimer(TalkNotificationRetryTimer, this, &UMD_CharacterVOIPTalker::TryCreateTalkNotification, RetryInterval, true);
	}
	// 발화 시작 직후 최초 HUD 검색과 알림 생성 시도
	TryCreateTalkNotification();
}

void UMD_CharacterVOIPTalker::TryCreateTalkNotification()
{
	// 발화가 끝난 뒤 예약된 콜백의 생성 방지
	if (bIsTalking == false)
	{
		return;
	}
	// 생성된 알림이 유지되는 동안 HUD 검색 중단
	if (TalkNotificationWidget.IsValid() == true)
	{
		// GC 이전에 HUD 목록에서 분리된 알림도 재생성 대상으로 판정
		UPanelWidget* ParentPanel = TalkNotificationWidget->GetParent();
		if (IsValid(ParentPanel) == true)
		{
			return;
		}
	}
	// 이전 알림의 약한 참조 초기화
	TalkNotificationWidget.Reset();
	// 발화자와 현재 월드의 준비 여부를 함께 확인
	APawn* OwningPawn = Cast<APawn>(GetOwner());
	UWorld* VoiceWorld = GetWorld();
	if (IsValid(OwningPawn) == false || IsValid(VoiceWorld) == false)
	{
		return;
	}
	// 유효한 Pawn과 월드에서 발화자 상태와 GameInstance 조회
	APlayerState* TalkingPlayerState = OwningPawn->GetPlayerState();
	UGameInstance* GameInstance = VoiceWorld->GetGameInstance();
	if (IsValid(TalkingPlayerState) == false || IsValid(GameInstance) == false)
	{
		return;
	}
	// 컨트롤러 검색 없이 첫 로컬 플레이어 조회
	ULocalPlayer* LocalPlayer = GameInstance->GetFirstGamePlayer();
	// 로컬 플레이어가 유효한 경우에만 조회할 UI 관리자 저장 공간
	UMD_LocalUIManager* UIManager = nullptr;
	// 단락 평가로 무효한 로컬 플레이어 접근을 방지하면서 두 유효성 검사 병합
	if (IsValid(LocalPlayer) == false || IsValid(UIManager = LocalPlayer->GetSubsystem<UMD_LocalUIManager>()) == false)
	{
		return;
	}
	// GameHUD 레이어의 활성 위젯을 HUD 클래스로 변환
	UCommonActivatableWidget* ActiveScreen = UIManager->GetActiveScreen(MDGameplayTags::UI_Layer_GameHUD);
	UMD_HUDWidget* HUD = Cast<UMD_HUDWidget>(ActiveScreen);
	if (IsValid(HUD) == false)
	{
		return;
	}
	// HUD에 알림 위젯 생성과 목록 추가 요청
	UMD_TalkNotificationWidget* Notification = HUD->CreateTalkNotification();
	if (IsValid(Notification) == false)
	{
		return;
	}
	// 대리 생성 중 BP 콜백에서 발화가 종료된 경우 반환된 알림 정리
	if (bIsTalking == false)
	{
		Notification->RemoveFromParent();
		return;
	}
	// 텍스트 설정 중 종료되더라도 정리할 수 있도록 약한 참조 보관
	TalkNotificationWidget = Notification;
	// 발화자의 현재 이름을 알림에 전달할 텍스트로 변환
	FString PlayerName = TalkingPlayerState->GetPlayerName();
	FText PlayerNameText = FText::FromString(PlayerName);
	// 알림 Blueprint에 표시할 이름 전달
	Notification->SetText(PlayerNameText);
}

void UMD_CharacterVOIPTalker::StopTalkNotification()
{
	// 발화 종료 후 재검색 차단
	bIsTalking = false;
	// 반복 검색 타이머 해제
	UWorld* VoiceWorld = GetWorld();
	if (IsValid(VoiceWorld) == true)
	{
		VoiceWorld->GetTimerManager().ClearTimer(TalkNotificationRetryTimer);
	}
	// 제거 콜백 재진입에 대비한 기존 알림 회수와 저장 참조 초기화
	TWeakObjectPtr<UMD_TalkNotificationWidget> PreviousNotification = TalkNotificationWidget;
	TalkNotificationWidget.Reset();
	// 생성된 알림이 유효한 경우 HUD 목록에서 제거
	if (PreviousNotification.IsValid() == true)
	{
		PreviousNotification->RemoveFromParent();
	}
}
