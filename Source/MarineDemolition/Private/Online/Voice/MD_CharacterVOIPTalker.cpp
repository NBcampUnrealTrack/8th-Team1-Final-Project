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
#include "Net/OnlineEngineInterface.h"
#include "Net/VoiceDataCommon.h"
#include "Util/MD_LogUtility.h"

UMD_CharacterVOIPTalker::UMD_CharacterVOIPTalker(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 프레임 끝에 비워지는 음성 패킷을 관찰하기 위한 Tick 활성화
	PrimaryComponentTick.bCanEverTick = true;
	// 플레이 시작부터 패킷 관찰 실행
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// 네트워크 전송 및 패킷 정리 이전에 크기 확인
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UMD_CharacterVOIPTalker::BeginPlay()
{
	Super::BeginPlay();

	TryRegisterWithOwnerPlayerState();
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
	}

	Super::EndPlay(EndPlayReason);
}

void UMD_CharacterVOIPTalker::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// 입력 및 송신 상태를 변경하지 않고 로컬 송신 대기 패킷 확인
	UpdateLocalVoiceState();
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
	// 패킷 감지 유지 시간을 계산할 월드 참조
	UWorld* VoiceWorld = GetWorld();
	if (IsValid(OwningPawn) == false || IsValid(VoiceWorld) == false)
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
		// 송신을 중지한 경우 이전 패킷의 유지 시간도 적용하지 않도록 확인
		bool bMicrophoneActive = VoiceChat->IsVoiceActivated();
		if (bMicrophoneActive == true)
		{
			// 엔진이 인코딩하여 송신 대기 중인 음성 데이터 크기 조회
			uint16 AvailableBytes = GetLocalVoicePacketBytes(PlayerController);
			// 마지막 패킷 이후 경과 시간 판정을 위한 월드 시각 조회
			double CurrentTime = VoiceWorld->GetTimeSeconds();
			if (AvailableBytes > 1)
			{
				// 실제 패킷 감지 시 표시 시작 및 패킷 사이의 짧은 유지 시간 갱신
				LocalVoicePacketDisplayUntil = CurrentTime + FMath::Max(LocalVoicePacketHoldSeconds, 0.0f);
				bLocalVoiceActive = true;
				if (bIsTalking == false)
				{
					// UI 표시를 시작한 패킷 크기 기록
					UE_LOG(MarineDemolition, Log, TEXT("[VoiceHUD] Local packet detected: AvailableBytes=%u"), static_cast<uint32>(AvailableBytes));
				}
			}
			else
			{
				// 패킷 간 공백으로 알림이 매 프레임 제거되는 현상 방지
				bLocalVoiceActive = CurrentTime < LocalVoicePacketDisplayUntil;
			}
		}
	}
	// 마이크 중지 또는 유지 시간 만료 이후 이전 패킷 상태 제거
	if (bLocalVoiceActive == false)
	{
		LocalVoicePacketDisplayUntil = 0.0;
	}
	// 변경되지 않은 상태의 반복 생성과 제거 방지
	if (bLocalVoiceActive == bIsTalking)
	{
		return;
	}
	// 1바이트를 초과하는 음성 패킷 감지 이후 알림 표시
	if (bLocalVoiceActive == true)
	{
		StartTalkNotification();
		return;
	}
	// 유효 패킷이 일정 시간 없거나 마이크가 꺼진 경우 알림 제거
	StopTalkNotification();
}

uint16 UMD_CharacterVOIPTalker::GetLocalVoicePacketBytes(APlayerController* PlayerController)
{
	// 패킷 조회가 불가능한 경우 반환할 기본 크기
	uint16 AvailableBytes = 0;
	// 소유 로컬 사용자 및 현재 월드 조회
	ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
	UWorld* VoiceWorld = GetWorld();
	if (IsValid(LocalPlayer) == false || IsValid(VoiceWorld) == false)
	{
		return AvailableBytes;
	}
	// 엔진 송신 사용자에 대응하는 컨트롤러 식별자 확인
	int32 LocalUserNum = LocalPlayer->GetControllerId();
	if (LocalUserNum < 0 || LocalUserNum > MAX_uint8)
	{
		return AvailableBytes;
	}
	// 네트워크 드라이버와 동일한 음성 패킷 접근 경로 사용
	UOnlineEngineInterface* OnlineEngine = UOnlineEngineInterface::Get();
	if (IsValid(OnlineEngine) == false)
	{
		return AvailableBytes;
	}
	// 송신 원본을 소비하지 않는 참조 카운트 복사본 조회
	TSharedPtr<FVoicePacket> VoicePacket = OnlineEngine->GetLocalPacket(VoiceWorld, static_cast<uint8>(LocalUserNum));
	if (VoicePacket.IsValid() == true)
	{
		// 네트워크 헤더를 제외한 인코딩 음성 버퍼 크기 조회
		AvailableBytes = VoicePacket->GetBufferSize();
	}
	// 실제 송신 대기 음성 데이터 크기 반환
	return AvailableBytes;
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
