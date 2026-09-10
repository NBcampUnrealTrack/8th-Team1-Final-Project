#if WITH_EDITOR
#include "UI/Foundation/Editor/MD_CarouselWidgetCustomization.h"
#include "UI/Foundation/MD_CarouselWidget.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailPropertyRow.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<IDetailCustomization> FMD_CarouselWidgetCustomization::MakeInstance()
{
	// 디테일 인스턴스 생성
	TSharedRef<IDetailCustomization> Instance = MakeShared<FMD_CarouselWidgetCustomization>();
	// 등록 델리게이트에 인스턴스 반환
	return Instance;
}

void FMD_CarouselWidgetCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// Enum 타입 편집 핸들 조회
	EnumProperty = DetailBuilder.GetProperty(TEXT("OptionsEnum"));
	// 실제 값 편집 핸들 조회
	ValueProperty = DetailBuilder.GetProperty(TEXT("SelectedEnumValue"));
	// 디테일 재구성 인터페이스 보관
	PropertyUtilities = DetailBuilder.GetPropertyUtilities();
	// 타입 변경 후 값 목록 갱신 연결
	EnumProperty->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FMD_CarouselWidgetCustomization::RefreshDetails));
	// 엔진 API의 읽기 전용 스키마 계약 준수
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	// 기존 타입 프로퍼티 행 조회
	IDetailPropertyRow* EnumRow = DetailBuilder.EditDefaultProperty(EnumProperty);
	// 유효한 디테일 행에 타입 선택기 적용
	if (EnumRow != nullptr)
	{
		// 프로퍼티의 기본 초기화 버튼을 유지한 편집 UI 교체
		EnumRow->CustomWidget()
		// 기존 프로퍼티 이름 유지
		.NameContent()[EnumProperty->CreatePropertyNameWidget()]
		// 타입 이름을 표시할 편집 영역 확보
		.ValueContent().MinDesiredWidth(250.0f)
		[
			// Enum 전용 변수 타입 선택기 생성
			SNew(SPinTypeSelector, FGetPinTypeTree::CreateStatic(&FMD_CarouselWidgetCustomization::GetEnumTypeTree))
			// Blueprint Enum 에셋의 지연 로드와 타입 설명 사용
			.Schema(Schema)
			// 현재 타입 표시 연결
			.TargetPinType(this, &FMD_CarouselWidgetCustomization::GetSelectedType)
			// 타입 선택 변경 연결
			.OnPinTypeChanged(this, &FMD_CarouselWidgetCustomization::OnTypeChanged)
			// 단일 Enum 타입만 허용
			.bAllowArrays(false)
			// 배열과 컨테이너 타입 선택 UI 제외
			.SelectorType(SPinTypeSelector::ESelectorType::Partial)
		];
	}
	// 실제 값 프로퍼티 행 조회
	IDetailPropertyRow* ValueRow = DetailBuilder.EditDefaultProperty(ValueProperty);
	// 유효한 행에 실제 값 드롭다운 적용
	if (ValueRow != nullptr)
	{
		// 정수 입력 대신 Enum 항목 선택 UI 사용
		ValueRow->CustomWidget()
		// 기존 선택값 프로퍼티 이름 유지
		.NameContent()[ValueProperty->CreatePropertyNameWidget()]
		// 표시 이름을 위한 편집 영역 확보
		.ValueContent().MinDesiredWidth(250.0f)
		[
			// 64비트 실제 값에 대응하는 선택 메뉴 생성
			SNew(SComboButton)
			// 최신 자동 목록 기준의 메뉴 구성
			.OnGetMenuContent(this, &FMD_CarouselWidgetCustomization::BuildValueMenu)
			// 현재 Enum 표시 이름 출력
			.ButtonContent()[SNew(STextBlock).Text(this, &FMD_CarouselWidgetCustomization::GetSelectedValueText)]
		];
	}
}

void FMD_CarouselWidgetCustomization::GetEnumTypeTree(TArray<FPinTypeTreeItem>& TypeTree, ETypeTreeFilter Filter)
{
	// 이전 타입 검색 결과 초기화
	TypeTree.Reset();
	// 엔진의 읽기 전용 타입 검색 스키마 사용
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	// 네이티브와 미로드 Blueprint 타입 검색 결과 보관
	TArray<FPinTypeTreeItem> AllTypes;
	// 기본 변수 타입 선택기의 전체 Enum 탐색 사용
	Schema->GetVariableTypeTree(AllTypes, ETypeTreeFilter::None);
	// 수동 목록 전환 항목 구성
	FPinTypeTreeItem NoneType = MakeShared<UEdGraphSchema_K2::FPinTypeTreeInfo>(FText::FromString(TEXT("None (Manual Options)")), UEdGraphSchema_K2::PC_Byte, Schema, FText::GetEmpty());
	// Enum 해제 선택지 등록
	TypeTree.Add(NoneType);
	// 검색 결과에서 Enum 분류만 추출
	for (FPinTypeTreeItem Type : AllTypes)
	{
		// 미로드 Enum 에셋의 불필요한 동기 로드 방지
		FEdGraphPinType PinType = Type->GetPinType(false);
		// 엔진이 Byte로 표현하는 Enum 분류 이외의 타입 제외
		if (PinType.PinCategory != UEdGraphSchema_K2::PC_Byte)
		{
			continue;
		}
		// 단순 Byte 값이 아닌 Enum 하위 분류에 한정한 선택 허용
		if (Type->bReadOnly == true)
		{
			// 네이티브 및 Blueprint Enum 하위 목록 유지
			TypeTree.Add(Type);
		}
	}
}

FEdGraphPinType FMD_CarouselWidgetCustomization::GetSelectedType() const
{
	// 타입 선택기에 반환할 단일 핀 타입 준비
	FEdGraphPinType PinType;
	// Enum 전용 기본 표시 지정
	PinType.PinCategory = UEdGraphSchema_K2::PC_Enum;
	// 프로퍼티에서 조회할 Enum 객체 준비
	UObject* EnumObject = nullptr;
	// 다중 선택과 유효성 결과 확인
	FPropertyAccess::Result Result = EnumProperty->GetValue(EnumObject);
	// 단일하게 결정된 타입의 표시 반영
	if (Result == FPropertyAccess::Success)
	{
		// 실제 선택된 Enum 타입 연결
		PinType.PinSubCategoryObject = EnumObject;
		// 엔진 Enum 핀 규약에 맞춘 선택 타입 표시
		if (IsValid(EnumObject) == true)
		{
			// Enum 객체를 가진 Byte 핀 타입 적용
			PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
		}
	}
	// 현재 타입 선택 상태 반환
	return PinType;
}

void FMD_CarouselWidgetCustomization::OnTypeChanged(const FEdGraphPinType& PinType)
{
	// 선택한 네이티브 또는 Blueprint Enum 조회
	UEnum* EnumType = Cast<UEnum>(PinType.PinSubCategoryObject.Get());
	// Undo와 다중 객체 편집을 보존하는 타입 변경 적용
	EnumProperty->SetValue(EnumType);
}

void FMD_CarouselWidgetCustomization::RefreshDetails()
{
	// 변경된 Enum 타입 기준의 값 편집 행 재구성 요청
	PropertyUtilities->RequestRefresh();
}

FText FMD_CarouselWidgetCustomization::GetSelectedValueText() const
{
	// 유효한 선택이 없는 상태의 표시 준비
	FText Label = FText::FromString(TEXT("None"));
	// 현재 Enum 타입 조회 대상 준비
	UObject* EnumObject = nullptr;
	// 현재 실제 선택값 조회 대상 준비
	int64 EnumValue = 0;
	// 타입의 다중 선택 결과 확인
	FPropertyAccess::Result TypeResult = EnumProperty->GetValue(EnumObject);
	// 선택값의 다중 선택 결과 확인
	FPropertyAccess::Result ValueResult = ValueProperty->GetValue(EnumValue);
	// 일치하지 않는 다중 타입의 표시 처리
	if (TypeResult != FPropertyAccess::Success)
	{
		// 확정되지 않은 타입의 빈 선택 표시 반환
		return Label;
	}
	// 일치하지 않는 다중 선택값의 표시 처리
	if (ValueResult != FPropertyAccess::Success)
	{
		// 다중 선택값 안내 문구 설정
		Label = FText::FromString(TEXT("Multiple Values"));
		// 다중 선택 상태 반환
		return Label;
	}
	// 반사 객체를 Enum 타입으로 검증
	UEnum* EnumType = Cast<UEnum>(EnumObject);
	// 유효한 타입에 한정한 표시 이름 조회
	if (IsValid(EnumType) == true)
	{
		// 실제 값에 대응하는 Enum 표시 이름 조회
		Label = EnumType->GetDisplayNameTextByValue(EnumValue);
	}
	// 디테일 선택값 표시 반환
	return Label;
}

TSharedRef<SWidget> FMD_CarouselWidgetCustomization::BuildValueMenu()
{
	// 선택 직후 닫히는 Enum 값 메뉴 생성
	FMenuBuilder Menu(true, nullptr);
	// 편집 대상 위젯 목록 준비
	TArray<UObject*> Objects;
	// 캐러셀 기본값 또는 디자이너 인스턴스 조회
	EnumProperty->GetOuterObjects(Objects);
	// 유효한 캐러셀의 자동 목록 조회
	for (UObject* Object : Objects)
	{
		// 편집 대상의 캐러셀 타입 검증
		UMD_CarouselWidget* Carousel = Cast<UMD_CarouselWidget>(Object);
		// 다른 객체 타입의 메뉴 구성 제외
		if (IsValid(Carousel) == false)
		{
			continue;
		}
		// 숨김과 MAX가 제외된 표시 목록 조회
		TArray<FText> Labels = Carousel->GetOptions();
		// 불연속 값이 유지된 실제 값 목록 조회
		TArray<int64> Values = Carousel->GetEnumValues();
		// 실제 Enum 항목 범위 확인
		int32 Count = Values.Num();
		// 자동 목록의 순서에 맞춘 선택 메뉴 구성
		for (int32 Index = 0; Index < Count; ++Index)
		{
			// 각 항목의 실제 값 선택 동작 연결
			FUIAction Action(FExecuteAction::CreateSP(this, &FMD_CarouselWidgetCustomization::SelectValue, Values[Index]));
			// 표시 이름으로 선택 가능한 Enum 항목 등록
			Menu.AddMenuEntry(Labels[Index], FText::GetEmpty(), FSlateIcon(), Action);
		}
		// 첫 유효 대상 기준의 메뉴 구성 완료
		break;
	}
	// 구성한 메뉴를 Slate 위젯으로 변환
	TSharedRef<SWidget> MenuWidget = Menu.MakeWidget();
	// 드롭다운 메뉴 반환
	return MenuWidget;
}

void FMD_CarouselWidgetCustomization::SelectValue(int64 EnumValue)
{
	// 에디터 트랜잭션을 통한 실제 Enum 값 선택 적용
	ValueProperty->SetValue(EnumValue);
}
#endif
