#pragma once

#include "CoreMinimal.h"
#include "AudioCaptureBlueprintLibrary.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Gameplay/Settings/MD_SettingsTypes.h"
#include "UI/Screen/Settings/MD_SettingsCategoryWidgetBase.h"
#include "MD_SettingsAudioWidget.generated.h"

class UComboBoxString;
class UMD_CarouselWidget;
class USlider;
class UTextBlock;

// 음성 입력 모드 캐러셀의 BP 디테일 선택용 타입
UENUM(BlueprintType, meta = (EnumDisplayNameFn = "UMD_SettingsAudioWidget::GetVoiceInputModeDisplayName"))
enum class EMD_VoiceInputMode : uint8
{
	// 지정 키를 누르는 동안만 음성 전송
	PushToTalk = 0 UMETA(DisplayName = "누르고 말하기"),
	// 별도 키 입력 없이 음성 전송
	OpenMicrophone = 1 UMETA(DisplayName = "항상 켜기")
};

// 오디오 설정의 미적용 편집값과 카테고리별 저장을 관리하는 화면
UCLASS(Blueprintable)
class MARINEDEMOLITION_API UMD_SettingsAudioWidget : public UMD_SettingsCategoryWidgetBase
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	FORCEINLINE virtual EMD_SettingsCategory GetSettingsCategory() override { return EMD_SettingsCategory::Audio; }
	virtual bool CommitPendingSettings() override;
	virtual bool ReloadPendingSettings() override;
	virtual void ResetPendingSettings() override;

protected:
	// 전체 음량의 선택적 바인딩
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Audio", meta = (BindWidgetOptional))
	TObjectPtr<USlider> Slider_MasterVolume;

	// 현재 WBP의 배경 음악 음량 바인딩
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Audio", meta = (BindWidgetOptional))
	TObjectPtr<USlider> Slider_BGMVolume;

	// 현재 WBP의 효과음 음량 바인딩
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Audio", meta = (BindWidgetOptional))
	TObjectPtr<USlider> Slider_SFXVolume;

	// 현재 WBP의 음성 입력 감도 바인딩
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Audio", meta = (BindWidgetOptional))
	TObjectPtr<USlider> Slider_MicThreshold;

	// 소리 재생 출력 장치 선택
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Audio", meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> CB_AudioOutput;

	// 음성 캡처 입력 장치 선택
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Audio", meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> CB_VoiceInput;

	// BP에서 EMD_VoiceInputMode 타입을 지정할 캐러셀
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Audio", meta = (BindWidgetOptional))
	TObjectPtr<UMD_CarouselWidget> Carousel_MicMode;

	// 복원과 초기화 시 갱신할 슬라이더 숫자 표시
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Audio", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Value_MasterVolume;

	// 복원과 초기화 시 갱신할 슬라이더 숫자 표시
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Audio", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Value_BGMVolume;

	// 복원과 초기화 시 갱신할 슬라이더 숫자 표시
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Audio", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Value_SFXVolume;

	// 복원과 초기화 시 갱신할 슬라이더 숫자 표시
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Audio", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Value_MicThreshold;

	// 적용 전까지 실제 카테고리와 분리된 화면 기준값
	UPROPERTY(Transient)
	FMD_AudioUserSettings PendingSettings;

	// 마지막으로 조회한 출력 장치 목록
	UPROPERTY(Transient)
	TArray<FAudioOutputDeviceInfo> AvailableOutputDevices;

	// 마지막으로 조회한 입력 장치 목록
	UPROPERTY(Transient)
	TArray<FAudioInputDeviceInfo> AvailableInputDevices;

	// 출력 드롭다운 인덱스와 장치 ID의 대응
	UPROPERTY(Transient)
	TArray<FString> OutputDeviceIds;

	// 입력 드롭다운 인덱스와 장치 ID의 대응
	UPROPERTY(Transient)
	TArray<FString> InputDeviceIds;

	// 종료된 화면의 비동기 목록 갱신 차단
	bool bAcceptDeviceResults = true;

	// 중복 출력 장치 조회와 응답 순서 역전 방지
	bool bOutputDeviceRequestPending = false;

	// 중복 입력 장치 조회와 응답 순서 역전 방지
	bool bInputDeviceRequestPending = false;

public:
	// 생략된 컨트롤의 기준값을 보존한 현재 화면 선택값 수집
	UFUNCTION(BlueprintPure, Category = "MD|Settings|Audio")
	FMD_AudioUserSettings GetPendingSettings();

	// 현재 편집 선택을 보존한 입출력 장치 목록의 비동기 갱신 요청
	UFUNCTION(BlueprintCallable, Category = "MD|Settings|Audio")
	void RefreshDeviceOptions();

	// 패키징 환경에서도 유지할 음성 입력 모드 표시 문구 제공
	static FText GetVoiceInputModeDisplayName(int32 NameIndex);

protected:
	// 출력 장치 조회 응답의 현재 선택 보존 및 목록 갱신
	UFUNCTION()
	void HandleOutputDevicesObtained(const TArray<FAudioOutputDeviceInfo>& Devices);

	// 입력 장치 조회 응답의 현재 선택 보존 및 목록 갱신
	UFUNCTION()
	void HandleInputDevicesObtained(const TArray<FAudioInputDeviceInfo>& Devices);

	// 게임 스레드에서 입력 장치 응답의 현재 선택 보존 및 목록 갱신
	void ProcessInputDevicesObtained(TArray<FAudioInputDeviceInfo> Devices);

	// 실제 설정 변경 없는 컨트롤 표시 갱신
	void RefreshDisplay();

	// 슬라이더의 디자이너 범위를 유지한 정규화 값 표시
	void RefreshSlider(USlider* Slider, UTextBlock* ValueText, float Value);

	// 디자이너 슬라이더 범위에서 정규화 설정값 수집
	float GetSliderValue(USlider* Slider, float PreviousValue);

	// 캐시된 출력 장치와 현재 ID를 포함한 드롭다운 구성
	void RefreshOutputDeviceOptions(FString SelectedDeviceId);

	// 캐시된 입력 장치와 현재 ID를 포함한 드롭다운 구성
	void RefreshInputDeviceOptions(FString SelectedDeviceId);

	// 같은 표시 이름의 장치 구분 및 ID 중복 방지
	void AddDeviceOption(UComboBoxString* ComboBox, TArray<FString>& DeviceIds, FString DeviceId, FString Label);

	// 목록에서 찾지 못한 저장 장치도 유지하는 선택 복원
	void SelectDeviceOption(UComboBoxString* ComboBox, TArray<FString>& DeviceIds, FString SelectedDeviceId);

	// 표시 문구와 분리된 실제 장치 ID 수집
	FString GetSelectedDeviceId(UComboBoxString* ComboBox, TArray<FString>& DeviceIds, FString PreviousDeviceId);
};
