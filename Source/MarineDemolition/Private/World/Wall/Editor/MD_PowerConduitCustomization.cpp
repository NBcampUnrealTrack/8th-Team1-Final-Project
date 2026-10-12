#if WITH_EDITOR
#include "World/Wall/Editor/MD_PowerConduitCustomization.h"
#include "World/Wall/MD_PowerConduit.h"
#include "Components/StaticMeshComponent.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "DragAndDrop/ActorDragDropOp.h"
#include "Engine/World.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "SDropTarget.h"
#include "UObject/UnrealType.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<IDetailCustomization> FMD_PowerConduitCustomization::MakeInstance()
{
	// 선택한 전선 디테일의 독립 편집 상태 생성
	return MakeShared<FMD_PowerConduitCustomization>();
}

void FMD_PowerConduitCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// 기존 배열 편집기를 유지할 액터 목록 핸들 조회
	TargetActorsProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(AMD_PowerConduit, TargetActors));
	// 배열 추가 이후 지연 표시 갱신에 사용할 인터페이스 보관
	PropertyUtilities = DetailBuilder.GetPropertyUtilities();
	// 드롭 영역의 배치 인스턴스 전용 노출 여부 확인
	TArray<AMD_PowerConduit*> Conduits;
	// BP 기본값과 다른 종류의 편집 화면에서 외부 액터 등록 제외
	if (GetEditableConduits(Conduits) == false)
	{
		// 실제 맵 인스턴스의 디테일에만 드롭 영역 생성
		return;
	}
	// 기존 대상 배열과 같은 카테고리에 다중 액터 등록 영역 추가
	DetailBuilder.EditCategory(TEXT("MD|Power|Targets"))
		// 액터 등록 목적을 디테일 검색에 연결
		.AddCustomRow(NSLOCTEXT("MDPowerTargets", "Search", "Target actors drag drop"))
		// 목록 전체에 추가하는 동작을 위한 전체 너비 사용
		.WholeRowContent()
		[
			// 아웃라이너 액터 드래그를 직접 수신하는 편집 영역 구성
			SNew(SDropTarget)
			// 검증과 접근성 조회에서 사용할 고정 위젯 식별자 지정
			.Tag(TEXT("MD.Power.TargetActors.Drop"))
			// 유효한 새 액터가 포함된 경우에만 드롭 허용
			.OnAllowDrop(this, &FMD_PowerConduitCustomization::CanDropActors)
			// 다른 종류의 드래그에 불필요한 등록 안내 표시 방지
			.OnIsRecognized(this, &FMD_PowerConduitCustomization::CanDropActors)
			// 선택한 모든 전선의 개별 목록에 대상 추가
			.OnDropped(this, &FMD_PowerConduitCustomization::DropActors)
			[
				// 아웃라이너에서 한 개 또는 여러 액터를 등록하는 방법 안내
				SNew(STextBlock)
				// 빈 배열에서도 찾을 수 있는 드롭 위치 표시
				.Text(NSLOCTEXT("MDPowerTargets", "DropActors", "Drop target actors here"))
				// 등록한 액터가 제어하는 컴포넌트 범위 안내
				.ToolTipText(NSLOCTEXT("MDPowerTargets", "DropActorsHint", "Lock this Details panel, then drag actors from the World Outliner. All static mesh components owned by each actor use the shared material slot and materials."))
			]
		];
}

bool FMD_PowerConduitCustomization::GetEditableConduits(TArray<AMD_PowerConduit*>& Conduits)
{
	// 이전 조회 결과의 편집 대상 잔존 방지
	Conduits.Reset();
	// 닫히거나 읽기 전용으로 바뀐 디테일에 대한 변경 차단
	if (TargetActorsProperty.IsValid() == false || TargetActorsProperty->IsValidHandle() == false || TargetActorsProperty->IsEditable() == false)
	{
		// 현재 수정할 수 없는 목록 제외
		return false;
	}
	// 다중 선택된 실제 프로퍼티 소유 객체 조회
	TArray<UObject*> Objects;
	// 프로퍼티 핸들의 객체 순서를 유지한 개별 값 편집 준비
	TargetActorsProperty->GetOuterObjects(Objects);
	// 선택한 전선의 배치 상태와 편집 월드 검증
	for (UObject* Object : Objects)
	{
		// 전선이 아닌 객체의 목록 변경 방지
		AMD_PowerConduit* Conduit = Cast<AMD_PowerConduit>(Object);
		// 삭제 중인 객체와 BP 기본값에 외부 배치 참조 저장 방지
		if (IsValid(Conduit) == false || Conduit->IsTemplate() == true || Conduit->IsActorBeingDestroyed() == true)
		{
			// 안전하게 함께 편집할 수 없는 선택 전체 제외
			return false;
		}
		// 런타임 또는 프리뷰 월드의 임시 객체 편집 제외
		if (IsValid(Conduit->GetWorld()) == false || Conduit->GetWorld()->WorldType != EWorldType::Editor)
		{
			// 실제 맵 배치 목록에 한정한 드롭 허용
			return false;
		}
		// 다중 선택 사이의 서로 다른 월드 참조 생성 방지
		if (Conduits.IsEmpty() == false && Conduit->GetWorld() != Conduits[0]->GetWorld())
		{
			// 동일한 편집 월드의 전선만 일괄 등록 허용
			return false;
		}
		// 프로퍼티 핸들 순서에 맞춘 전선 목록 보관
		Conduits.Add(Conduit);
	}
	// 실제 편집 대상이 존재하는 경우에만 변경 허용
	return Conduits.IsEmpty() == false;
}

void FMD_PowerConduitCustomization::GetDraggedActors(TSharedPtr<FDragDropOperation> Operation, TArray<AActor*>& Actors)
{
	// 이전 드래그의 액터 목록 초기화
	Actors.Reset();
	// 콘텐츠 에셋과 다른 위젯의 드래그를 배치 액터로 해석하는 상황 차단
	if (Operation.IsValid() == false || Operation->IsOfType<FActorDragDropOp>() == false)
	{
		// 아웃라이너 액터 드래그에 한정한 등록 유지
		return;
	}
	// 드래그한 각 액터의 배치 상태와 메시 소유 여부 확인
	for (TWeakObjectPtr<AActor> ActorReference : StaticCastSharedPtr<FActorDragDropOp>(Operation)->Actors)
	{
		// 드래그 도중 제거될 수 있는 액터 참조 해석
		AActor* Actor = ActorReference.Get();
		// BP 재구성으로 교체되는 자식 액터와 삭제 중인 대상 및 기본 객체 제외
		if (IsValid(Actor) == false || Actor->IsTemplate() == true || Actor->IsChildActor() == true || Actor->IsActorBeingDestroyed() == true)
		{
			// 머티리얼을 제어할 수 없는 대상 생략
			continue;
		}
		// 메시 에셋 준비 전의 컴포넌트도 유효한 등록 대상으로 허용
		if (IsValid(Actor->FindComponentByClass<UStaticMeshComponent>()) == true)
		{
			// 한 번의 드래그에 포함된 중복 액터 제거
			Actors.AddUnique(Actor);
		}
	}
}

bool FMD_PowerConduitCustomization::CanDropActors(TSharedPtr<FDragDropOperation> Operation)
{
	// 드롭 직전 선택한 전선의 현재 편집 가능 상태 조회
	TArray<AMD_PowerConduit*> Conduits;
	// 유효하지 않은 디테일에 대한 드롭 차단
	if (GetEditableConduits(Conduits) == false)
	{
		// 편집할 수 있는 배치 전선이 없는 경우 제외
		return false;
	}
	// 드래그 중인 유효 메시 액터 목록 준비
	TArray<AActor*> Actors;
	// 액터가 아닌 드래그와 메시 없는 액터 제외
	GetDraggedActors(Operation, Actors);
	// 어느 선택 전선에든 새로 추가할 액터가 있는지 확인
	for (AMD_PowerConduit* Conduit : Conduits)
	{
		// 유효한 드래그 대상별 기존 등록 상태 조회
		for (AActor* Actor : Actors)
		{
			// 다른 월드의 액터 참조와 기존 목록 중복 추가 방지
			if (Actor->GetWorld() == Conduit->GetWorld() && Conduit->TargetActors.Contains(Actor) == false)
			{
				// 실제 목록 변경이 발생하는 드롭만 허용
				return true;
			}
		}
	}
	// 무효 대상 또는 이미 등록된 액터만 있는 드롭 제외
	return false;
}

FReply FMD_PowerConduitCustomization::DropActors(const FGeometry& Geometry, const FDragDropEvent& Event)
{
	// 드래그 시작 이후 달라진 선택과 액터 상태 재확인
	if (CanDropActors(Event.GetOperation()) == false)
	{
		// 실제 추가 대상이 없는 드롭의 변경 및 트랜잭션 생성 방지
		return FReply::Unhandled();
	}
	// 각 선택 전선의 기존 배열을 독립적으로 유지할 대상 조회
	TArray<AMD_PowerConduit*> Conduits;
	// 프로퍼티의 현재 소유 객체 순서 확보
	GetEditableConduits(Conduits);
	// 중복 제거가 끝난 드래그 액터 목록 준비
	TArray<AActor*> Actors;
	// 현재 유효한 배치 액터만 등록 대상으로 해석
	GetDraggedActors(Event.GetOperation(), Actors);
	// 원본 배열을 직접 수정하지 않고 프로퍼티 편집에 전달할 값 준비
	TArray<FString> PerObjectValues;
	// 네이티브 배열 타입의 표준 직렬화 규칙 조회
	FProperty* ArrayProperty = TargetActorsProperty->GetProperty();
	// 서로 다른 기존 목록을 덮어쓰지 않는 개별 새 값 구성
	for (AMD_PowerConduit* Conduit : Conduits)
	{
		// 기존 등록 순서와 선택별 차이를 유지한 배열 사본 준비
		TArray<TObjectPtr<AActor>> UpdatedActors = Conduit->TargetActors;
		// 같은 월드에 속한 새 대상만 사본에 추가
		for (AActor* Actor : Actors)
		{
			// 에디터의 다른 월드로 이어지는 참조 생성 방지
			if (Actor->GetWorld() == Conduit->GetWorld())
			{
				// 중복 드롭과 선택 전선별 기존 등록 중복 방지
				UpdatedActors.AddUnique(Actor);
			}
		}
		// 프로퍼티 핸들의 선택 객체 순서에 맞춘 문자열 슬롯 확보
		FString& Value = PerObjectValues.AddDefaulted_GetRef();
		// 엔진의 배열 직렬화 형식을 통한 액터 참조 전달
		ArrayProperty->ExportText_Direct(Value, &UpdatedActors, nullptr, Conduit, PPF_None);
	}
	// 모든 선택 전선의 이번 드롭을 한 번의 실행 취소 단위로 묶기
	FScopedTransaction Transaction(NSLOCTEXT("MDPowerTargets", "AddActors", "Add power target actors"));
	// 수정 전후 알림과 Undo 기록을 보존하는 프로퍼티 편집 경로 사용
	if (TargetActorsProperty->SetPerObjectValues(PerObjectValues) != FPropertyAccess::Success)
	{
		// 값 적용이 거부된 트랜잭션 기록 제외
		Transaction.Cancel();
		// 적용되지 않은 드롭 결과 반환
		return FReply::Unhandled();
	}
	// 배열 길이가 달라진 디테일의 다음 갱신 요청
	PropertyUtilities->RequestRefresh();
	// 액터 목록에 반영된 드롭 처리 완료
	return FReply::Handled();
}
#endif
