#include "UI/Screen/Settings/MD_SettingsGraphicsWidget.h"

#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Gameplay/Settings/MD_GraphicsSettingCategory.h"
#include "Gameplay/Settings/MD_SettingsManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/Foundation/MD_CarouselWidget.h"

void UMD_SettingsGraphicsWidget::NativeConstruct()
{
	// 전체 품질 컨트롤을 포함한 화면의 사용자 선택 구독
	if (IsValid(Carousel_OverallQuality) == true)
	{
		// 화면 재생성 시 중복 구독 없는 품질 동기화 연결
		Carousel_OverallQuality->OnEnumSelectionChanged.AddUniqueDynamic(this, &UMD_SettingsGraphicsWidget::HandleOverallQualityChanged);
	}
	// Blueprint 생성 이벤트 실행 전 현재 카테고리 값 표시
	bool bLoaded = ReloadPendingSettings();
	// 게임 인스턴스가 없는 미리보기의 프로젝트 기본값 표시
	if (bLoaded == false)
	{
		// 저장이나 적용 없는 표시 초기화
		RefreshDisplay();
	}
	Super::NativeConstruct();
}

void UMD_SettingsGraphicsWidget::NativeDestruct()
{
	// 화면 종료 시 기존 전체 품질 컨트롤의 구독 정리
	if (IsValid(Carousel_OverallQuality) == true)
	{
		// 종료한 화면으로 전달되는 품질 변경 이벤트 차단
		Carousel_OverallQuality->OnEnumSelectionChanged.RemoveDynamic(this, &UMD_SettingsGraphicsWidget::HandleOverallQualityChanged);
	}
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
	// 생략된 컨트롤의 값 보존을 위한 현재 기준값 복사
	FMD_GraphicsSettings SelectedSettings = PendingSettings;
	// 선택 가능한 화면 모드 컨트롤 확인
	if (IsValid(CB_WindowMode) == true)
	{
		// 표시 순서와 엔진 Enum 순서의 명시적 대응
		TArray<TEnumAsByte<EWindowMode::Type>> WindowModes = {EWindowMode::Fullscreen, EWindowMode::Windowed, EWindowMode::WindowedFullscreen};
		// 현재 드롭다운 선택 위치 조회
		int32 SelectedIndex = CB_WindowMode->GetSelectedIndex();
		// 선택 해제나 잘못된 외부 인덱스의 기존 값 유지
		bool bValidIndex = WindowModes.IsValidIndex(SelectedIndex);
		// 지원하는 창 모드 선택에 한정한 값 갱신
		if (bValidIndex == true)
		{
			// 실제 엔진 창 모드 값 반영
			SelectedSettings.WindowMode = WindowModes[SelectedIndex];
		}
	}
	// 해상도 컨트롤이 있는 레이아웃의 선택 수집
	if (IsValid(CB_Resolution) == true)
	{
		// 화면 표시 위치와 실제 해상도의 대응 조회
		int32 SelectedIndex = CB_Resolution->GetSelectedIndex();
		// 선택 인덱스의 해상도 목록 범위 검증
		bool bValidIndex = ResolutionOptions.IsValidIndex(SelectedIndex);
		// 유효한 선택의 실제 해상도 반영
		if (bValidIndex == true)
		{
			// 표시 문자열 파싱 없이 원본 해상도 값 사용
			SelectedSettings.ScreenResolution = ResolutionOptions[SelectedIndex];
		}
	}
	// 수직 동기화 컨트롤의 선택 수집
	if (IsValid(CheckBox_VSync) == true)
	{
		// 화면에서 선택한 수직 동기화 상태 반영
		SelectedSettings.bUseVSync = CheckBox_VSync->IsChecked();
	}
	// 프레임 제한 캐러셀이 있는 레이아웃의 선택 수집
	if (IsValid(Carousel_FrameRate) == true)
	{
		// 선택 항목이 있는 프레임 캐러셀의 현재 위치 조회
		int32 SelectedIndex = Carousel_FrameRate->GetSelectedIndex();
		// BP에서 지정한 프레임 Enum 타입 조회
		UEnum* OptionsEnum = Carousel_FrameRate->GetOptionsEnum();
		// Enum 항목의 표시 순서와 실제 FPS 값을 구분한 선택 수집
		if (IsValid(OptionsEnum) == true)
		{
			// 빈 Enum 목록의 임의 프레임 제한 변경 방지
			if (SelectedIndex != INDEX_NONE)
			{
				// BP Enum에 정의된 실제 FPS 값 조회
				int64 FrameRate = Carousel_FrameRate->GetSelectedEnumValue();
				// 무제한 0을 포함하는 프레임 제한 최솟값 확인
				if (FrameRate >= 0)
				{
					// 그래픽 카테고리에서 지원하는 최대 프레임 제한 확인
					if (FrameRate <= 360)
					{
						// 유효한 실제 Enum 값의 프레임 제한 반영
						SelectedSettings.FrameRateLimit = static_cast<float>(FrameRate);
					}
				}
			}
		}
		// Enum을 사용하지 않는 기존 FPS 목록 처리
		else
		{
			// 선택 인덱스의 수동 FPS 목록 범위 검증
			bool bValidIndex = FrameRateOptions.IsValidIndex(SelectedIndex);
			// 유효한 FPS 항목에 한정한 선택 반영
			if (bValidIndex == true)
			{
				// 무제한의 0과 사용자 지정 소수 FPS 유지
				SelectedSettings.FrameRateLimit = FrameRateOptions[SelectedIndex];
			}
		}
	}
	// 각 품질 캐러셀의 독립적인 실제 Enum 선택 수집
	SelectedSettings.OverallQuality = GetQualityFromCarousel(Carousel_OverallQuality, SelectedSettings.OverallQuality);
	// 전체 품질과 별도로 선택한 그림자 품질 유지
	SelectedSettings.ShadowQuality = GetQualityFromCarousel(Carousel_ShadowQuality, SelectedSettings.ShadowQuality);
	// 전체 품질과 별도로 선택한 효과 품질 유지
	SelectedSettings.EffectsQuality = GetQualityFromCarousel(Carousel_EffectQuality, SelectedSettings.EffectsQuality);
	// 전체 품질과 별도로 선택한 후처리 품질 유지
	SelectedSettings.PostProcessQuality = GetQualityFromCarousel(Carousel_PPQuality, SelectedSettings.PostProcessQuality);
	// 아직 카테고리나 엔진에 적용하지 않은 화면 선택값 반환
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

void UMD_SettingsGraphicsWidget::HandleOverallQualityChanged(UEnum* EnumType, int64 EnumValue, FText SelectedOption)
{
	// Enum이 없는 선택 알림의 품질 변경 방지
	if (IsValid(EnumType) == false)
	{
		return;
	}
	// 엔진 품질 범위보다 작은 실제 Enum 값 제외
	if (EnumValue < 0)
	{
		return;
	}
	// 엔진 품질 범위를 벗어난 실제 Enum 값 제외
	if (EnumValue > 4)
	{
		return;
	}
	// 표시 인덱스와 구분되는 실제 전체 품질 단계 보관
	int32 Quality = static_cast<int32>(EnumValue);
	// 실제 카테고리와 분리된 전체 품질 편집값 갱신
	PendingSettings.OverallQuality = Quality;
	// 컨트롤 생략 시에도 적용할 그림자 품질 편집값 갱신
	PendingSettings.ShadowQuality = Quality;
	// 컨트롤 생략 시에도 적용할 효과 품질 편집값 갱신
	PendingSettings.EffectsQuality = Quality;
	// 컨트롤 생략 시에도 적용할 후처리 품질 편집값 갱신
	PendingSettings.PostProcessQuality = Quality;
	// 변경 이벤트 재호출 없는 그림자 품질 선택 동기화
	RefreshQualityCarousel(Carousel_ShadowQuality, Quality);
	// 변경 이벤트 재호출 없는 효과 품질 선택 동기화
	RefreshQualityCarousel(Carousel_EffectQuality, Quality);
	// 해상도와 프레임 등 다른 편집값을 유지한 후처리 품질 선택 동기화
	RefreshQualityCarousel(Carousel_PPQuality, Quality);
}

void UMD_SettingsGraphicsWidget::RefreshDisplay()
{
	// 화면 모드 목록을 담당하는 컨트롤 유효성 확인
	if (IsValid(CB_WindowMode) == true)
	{
		// 이전 표시 항목의 중복 등록 방지
		CB_WindowMode->ClearOptions();
		// 전체 화면 모드의 표시 항목 등록
		CB_WindowMode->AddOption(NSLOCTEXT("MDSettingsGraphics", "Fullscreen", "전체 화면").ToString());
		// 일반 창 모드의 표시 항목 등록
		CB_WindowMode->AddOption(NSLOCTEXT("MDSettingsGraphics", "Windowed", "창 모드").ToString());
		// 테두리 없는 창 모드의 표시 항목 등록
		CB_WindowMode->AddOption(NSLOCTEXT("MDSettingsGraphics", "Borderless", "테두리 없는 창").ToString());
		// 화면 모드 표시 순서에 대응하는 엔진 Enum 목록 구성
		TArray<TEnumAsByte<EWindowMode::Type>> WindowModes = {EWindowMode::Fullscreen, EWindowMode::Windowed, EWindowMode::WindowedFullscreen};
		// 현재 엔진 Enum 값의 표시 위치 조회
		int32 SelectedIndex = WindowModes.Find(PendingSettings.WindowMode);
		// 현재 창 모드에 해당하는 드롭다운 항목 선택
		CB_WindowMode->SetSelectedIndex(SelectedIndex);
	}
	// 장치와 현재 값 기준의 해상도 목록 갱신
	RefreshResolutionOptions();
	// 수직 동기화 컨트롤이 있는 레이아웃의 상태 갱신
	if (IsValid(CheckBox_VSync) == true)
	{
		// 현재 기준값의 체크 상태 반영
		CheckBox_VSync->SetIsChecked(PendingSettings.bUseVSync);
	}
	// 현재 저장값을 포함하는 프레임 제한 목록 갱신
	RefreshFrameRateOptions();
	// 전체 품질의 Enum 선택 표시
	RefreshQualityCarousel(Carousel_OverallQuality, PendingSettings.OverallQuality);
	// 그림자 품질의 독립적인 Enum 선택 표시
	RefreshQualityCarousel(Carousel_ShadowQuality, PendingSettings.ShadowQuality);
	// 효과 품질의 독립적인 Enum 선택 표시
	RefreshQualityCarousel(Carousel_EffectQuality, PendingSettings.EffectsQuality);
	// 후처리 품질의 독립적인 Enum 선택 표시
	RefreshQualityCarousel(Carousel_PPQuality, PendingSettings.PostProcessQuality);
}

void UMD_SettingsGraphicsWidget::RefreshResolutionOptions()
{
	// 해상도 컨트롤을 생략한 레이아웃의 불필요한 장치 조회 방지
	if (IsValid(CB_Resolution) == false)
	{
		return;
	}
	// 현재 출력 장치 기준으로 선택 목록 재구성
	ResolutionOptions.Reset();
	// 현재 플랫폼이 지원하는 전체 화면 해상도 조회
	bool bFoundResolutions = UKismetSystemLibrary::GetSupportedFullscreenResolutions(ResolutionOptions);
	// 장치 조회 실패 시 미확정 목록 제거
	if (bFoundResolutions == false)
	{
		// 현재 설정값만으로도 선택 화면을 구성할 빈 목록 준비
		ResolutionOptions.Reset();
	}
	// 저장된 창 크기처럼 장치 목록에 없는 현재 해상도 보존
	ResolutionOptions.AddUnique(PendingSettings.ScreenResolution);
	// 이전 표시 항목의 중복 등록 방지
	CB_Resolution->ClearOptions();
	// 조회된 해상도의 실제 순서에 맞춘 표시 문자열 구성
	for (FIntPoint Resolution : ResolutionOptions)
	{
		// 가로와 세로 크기의 사용자 표시 문구 준비
		FString Label = FString::Printf(TEXT("%d x %d"), Resolution.X, Resolution.Y);
		// 해상도 선택 항목 등록
		CB_Resolution->AddOption(Label);
	}
	// 현재 해상도에 해당하는 실제 목록 위치 조회
	int32 SelectedIndex = ResolutionOptions.Find(PendingSettings.ScreenResolution);
	// 현재 출력 해상도의 드롭다운 선택 표시
	CB_Resolution->SetSelectedIndex(SelectedIndex);
}

void UMD_SettingsGraphicsWidget::RefreshFrameRateOptions()
{
	// 프레임 제한 컨트롤을 생략한 레이아웃 허용
	if (IsValid(Carousel_FrameRate) == false)
	{
		return;
	}
	// BP에서 지정한 프레임 제한 Enum 타입 조회
	UEnum* OptionsEnum = Carousel_FrameRate->GetOptionsEnum();
	// BP의 타입과 표시 목록을 그대로 유지하는 Enum 선택 동기화
	if (IsValid(OptionsEnum) == true)
	{
		// 이전 수동 FPS 목록과 Enum 선택의 혼용 방지
		FrameRateOptions.Reset();
		// 캐러셀에 직렬화된 실제 Enum 값 목록 조회
		TArray<int64> EnumValues = Carousel_FrameRate->GetEnumValues();
		// 저장된 FPS와 정확히 대응하는 Enum 항목 탐색
		for (int64 EnumValue : EnumValues)
		{
			// 저장된 실수 FPS와 비교할 실제 Enum 값 변환
			float EnumFrameRate = static_cast<float>(EnumValue);
			// 소수 FPS를 임의 정수 항목으로 변경하지 않는 일치 검사
			if (EnumFrameRate == PendingSettings.FrameRateLimit)
			{
				// 타입과 목록 변경 없이 현재 FPS 선택 표시
				bool bSelected = Carousel_FrameRate->SetSelectedEnumValue(EnumValue);
				// 실제 값 선택 결과 소비
				(void)bSelected;
				// 현재 FPS 선택 동기화 완료
				break;
			}
		}
		// BP Enum 목록의 수동 목록 덮어쓰기 방지
		return;
	}
	// 일반적인 프레임 제한과 무제한 항목 구성
	FrameRateOptions = {30.0f, 60.0f, 90.0f, 120.0f, 144.0f, 165.0f, 240.0f, 360.0f, 0.0f};
	// 표준 목록에 없는 저장된 사용자 지정 FPS 보존
	FrameRateOptions.AddUnique(PendingSettings.FrameRateLimit);
	// 실제 FPS 배열과 동일한 순서의 표시 목록 준비
	TArray<FText> Labels;
	// 각각의 프레임 제한 값에 대응하는 표시 문구 구성
	for (float FrameRate : FrameRateOptions)
	{
		// 소수 FPS의 현재 문화권 표시값 생성
		FText FrameRateText = FText::AsNumber(FrameRate);
		// 단위와 함께 표시할 일반 프레임 제한 문구 생성
		FText Label = FText::Format(NSLOCTEXT("MDSettingsGraphics", "FrameRate", "{0} FPS"), FrameRateText);
		// 0 FPS의 실제 의미인 프레임 제한 해제 표시
		if (FrameRate == 0.0f)
		{
			// 무제한 선택 문구 사용
			Label = NSLOCTEXT("MDSettingsGraphics", "Unlimited", "무제한");
		}
		// 현재 FPS 값에 대응하는 문구 등록
		Labels.Add(Label);
	}
	// 현재 프레임 제한의 표시 위치 조회
	int32 SelectedIndex = FrameRateOptions.Find(PendingSettings.FrameRateLimit);
	// 변경 이벤트 없이 프레임 제한 목록과 현재 값 표시
	Carousel_FrameRate->SetOptions(Labels, SelectedIndex);
}

void UMD_SettingsGraphicsWidget::RefreshQualityCarousel(UMD_CarouselWidget* Carousel, int32 Quality)
{
	// 특정 품질 컨트롤을 생략한 레이아웃 허용
	if (IsValid(Carousel) == false)
	{
		return;
	}
	// BP 디테일에서 지정한 품질 Enum 타입 조회
	UEnum* OptionsEnum = Carousel->GetOptionsEnum();
	// Enum 미지정 시 타입 강제 지정 없이 기존 BP 목록 유지
	if (IsValid(OptionsEnum) == false)
	{
		return;
	}
	// 저장값을 엔진이 지원하는 품질 범위로 보정
	int32 SelectedQuality = FMath::Clamp(Quality, 0, 4);
	// 변경 이벤트 없이 실제 품질 값 선택
	bool bSelected = Carousel->SetSelectedEnumValue(SelectedQuality);
	// BP Enum에 현재 품질 항목이 없는 경우의 기존 선택 유지
	(void)bSelected;
}

int32 UMD_SettingsGraphicsWidget::GetQualityFromCarousel(UMD_CarouselWidget* Carousel, int32 PreviousQuality)
{
	// 컨트롤 생략이나 잘못된 선택에 사용할 기존 값 보관
	int32 Quality = PreviousQuality;
	// 실제 품질 컨트롤이 있는 경우에만 현재 값 조회
	if (IsValid(Carousel) == true)
	{
		// 아직 목록이 초기화되지 않은 캐러셀의 선택 상태 확인
		int32 SelectedIndex = Carousel->GetSelectedIndex();
		// 선택 항목이 없는 경우의 기존 품질 유지
		if (SelectedIndex == INDEX_NONE)
		{
			// 빈 캐러셀의 임의 품질 변경 방지
			return Quality;
		}
		// 실제 값 조회를 지원하는 Enum 목록 여부 확인
		UEnum* OptionsEnum = Carousel->GetOptionsEnum();
		// 수동 목록을 품질 Enum 값으로 해석하는 오류 방지
		if (IsValid(OptionsEnum) == false)
		{
			// 잘못된 목록 형식의 기존 품질 유지
			return Quality;
		}
		// 표시 인덱스와 구분되는 실제 Enum 값 조회
		int64 SelectedQuality = Carousel->GetSelectedEnumValue();
		// 엔진이 지원하는 품질 최솟값 검증
		if (SelectedQuality >= 0)
		{
			// 엔진이 지원하는 품질 최댓값 검증
			if (SelectedQuality <= 4)
			{
				// 검증된 실제 품질 값 반영
				Quality = static_cast<int32>(SelectedQuality);
			}
		}
	}
	// 현재 화면의 유효한 품질 값 또는 기존 값 반환
	return Quality;
}
