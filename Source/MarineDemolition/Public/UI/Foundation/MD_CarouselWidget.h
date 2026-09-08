#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MD_CarouselWidget.generated.h"

class UButton;
class UTextBlock;

// 외부 설정 화면에 선택 인덱스와 표시 문구 전달
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMD_CarouselSelectionChanged, int32, SelectedIndex, FText, SelectedOption);

// 선택한 Enum 타입과 실제 값 및 표시 문구 전달
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMD_CarouselEnumSelectionChanged, UEnum*, EnumType, int64, EnumValue, FText, SelectedOption);

// 이전 및 다음 버튼으로 텍스트 옵션을 선택하는 재사용 위젯
UCLASS(Blueprintable)
class MARINEDEMOLITION_API UMD_CarouselWidget : public UUserWidget
{

	GENERATED_BODY()

public:
// 에디터 편집과 저장 시 Enum 목록 동기화
#if WITH_EDITOR

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	virtual void PostEditUndo() override;

	virtual void PreSave(FObjectPreSaveContext SaveContext) override;
#endif

protected:
	virtual void NativePreConstruct() override;

public:
	// 사용자 이동 또는 명시적 알림 요청에 따른 실제 선택 변경 이벤트
	UPROPERTY(BlueprintAssignable, Category = "MD|UI|Carousel")
	FMD_CarouselSelectionChanged OnSelectionChanged;

	// 실제 Enum 값의 사용자 선택 변경 이벤트
	UPROPERTY(BlueprintAssignable, Category = "MD|UI|Carousel")
	FMD_CarouselEnumSelectionChanged OnEnumSelectionChanged;

protected:
	// 디테일 타입 선택기로 지정할 Enum 및 미지정 시 수동 목록 사용
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	TObjectPtr<UEnum> OptionsEnum;

	// 배열 인덱스와 구분하여 저장할 실제 Enum 선택값
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (EditCondition = "OptionsEnum != nullptr", EditConditionHides))
	int64 SelectedEnumValue = 0;

	// 설정 값의 순서에 대응하는 표시 문구 목록
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|UI|Carousel", meta = (EditCondition = "OptionsEnum == nullptr"))
	TArray<FText> Options;

	// 패키징 후에도 표시 문구와 실제 값의 대응을 유지할 직렬화 목록
	UPROPERTY()
	TArray<int64> EnumValues;

	// 빈 목록의 INDEX_NONE 처리 및 초기화 시 유효 범위 보정
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|UI|Carousel", meta = (ClampMin = "-1", EditCondition = "OptionsEnum == nullptr", EditConditionHides))
	int32 SelectedIndex = INDEX_NONE;

	// 목록 양 끝에서 반대쪽 끝으로 이동하는 순환 설정
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|UI|Carousel")
	bool bWrapSelection = true;

	// 이전 이동 가능 여부 반영 및 Blueprint 클릭 이벤트 연결
	UPROPERTY(BlueprintReadOnly, Category = "MD|UI|Carousel", meta = (BindWidgetOptional))
	TObjectPtr<UButton> Btn_Previous;

	// 다음 이동 가능 여부 반영 및 Blueprint 클릭 이벤트 연결
	UPROPERTY(BlueprintReadOnly, Category = "MD|UI|Carousel", meta = (BindWidgetOptional))
	TObjectPtr<UButton> Btn_Next;

	// 기존 WBP_Carousel의 중앙 값 표시 바인딩
	UPROPERTY(BlueprintReadOnly, Category = "MD|UI|Carousel", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Value;

public:
	// Enum 타입 변경에 따른 목록 자동 구성 및 무이벤트 초기 선택 보정
	UFUNCTION(BlueprintCallable, Category = "MD|UI|Carousel")
	void SetOptionsEnum(UEnum* InOptionsEnum);

	// 표시 순서와 무관한 실제 Enum 값으로 선택 및 미등록 값의 실패 반환
	UFUNCTION(BlueprintCallable, Category = "MD|UI|Carousel")
	bool SetSelectedEnumValue(int64 InEnumValue, bool bBroadcastChange = false);

	// 현재 옵션을 구성하는 Enum 타입 반환
	UFUNCTION(BlueprintPure, Category = "MD|UI|Carousel")
	FORCEINLINE UEnum* GetOptionsEnum() { return OptionsEnum; }

	// 현재 선택에 대응하는 실제 Enum 값 반환
	UFUNCTION(BlueprintPure, Category = "MD|UI|Carousel")
	FORCEINLINE int64 GetSelectedEnumValue() { return SelectedEnumValue; }

	// 표시 목록의 각 인덱스에 대응하는 실제 Enum 값 목록 반환
	UFUNCTION(BlueprintPure, Category = "MD|UI|Carousel")
	FORCEINLINE TArray<int64> GetEnumValues() { return EnumValues; }

	// 이벤트 없는 옵션 및 초기 선택 갱신을 통한 저장 설정 반영
	UFUNCTION(BlueprintCallable, Category = "MD|UI|Carousel")
	void SetOptions(TArray<FText> InOptions, int32 InSelectedIndex = 0);

	// 목록 끝에 옵션을 무이벤트 추가하고 등록 인덱스 반환
	UFUNCTION(BlueprintCallable, Category = "MD|UI|Carousel")
	int32 AddOption(FText InOption);

	// 문구가 일치하는 첫 옵션의 무이벤트 제거 및 성공 여부 반환
	UFUNCTION(BlueprintCallable, Category = "MD|UI|Carousel")
	bool RemoveOption(FText InOption);

	// 지정 인덱스 옵션의 무이벤트 제거 및 남은 선택 항목 유지
	UFUNCTION(BlueprintCallable, Category = "MD|UI|Carousel")
	bool RemoveOptionAtIndex(int32 InOptionIndex);

	// 전체 옵션과 선택 상태의 무이벤트 초기화
	UFUNCTION(BlueprintCallable, Category = "MD|UI|Carousel")
	void ClearOptions();

	// 문구가 일치하는 첫 옵션의 인덱스 반환 및 미등록 문구의 INDEX_NONE 처리
	UFUNCTION(BlueprintPure, Category = "MD|UI|Carousel")
	int32 FindOptionIndex(FText InOption);

	// 지정 인덱스의 문구 반환 및 잘못된 인덱스의 빈 텍스트 처리
	UFUNCTION(BlueprintPure, Category = "MD|UI|Carousel")
	FText GetOptionAtIndex(int32 InOptionIndex);

	// 현재 등록된 옵션 수 반환
	UFUNCTION(BlueprintPure, Category = "MD|UI|Carousel")
	FORCEINLINE int32 GetOptionCount() { return Options.Num(); }

	// 문구가 일치하는 첫 옵션 선택 및 미등록 문구 입력 시 기존 선택 유지
	UFUNCTION(BlueprintCallable, Category = "MD|UI|Carousel")
	bool SetSelectedOption(FText InOption, bool bBroadcastChange = false);

	// 선택 인덱스 범위 보정 및 요청 시 변경 이벤트 전달
	UFUNCTION(BlueprintCallable, Category = "MD|UI|Carousel")
	void SetSelectedIndex(int32 InSelectedIndex, bool bBroadcastChange = false);

	// 현재 선택 문구 반환 및 빈 목록의 빈 텍스트 처리
	UFUNCTION(BlueprintPure, Category = "MD|UI|Carousel")
	FText GetSelectedOption();

	// 외부 설정 배열 인덱스에 대응하는 현재 선택값 반환
	UFUNCTION(BlueprintPure, Category = "MD|UI|Carousel")
	FORCEINLINE int32 GetSelectedIndex() { return SelectedIndex; }

	// 현재 화면에서 선택 가능한 문구 목록 반환
	UFUNCTION(BlueprintPure, Category = "MD|UI|Carousel")
	FORCEINLINE TArray<FText> GetOptions() { return Options; }

	// 이전 버튼의 Blueprint OnClicked 연결용 선택 함수
	UFUNCTION(BlueprintCallable, Category = "MD|UI|Carousel")
	void SelectPreviousOption();

	// 다음 버튼의 Blueprint OnClicked 연결용 선택 함수
	UFUNCTION(BlueprintCallable, Category = "MD|UI|Carousel")
	void SelectNextOption();

	// 순환 정책 변경에 따른 양 끝 버튼 활성 상태 갱신
	UFUNCTION(BlueprintCallable, Category = "MD|UI|Carousel")
	void SetWrapSelection(bool bInWrapSelection);

	// 현재 목록의 순환 이동 허용 여부 반환
	UFUNCTION(BlueprintPure, Category = "MD|UI|Carousel")
	FORCEINLINE bool GetWrapSelection() { return bWrapSelection; }

protected:
	// Enum 표시 이름과 실제 값의 자동 구성 및 숨김과 MAX 항목 제외
	void RebuildEnumOptions();

	// 이전 및 다음 이동의 공통 경계 검사와 순환 처리
	void MoveSelection(bool bForward);

	// 레이아웃과 스타일을 유지한 문구 및 이동 버튼 상태 갱신
	void RefreshDisplay();
};
