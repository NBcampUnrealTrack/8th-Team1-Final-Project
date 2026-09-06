#include "UI/Screen/MD_PlayerInfoWidget.h"

#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Online/Voice/MD_CharacterVOIPTalker.h"
#include "Player/MD_PlayerController.h"
#include "Player/MD_PlayerState.h"

void UMD_PlayerInfoWidget::NativePreConstruct()
{
	Super::NativePreConstruct();

	// 에디터 미리보기와 런타임 생성 전 저장된 플레이어 이름 반영
	ApplyPlayerName();
	// 에디터 미리보기와 런타임 생성 전 저장된 볼륨 표시 반영
	UpdateVolumeIndicator();
}

void UMD_PlayerInfoWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 현재 저장된 플레이어 이름의 본문과 그림자 동시 반영
	ApplyPlayerName();

	// 강퇴 버튼 클릭 시 현재 카드 대상의 서버 강퇴 요청 연결
	if (IsValid(Btn_Kick) == true)
	{
		Btn_Kick->OnClicked.AddUniqueDynamic(this, &UMD_PlayerInfoWidget::KickAssignedPlayer);
	}

	// Blueprint의 전역 Material Parameter 변경 이벤트 제거와 C++ 개별 처리 준비
	if (IsValid(Sd_Volume) == true)
	{
		Sd_Volume->OnValueChanged.Clear();
		InitializeVolumeBarMaterial();
		Sd_Volume->SetValue(Volume);
		UpdateVolumeBarMaterial();
		Sd_Volume->OnValueChanged.AddUniqueDynamic(this, &UMD_PlayerInfoWidget::HandleVolumeChanged);
	}

	// 현재 볼륨에 맞춘 음성 단계와 음소거 아이콘 동기화
	UpdateVolumeIndicator();
}

void UMD_PlayerInfoWidget::NativeDestruct()
{
	// 제거된 위젯의 강퇴 버튼 클릭 이벤트 연결 해제
	if (IsValid(Btn_Kick) == true)
	{
		Btn_Kick->OnClicked.RemoveDynamic(this, &UMD_PlayerInfoWidget::KickAssignedPlayer);
	}

	// 제거된 위젯의 Slider 값 변경 이벤트 연결 해제
	if (IsValid(Sd_Volume) == true)
	{
		Sd_Volume->OnValueChanged.RemoveDynamic(this, &UMD_PlayerInfoWidget::HandleVolumeChanged);
	}

	Super::NativeDestruct();
}

void UMD_PlayerInfoWidget::SetPlayerState(APlayerState* InPlayerState)
{
	// 플레이어 카드가 표시하고 제어할 대상 저장
	AssignedPlayerState = InPlayerState;

	// 유효하지 않은 대상 할당 시 기존 플레이어 이름 제거
	if (IsValid(AssignedPlayerState) == false)
	{
		// 유효한 플레이어 정보가 없는 카드와 목록 공간 숨김
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	// 유효한 플레이어가 다시 할당된 카드 표시
	SetVisibility(ESlateVisibility::Visible);

	// 할당된 PlayerState의 복제 이름 조회
	FString AssignedPlayerName = AssignedPlayerState->GetPlayerName();
	// 플레이어 카드 표시를 위한 이름 텍스트 변환
	FText AssignedPlayerNameText = FText::FromString(AssignedPlayerName);
	SetPlayerName(AssignedPlayerNameText);

	// 동일 PlayerState의 이전 Pawn에서 저장된 플레이어별 음량 조회
	AMD_PlayerState* ProjectPlayerState = Cast<AMD_PlayerState>(AssignedPlayerState);
	if (IsValid(ProjectPlayerState) == true)
	{
		float StoredVolumeMultiplier = ProjectPlayerState->GetVoiceVolumeMultiplier();
		float StoredNormalizedVolume = StoredVolumeMultiplier / UMD_CharacterVOIPTalker::MaxVolumeMultiplier;
		SetVolume(StoredNormalizedVolume);
		return;
	}

	// Talker에 저장된 현재 플레이어별 음량 조회
	UMD_CharacterVOIPTalker* VoiceTalker = GetAssignedVoiceTalker();
	if (IsValid(VoiceTalker) == false)
	{
		// Talker 준비 전 카드에 새 접속 기본 음량 적용
		float DefaultNormalizedVolume = UMD_CharacterVOIPTalker::DefaultVolumeMultiplier / UMD_CharacterVOIPTalker::MaxVolumeMultiplier;
		SetVolume(DefaultNormalizedVolume);
		return;
	}

	// Talker 재생 배율의 Slider 정규화 값 변환
	float CurrentVolumeMultiplier = VoiceTalker->GetVolumeMultiplier();
	float NormalizedVolume = CurrentVolumeMultiplier / UMD_CharacterVOIPTalker::MaxVolumeMultiplier;
	SetVolume(NormalizedVolume);
}

void UMD_PlayerInfoWidget::SetPlayerName(FText InPlayerName)
{
	// 새 플레이어 이름의 이후 위젯 재생성용 저장
	PlayerName = InPlayerName;
	// 변경된 플레이어 이름의 본문과 그림자 동시 반영
	ApplyPlayerName();
}

void UMD_PlayerInfoWidget::KickAssignedPlayer()
{
	// 강퇴할 카드 대상이 없는 경우 요청 생략
	if (IsValid(AssignedPlayerState) == false)
	{
		return;
	}

	// 강퇴 요청을 서버로 전달할 프로젝트 PlayerController 조회
	AMD_PlayerController* OwningPlayerController = Cast<AMD_PlayerController>(GetOwningPlayer());
	if (IsValid(OwningPlayerController) == false)
	{
		return;
	}

	// 서버 권한 검증에 사용할 카드 대상 PlayerState 전달
	OwningPlayerController->ServerRequestKickPlayer(AssignedPlayerState);
}

void UMD_PlayerInfoWidget::SetVolume(float InVolume)
{
	// Slider 범위에 맞춘 새 볼륨 값 보정
	float ClampedVolume = FMath::Clamp(InVolume, 0.0f, 1.0f);
	Volume = ClampedVolume;

	// 바인딩된 Slider에 보정된 볼륨 값 반영
	if (IsValid(Sd_Volume) == true)
	{
		Sd_Volume->SetValue(Volume);
	}

	// 카드 전용 Slider 바의 진행도 동기화
	UpdateVolumeBarMaterial();
	// 음성 크기 단계와 음소거 아이콘 동기화
	UpdateVolumeIndicator();

	// 할당된 플레이어의 실제 음성 재생 배율 갱신
	ApplyAssignedVoiceVolume();
}

void UMD_PlayerInfoWidget::ApplyPlayerName()
{
	// 플레이어 이름 본문에 저장된 이름 적용
	if (IsValid(Txt_PlayerName) == true)
	{
		Txt_PlayerName->SetText(PlayerName);
	}

	// 플레이어 이름 그림자에 본문과 같은 이름 적용
	if (IsValid(Txt_PlayerName_Shadow) == true)
	{
		Txt_PlayerName_Shadow->SetText(PlayerName);
	}
}

void UMD_PlayerInfoWidget::ApplyAssignedVoiceVolume()
{
	// Slider 정규화 값을 실제 음성 재생 배율로 변환
	float VolumeMultiplier = Volume * UMD_CharacterVOIPTalker::MaxVolumeMultiplier;

	// Pawn 교체 이후 복원을 위한 현재 PlayerState의 로컬 음량 저장
	AMD_PlayerState* ProjectPlayerState = Cast<AMD_PlayerState>(AssignedPlayerState);
	if (IsValid(ProjectPlayerState) == true)
	{
		ProjectPlayerState->SetVoiceVolumeMultiplier(VolumeMultiplier);
	}

	// 현재 할당된 플레이어의 프로젝트 음성 Talker 조회
	UMD_CharacterVOIPTalker* VoiceTalker = GetAssignedVoiceTalker();
	if (IsValid(VoiceTalker) == false)
	{
		return;
	}

	// 현재 또는 다음 음성 재생에 저장된 배율 적용
	VoiceTalker->SetVolumeMuliplier(VolumeMultiplier);
}

void UMD_PlayerInfoWidget::InitializeVolumeBarMaterial()
{
	// 재활성화된 카드가 이미 보유한 동적 Material 재사용
	if (IsValid(VolumeBarMaterialInstance) == true)
	{
		return;
	}

	// Slider가 없는 Blueprint 변형의 Material 생성 생략
	if (IsValid(Sd_Volume) == false)
	{
		return;
	}

	// 현재 Slider Style의 기본 바 Material 조회
	FSliderStyle SliderStyle = Sd_Volume->GetWidgetStyle();
	UObject* BarResource = SliderStyle.NormalBarImage.GetResourceObject();
	UMaterialInterface* BarMaterial = Cast<UMaterialInterface>(BarResource);
	if (IsValid(BarMaterial) == false)
	{
		return;
	}

	// 다른 플레이어 카드와 Parameter를 공유하지 않는 동적 Material 생성
	UMaterialInstanceDynamic* DynamicBarMaterial = UMaterialInstanceDynamic::Create(BarMaterial, this);
	if (IsValid(DynamicBarMaterial) == false)
	{
		return;
	}

	// 모든 Slider 상태의 바 Brush에 같은 카드 전용 Material 적용
	VolumeBarMaterialInstance = DynamicBarMaterial;
	SliderStyle.NormalBarImage.SetResourceObject(VolumeBarMaterialInstance);
	SliderStyle.HoveredBarImage.SetResourceObject(VolumeBarMaterialInstance);
	SliderStyle.DisabledBarImage.SetResourceObject(VolumeBarMaterialInstance);
	Sd_Volume->SetWidgetStyle(SliderStyle);
}

void UMD_PlayerInfoWidget::UpdateVolumeBarMaterial()
{
	// 동적 Material 생성 전 호출의 진행도 갱신 생략
	if (IsValid(VolumeBarMaterialInstance) == false)
	{
		return;
	}

	// Slider Material의 정규화 진행도 Parameter 이름
	FName VolumeBarValueParameterName(TEXT("Value"));
	// 현재 카드의 정규화 볼륨을 바 진행도에 적용
	VolumeBarMaterialInstance->SetScalarParameterValue(VolumeBarValueParameterName, Volume);
}

void UMD_PlayerInfoWidget::UpdateVolumeIndicator()
{
	// 음량 표시 구성 요소가 없는 Blueprint 변형의 갱신 생략
	if (IsValid(Img_Icon_Mute) == false || IsValid(HB_VoumeIndicator) == false)
	{
		return;
	}

	// 음소거 상태의 아이콘 전환과 자식 표시 갱신 생략
	if (Volume <= 0.0f)
	{
		Img_Icon_Mute->SetVisibility(ESlateVisibility::Visible);
		HB_VoumeIndicator->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	// 음량이 있는 상태의 아이콘 전환
	Img_Icon_Mute->SetVisibility(ESlateVisibility::Collapsed);
	HB_VoumeIndicator->SetVisibility(ESlateVisibility::Visible);

	// 0보다 큰 음량의 기본 표시 단계 수
	int32 VisibleVolumeLineCount = 1;
	if (Volume >= 0.5f)
	{
		// 높은 음량의 표시 단계 수 확장
		VisibleVolumeLineCount = 3;
	}
	else if (Volume >= 0.25f)
	{
		// 중간 이상 음량의 표시 단계 수 확장
		VisibleVolumeLineCount = 2;
	}

	// HorizontalBox의 자식 순서에 따른 음성 크기 단계 표시
	int32 VolumeLineCount = HB_VoumeIndicator->GetChildrenCount();
	for (int32 VolumeLineIndex = 0; VolumeLineIndex < VolumeLineCount; ++VolumeLineIndex)
	{
		// 계산된 단계 수에 따른 현재 자식의 표시 상태
		ESlateVisibility VolumeLineVisibility =
			VolumeLineIndex < VisibleVolumeLineCount ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
		// 개별 변수 바인딩 없이 HorizontalBox 자식의 표시 상태 갱신
		UWidget* VolumeLineWidget = HB_VoumeIndicator->GetChildAt(VolumeLineIndex);
		if (IsValid(VolumeLineWidget) == true)
		{
			VolumeLineWidget->SetVisibility(VolumeLineVisibility);
		}
	}
}

UMD_CharacterVOIPTalker* UMD_PlayerInfoWidget::GetAssignedVoiceTalker()
{
	UMD_CharacterVOIPTalker* AssignedVoiceTalker = nullptr;

	// PlayerState가 없는 카드의 Talker 조회 생략
	if (IsValid(AssignedPlayerState) == false)
	{
		return AssignedVoiceTalker;
	}

	// 아직 등록 전인 Talker도 찾을 수 있도록 현재 Pawn 컴포넌트 우선 조회
	APawn* AssignedPlayerPawn = AssignedPlayerState->GetPawn();
	if (IsValid(AssignedPlayerPawn) == true)
	{
		AssignedVoiceTalker = AssignedPlayerPawn->FindComponentByClass<UMD_CharacterVOIPTalker>();
	}

	// Pawn에서 찾은 Talker 즉시 반환
	if (IsValid(AssignedVoiceTalker) == true)
	{
		return AssignedVoiceTalker;
	}

	// Pawn 교체 구간의 등록 Talker 조회를 위한 플레이어 고유 식별자 저장
	FUniqueNetIdRepl AssignedPlayerUniqueId = AssignedPlayerState->GetUniqueId();
	if (AssignedPlayerUniqueId.IsValid() == false)
	{
		return AssignedVoiceTalker;
	}

	// 엔진 음성 Talker 등록부에서 해당 플레이어 컴포넌트 조회
	UVOIPTalker* RegisteredVoiceTalker = UVOIPStatics::GetVOIPTalkerForPlayer(AssignedPlayerUniqueId);
	AssignedVoiceTalker = Cast<UMD_CharacterVOIPTalker>(RegisteredVoiceTalker);

	return AssignedVoiceTalker;
}

void UMD_PlayerInfoWidget::HandleVolumeChanged(float InVolume)
{
	// Slider 값을 저장하고 할당된 플레이어의 실제 음성 재생에 적용
	SetVolume(InVolume);
	// 변경된 정규화 볼륨의 외부 구독자 전달
	OnVolumeChanged.Broadcast(Volume);
}
