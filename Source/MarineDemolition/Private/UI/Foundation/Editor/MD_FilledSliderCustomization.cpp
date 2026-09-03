// 런타임 빌드에서 디테일 구현 제외
#if WITH_EDITOR
#include "UI/Foundation/Editor/MD_FilledSliderCustomization.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyTypeCustomization.h"
#include "PropertyHandle.h"

// 이 위젯의 Style 행에만 채움 이미지 편집 행 추가
class FMD_FilledSliderStyleCustomization : public IPropertyTypeCustomization
{
public:
	// 기존 UObject 프로퍼티 핸들을 유지하는 초기화
	FMD_FilledSliderStyleCustomization(TArray<TSharedRef<IPropertyHandle>> InFillProperties);
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

protected:
	// 저장과 Undo 및 다중 선택 편집을 유지하는 원본 프로퍼티 핸들
	TArray<TSharedRef<IPropertyHandle>> FillProperties;
};

FMD_FilledSliderStyleCustomization::FMD_FilledSliderStyleCustomization(TArray<TSharedRef<IPropertyHandle>> InFillProperties)
	: FillProperties(MoveTemp(InFillProperties))
{
}

void FMD_FilledSliderStyleCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// 기본 Style 프로퍼티의 이름과 값 편집 위젯 유지
	HeaderRow.NameContent()[PropertyHandle->CreatePropertyNameWidget()]
		.ValueContent()[PropertyHandle->CreatePropertyValueWidget()];
}

void FMD_FilledSliderStyleCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// 기존 Slider 스타일의 자식 개수 조회용 저장소
	uint32 ChildCount = 0;
	// 기본 트랙과 핸들 및 두께 프로퍼티 개수 조회
	PropertyHandle->GetNumChildren(ChildCount);
	// 기존 스타일의 순서와 편집 기능 유지
	for (uint32 ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
	{
		// 기존 스타일 필드의 원본 편집 핸들 조회
		TSharedPtr<IPropertyHandle> ChildProperty = PropertyHandle->GetChildHandle(ChildIndex);
		// 접근 가능한 자식 핸들 확인
		bool bHasChild = ChildProperty.IsValid();
		// 유효한 기존 스타일 필드만 추가
		if (bHasChild == true)
		{
			// 기존 트랙과 핸들 이미지 편집 행 유지
			ChildBuilder.AddProperty(ChildProperty.ToSharedRef());
		}
	}
	// 채움 이미지도 기존 이미지와 동일한 깊이에 배치
	for (TSharedRef<IPropertyHandle> FillProperty : FillProperties)
	{
		// 원본 브러시 프로퍼티의 디테일 편집 기능 유지
		ChildBuilder.AddProperty(FillProperty);
	}
}

TSharedRef<IDetailCustomization> FMD_FilledSliderCustomization::MakeInstance()
{
	// 채움 슬라이더 전용 디테일 구성 생성
	TSharedRef<IDetailCustomization> Customization = MakeShared<FMD_FilledSliderCustomization>();
	// 등록 델리게이트에 인스턴스 전달
	return Customization;
}

void FMD_FilledSliderCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// 기존 UObject 프로퍼티를 Style 내부로 옮길 핸들 목록
	TArray<TSharedRef<IPropertyHandle>> FillProperties;
	// 일반 상태 채움 브러시 편집 핸들 보관
	FillProperties.Add(DetailBuilder.GetProperty(TEXT("NormalFillImage")));
	// 호버 상태 채움 브러시 편집 핸들 보관
	FillProperties.Add(DetailBuilder.GetProperty(TEXT("HoveredFillImage")));
	// 비활성 상태 채움 브러시 편집 핸들 보관
	FillProperties.Add(DetailBuilder.GetProperty(TEXT("DisabledFillImage")));
	// Style 바깥의 중복 프로퍼티 행 제거
	for (TSharedRef<IPropertyHandle> FillProperty : FillProperties)
	{
		// 자동 배치 대신 Style 자식 행으로 표시
		DetailBuilder.HideProperty(FillProperty);
	}
	// 현재 디테일 인스턴스에만 적용할 스타일 확장 팩토리
	FOnGetPropertyTypeCustomizationInstance StyleFactory = FOnGetPropertyTypeCustomizationInstance::CreateLambda([FillProperties]()
	{
		// 기존 스타일 행과 채움 프로퍼티를 함께 표시하는 구성 생성
		TSharedRef<IPropertyTypeCustomization> Customization = MakeShared<FMD_FilledSliderStyleCustomization>(FillProperties);
		// 스타일 행 빌더에 인스턴스 전달
		return Customization;
	});
	// 일반 Slider에는 영향을 주지 않는 인스턴스별 타입 편집 등록
	DetailBuilder.RegisterInstancedCustomPropertyTypeLayout(TEXT("SliderStyle"), StyleFactory);
	// 기존 Style 구조체의 원본 편집 핸들 조회
	TSharedRef<IPropertyHandle> StyleProperty = DetailBuilder.GetProperty(TEXT("WidgetStyle"));
	// 등록한 자식 행 구성을 사용하는 Style 프로퍼티 배치
	DetailBuilder.EditCategory(TEXT("Style")).AddProperty(StyleProperty);
}
#endif
