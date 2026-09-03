#include "UI/Screen/Settings/MD_SettingsAudioWidget.h"

#include "Async/Async.h"
#include "Components/ComboBoxString.h"
#include "Components/Slider.h"
#include "Gameplay/Settings/MD_AudioSettingCategory.h"
#include "Gameplay/Settings/MD_SettingsManager.h"
#include "UI/Foundation/MD_CarouselWidget.h"

void UMD_SettingsAudioWidget::NativeConstruct()
{
	// 다시 열린 화면의 장치 목록 응답 허용
	bAcceptDeviceResults = true;
	// Blueprint 생성 이벤트 실행 전 현재 카테고리 값 조회
	bool bLoaded = ReloadSettings();
	// 게임 인스턴스가 없는 상태의 기본 표시 준비
	if (bLoaded == false)
	{
		// 실제 적용 없는 기본값 표시
		RefreshDisplay();
	}
	// 마이크 캡처나 출력 전환 없는 장치 목록 조회
	RefreshDeviceOptions();
	Super::NativeConstruct();
}

void UMD_SettingsAudioWidget::NativeDestruct()
{
	// 종료 이후 도착한 비동기 응답의 컨트롤 접근 차단
	bAcceptDeviceResults = false;
	Super::NativeDestruct();
}

bool UMD_SettingsAudioWidget::ReloadSettings()
{
	// 현재 화면의 게임 인스턴스에 속한 설정 매니저 조회
	UMD_SettingsManager* SettingsManager = UMD_SettingsManager::Get(this);
	// 미리보기 등 매니저가 없는 상태의 안전한 실패
	if (IsValid(SettingsManager) == false)
	{
		// 카테고리 부재의 로딩 실패값 준비
		bool bLoaded = false;
		// 기존 편집값을 유지한 실패 반환
		return bLoaded;
	}
	// 다른 설정 탭과 독립적인 오디오 카테고리 조회
	UMD_AudioSettingCategory* AudioCategory = SettingsManager->GetAudioSettings();
	// 초기화되지 않은 카테고리 접근 방지
	if (IsValid(AudioCategory) == false)
	{
		// 카테고리 조회 실패값 준비
		bool bLoaded = false;
		// 기존 편집값을 유지한 실패 반환
		return bLoaded;
	}
	// 미저장 화면 편집을 현재 카테고리 값으로 교체
	PendingSettings = AudioCategory->Settings;
	// 슬라이더와 선택 컨트롤의 기준값 복원
	RefreshDisplay();
	// 현재 카테고리 로딩 완료값 준비
	bool bLoaded = true;
	// 로딩 결과 반환
	return bLoaded;
}

bool UMD_SettingsAudioWidget::SaveSettings()
{
	// 현재 오디오 탭의 설정 매니저 조회
	UMD_SettingsManager* SettingsManager = UMD_SettingsManager::Get(this);
	// 게임 인스턴스 없는 화면의 적용과 저장 방지
	if (IsValid(SettingsManager) == false)
	{
		// 저장 실패값 준비
		bool bSaved = false;
		// 카테고리 변경 없는 실패 반환
		return bSaved;
	}
	// 저장 대상 오디오 카테고리 조회
	UMD_AudioSettingCategory* AudioCategory = SettingsManager->GetAudioSettings();
	// 준비되지 않은 카테고리의 저장 방지
	if (IsValid(AudioCategory) == false)
	{
		// 저장 실패값 준비
		bool bSaved = false;
		// 카테고리 변경 없는 실패 반환
		return bSaved;
	}
	// 적용 버튼 시점의 화면 선택값 수집
	FMD_AudioUserSettings SelectedSettings = GetPendingSettings();
	// 오디오 카테고리에 한정한 편집값 반영
	AudioCategory->Settings = SelectedSettings;
	// 기존 매니저의 오디오 전용 적용 및 저장 이벤트 실행
	bool bSaved = SettingsManager->SaveSettings(EMD_SettingsCategory::Audio);
	// 범위 보정이 반영된 카테고리 값 보관
	PendingSettings = AudioCategory->Settings;
	// 저장 시점의 적용값 표시
	RefreshDisplay();
	// 파일 저장 성공 여부 반환
	return bSaved;
}

void UMD_SettingsAudioWidget::ResetToDefaults()
{
	// 실제 카테고리와 분리된 기본값 준비
	PendingSettings = FMD_AudioUserSettings();
	// 적용 버튼 호출 전까지 화면에만 기본값 반영
	RefreshDisplay();
}

FMD_AudioUserSettings UMD_SettingsAudioWidget::GetPendingSettings()
{
	// 생략된 컨트롤의 기존 값 보존용 기준값 복사
	FMD_AudioUserSettings SelectedSettings = PendingSettings;
	// 전체 음량의 정규화 선택값 수집
	SelectedSettings.MasterVolume = GetSliderValue(Slider_MasterVolume, SelectedSettings.MasterVolume);
	// 현재 WBP의 BGM 이름과 음악 음량 필드 대응
	SelectedSettings.MusicVolume = GetSliderValue(Slider_BGMVolume, SelectedSettings.MusicVolume);
	// 현재 WBP의 VFX 이름과 효과음 필드 대응
	SelectedSettings.EffectsVolume = GetSliderValue(Slider_VFXVolume, SelectedSettings.EffectsVolume);
	// MicThreshold 이름의 컨트롤을 입력 감도 설정값으로 수집
	SelectedSettings.VoiceInputSensitivity = GetSliderValue(Slider_MicThreshold, SelectedSettings.VoiceInputSensitivity);
	// 출력 장치 표시 인덱스에 대응하는 실제 ID 수집
	SelectedSettings.OutputDeviceId = GetSelectedDeviceId(CB_AudioOutput, OutputDeviceIds, SelectedSettings.OutputDeviceId);
	// 입력 장치 표시 인덱스에 대응하는 실제 ID 수집
	SelectedSettings.InputDeviceId = GetSelectedDeviceId(CB_VoiceInput, InputDeviceIds, SelectedSettings.InputDeviceId);
	// 음성 모드 컨트롤이 있는 레이아웃의 선택 확인
	if (IsValid(Carousel_VoiceInputMode) == true)
	{
		// BP 디테일에서 지정한 Enum 타입 조회
		UEnum* OptionsEnum = Carousel_VoiceInputMode->GetOptionsEnum();
		// 그래픽 품질 등 다른 Enum의 음성 모드 오해석 방지
		UEnum* VoiceModeEnum = StaticEnum<EMD_VoiceInputMode>();
		// 음성 모드 전용 타입에 한정한 선택 해석
		if (OptionsEnum == VoiceModeEnum)
		{
			// 빈 목록의 임의 모드 변경 방지용 선택 위치 조회
			int32 SelectedIndex = Carousel_VoiceInputMode->GetSelectedIndex();
			// 실제 항목이 선택된 경우의 값 해석
			if (SelectedIndex != INDEX_NONE)
			{
				// 표시 순서와 분리된 실제 Enum 값 조회
				int64 SelectedMode = Carousel_VoiceInputMode->GetSelectedEnumValue();
				// 키 입력 중 전송 모드의 설정값 반영
				if (SelectedMode == static_cast<int64>(EMD_VoiceInputMode::PushToTalk))
				{
					// 실제 송신 변경 없는 Push to Talk 선택 보관
					SelectedSettings.bUsePushToTalk = true;
				}
				// 열린 마이크 모드의 설정값 반영
				else if (SelectedMode == static_cast<int64>(EMD_VoiceInputMode::OpenMicrophone))
				{
					// 실제 마이크 활성화 없는 모드 선택 보관
					SelectedSettings.bUsePushToTalk = false;
				}
			}
		}
	}
	// 카테고리에 적용하지 않은 현재 화면 값 반환
	return SelectedSettings;
}

void UMD_SettingsAudioWidget::RefreshDeviceOptions()
{
	// 닫힌 화면의 불필요한 비동기 작업 방지
	if (bAcceptDeviceResults == false)
	{
		return;
	}
	// 유효한 월드 컨텍스트 확인
	UWorld* World = GetWorld();
	// 디자이너 등 월드가 없는 상태의 조회 방지
	if (IsValid(World) == false)
	{
		return;
	}
	// 출력 컨트롤이 있는 경우에 한정한 장치 조회
	if (IsValid(CB_AudioOutput) == true)
	{
		// 이전 출력 요청이 끝나기 전 중복 요청 방지
		if (bOutputDeviceRequestPending == false)
		{
			// 출력 목록 응답의 대기 상태 기록
			bOutputDeviceRequestPending = true;
			// 위젯 수명을 추적하는 동적 응답 델리게이트 준비
			FOnAudioOutputDevicesObtained OnDevicesObtained;
			// 출력 목록 수신 함수 연결
			OnDevicesObtained.BindDynamic(this, &UMD_SettingsAudioWidget::HandleOutputDevicesObtained);
			// 화면의 월드 컨텍스트를 통한 출력 장치 조회
			UAudioMixerBlueprintLibrary::GetAvailableAudioOutputDevices(this, OnDevicesObtained);
		}
	}
	// 입력 컨트롤이 있는 경우에 한정한 장치 조회
	if (IsValid(CB_VoiceInput) == true)
	{
		// 이전 입력 요청이 끝나기 전 중복 요청 방지
		if (bInputDeviceRequestPending == false)
		{
			// 입력 목록 응답의 대기 상태 기록
			bInputDeviceRequestPending = true;
			// 위젯 수명을 추적하는 동적 응답 델리게이트 준비
			FOnAudioInputDevicesObtained OnDevicesObtained;
			// 입력 목록 수신 함수 연결
			OnDevicesObtained.BindDynamic(this, &UMD_SettingsAudioWidget::HandleInputDevicesObtained);
			// 캡처 시작 없는 입력 장치 정보 조회
			UAudioCaptureBlueprintLibrary::GetAvailableAudioInputDevices(this, OnDevicesObtained);
		}
	}
}

FText UMD_SettingsAudioWidget::GetVoiceInputModeDisplayName(int32 NameIndex)
{
	// 잘못된 Enum 항목 조회에 사용할 빈 표시값 준비
	FText DisplayName = FText::GetEmpty();
	// 키 입력 기반 음성 모드의 런타임 표시 문구
	if (NameIndex == 0)
	{
		// 패키징 메타데이터와 독립적인 현지화 문구 사용
		DisplayName = NSLOCTEXT("MDSettingsAudio", "PushToTalk", "누르고 말하기");
	}
	// 열린 마이크 모드의 런타임 표시 문구
	else if (NameIndex == 1)
	{
		// 패키징 메타데이터와 독립적인 현지화 문구 사용
		DisplayName = NSLOCTEXT("MDSettingsAudio", "OpenMicrophone", "항상 켜기");
	}
	// 선택 모드에 대응하는 문구 반환
	return DisplayName;
}

void UMD_SettingsAudioWidget::HandleOutputDevicesObtained(const TArray<FAudioOutputDeviceInfo>& Devices)
{
	// 완료된 출력 목록 요청의 대기 상태 해제
	bOutputDeviceRequestPending = false;
	// 종료된 화면의 늦은 응답 무시
	if (bAcceptDeviceResults == false)
	{
		return;
	}
	// 요청 이후 사용자가 바꾼 출력 선택값 보관
	FString SelectedDeviceId = GetSelectedDeviceId(CB_AudioOutput, OutputDeviceIds, PendingSettings.OutputDeviceId);
	// 최신 출력 장치 정보 보관
	AvailableOutputDevices = Devices;
	// 다른 오디오 편집값을 건드리지 않는 출력 목록 갱신
	RefreshOutputDeviceOptions(SelectedDeviceId);
}

void UMD_SettingsAudioWidget::HandleInputDevicesObtained(const TArray<FAudioInputDeviceInfo>& Devices)
{
	// 테스트와 향후 엔진 변경에서 게임 스레드로 전달된 응답 확인
	bool bIsGameThread = IsInGameThread();
	// 이미 UI 접근이 가능한 스레드의 동기 처리 분기
	if (bIsGameThread == true)
	{
		// 추가 예약 없는 즉시 입력 장치 목록 갱신
		ProcessInputDevicesObtained(Devices);
		// 같은 응답의 중복 예약 방지
		return;
	}

	// 오디오 작업 스레드의 임시 장치 배열 수명과 분리된 복사본
	TArray<FAudioInputDeviceInfo> CopiedDevices = Devices;
	// 게임 스레드 실행 전 파괴될 수 있는 위젯의 약한 참조
	TWeakObjectPtr<UMD_SettingsAudioWidget> WeakWidget(this);
	// Slate 접근이 허용된 게임 스레드로 입력 장치 응답 전달
	AsyncTask(ENamedThreads::GameThread, [WeakWidget, CopiedDevices = MoveTemp(CopiedDevices)]() mutable
	{
		// 예약 작업 실행 시점의 위젯 인스턴스 조회
		UMD_SettingsAudioWidget* Widget = WeakWidget.Get();
		// 예약 이후 파괴된 위젯의 접근 차단
		if (IsValid(Widget) == false)
		{
			// 유효하지 않은 수신 대상의 지연 응답 폐기
			return;
		}

		// 유효한 화면에서만 입력 장치 목록 및 콤보박스 갱신
		Widget->ProcessInputDevicesObtained(MoveTemp(CopiedDevices));
	});
}

void UMD_SettingsAudioWidget::ProcessInputDevicesObtained(TArray<FAudioInputDeviceInfo> Devices)
{
	// 완료된 입력 목록 요청의 대기 상태 해제
	bInputDeviceRequestPending = false;
	// 종료된 화면의 늦은 응답 무시
	if (bAcceptDeviceResults == false)
	{
		return;
	}
	// 요청 이후 사용자가 바꾼 입력 선택값 보관
	FString SelectedDeviceId = GetSelectedDeviceId(CB_VoiceInput, InputDeviceIds, PendingSettings.InputDeviceId);
	// 최신 입력 장치 정보 보관
	AvailableInputDevices = MoveTemp(Devices);
	// 다른 오디오 편집값을 건드리지 않는 입력 목록 갱신
	RefreshInputDeviceOptions(SelectedDeviceId);
}

void UMD_SettingsAudioWidget::RefreshDisplay()
{
	// 디자이너 범위와 스타일을 유지한 전체 음량 표시
	RefreshSlider(Slider_MasterVolume, PendingSettings.MasterVolume);
	// 현재 음악 음량 표시
	RefreshSlider(Slider_BGMVolume, PendingSettings.MusicVolume);
	// 현재 효과음 음량 표시
	RefreshSlider(Slider_VFXVolume, PendingSettings.EffectsVolume);
	// 현재 입력 감도 표시
	RefreshSlider(Slider_MicThreshold, PendingSettings.VoiceInputSensitivity);
	// 기준 출력 장치 선택 표시
	RefreshOutputDeviceOptions(PendingSettings.OutputDeviceId);
	// 기준 입력 장치 선택 표시
	RefreshInputDeviceOptions(PendingSettings.InputDeviceId);
	// 음성 모드 컨트롤의 선택적 표시
	if (IsValid(Carousel_VoiceInputMode) == true)
	{
		// BP에서 지정한 타입 조회
		UEnum* OptionsEnum = Carousel_VoiceInputMode->GetOptionsEnum();
		// 음성 모드 표시 규약의 기준 타입 조회
		UEnum* VoiceModeEnum = StaticEnum<EMD_VoiceInputMode>();
		// BP의 타입을 교체하지 않는 음성 모드 선택 갱신
		if (OptionsEnum == VoiceModeEnum)
		{
			// 저장된 사용 여부에 대응하는 모드 결정
			EMD_VoiceInputMode Mode = PendingSettings.bUsePushToTalk ? EMD_VoiceInputMode::PushToTalk : EMD_VoiceInputMode::OpenMicrophone;
			// 선택 이벤트 없는 실제 Enum 값 동기화
			bool bSelected = Carousel_VoiceInputMode->SetSelectedEnumValue(static_cast<int64>(Mode));
			// 선택 결과의 명시적 소비
			(void)bSelected;
		}
	}
}

void UMD_SettingsAudioWidget::RefreshSlider(USlider* Slider, float Value)
{
	// 생략된 음량 컨트롤 허용
	if (IsValid(Slider) == false)
	{
		return;
	}
	// 디자이너에서 지정한 표시 범위의 최솟값 조회
	float MinValue = Slider->GetMinValue();
	// 디자이너에서 지정한 표시 범위의 최댓값 조회
	float MaxValue = Slider->GetMaxValue();
	// 비정상적인 표시 범위의 값 덮어쓰기 방지
	if (MaxValue <= MinValue)
	{
		return;
	}
	// 저장 데이터의 정규화 범위 보정
	float NormalizedValue = FMath::Clamp(Value, 0.0f, 1.0f);
	// 정규화 설정값을 현재 슬라이더 범위로 변환
	float DisplayValue = FMath::Lerp(MinValue, MaxValue, NormalizedValue);
	// 사용자 변경 이벤트 없는 선택값 표시
	Slider->SetValue(DisplayValue);
}

float UMD_SettingsAudioWidget::GetSliderValue(USlider* Slider, float PreviousValue)
{
	// 컨트롤 부재 시 보존할 기존 값 준비
	float Value = PreviousValue;
	// 존재하는 컨트롤에 한정한 선택값 수집
	if (IsValid(Slider) == true)
	{
		// 슬라이더 범위 유효성 확인용 최솟값 조회
		float MinValue = Slider->GetMinValue();
		// 슬라이더 범위 유효성 확인용 최댓값 조회
		float MaxValue = Slider->GetMaxValue();
		// 정규화 가능한 범위에 한정한 입력 수집
		if (MaxValue > MinValue)
		{
			// 0~100 등 디자이너 범위를 정규화한 현재 값 조회
			float NormalizedValue = Slider->GetNormalizedValue();
			// 외부 코드로 입력된 범위 밖 값 보정
			Value = FMath::Clamp(NormalizedValue, 0.0f, 1.0f);
		}
	}
	// 유효한 화면 선택값 또는 기존 값 반환
	return Value;
}

void UMD_SettingsAudioWidget::RefreshOutputDeviceOptions(FString SelectedDeviceId)
{
	// 출력 선택기를 생략한 레이아웃 허용
	if (IsValid(CB_AudioOutput) == false)
	{
		return;
	}
	// 이전 표시 목록 제거
	CB_AudioOutput->ClearOptions();
	// 표시 순서에 대응하는 장치 ID 목록 초기화
	OutputDeviceIds.Reset();
	// 운영체제 기본 장치를 따르는 빈 ID 항목의 문구 준비
	FString DefaultLabel = NSLOCTEXT("MDSettingsAudio", "DefaultOutput", "시스템 기본 출력 장치").ToString();
	// 기본 장치 항목 등록
	AddDeviceOption(CB_AudioOutput, OutputDeviceIds, FString(), DefaultLabel);
	// 현재 사용 가능한 출력 장치 순회
	for (FAudioOutputDeviceInfo& Device : AvailableOutputDevices)
	{
		// 장치 ID 기준의 중복 제거 및 표시 항목 등록
		AddDeviceOption(CB_AudioOutput, OutputDeviceIds, Device.DeviceId, Device.Name);
	}
	// 목록에 없는 저장 장치까지 보존하는 선택 복원
	SelectDeviceOption(CB_AudioOutput, OutputDeviceIds, SelectedDeviceId);
}

void UMD_SettingsAudioWidget::RefreshInputDeviceOptions(FString SelectedDeviceId)
{
	// 입력 선택기를 생략한 레이아웃 허용
	if (IsValid(CB_VoiceInput) == false)
	{
		return;
	}
	// 이전 표시 목록 제거
	CB_VoiceInput->ClearOptions();
	// 표시 순서에 대응하는 장치 ID 목록 초기화
	InputDeviceIds.Reset();
	// 운영체제 기본 마이크를 따르는 빈 ID 항목의 문구 준비
	FString DefaultLabel = NSLOCTEXT("MDSettingsAudio", "DefaultInput", "시스템 기본 마이크").ToString();
	// 기본 장치 항목 등록
	AddDeviceOption(CB_VoiceInput, InputDeviceIds, FString(), DefaultLabel);
	// 현재 사용 가능한 입력 장치 순회
	for (FAudioInputDeviceInfo& Device : AvailableInputDevices)
	{
		// 장치 ID 기준의 중복 제거 및 표시 항목 등록
		AddDeviceOption(CB_VoiceInput, InputDeviceIds, Device.DeviceId, Device.DeviceName);
	}
	// 목록에 없는 저장 장치까지 보존하는 선택 복원
	SelectDeviceOption(CB_VoiceInput, InputDeviceIds, SelectedDeviceId);
}

void UMD_SettingsAudioWidget::AddDeviceOption(UComboBoxString* ComboBox, TArray<FString>& DeviceIds, FString DeviceId, FString Label)
{
	// 동일 장치 ID의 중복 등록 여부 조회
	bool bAlreadyAdded = DeviceIds.Contains(DeviceId);
	// 기본 장치를 포함한 같은 ID의 중복 표시 방지
	if (bAlreadyAdded == true)
	{
		return;
	}
	// 장치 이름 부재 확인
	bool bEmptyLabel = Label.IsEmpty();
	// 이름 없는 장치의 식별 가능한 표시 문구 사용
	if (bEmptyLabel == true)
	{
		// 빈 이름 대신 장치 식별자 표시
		Label = DeviceId;
	}
	// 동일한 이름의 서로 다른 장치를 구분할 원본 문구 보관
	FString BaseLabel = Label;
	// 중복 이름에 붙일 표시 순번 준비
	int32 LabelNumber = 2;
	// 현재 문구의 중복 여부 조회
	int32 ExistingIndex = ComboBox->FindOptionIndex(Label);
	// 드롭다운 내부 문자열 비교에서도 구분 가능한 문구 생성
	while (ExistingIndex != INDEX_NONE)
	{
		// 동일 이름에 순번을 붙인 고유 문구 구성
		Label = FString::Printf(TEXT("%s (%d)"), *BaseLabel, LabelNumber);
		// 다음 중복 검사의 순번 증가
		++LabelNumber;
		// 기존 순번 포함 이름과의 충돌 확인
		ExistingIndex = ComboBox->FindOptionIndex(Label);
	}
	// 실제 장치 ID를 표시 인덱스와 같은 순서로 보관
	DeviceIds.Add(DeviceId);
	// 선택기에 사용자 표시 이름 등록
	ComboBox->AddOption(Label);
}

void UMD_SettingsAudioWidget::SelectDeviceOption(UComboBoxString* ComboBox, TArray<FString>& DeviceIds, FString SelectedDeviceId)
{
	// 현재 장치 ID의 목록 위치 조회
	int32 SelectedIndex = DeviceIds.Find(SelectedDeviceId);
	// 조회 실패나 분리된 장치의 저장 ID 보존
	if (SelectedIndex == INDEX_NONE)
	{
		// 장치 목록에 없는 상태를 나타낼 표시 문구 준비
		FText DeviceText = FText::FromString(SelectedDeviceId);
		// 실제 식별자를 유지한 미조회 장치 문구 구성
		FText LabelText = FText::Format(NSLOCTEXT("MDSettingsAudio", "UnavailableDevice", "사용 불가 장치 ({0})"), DeviceText);
		// 드롭다운 문자열 변환
		FString Label = LabelText.ToString();
		// 다른 설정 적용 시 장치가 기본값으로 바뀌지 않도록 원본 ID 추가
		AddDeviceOption(ComboBox, DeviceIds, SelectedDeviceId, Label);
		// 추가된 원본 ID의 인덱스 조회
		SelectedIndex = DeviceIds.Find(SelectedDeviceId);
	}
	// 현재 장치에 대응하는 항목 선택
	ComboBox->SetSelectedIndex(SelectedIndex);
}

FString UMD_SettingsAudioWidget::GetSelectedDeviceId(UComboBoxString* ComboBox, TArray<FString>& DeviceIds, FString PreviousDeviceId)
{
	// 컨트롤 생략이나 미선택 상태에 사용할 기존 ID 보관
	FString SelectedDeviceId = PreviousDeviceId;
	// 유효한 선택기의 현재 위치 조회
	if (IsValid(ComboBox) == true)
	{
		// 화면에서 선택한 항목 인덱스 조회
		int32 SelectedIndex = ComboBox->GetSelectedIndex();
		// 장치 ID 목록에 대응하는 유효한 선택인지 확인
		bool bValidIndex = DeviceIds.IsValidIndex(SelectedIndex);
		// 선택된 항목이 있는 경우 실제 장치 ID 사용
		if (bValidIndex == true)
		{
			// 표시 이름 대신 저장용 장치 ID 수집
			SelectedDeviceId = DeviceIds[SelectedIndex];
		}
	}
	// 현재 화면의 장치 선택값 또는 기준값 반환
	return SelectedDeviceId;
}
