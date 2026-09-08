#pragma once

#if WITH_EDITOR
#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "SPinTypeSelector.h"

class IPropertyHandle;
class IPropertyUtilities;

// Enum 타입과 실제 선택값을 드롭다운으로 편집하는 캐러셀 디테일 구성
class FMD_CarouselWidgetCustomization : public IDetailCustomization
{
public:
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

protected:
	// 선택한 Enum 타입의 편집 핸들
	TSharedPtr<IPropertyHandle> EnumProperty;
	// 실제 Enum 값의 편집 핸들
	TSharedPtr<IPropertyHandle> ValueProperty;
	// 타입 변경 후 행 재구성 참조
	TSharedPtr<IPropertyUtilities> PropertyUtilities;

public:
	// 프로퍼티 에디터 등록용 인스턴스 생성
	static TSharedRef<IDetailCustomization> MakeInstance();
	// 네이티브와 Blueprint Enum 전용 타입 목록 구성
	static void GetEnumTypeTree(TArray<FPinTypeTreeItem>& TypeTree, ETypeTreeFilter Filter);

protected:
	// Slate 속성의 읽기 전용 콜백 계약에 따른 타입 조회
	FEdGraphPinType GetSelectedType() const;
	// 선택기 델리게이트 계약에 따른 타입 변경 적용
	void OnTypeChanged(const FEdGraphPinType& PinType);
	// 타입 변경 후 실제 값 선택 행 재구성
	void RefreshDetails();
	// Slate 속성의 읽기 전용 콜백 계약에 따른 선택 문구 조회
	FText GetSelectedValueText() const;
	// 표시 가능한 Enum 값의 드롭다운 메뉴 구성
	TSharedRef<SWidget> BuildValueMenu();
	// 디테일 트랜잭션을 통한 실제 Enum 값 변경
	void SelectValue(int64 EnumValue);
};
#endif
