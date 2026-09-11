// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Equipment/MD_InventorySlotWidget.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Data/Equipment/MD_ItemDefinition.h"
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

void UMD_InventorySlotWidget::NativeDestruct()
{
	// 제거된 카드의 드래그 알림과 강조 표시 정리
	ClearDropHighlight();
	Super::NativeDestruct();
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

void UMD_InventorySlotWidget::NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	Super::NativeOnDragEnter(InGeometry, InDragDropEvent, InOperation);
	// 커서가 진입한 카드의 실제 교환 가능 여부 표시
	UpdateDropHighlight(Cast<UMD_InventoryDragDropOperation>(InOperation));
}

void UMD_InventorySlotWidget::NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	// 커서를 벗어난 카드의 기존 테두리 즉시 복원
	ClearDropHighlight();
	Super::NativeOnDragLeave(InDragDropEvent, InOperation);
}

bool UMD_InventorySlotWidget::NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	// 현재 슬롯의 드롭 검사 결과를 강조 표시와 입력 처리에 함께 사용
	if (UpdateDropHighlightState(Cast<UMD_InventoryDragDropOperation>(InOperation)) == true)
	{
		// 현재 카드가 유효한 드롭 대상임을 입력 경로에 전달
		return true;
	}

	// 처리할 수 없는 드래그의 부모 응답 유지
	return Super::NativeOnDragOver(InGeometry, InDragDropEvent, InOperation);
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

	// 공통 교환 경로를 거치지 않는 다른 종류의 드롭 강조 정리
	ClearDropHighlight();
	// 다른 종류의 드래그에 대한 부모 처리 결과 반환
	return Super::NativeOnDrop(InGeometry, InDragDropEvent, InOperation);
}

void UMD_InventorySlotWidget::InitializeSlot(UMD_InventoryWidget* Inventory, int32 InSlotIndex)
{
	// 카드의 데이터 출처가 바뀌기 전 이전 강조와 알림 정리
	ClearDropHighlight();
	// 카드의 데이터 출처와 드롭 요청 화면 연결
	OwningInventory = Inventory;
	// 화면 배치와 동일한 Hotbar 인덱스 보관
	SlotIndex = InSlotIndex;
	// 고정 번호의 숫자 서식 생성을 슬롯 연결 시점으로 제한
	SlotNumberText = SlotIndex >= 0 ? FText::AsNumber(SlotIndex + 1) : FText::GetEmpty();
}

void UMD_InventorySlotWidget::RefreshSlot()
{
	// 수량 텍스트의 재생성 여부를 비교할 이전 보유 수량 보관
	int32 PreviousItemCount = ItemCount;
	// 연결된 인벤토리의 실제 슬롯 상태 조회
	if (IsValid(OwningInventory) == true)
	{
		// 이름과 아이콘을 공유할 현재 아이템 정의 보관
		ItemDefinition = OwningInventory->GetItemAtSlot(SlotIndex);
		// 이동된 아이템과 함께 보존된 현재 수량 조회
		ItemCount = OwningInventory->GetItemCountAtSlot(SlotIndex);
	}
	else
	{
		// 해제된 화면에서 이전 아이템 참조 제거
		ItemDefinition = nullptr;
		// 연결이 없는 카드의 이전 수량 초기화
		ItemCount = 0;
	}
	// 실제 수량이 바뀐 경우에만 숫자 서식 텍스트 재생성
	if (PreviousItemCount != ItemCount)
	{
		// 빈 슬롯의 숫자 제거와 보유 수량의 지역화 서식 보관
		ItemCountText = ItemCount > 0 ? FText::AsNumber(ItemCount) : FText::GetEmpty();
	}

	// 슬롯 연결 시 생성한 번호 텍스트 재사용
	SetSlotText(Txt_SlotNumber, SlotNumberText);
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
	// 실제 아이템을 보유한 카드에만 품목 표시 적용
	if (IsValid(ItemDefinition) == true && ItemCount > 0)
	{
		// 중간 초기화 없이 실제 아이템 이름을 한 번만 반영
		SetSlotText(Txt_ToolName, ItemDefinition->ItemName);
		// 동일 수량의 숫자 서식과 텍스트 위젯 갱신 재사용
		SetSlotText(Txt_ToolCount, ItemCountText);
	}
	else
	{
		// 빈 슬롯에 남아 있는 이전 이름만 제거
		SetSlotText(Txt_ToolName, FText::GetEmpty());
		// 빈 슬롯에 남아 있는 이전 수량만 제거
		SetSlotText(Txt_ToolCount, FText::GetEmpty());
	}

	// 실제 보유 수량에 맞춰 수량 영역의 표시를 한 번만 갱신
	if (IsValid(HB_ToolCount) == true)
	{
		// 빈 카드 높이를 유지하며 수량 문구와 숫자를 함께 숨기거나 복원
		HB_ToolCount->SetVisibility(ItemCount > 0 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Hidden);
	}

	// 아이템 상태에 따른 카드 디자인 갱신
	OnSlotRefreshed();
	// 드래그 중 아이템 소진과 교체로 달라진 드롭 가능 상태 반영
	UpdateDropHighlight(HoveredDragOperation.Get());
}

void UMD_InventorySlotWidget::SetSlotText(UTextBlock* TextBlock, FText NewText)
{
	// 선택 바인딩이 존재하고 기존 텍스트와 이력이 달라진 경우만 반영
	if (IsValid(TextBlock) == true && TextBlock->GetText().IdenticalTo(NewText) == false)
	{
		// 변경된 텍스트의 표시와 지역화 이력 적용
		TextBlock->SetText(NewText);
	}
}

bool UMD_InventorySlotWidget::CanDragItem()
{
	// 닫힌 화면 또는 보관 공간이 없는 카드의 드래그 시작 차단
	if (IsValid(OwningInventory) == false || OwningInventory->IsActivated() == false)
	{
		// 사용 가능한 화면이 없는 드래그 거절
		return false;
	}

	// 아이템 유효성 검사까지 포함하는 실제 수량 조회로 드래그 가능 여부 반환
	return OwningInventory->GetItemCountAtSlot(SlotIndex) > 0;
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

void UMD_InventorySlotWidget::SetDropHighlightColor(FLinearColor InColor)
{
	// 이후 드롭 대상 검사에서도 사용할 지정 색상 보관
	DropHighlightColor = InColor;
	// 강조 중인 테두리에만 즉시 색상 변경 적용
	if (bIsDropHighlighted == true && IsValid(BDR_Background) == true)
	{
		// 강조 종료 시 복원할 원래 색상과 현재 배경 디자인 유지
		FSlateBrush Brush = BDR_Background->Background;
		// 외곽선에 새로 지정한 강조 색상 적용
		Brush.OutlineSettings.Color = FSlateColor(DropHighlightColor);
		// 현재 드래그가 끝나기 전에도 변경 색상 표시
		BDR_Background->SetBrush(Brush);
	}
}

void UMD_InventorySlotWidget::UpdateDropHighlight(UMD_InventoryDragDropOperation* Operation)
{
	// 기존 호출 경로를 유지하며 강조 상태의 공통 갱신 실행
	UpdateDropHighlightState(Operation);
}

bool UMD_InventorySlotWidget::UpdateDropHighlightState(UMD_InventoryDragDropOperation* Operation)
{
	// 다른 드래그로 바뀐 경우에만 종료 알림 연결 교체
	if (HoveredDragOperation.Get() != Operation)
	{
		// 이전 드래그의 알림과 색상 정리
		ClearDropHighlight();
		// 슬롯 위에서 진행 중인 작업 참조 보관
		HoveredDragOperation = Operation;
		// 실제 작업에만 완료와 취소 알림 등록
		if (IsValid(Operation) == true)
		{
			// 드롭 완료 시 강조가 남는 상태 방지
			Operation->OnDrop.AddUniqueDynamic(this, &UMD_InventorySlotWidget::HandleDragEnded);
			// 화면 밖 취소 시에도 기존 테두리 복원
			Operation->OnDragCancelled.AddUniqueDynamic(this, &UMD_InventorySlotWidget::HandleDragEnded);
		}
	}

	// 강조 표시와 입력 응답에서 재사용할 현재 드롭 가능 여부 계산
	bool bCanAcceptDrop = CanAcceptDrop(Operation);
	// 한 번 계산한 결과로 지정한 테두리 강조 적용
	SetDropHighlighted(bCanAcceptDrop);
	// 테두리 바인딩 유무와 무관한 실제 드롭 가능 여부 반환
	return bCanAcceptDrop;
}

void UMD_InventorySlotWidget::ClearDropHighlight()
{
	// 드래그 작업이 남아 있는 경우 등록한 종료 알림 해제
	if (HoveredDragOperation.IsValid() == true)
	{
		// 해당 카드가 등록한 드롭 완료 알림 제거
		HoveredDragOperation->OnDrop.RemoveDynamic(this, &UMD_InventorySlotWidget::HandleDragEnded);
		// 해당 카드가 등록한 취소 알림 제거
		HoveredDragOperation->OnDragCancelled.RemoveDynamic(this, &UMD_InventorySlotWidget::HandleDragEnded);
	}

	// 종료되거나 커서가 떠난 드래그 참조 정리
	HoveredDragOperation.Reset();
	// 강조 이전의 외곽선 색상 복원
	SetDropHighlighted(false);
}

void UMD_InventorySlotWidget::SetDropHighlighted(bool bHighlight)
{
	// 동일 상태에서 반복되는 브러시 갱신 생략
	if (bIsDropHighlighted == bHighlight)
	{
		// 현재 적용한 외곽선 유지
		return;
	}
	// 선택 바인딩이 없는 슬롯의 배경 접근 차단
	if (IsValid(BDR_Background) == false)
	{
		// 실제 적용할 테두리가 없는 상태 기록
		bIsDropHighlighted = false;
		// 사용자 지정 테두리가 없는 카드의 표시 변경 생략
		return;
	}

	// 배경색과 모서리 및 두께를 보존할 현재 브러시 복사
	FSlateBrush Brush = BDR_Background->Background;
	// 강조를 시작하는 순간의 실제 외곽선 색상 보관
	if (bHighlight == true)
	{
		// 강조 종료 후 복원할 현재 디자인 색상 기록
		DefaultOutlineColor = Brush.OutlineSettings.Color;
		// 드롭 대상의 외곽선에 지정한 강조 색상 적용
		Brush.OutlineSettings.Color = FSlateColor(DropHighlightColor);
	}
	else
	{
		// 드래그 전에 사용하던 외곽선 색상 복원
		Brush.OutlineSettings.Color = DefaultOutlineColor;
	}
	// 외곽선만 변경한 브러시를 실제 테두리에 반영
	BDR_Background->SetBrush(Brush);
	// 다음 입력에서 중복 갱신을 피할 현재 강조 상태 기록
	bIsDropHighlighted = bHighlight;
}

void UMD_InventorySlotWidget::HandleDragEnded(UDragDropOperation* Operation)
{
	// 현재 커서가 가리키던 작업의 종료 알림만 처리
	if (HoveredDragOperation.Get() == Operation)
	{
		// 드롭과 취소의 공통 강조 및 알림 정리
		ClearDropHighlight();
	}
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
		// 화면 위젯 생성 이후에도 유지할 아이콘 브러시 설정 복사
		FSlateBrush DragBrush = DragIcon->GetBrush();
		// 보이지 않는 크기를 방지하며 지정한 가로 및 세로 크기 저장
		DragBrush.SetImageSize(FVector2D(FMath::Max(1.0, DragIconSize.X), FMath::Max(1.0, DragIconSize.Y)));
		// 초기 화면 위젯 생성 전에도 유효한 미리보기 크기 반영
		DragIcon->SetBrush(DragBrush);
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
	// 사용자 지정 드롭 호출도 처리 결과와 무관하게 강조 종료
	ClearDropHighlight();
	// 드래그 출처와 현재 목적지에 동일한 유효성 검사 적용
	if (CanAcceptDrop(Operation) == false)
	{
		// 화면 밖 취소와 잘못된 드롭에서 원본 상태 유지
		return false;
	}

	// 실제 드롭된 두 슬롯만 교환하며 클라이언트에서는 서버 확정 요청 전송
	return OwningInventory->SwapSlots(Operation->SourceSlotIndex, SlotIndex);
}
