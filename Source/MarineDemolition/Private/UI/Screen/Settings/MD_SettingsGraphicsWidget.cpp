#include "UI/Screen/Settings/MD_SettingsGraphicsWidget.h"

#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Gameplay/Settings/MD_GraphicsSettingCategory.h"
#include "Gameplay/Settings/MD_SettingsManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/Foundation/MD_CarouselWidget.h"

void UMD_SettingsGraphicsWidget::NativeConstruct()
{
	// 사용자 전체 품질 선택의 개별 품질 동기화 연결
	if (IsValid(CB_TotalQuality) == true)
	{
		// 위젯 재구성 시 중복 구독 방지
		CB_TotalQuality->OnSelectionChanged.AddUniqueDynamic(this, &UMD_SettingsGraphicsWidget::HandleOverallQualityChanged);
	}
	// 개별 품질 선택의 사용자 지정 표시 연결
	for (UComboBoxString* ComboBox : {CB_ShadowQuality.Get(), CB_VFXQuality.Get(), CB_PPQuality.Get()})
	{
		// 현재 레이아웃에 포함된 컨트롤의 구독
		if (IsValid(ComboBox) == true)
		{
			// 중복 없는 개별 품질 변경 구독
			ComboBox->OnSelectionChanged.AddUniqueDynamic(this, &UMD_SettingsGraphicsWidget::HandleIndividualQualityChanged);
		}
	}
	// Blueprint 생성 이벤트 실행 전 현재 카테고리 값 표시
	if (ReloadPendingSettings() == false)
	{
		// 게임 인스턴스 없는 미리보기의 기본값 표시
		RefreshDisplay();
	}
	// 카테고리 등록과 Blueprint 생성 이벤트 실행
	Super::NativeConstruct();
}

void UMD_SettingsGraphicsWidget::NativeDestruct()
{
	// 종료한 위젯의 전체 품질 선택 구독 해제
	if (IsValid(CB_TotalQuality) == true)
	{
		// 전체 품질 이벤트 수신 중단
		CB_TotalQuality->OnSelectionChanged.RemoveDynamic(this, &UMD_SettingsGraphicsWidget::HandleOverallQualityChanged);
	}
	// 종료한 위젯의 개별 품질 선택 구독 순회
	for (UComboBoxString* ComboBox : {CB_ShadowQuality.Get(), CB_VFXQuality.Get(), CB_PPQuality.Get()})
	{
		// 유효한 개별 품질 컨트롤의 구독 해제
		if (IsValid(ComboBox) == true)
		{
			// 개별 품질 이벤트 수신 중단
			ComboBox->OnSelectionChanged.RemoveDynamic(this, &UMD_SettingsGraphicsWidget::HandleIndividualQualityChanged);
		}
	}
	// 카테고리 등록 해제와 상위 종료 처리
	Super::NativeDestruct();
}

bool UMD_SettingsGraphicsWidget::ReloadPendingSettings()
{
	// 현재 위젯 월드의 설정 매니저 조회
	UMD_SettingsManager* SettingsManager = UMD_SettingsManager::Get(this);
	// 게임 인스턴스가 없는 상태의 조회 실패 처리
	if (IsValid(SettingsManager) == false)
	{
		// 현재 화면 선택을 유지한 실패 결과 준비
		bool bLoaded = false;
		// 카테고리 조회 실패 반환
		return bLoaded;
	}
	// 매니저가 소유한 그래픽 카테고리 조회
	UMD_GraphicsSettingCategory* GraphicsCategory = SettingsManager->GetGraphicsSettings();
	// 해제되었거나 준비되지 않은 카테고리 접근 방지
	if (IsValid(GraphicsCategory) == false)
	{
		// 카테고리 부재의 실패 결과 준비
		bool bLoaded = false;
		// 그래픽 값 조회 실패 반환
		return bLoaded;
	}
	// 미저장 화면 편집을 현재 카테고리 값으로 교체
	PendingSettings = GraphicsCategory->Settings;
	// 현재 저장 기준값의 컨트롤 선택 반영
	RefreshDisplay();
	// 현재 카테고리 값 로딩 완료 표시
	bool bLoaded = true;
	// 화면 재로딩 성공 반환
	return bLoaded;
}

bool UMD_SettingsGraphicsWidget::CommitPendingSettings()
{
	// 현재 화면에 대응하는 설정 매니저 조회
	UMD_SettingsManager* SettingsManager = UMD_SettingsManager::Get(this);
	// 설정 매니저가 없는 상태의 카테고리 변경 방지
	if (IsValid(SettingsManager) == false)
	{
		// 매니저 부재의 전달 실패값 준비
		bool bCommitted = false;
		// 카테고리 변경 없는 실패 반환
		return bCommitted;
	}
	// 편집값 전달 대상 그래픽 카테고리 조회
	UMD_GraphicsSettingCategory* GraphicsCategory = SettingsManager->GetGraphicsSettings();
	// 준비되지 않은 카테고리의 편집값 전달 방지
	if (IsValid(GraphicsCategory) == false)
	{
		// 그래픽 카테고리 부재의 전달 실패값 준비
		bool bCommitted = false;
		// 카테고리 변경 없는 실패 반환
		return bCommitted;
	}
	// 매니저 저장 시점의 모든 그래픽 컨트롤 선택 수집
	FMD_GraphicsSettings SelectedSettings = GetPendingSettings();
	// 다른 카테고리 값의 변경 없이 그래픽 값만 갱신
	GraphicsCategory->Settings = SelectedSettings;
	// 화면 기준값도 같은 미적용 선택으로 동기화
	PendingSettings = SelectedSettings;
	// 편집값 전달 성공 결과 준비
	bool bCommitted = true;
	// 매니저의 적용과 저장을 허용하는 성공 반환
	return bCommitted;
}

void UMD_SettingsGraphicsWidget::ResetPendingSettings()
{
	// 실제 카테고리 변경 없는 프로젝트 기본값 준비
	PendingSettings = FMD_GraphicsSettings();
	// 저장 버튼 호출 전까지 화면에만 기본값 반영
	RefreshDisplay();
}

FMD_GraphicsSettings UMD_SettingsGraphicsWidget::GetPendingSettings()
{
	// 생략된 컨트롤의 기준값 보존
	FMD_GraphicsSettings SelectedSettings = PendingSettings;
	// 창 모드 캐러셀의 현재 선택 확인
	if (IsValid(Carousel_WindowMode) == true && Carousel_WindowMode->GetSelectedIndex() != INDEX_NONE)
	{
		// 표시 순서와 분리된 실제 창 모드 값 조회
		int64 WindowMode = Carousel_WindowMode->GetSelectedEnumValue();
		// 엔진에서 지원하는 세 창 모드의 범위 검증
		if (WindowMode >= EWindowMode::Fullscreen && WindowMode <= EWindowMode::Windowed)
		{
			// 저장할 엔진 창 모드 값 반영
			SelectedSettings.WindowMode = static_cast<EWindowMode::Type>(WindowMode);
		}
	}
	// 현재 해상도 선택기 확인
	if (IsValid(CB_Resolution) == true)
	{
		// 실제 해상도 목록에 대응하는 선택 위치 조회
		int32 SelectedIndex = CB_Resolution->GetSelectedIndex();
		// 미선택 상태의 기준값 보존
		if (ResolutionOptions.IsValidIndex(SelectedIndex) == true)
		{
			// 화면 표시 문자열과 분리된 실제 해상도 반영
			SelectedSettings.ScreenResolution = ResolutionOptions[SelectedIndex];
		}
	}
	// 수직 동기화 컨트롤의 선택 수집
	if (IsValid(TB_VSync) == true)
	{
		// 현재 화면의 수직 동기화 선택 반영
		SelectedSettings.bUseVSync = TB_VSync->IsChecked();
	}
	// 프레임 제한 컨트롤의 선택 수집
	if (IsValid(CB_FPSLimit) == true)
	{
		// 실제 FPS 배열에 대응하는 선택 위치 조회
		int32 SelectedIndex = CB_FPSLimit->GetSelectedIndex();
		// 미선택 상태의 기준값 보존
		if (FrameRateOptions.IsValidIndex(SelectedIndex) == true)
		{
			// 무제한과 사용자 지정 소수 FPS를 포함한 실제 값 반영
			SelectedSettings.FrameRateLimit = FrameRateOptions[SelectedIndex];
		}
	}
	// 사용자 지정 상태에서는 마지막 전체 품질 기준값 보존
	SelectedSettings.OverallQuality = GetQualityFromComboBox(CB_TotalQuality, SelectedSettings.OverallQuality);
	// 전체 품질과 독립적인 그림자 품질 수집
	SelectedSettings.ShadowQuality = GetQualityFromComboBox(CB_ShadowQuality, SelectedSettings.ShadowQuality);
	// 전체 품질과 독립적인 시각 효과 품질 수집
	SelectedSettings.EffectsQuality = GetQualityFromComboBox(CB_VFXQuality, SelectedSettings.EffectsQuality);
	// 전체 품질과 독립적인 후처리 품질 수집
	SelectedSettings.PostProcessQuality = GetQualityFromComboBox(CB_PPQuality, SelectedSettings.PostProcessQuality);
	// 실제 적용 전의 현재 화면 선택값 반환
	return SelectedSettings;
}

FText UMD_SettingsGraphicsWidget::GetQualityDisplayName(int32 NameIndex)
{
	// 선언 순서와 일치하는 패키징용 품질 표시 문구 구성
	TArray<FText> DisplayNames = {
		// 최소 품질 단계의 현지화 문구
		NSLOCTEXT("MDSettingsGraphics", "QualityLow", "낮음"),
		// 중간 품질 단계의 현지화 문구
		NSLOCTEXT("MDSettingsGraphics", "QualityMedium", "보통"),
		// 높은 품질 단계의 현지화 문구
		NSLOCTEXT("MDSettingsGraphics", "QualityHigh", "높음"),
		// 최고 일반 품질 단계의 현지화 문구
		NSLOCTEXT("MDSettingsGraphics", "QualityEpic", "매우 높음"),
		// 시네마틱 품질 단계의 현지화 문구
		NSLOCTEXT("MDSettingsGraphics", "QualityCinematic", "시네마틱")
	};
	// 유효하지 않은 Enum 인덱스의 빈 표시 준비
	FText DisplayName = FText::GetEmpty();
	// 품질 표시 목록의 실제 인덱스 범위 확인
	bool bValidIndex = DisplayNames.IsValidIndex(NameIndex);
	// 유효한 품질 항목에 한정한 현지화 문구 조회
	if (bValidIndex == true)
	{
		// 패키징 메타데이터와 독립적인 표시 문구 선택
		DisplayName = DisplayNames[NameIndex];
	}
	// Enum 런타임 표시 콜백의 현지화 문구 반환
	return DisplayName;
}

void UMD_SettingsGraphicsWidget::HandleOverallQualityChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	// 복원에 의한 선택 변경과 생략된 컨트롤의 처리 제외
	if (SelectionType == ESelectInfo::Direct || IsValid(CB_TotalQuality) == false)
	{
		// 프로그램에 의한 표시 갱신의 재진입 차단
		return;
	}
	// 전체 품질의 실제 엔진 단계 조회
	int32 Quality = CB_TotalQuality->GetSelectedIndex();
	// 사용자 지정 항목의 개별 품질 덮어쓰기 방지
	if (Quality < 0 || Quality > 4)
	{
		// 실제 엔진 품질 단계가 아닌 선택 처리 종료
		return;
	}
	// 다른 컨트롤의 미적용 편집을 유지한 기준값 갱신
	PendingSettings = GetPendingSettings();
	// 전체 렌더링 품질 기준값 갱신
	PendingSettings.OverallQuality = Quality;
	// 그림자 품질의 동일 단계 반영
	PendingSettings.ShadowQuality = Quality;
	// 시각 효과 품질의 동일 단계 반영
	PendingSettings.EffectsQuality = Quality;
	// 후처리 품질의 동일 단계 반영
	PendingSettings.PostProcessQuality = Quality;
	// 현재 개별 품질 컨트롤 순회
	for (UComboBoxString* ComboBox : {CB_ShadowQuality.Get(), CB_VFXQuality.Get(), CB_PPQuality.Get()})
	{
		// 현재 레이아웃에 포함된 품질 컨트롤 확인
		if (IsValid(ComboBox) == true)
		{
			// 사용자 이벤트 재진입 없는 개별 품질 표시 동기화
			ComboBox->SetSelectedIndex(Quality);
		}
	}
}

void UMD_SettingsGraphicsWidget::HandleIndividualQualityChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	// 복원 중 발생하는 중간 선택 상태의 처리 제외
	if (SelectionType != ESelectInfo::Direct)
	{
		// 사용자 변경 이후의 전체 품질 또는 사용자 지정 표시
		RefreshOverallQualitySelection();
	}
}

void UMD_SettingsGraphicsWidget::RefreshDisplay()
{
	// 현재 위젯의 창 모드 캐러셀 표시
	if (IsValid(Carousel_WindowMode) == true)
	{
		// 디자이너에 지정된 엔진 Enum의 실제 값으로 선택 복원
		Carousel_WindowMode->SetSelectedEnumValue(PendingSettings.WindowMode);
	}
	// 지원 해상도와 저장 해상도를 포함하는 선택 목록 갱신
	RefreshResolutionOptions();
	// 수직 동기화 컨트롤의 선택 표시
	if (IsValid(TB_VSync) == true)
	{
		// 현재 기준값의 체크 상태 반영
		TB_VSync->SetIsChecked(PendingSettings.bUseVSync);
	}
	// 프레임 제한 선택 목록 갱신
	RefreshFrameRateOptions();
	// 전체 품질과 사용자 지정 항목 구성
	RefreshQualityOptions(CB_TotalQuality, PendingSettings.OverallQuality, true);
	// 그림자 품질 목록과 현재 선택 구성
	RefreshQualityOptions(CB_ShadowQuality, PendingSettings.ShadowQuality);
	// 시각 효과 품질 목록과 현재 선택 구성
	RefreshQualityOptions(CB_VFXQuality, PendingSettings.EffectsQuality);
	// 후처리 품질 목록과 현재 선택 구성
	RefreshQualityOptions(CB_PPQuality, PendingSettings.PostProcessQuality);
	// 저장된 개별 품질에 대응하는 전체 품질 표시
	RefreshOverallQualitySelection();
}

void UMD_SettingsGraphicsWidget::RefreshResolutionOptions()
{
	// 해상도 컨트롤을 생략한 레이아웃의 장치 조회 방지
	if (IsValid(CB_Resolution) == false)
	{
		// 생략된 컨트롤의 표시 처리 종료
		return;
	}
	// 현재 장치 기준의 해상도 목록 재구성
	ResolutionOptions.Reset();
	// 플랫폼이 지원하는 전체 화면 해상도 조회
	if (UKismetSystemLibrary::GetSupportedFullscreenResolutions(ResolutionOptions) == false)
	{
		// 장치 조회 실패 시 불완전한 목록 제거
		ResolutionOptions.Reset();
	}
	// 장치 목록에 없는 저장된 창 크기 보존
	ResolutionOptions.AddUnique(PendingSettings.ScreenResolution);
	// 이전 드롭다운 항목의 중복 등록 방지
	CB_Resolution->ClearOptions();
	// 실제 해상도 배열 순서에 맞춘 표시 목록 구성
	for (FIntPoint Resolution : ResolutionOptions)
	{
		// 가로와 세로 크기의 선택 문구 등록
		CB_Resolution->AddOption(FString::Printf(TEXT("%d x %d"), Resolution.X, Resolution.Y));
	}
	// 현재 저장 해상도에 대응하는 항목 선택
	CB_Resolution->SetSelectedIndex(ResolutionOptions.Find(PendingSettings.ScreenResolution));
}

void UMD_SettingsGraphicsWidget::RefreshFrameRateOptions()
{
	// 프레임 제한 컨트롤을 생략한 레이아웃 허용
	if (IsValid(CB_FPSLimit) == false)
	{
		// 생략된 컨트롤의 표시 처리 종료
		return;
	}
	// 일반적인 FPS 제한과 무제한 항목 준비
	FrameRateOptions = {30.0f, 60.0f, 90.0f, 120.0f, 144.0f, 165.0f, 240.0f, 360.0f, 0.0f};
	// 표준 목록에 없는 현재 FPS의 정밀도 보존
	FrameRateOptions.AddUnique(PendingSettings.FrameRateLimit);
	// 이전 FPS 표시 항목 제거
	CB_FPSLimit->ClearOptions();
	// 실제 FPS 배열 순서에 맞춘 표시 목록 구성
	for (float FrameRate : FrameRateOptions)
	{
		// 제한 해제를 의미하는 FPS 값의 별도 문구 선택
		if (FrameRate == 0.0f)
		{
			// 무제한 프레임 항목 등록
			CB_FPSLimit->AddOption(NSLOCTEXT("MDSettingsGraphics", "Unlimited", "무제한").ToString());
		}
		else
		{
			// 실제 FPS와 단위를 포함하는 선택 문구 등록
			CB_FPSLimit->AddOption(FText::Format(NSLOCTEXT("MDSettingsGraphics", "FrameRate", "{0} FPS"), FText::AsNumber(FrameRate)).ToString());
		}
	}
	// 현재 저장 FPS에 대응하는 항목 선택
	CB_FPSLimit->SetSelectedIndex(FrameRateOptions.Find(PendingSettings.FrameRateLimit));
}

void UMD_SettingsGraphicsWidget::RefreshQualityOptions(UComboBoxString* ComboBox, int32 Quality, bool bIncludeCustom)
{
	// 생략된 품질 선택기 허용
	if (IsValid(ComboBox) == false)
	{
		// 생략된 컨트롤의 표시 처리 종료
		return;
	}
	// 이전 품질 표시 항목 제거
	ComboBox->ClearOptions();
	// 엔진 품질 0부터 4까지 순서대로 선택 목록 구성
	for (int32 QualityIndex = 0; QualityIndex <= 4; ++QualityIndex)
	{
		// 실제 엔진 단계와 동일한 인덱스의 문구 등록
		ComboBox->AddOption(GetQualityDisplayName(QualityIndex).ToString());
	}
	// 전체 품질 선택기에 한정한 사용자 지정 항목 추가
	if (bIncludeCustom == true)
	{
		// 엔진 품질 단계와 분리된 마지막 상태 항목 등록
		ComboBox->AddOption(NSLOCTEXT("MDSettingsGraphics", "Custom", "사용자 지정").ToString());
	}
	// 현재 기준값의 유효 품질 선택 표시
	ComboBox->SetSelectedIndex(FMath::Clamp(Quality, 0, 4));
}

int32 UMD_SettingsGraphicsWidget::GetQualityFromComboBox(UComboBoxString* ComboBox, int32 PreviousQuality)
{
	// 컨트롤 부재의 기존 품질 보존
	if (IsValid(ComboBox) == false)
	{
		// 생략된 품질의 기준값 반환
		return PreviousQuality;
	}
	// 실제 엔진 단계와 동일한 선택 위치 조회
	int32 Quality = ComboBox->GetSelectedIndex();
	// 미선택 또는 사용자 지정 항목의 기준값 보존
	return Quality >= 0 && Quality <= 4 ? Quality : PreviousQuality;
}

void UMD_SettingsGraphicsWidget::RefreshOverallQualitySelection()
{
	// 전체 품질 선택기를 생략한 레이아웃 허용
	if (IsValid(CB_TotalQuality) == false)
	{
		// 생략된 컨트롤의 표시 처리 종료
		return;
	}
	// 개별 컨트롤의 현재 값을 포함하는 편집값 수집
	FMD_GraphicsSettings SelectedSettings = GetPendingSettings();
	// 현재 전체 기준값을 변경하지 않는 개별 품질 비교
	if (SelectedSettings.ShadowQuality == PendingSettings.OverallQuality && SelectedSettings.EffectsQuality == PendingSettings.OverallQuality &&
		// 후처리까지 전체 품질 기준과 일치하는 상태 확인
		SelectedSettings.PostProcessQuality == PendingSettings.OverallQuality)
	{
		// 모든 개별 값이 기준값과 같은 경우 전체 품질 표시
		CB_TotalQuality->SetSelectedIndex(PendingSettings.OverallQuality);
	}
	else
	{
		// 일부 개별 값만 변경된 경우 사용자 지정 상태 표시
		CB_TotalQuality->SetSelectedIndex(5);
	}
}
