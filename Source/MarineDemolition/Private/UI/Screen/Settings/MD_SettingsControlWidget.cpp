#include "UI/Screen/Settings/MD_SettingsControlWidget.h"

#include "Components/CheckBox.h"
#include "Components/InputKeySelector.h"
#include "Components/Slider.h"
#include "Gameplay/Settings/MD_ControlSettingCategory.h"
#include "Gameplay/Settings/MD_SettingsManager.h"

void UMD_SettingsControlWidget::NativeConstruct()
{
	// Blueprint 생성 이벤트 실행 전 현재 카테고리 값 조회
	bool bLoaded = ReloadPendingSettings();
	// 게임 인스턴스가 없는 상태의 기본 표시 준비
	if (bLoaded == false)
	{
		// 실제 적용 없는 기본값 표시
		RefreshDisplay();
	}
	Super::NativeConstruct();
}

bool UMD_SettingsControlWidget::ReloadPendingSettings()
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

	// 다른 설정 탭과 독립적인 조작 카테고리 조회
	UMD_ControlSettingCategory* ControlCategory = SettingsManager->GetControlSettings();
	// 초기화되지 않은 카테고리 접근 방지
	if (IsValid(ControlCategory) == false)
	{
		// 카테고리 조회 실패값 준비
		bool bLoaded = false;
		// 기존 편집값을 유지한 실패 반환
		return bLoaded;
	}

	// 미저장 화면 편집을 현재 카테고리 값으로 교체
	PendingSettings = ControlCategory->Settings;
	// 슬라이더와 선택 컨트롤의 기준값 복원
	RefreshDisplay();
	// 현재 카테고리 로딩 완료값 준비
	bool bLoaded = true;
	// 로딩 결과 반환
	return bLoaded;
}

bool UMD_SettingsControlWidget::CommitPendingSettings()
{
	// 현재 조작 탭의 설정 매니저 조회
	UMD_SettingsManager* SettingsManager = UMD_SettingsManager::Get(this);
	// 게임 인스턴스 없는 화면의 카테고리 변경 방지
	if (IsValid(SettingsManager) == false)
	{
		// 편집값 전달 실패값 준비
		bool bCommitted = false;
		// 카테고리 변경 없는 실패 반환
		return bCommitted;
	}

	// 편집값 전달 대상 조작 카테고리 조회
	UMD_ControlSettingCategory* ControlCategory = SettingsManager->GetControlSettings();
	// 준비되지 않은 카테고리의 편집값 전달 방지
	if (IsValid(ControlCategory) == false)
	{
		// 편집값 전달 실패값 준비
		bool bCommitted = false;
		// 카테고리 변경 없는 실패 반환
		return bCommitted;
	}

	// 매니저 저장 시점의 화면 선택값 수집
	FMD_ControlSettings SelectedSettings = GetPendingSettings();
	// 조작 카테고리에 한정한 편집값 반영
	ControlCategory->Settings = SelectedSettings;
	// 화면 기준값도 같은 미적용 선택으로 동기화
	PendingSettings = SelectedSettings;
	// 편집값 전달 성공 결과 준비
	bool bCommitted = true;
	// 매니저의 적용과 저장을 허용하는 성공 반환
	return bCommitted;
}

void UMD_SettingsControlWidget::ResetPendingSettings()
{
	// 실제 카테고리와 분리된 기본값 준비
	PendingSettings = FMD_ControlSettings();
	// 적용 버튼 호출 전까지 화면에만 기본값 반영
	RefreshDisplay();
}

FMD_ControlSettings UMD_SettingsControlWidget::GetPendingSettings()
{
	// 생략된 컨트롤의 기존 값 보존용 기준값 복사
	FMD_ControlSettings SelectedSettings = PendingSettings;
	// 마우스 감도 슬라이더가 있는 레이아웃의 선택값 수집
	if (IsValid(Slider_MouseSensitivity) == true)
	{
		// 설정 구조체의 지원 범위에 맞춘 감도값 보관
		SelectedSettings.MouseSensitivity = FMath::Clamp(Slider_MouseSensitivity->GetValue(), 0.1f, 10.0f);
	}
	// 세로 시점 반전 선택이 있는 레이아웃의 현재 상태 수집
	if (IsValid(CheckBox_AxisNegate) == true)
	{
		// 체크 상태와 세로 시점 반전 설정의 대응
		SelectedSettings.bInvertVerticalLook = CheckBox_AxisNegate->IsChecked();
	}
	// 점프 키 선택기의 현재 단일 입력 키 수집
	SelectedSettings.JumpKey = GetSelectedKey(KeySelector_Jump, SelectedSettings.JumpKey);
	// 상호작용 키 선택기의 현재 단일 입력 키 수집
	SelectedSettings.InteractKey = GetSelectedKey(KeySelector_Interact, SelectedSettings.InteractKey);
	// 기존 UseItem 이름의 선택기를 능력 사용 입력에 대응
	SelectedSettings.AbilityKey = GetSelectedKey(KeySelector_UseItem, SelectedSettings.AbilityKey);
	// 음성 채팅 키 선택기의 현재 단일 입력 키 수집
	SelectedSettings.VoiceChatKey = GetSelectedKey(KeySelector_VoiceChat, SelectedSettings.VoiceChatKey);
	// 카테고리에 적용하지 않은 현재 화면 값 반환
	return SelectedSettings;
}

void UMD_SettingsControlWidget::RefreshDisplay()
{
	// 현재 WBP의 슬라이더 범위를 유지한 마우스 감도 표시
	if (IsValid(Slider_MouseSensitivity) == true)
	{
		// 디자이너 범위와 설정 범위를 모두 만족하는 표시값 계산
		float MinimumValue = FMath::Max(Slider_MouseSensitivity->GetMinValue(), 0.1f);
		float MaximumValue = FMath::Min(Slider_MouseSensitivity->GetMaxValue(), 10.0f);
		// 잘못된 디자이너 범위의 안전한 최대값 보정
		if (MaximumValue < MinimumValue)
		{
			MaximumValue = MinimumValue;
		}
		// 보정된 범위에 맞춘 현재 감도 표시
		float DisplayValue = FMath::Clamp(PendingSettings.MouseSensitivity, MinimumValue, MaximumValue);
		// 실제 카테고리 변경 없는 슬라이더 표시 갱신
		Slider_MouseSensitivity->SetValue(DisplayValue);
	}
	// 세로 시점 반전 선택이 있는 레이아웃의 표시 갱신
	if (IsValid(CheckBox_AxisNegate) == true)
	{
		// 실제 카테고리 변경 없는 체크 상태 반영
		CheckBox_AxisNegate->SetIsChecked(PendingSettings.bInvertVerticalLook);
	}
	// 점프 키의 현재 화면 표시
	RefreshKeySelector(KeySelector_Jump, PendingSettings.JumpKey);
	// 상호작용 키의 현재 화면 표시
	RefreshKeySelector(KeySelector_Interact, PendingSettings.InteractKey);
	// 능력 사용 키의 현재 화면 표시
	RefreshKeySelector(KeySelector_UseItem, PendingSettings.AbilityKey);
	// 음성 채팅 키의 현재 화면 표시
	RefreshKeySelector(KeySelector_VoiceChat, PendingSettings.VoiceChatKey);
}

FKey UMD_SettingsControlWidget::GetSelectedKey(UInputKeySelector* KeySelector, FKey PreviousKey)
{
	// 선택기가 없는 레이아웃에서 보존할 기존 키
	FKey SelectedKey = PreviousKey;
	// 실제 키 선택기가 있는 경우에 한정한 화면 값 수집
	if (IsValid(KeySelector) == true)
	{
		// 수정 키 정보를 제외한 현재 입력 코드 조회
		FInputChord SelectedChord = KeySelector->GetSelectedKey();
		// 유효한 단일 입력 키 선택 여부 확인
		bool bValidKey = SelectedChord.Key.IsValid();
		if (bValidKey == true)
		{
			// 선택기가 보유한 단일 입력 키 반영
			SelectedKey = SelectedChord.Key;
		}
	}
	// 현재 선택 키 또는 기존 키 반환
	return SelectedKey;
}

void UMD_SettingsControlWidget::RefreshKeySelector(UInputKeySelector* KeySelector, FKey Key)
{
	// 키 선택기를 생략한 레이아웃의 표시 갱신 생략
	if (IsValid(KeySelector) == false)
	{
		return;
	}
	// 런타임 매핑에서 지원하지 않는 수정 키 조합 선택 차단
	KeySelector->SetAllowModifierKeys(false);
	// 사용자 입력 이벤트 없는 단일 키 선택 상태 구성
	FInputChord InputChord(Key);
	// 현재 설정 키의 선택기 표시 반영
	KeySelector->SetSelectedKey(InputChord);
}
