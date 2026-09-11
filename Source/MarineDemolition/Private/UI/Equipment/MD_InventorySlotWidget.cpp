// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Equipment/MD_InventorySlotWidget.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Data/Equipment/MD_ItemDefinition.h"
#include "Engine/Texture2D.h"
#include "InputCoreTypes.h"
#include "UI/Equipment/MD_InventoryDragDropOperation.h"
#include "UI/Equipment/MD_InventoryWidget.h"

void UMD_InventorySlotWidget::NativeConstruct()
{
	// 카드의 기본 위젯 구성 처리
	Super::NativeConstruct();
	// 빈 카드도 드롭 영역으로 사용할 수 있도록 슬롯 자체의 입력 판정 활성화
	SetVisibility(ESlateVisibility::Visible);
	// 위젯 트리 생성 이후 실제 슬롯 데이터 표시
	RefreshSlot();
}

FReply UMD_InventorySlotWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// 부모의 마우스 입력 처리 우선 적용
	FReply Reply = Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	// 이미 처리한 입력과 드래그할 아이템이 없는 경우 기본 응답 유지
	if (Reply.IsEventHandled() == true || CanDragItem() == false)
	{
		// 부모 처리 결과 또는 빈 슬롯의 미처리 응답 반환
		return Reply;
	}

	// 왼쪽 마우스를 누른 상태의 드래그 시작 감지
	return UWidgetBlueprintLibrary::DetectDragIfPressed(InMouseEvent, this, EKeys::LeftMouseButton).NativeReply;
}

void UMD_InventorySlotWidget::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
{
	// 부모의 드래그 생성 처리 우선 적용
	Super::NativeOnDragDetected(InGeometry, InMouseEvent, OutOperation);
	// 별도 작업이 없는 경우 공통 인벤토리 드래그 생성
	if (IsValid(OutOperation) == false)
	{
		// 원본 아이템을 제거하지 않고 출처를 보관하는 드래그 작업 반환
		OutOperation = CreateItemDragOperation();
	}
}

bool UMD_InventorySlotWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	// 프로젝트 인벤토리의 드래그 작업인지 확인할 타입 변환
	UMD_InventoryDragDropOperation* InventoryOperation = Cast<UMD_InventoryDragDropOperation>(InOperation);
	// 인벤토리 아이템의 드롭은 공통 교환 경로로 처리
	if (IsValid(InventoryOperation) == true)
	{
		// 유효한 대상에만 두 슬롯 교환 요청 전달
		return TryDropItem(InventoryOperation);
	}

	// 다른 종류의 드래그에 대한 부모 처리 결과 반환
	return Super::NativeOnDrop(InGeometry, InDragDropEvent, InOperation);
}

void UMD_InventorySlotWidget::InitializeSlot(UMD_InventoryWidget* Inventory, int32 InSlotIndex)
{
	// 카드의 데이터 출처와 드롭 요청 화면 연결
	OwningInventory = Inventory;
	// 화면 배치와 동일한 Hotbar 인덱스 보관
	SlotIndex = InSlotIndex;
}

void UMD_InventorySlotWidget::RefreshSlot()
{
	// Pawn 해제와 빈 슬롯에 남아 있을 이전 아이템 표시 초기화
	ItemDefinition = nullptr;
	// 빈 카드의 이전 수량 표시 제거
	ItemCount = 0;
	// 연결된 인벤토리의 실제 슬롯 상태 조회
	if (IsValid(OwningInventory) == true)
	{
		// 이름과 아이콘을 공유할 현재 아이템 정의 보관
		ItemDefinition = OwningInventory->GetItemAtSlot(SlotIndex);
		// 이동된 아이템과 함께 보존된 현재 수량 조회
		ItemCount = OwningInventory->GetItemCountAtSlot(SlotIndex);
	}

	// 카드 상단 번호 표시가 연결된 경우 실제 Hotbar 번호 반영
	if (IsValid(Txt_SlotNumber) == true)
	{
		// 내부 인덱스에 1을 더한 사용자 표시 번호 적용
		Txt_SlotNumber->SetText(SlotIndex >= 0 ? FText::AsNumber(SlotIndex + 1) : FText::GetEmpty());
	}
	// 실제 아이콘이 있는 경우에만 이미지 영역 표시
	if (IsValid(Img_ToolIcon) == true)
	{
		// 빈 슬롯과 아이콘 미지정 항목의 단색 사각형 표시 방지
		if (IsValid(ItemDefinition) == true && ItemCount > 0 && IsValid(ItemDefinition->ItemIcon) == true)
		{
			// 카드에 지정된 아이콘 크기를 유지한 아이템 이미지 교체
			Img_ToolIcon->SetBrushFromTexture(ItemDefinition->ItemIcon);
			// 아이콘이 다시 채워진 슬롯의 이미지 표시 복원
			Img_ToolIcon->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		else
		{
			// 이전 아이템의 텍스처 참조 제거
			Img_ToolIcon->SetBrushFromTexture(nullptr);
			// 카드 높이를 유지하면서 텍스처 없는 이미지의 그리기 차단
			Img_ToolIcon->SetVisibility(ESlateVisibility::Hidden);
		}
	}
	// 아이템 이름이 연결된 경우 빈 슬롯 표시로 초기화
	if (IsValid(Txt_ToolName) == true)
	{
		// 이전 아이템의 이름 제거
		Txt_ToolName->SetText(FText::GetEmpty());
	}
	// 수량 텍스트가 연결된 경우 빈 슬롯에 남은 값 초기화
	if (IsValid(Txt_ToolCount) == true)
	{
		// 비어 있는 카드의 수량 텍스트 제거
		Txt_ToolCount->SetText(FText::GetEmpty());
	}

	// 실제 아이템을 보유한 카드에만 품목 표시 적용
	if (IsValid(ItemDefinition) == true && ItemCount > 0)
	{
		// 선택적으로 연결한 이름 위젯에 현재 아이템 이름 적용
		if (IsValid(Txt_ToolName) == true)
		{
			// 내부 슬롯 식별자 대신 아이템 표시 이름 사용
			Txt_ToolName->SetText(ItemDefinition->ItemName);
		}
		// 선택적으로 연결한 수량 위젯에 실제 보유 수량 적용
		if (IsValid(Txt_ToolCount) == true)
		{
			// 드래그 교환과 함께 이동한 전체 수량 표시
			Txt_ToolCount->SetText(FText::AsNumber(ItemCount));
		}
	}

	// 실제 보유 수량에 맞춰 수량 영역의 표시를 한 번만 갱신
	if (IsValid(HB_ToolCount) == true)
	{
		// 빈 카드 높이를 유지하며 수량 문구와 숫자를 함께 숨기거나 복원
		HB_ToolCount->SetVisibility(ItemCount > 0 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Hidden);
	}

	// 아이템 상태에 따른 카드 디자인 갱신
	OnSlotRefreshed();
}

bool UMD_InventorySlotWidget::CanDragItem()
{
	// 닫힌 화면 또는 보관 공간이 없는 카드의 드래그 시작 차단
	if (IsValid(OwningInventory) == false || OwningInventory->IsActivated() == false)
	{
		// 사용 가능한 화면이 없는 드래그 거절
		return false;
	}

	// 표시 캐시 대신 현재 슬롯의 실제 아이템과 수량 검사
	if (IsValid(OwningInventory->GetItemAtSlot(SlotIndex)) == false || OwningInventory->GetItemCountAtSlot(SlotIndex) <= 0)
	{
		// 비어 있거나 소진된 아이템의 드래그 거절
		return false;
	}

	// 유효한 아이템 카드의 드래그 시작 허용
	return true;
}

bool UMD_InventorySlotWidget::CanAcceptDrop(UMD_InventoryDragDropOperation* Operation)
{
	// 다른 작업 유형과 해제된 화면의 드롭 차단
	if (IsValid(Operation) == false || IsValid(OwningInventory) == false)
	{
		// 처리할 수 없는 드래그 작업 거절
		return false;
	}

	// 비활성화 후 같은 화면과 Pawn을 다시 사용한 오래된 작업 차단
	if (Operation->SourceDragSessionId != OwningInventory->GetDragSessionId())
	{
		// 현재 화면 연결에서 시작하지 않은 드래그 거절
		return false;
	}

	// 다른 화면에서 시작했거나 드래그 중 Pawn이 교체된 작업 차단
	if (Operation->SourceInventory.IsValid() == false || Operation->SourceInventory.Get() != OwningInventory
		|| Operation->SourceHotbar.IsValid() == false || Operation->SourceHotbar.Get() != OwningInventory->GetHotbarComponent())
	{
		// 원래 인벤토리와 보관 공간이 일치하지 않는 드롭 거절
		return false;
	}

	// 드래그 시작 이후 원본 슬롯의 아이템이 교체되거나 소진된 경우 차단
	if (IsValid(Operation->SourceItem) == false || OwningInventory->GetItemAtSlot(Operation->SourceSlotIndex) != Operation->SourceItem)
	{
		// 다른 아이템을 잘못 이동시키는 오래된 드래그 거절
		return false;
	}

	// 빈 목적지를 허용하고 같은 슬롯과 비활성 화면을 거절하는 공통 검사
	return OwningInventory->CanSwapSlots(Operation->SourceSlotIndex, SlotIndex);
}

UMD_InventoryDragDropOperation* UMD_InventorySlotWidget::CreateItemDragOperation()
{
	// 실제 보유 아이템이 없는 슬롯의 드래그 작업 생성 차단
	if (CanDragItem() == false)
	{
		// 처리할 수 없는 드래그 생성 실패 반환
		return nullptr;
	}

	// 드롭할 때까지 원본을 유지하는 작업 데이터 생성
	UMD_InventoryDragDropOperation* Operation = NewObject<UMD_InventoryDragDropOperation>();
	// 화면 재활성화 이전의 드래그를 구분할 연결 식별자 보관
	Operation->SourceDragSessionId = OwningInventory->GetDragSessionId();
	// 드래그 출처 화면 보관
	Operation->SourceInventory = OwningInventory;
	// Pawn 교체 이후의 오래된 드래그를 차단할 보관 공간 보관
	Operation->SourceHotbar = OwningInventory->GetHotbarComponent();
	// 교환할 원본의 고정 Hotbar 위치 보관
	Operation->SourceSlotIndex = SlotIndex;
	// 드래그 시작 당시의 실제 아이템 정의 보관
	Operation->SourceItem = OwningInventory->GetItemAtSlot(SlotIndex);
	// 커서 중앙에 아이템 미리보기 배치
	Operation->Pivot = EDragPivot::CenterCenter;
	// 사용자 지정 미리보기 위젯 조회
	Operation->DefaultDragVisual = CreateItemDragVisual();
	// 별도 미리보기가 없는 경우 기본 아이템 아이콘 구성
	if (IsValid(Operation->DefaultDragVisual) == false)
	{
		// 실제 슬롯 위젯을 떼어내지 않는 독립 아이콘 생성
		UImage* DragIcon = NewObject<UImage>(Operation);
		// 원본 아이템과 동일한 아이콘을 미리보기에 표시
		DragIcon->SetBrushFromTexture(Operation->SourceItem->ItemIcon);
		// 드래그 중 읽기 쉬운 기본 아이콘 크기 적용
		DragIcon->SetDesiredSizeOverride(FVector2D(96.0f, 96.0f));
		// 드래그 수명 동안 유지할 미리보기 참조 저장
		Operation->DefaultDragVisual = DragIcon;
	}

	// 커서 미리보기가 목적지 슬롯의 드롭 판정을 가리지 않도록 설정
	Operation->DefaultDragVisual->SetVisibility(ESlateVisibility::HitTestInvisible);
	// 원본 보관 상태를 변경하지 않은 드래그 작업 반환
	return Operation;
}

bool UMD_InventorySlotWidget::TryDropItem(UMD_InventoryDragDropOperation* Operation)
{
	// 드래그 출처와 현재 목적지에 동일한 유효성 검사 적용
	if (CanAcceptDrop(Operation) == false)
	{
		// 화면 밖 취소와 잘못된 드롭에서 원본 상태 유지
		return false;
	}

	// 실제 드롭된 두 슬롯만 교환하며 클라이언트에서는 서버 확정 요청 전송
	return OwningInventory->SwapSlots(Operation->SourceSlotIndex, SlotIndex);
}
