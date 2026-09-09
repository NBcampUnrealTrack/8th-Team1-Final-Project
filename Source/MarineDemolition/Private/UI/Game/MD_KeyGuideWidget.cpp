#include "UI/Game/MD_KeyGuideWidget.h"

#include "Components/TextBlock.h"
#include "Gameplay/Settings/MD_AudioSettingCategory.h"
#include "Gameplay/Settings/MD_ControlSettingCategory.h"
#include "Gameplay/Settings/MD_SettingsManager.h"

void UMD_KeyGuideWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 현재 게임 인스턴스의 설정 매니저 조회
	UMD_SettingsManager* SettingsManager = UMD_SettingsManager::Get(this);
	// 실제 게임에서 사용 가능한 설정 매니저 확인
	if (IsValid(SettingsManager) == true)
	{
		// 구독 해제 대상 매니저 보관
		RegisteredSettingsManager = SettingsManager;
		// 재구성 시 중복 없는 설정 적용 구독
		SettingsManager->OnSettingsApplied.AddUniqueDynamic(this, &ThisClass::HandleSettingsApplied);
	}

	// 재구성된 위젯의 이전 입력 유지 상태 초기화
	bVoiceChatKeyPressed = false;
	// 저장된 설정 또는 프로젝트 기본 키의 최초 표시
	RefreshVoiceChatKey();
	// 처음 표시할 때부터 적용된 음성 입력 모드 반영
	RefreshVoiceChatMode();
}

void UMD_KeyGuideWidget::NativeDestruct()
{
	// 실제 이벤트를 구독한 매니저 조회
	UMD_SettingsManager* SettingsManager = RegisteredSettingsManager.Get();
	// 종료 중인 매니저에 대한 접근 방지
	if (IsValid(SettingsManager) == true)
	{
		// 제거된 HUD의 설정 갱신 구독 해제
		SettingsManager->OnSettingsApplied.RemoveDynamic(this, &ThisClass::HandleSettingsApplied);
	}
	// 종료된 위젯의 구독 대상 초기화
	RegisteredSettingsManager.Reset();

	Super::NativeDestruct();
}

void UMD_KeyGuideWidget::RefreshVoiceChatKey()
{
	// 매니저가 없는 환경에서 사용할 프로젝트 기본 조작 설정
	FMD_ControlSettings AppliedSettings;
	// 현재 안내가 구독 중인 설정 매니저 조회
	UMD_SettingsManager* SettingsManager = RegisteredSettingsManager.Get();
	// 실제 적용값을 제공하는 매니저 확인
	if (IsValid(SettingsManager) == true)
	{
		// 음성 채팅 키를 관리하는 조작 카테고리 조회
		UMD_ControlSettingCategory* ControlSettings = SettingsManager->GetControlSettings();
		// 초기화된 조작 설정에 한정한 적용값 조회
		if (IsValid(ControlSettings) == true)
		{
			// 미적용 편집값을 제외한 실제 입력 설정 사용
			AppliedSettings = ControlSettings->GetAppliedSettings();
		}
	}

	// 키 안내에 사용할 짧은 입력 키 표시명 생성
	FText KeyText = AppliedSettings.VoiceChatKey.GetDisplayName(false);
	// 키 그림자 텍스트의 유효성 확인
	if (IsValid(TextBlock) == true)
	{
		// 그림자에 현재 음성 채팅 키 표시
		TextBlock->SetText(KeyText);
	}
	// 키 본문 텍스트의 유효성 확인
	if (IsValid(TextBlock_153) == true)
	{
		// 본문에 그림자와 동일한 음성 채팅 키 표시
		TextBlock_153->SetText(KeyText);
	}
}

void UMD_KeyGuideWidget::RefreshVoiceChatMode()
{
	// 설정 매니저가 없는 환경의 기본 음성 입력 모드 준비
	FMD_AudioUserSettings DefaultSettings;
	// 이전 위젯 상태와 분리된 기본 Push to Talk 사용 여부 초기화
	bUsePushToTalk = DefaultSettings.bUsePushToTalk;
	// 적용 이벤트를 구독 중인 설정 매니저 조회
	UMD_SettingsManager* SettingsManager = RegisteredSettingsManager.Get();
	// 현재 음성 입력 설정을 제공하는 매니저 확인
	if (IsValid(SettingsManager) == true)
	{
		// 음성 입력 모드를 관리하는 소리 카테고리 조회
		UMD_AudioSettingCategory* AudioSettings = SettingsManager->GetAudioSettings();
		// 초기화된 소리 설정에 한정한 모드 조회
		if (IsValid(AudioSettings) == true)
		{
			// 최초 표시 또는 소리 설정 적용 시점의 모드 보관
			bUsePushToTalk = AudioSettings->IsPushToTalkEnabled();
		}
	}

	// 변경된 음성 입력 모드와 현재 키 유지 상태 동시 반영
	RefreshGuideVisibility();
}

void UMD_KeyGuideWidget::RefreshGuideVisibility()
{
	// Push to Talk 외 모드와 입력 유지 구간의 기본 숨김
	ESlateVisibility GuideVisibility = ESlateVisibility::Collapsed;
	// 키 안내가 필요한 음성 입력 모드 확인
	if (bUsePushToTalk == true)
	{
		// 말하기 키를 놓은 상태에서만 안내 표시
		if (bVoiceChatKeyPressed == false)
		{
			// 기존 안내의 표시 및 마우스 통과 상태 복원
			GuideVisibility = ESlateVisibility::SelfHitTestInvisible;
		}
	}

	// 키와 설명 및 마이크 아이콘의 표시 상태 동시 적용
	SetVisibility(GuideVisibility);
}

void UMD_KeyGuideWidget::SetVoiceChatKeyPressed(bool bPressed)
{
	// 모드 전환 중에도 유지할 음성 입력 상태 기록
	bVoiceChatKeyPressed = bPressed;
	// 키 해제 시 Push to Talk 외 모드의 숨김 상태 유지
	RefreshGuideVisibility();
}

void UMD_KeyGuideWidget::HandleSettingsApplied(EMD_SettingsCategory Category)
{
	// 조작 설정 적용 시 키 표시 갱신
	if (Category == EMD_SettingsCategory::Controls)
	{
		// 적용된 새 음성 채팅 키로 HUD 안내 갱신
		RefreshVoiceChatKey();
	}

	// 소리 설정 적용 시 음성 입력 모드 갱신
	if (Category == EMD_SettingsCategory::Audio)
	{
		// Push to Talk 여부에 따른 안내 표시 갱신
		RefreshVoiceChatMode();
	}
}
