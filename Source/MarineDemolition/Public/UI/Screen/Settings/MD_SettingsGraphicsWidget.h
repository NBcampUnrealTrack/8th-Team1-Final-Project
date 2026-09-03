// 그래픽 설정 화면 선언의 중복 포함 방지
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Gameplay/Settings/MD_SettingsTypes.h"
#include "MD_SettingsGraphicsWidget.generated.h"

// 화면 모드와 해상도 선택 컨트롤 참조
class UComboBoxString;
// 수직 동기화 선택 컨트롤 참조
class UCheckBox;
// 그래픽 설정의 적용과 변경 취소 및 기본값 복원용 Common UI 버튼 참조
class UCommonButtonBase;
// 기존 공용 캐러셀의 목록과 선택 기능 재사용
class UMD_CarouselWidget;

// BP 캐러셀에서 선택 가능한 엔진 확장성 품질 타입
UENUM(BlueprintType, meta = (EnumDisplayNameFn = "UMD_SettingsGraphicsWidget::GetQualityDisplayName"))
enum class EMD_GraphicsQuality : uint8
{
	// 최소 렌더링 품질 단계
	Low = 0 UMETA(DisplayName = "낮음"),
	// 성능과 표현의 중간 품질 단계
	Medium = 1 UMETA(DisplayName = "보통"),
	// 고품질 렌더링 단계
	High = 2 UMETA(DisplayName = "높음"),
	// 최고 일반 렌더링 품질 단계
	Epic = 3 UMETA(DisplayName = "매우 높음"),
	// 시네마틱 렌더링 품질 단계
	Cinematic = 4 UMETA(DisplayName = "시네마틱")
};

// 그래픽 설정 표시와 현재 화면 선택값의 카테고리별 저장 지원
UCLASS(Blueprintable)
class MARINEDEMOLITION_API UMD_SettingsGraphicsWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;

protected:
	// 전체 화면과 창 모드 선택
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Graphics", meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> CB_WindowMode;

	// 출력 해상도 선택
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Graphics", meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> CB_Resolution;

	// 수직 동기화 사용 여부 선택
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Graphics", meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> CheckBox_VSync;

	// 프레임 제한 선택
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Graphics", meta = (BindWidgetOptional))
	TObjectPtr<UMD_CarouselWidget> Carousel_FrameRate;

	// 전체 확장성 품질 선택
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Graphics", meta = (BindWidgetOptional))
	TObjectPtr<UMD_CarouselWidget> Carousel_OverallQuality;

	// 그림자 품질 선택
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Graphics", meta = (BindWidgetOptional))
	TObjectPtr<UMD_CarouselWidget> Carousel_ShadowQuality;

	// 실제 WBP의 단수형 Effect 이름에 대응하는 효과 품질 선택
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Graphics", meta = (BindWidgetOptional))
	TObjectPtr<UMD_CarouselWidget> Carousel_EffectQuality;

	// 후처리 품질 선택
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Graphics", meta = (BindWidgetOptional))
	TObjectPtr<UMD_CarouselWidget> Carousel_PPQuality;

	// 그래픽 설정 적용 버튼의 선택적 바인딩
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Graphics", meta = (BindWidgetOptional))
	TObjectPtr<UCommonButtonBase> Btn_Apply;

	// 미적용 그래픽 설정 변경 취소 버튼의 선택적 바인딩
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Graphics", meta = (BindWidgetOptional))
	TObjectPtr<UCommonButtonBase> Btn_Revert;

	// 그래픽 설정 기본값 복원 버튼의 선택적 바인딩
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Graphics", meta = (BindWidgetOptional))
	TObjectPtr<UCommonButtonBase> Btn_Reset;

	// 저장 전까지 카테고리 원본과 분리할 화면 기준값
	UPROPERTY(Transient)
	FMD_GraphicsSettings PendingSettings;

	// 해상도 표시 순서에 대응하는 실제 가로와 세로 값
	UPROPERTY(Transient)
	TArray<FIntPoint> ResolutionOptions;

	// Enum 미지정 프레임 캐러셀의 표시 순서에 대응하는 실제 FPS 값
	UPROPERTY(Transient)
	TArray<float> FrameRateOptions;

public:
	// 카테고리의 현재 값으로 미저장 편집 취소 및 화면 갱신
	UFUNCTION(BlueprintCallable, Category = "MD|Settings|Graphics")
	bool ReloadSettings();

	// 적용 버튼에서 호출할 그래픽 카테고리 전용 적용과 저장 및 저장 성공 여부 반환
	UFUNCTION(BlueprintCallable, Category = "MD|Settings|Graphics")
	bool SaveSettings();

	// 저장과 실제 시스템 적용 없이 화면 선택만 프로젝트 기본값으로 복원
	UFUNCTION(BlueprintCallable, Category = "MD|Settings|Graphics")
	void ResetToDefaults();

	// 바인딩된 컨트롤의 현재 선택값과 생략한 컨트롤의 기존 값 반환
	UFUNCTION(BlueprintPure, Category = "MD|Settings|Graphics")
	FMD_GraphicsSettings GetPendingSettings();

	// 메타데이터가 제거된 패키징 환경의 품질 Enum 현지화 문구 제공
	static FText GetQualityDisplayName(int32 NameIndex);

protected:
	// 화면 기준값에 맞춘 목록과 선택 표시의 무저장 갱신
	void RefreshDisplay();

	// 장치가 지원하는 해상도와 현재 저장된 해상도의 선택 목록 구성
	void RefreshResolutionOptions();

	// BP 지정 Enum의 선택 동기화 또는 Enum 미지정 시 FPS 목록 구성
	void RefreshFrameRateOptions();

	// BP에서 지정한 품질 Enum 타입을 유지한 현재 값 선택 표시
	void RefreshQualityCarousel(UMD_CarouselWidget* Carousel, int32 Quality);

	// 품질 캐러셀의 실제 Enum 값 조회 및 유효하지 않은 선택의 기존 값 유지
	int32 GetQualityFromCarousel(UMD_CarouselWidget* Carousel, int32 PreviousQuality);
};
