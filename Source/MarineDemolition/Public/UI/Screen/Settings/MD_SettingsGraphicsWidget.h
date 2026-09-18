#pragma once

#include "CoreMinimal.h"
#include "Components/ComboBoxString.h"
#include "Gameplay/Settings/MD_SettingsTypes.h"
#include "UI/Screen/Settings/MD_SettingsCategoryWidgetBase.h"
#include "MD_SettingsGraphicsWidget.generated.h"

class UComboBoxString;
class UCheckBox;
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

// 현재 그래픽 화면의 선택값과 적용 전 편집 상태를 연결하는 위젯
UCLASS(Blueprintable)
class MARINEDEMOLITION_API UMD_SettingsGraphicsWidget : public UMD_SettingsCategoryWidgetBase
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	FORCEINLINE virtual EMD_SettingsCategory GetSettingsCategory() override { return EMD_SettingsCategory::Graphics; }
	virtual bool CommitPendingSettings() override;
	virtual bool ReloadPendingSettings() override;
	virtual void ResetPendingSettings() override;

protected:
	// 실제 엔진 창 모드 Enum을 선택하는 캐러셀
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Graphics", meta = (BindWidgetOptional))
	TObjectPtr<UMD_CarouselWidget> Carousel_WindowMode;

	// 지원 해상도와 저장된 창 크기 선택
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Graphics", meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> CB_Resolution;

	// 수직 동기화 선택 토글
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Graphics", meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> TB_VSync;

	// 실제 FPS와 표시 순서를 분리하는 프레임 제한 선택
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Graphics", meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> CB_FPSLimit;

	// 개별 품질 변경 시 사용자 지정 상태를 표시하는 전체 품질 선택
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Graphics", meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> CB_TotalQuality;

	// 그림자 품질 선택
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Graphics", meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> CB_ShadowQuality;

	// 시각 효과 품질 선택
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Graphics", meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> CB_VFXQuality;

	// 후처리 품질 선택
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Graphics", meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> CB_PPQuality;

	// 적용된 카테고리 원본과 분리할 편집 기준값
	UPROPERTY(Transient)
	FMD_GraphicsSettings PendingSettings;

	// 해상도 표시 위치에 대응하는 실제 가로와 세로 값
	UPROPERTY(Transient)
	TArray<FIntPoint> ResolutionOptions;

	// 프레임 제한 표시 위치에 대응하는 실제 FPS 값
	UPROPERTY(Transient)
	TArray<float> FrameRateOptions;

public:
	// 현재 컨트롤 선택과 생략된 컨트롤의 기준값 수집
	UFUNCTION(BlueprintPure, Category = "MD|Settings|Graphics")
	FMD_GraphicsSettings GetPendingSettings();

	// 패키징 환경에서도 유지되는 품질 Enum 표시 문구 제공
	static FText GetQualityDisplayName(int32 NameIndex);

protected:
	// 전체 품질의 사용자 선택에 따른 개별 품질 동기화
	UFUNCTION()
	void HandleOverallQualityChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	// 개별 품질의 사용자 선택에 따른 전체 품질 표시 갱신
	UFUNCTION()
	void HandleIndividualQualityChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	// 실제 적용 없이 기준값을 모든 컨트롤에 표시
	void RefreshDisplay();

	// 지원 해상도와 현재 저장 해상도의 목록 구성
	void RefreshResolutionOptions();

	// 무제한과 사용자 지정 FPS를 포함한 목록 구성
	void RefreshFrameRateOptions();

	// 엔진 품질 단계 순서의 콤보박스 목록과 선택 구성
	void RefreshQualityOptions(UComboBoxString* ComboBox, int32 Quality, bool bIncludeCustom = false);

	// 유효한 품질 선택 또는 이전 기준값 반환
	int32 GetQualityFromComboBox(UComboBoxString* ComboBox, int32 PreviousQuality);

	// 개별 품질과 전체 기준값의 불일치에 따른 사용자 지정 표시
	void RefreshOverallQualitySelection();
};
