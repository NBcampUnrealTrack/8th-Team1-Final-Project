#pragma once

#if WITH_EDITOR
#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Input/Reply.h"

class AActor;
class AMD_PowerConduit;
class FDragDropEvent;
class FDragDropOperation;
class IPropertyHandle;
class IPropertyUtilities;
struct FGeometry;

// 배치 전선의 액터 목록에 아웃라이너 드롭을 연결하는 디테일 구성
class FMD_PowerConduitCustomization : public IDetailCustomization
{
public:
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

protected:
	// 배열별 기존 값을 보존하며 편집 알림을 전달할 프로퍼티 핸들
	TSharedPtr<IPropertyHandle> TargetActorsProperty;
	// 액터 추가 이후 목록 표시를 갱신할 디테일 인터페이스
	TSharedPtr<IPropertyUtilities> PropertyUtilities;

public:
	// 에디터 모듈에 등록할 디테일 인스턴스 생성
	static TSharedRef<IDetailCustomization> MakeInstance();

protected:
	// 같은 편집 월드에 배치된 수정 가능한 전선 목록 확인
	bool GetEditableConduits(TArray<AMD_PowerConduit*>& Conduits);
	// 액터 드래그에서 스태틱 메시를 소유한 유효 대상 추출
	void GetDraggedActors(TSharedPtr<FDragDropOperation> Operation, TArray<AActor*>& Actors);
	// 현재 목록에 추가 가능한 드롭 대상 존재 여부 확인
	bool CanDropActors(TSharedPtr<FDragDropOperation> Operation);
	// 한 번의 실행 취소로 되돌릴 수 있는 액터 목록 추가
	FReply DropActors(const FGeometry& Geometry, const FDragDropEvent& Event);
};
#endif
