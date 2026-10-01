// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Equipment/MD_InventorySlotWidget.h"

#include "Components/Border.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/TextBlock.h"
#include "Data/Equipment/MD_ItemDefinition.h"
#include "Engine/Texture2D.h"
#include "UI/Equipment/MD_InventoryDragDropOperation.h"
#include "UI/Equipment/MD_InventoryWidget.h"

void UMD_InventorySlotWidget::NativeDestruct()
{
	// 제거된 카드의 드래그 알림과 강조 표시 정리
	ClearDropHighlight();
	Super::NativeDestruct();
}

FReply UMD_InventorySlotWidget::HandleButtonDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// 버튼 내부에서 감지한 드래그를 UMG 작업 생성과 수명 관리 경로로 전달
	return TakeWidget()->OnDragDetected(InGeometry, InMouseEvent);
}

void UMD_InventorySlotWidget::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
{
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
	if (UpdateDropHighlight(Cast<UMD_InventoryDragDropOperation>(InOperation)) == true)
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
		// 아이템 유효성까지 확인하는 실제 보유 수량 우선 조회
		ItemCount = OwningInventory->GetItemCountAtSlot(SlotIndex);
		// 빈 슬롯의 중복 조회를 생략하고 남아 있는 아이템 참조 해제
		ItemDefinition = ItemCount > 0 ? OwningInventory->GetItemAtSlot(SlotIndex) : nullptr;
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
	// 이름과 아이콘 및 본문 전환에 공통으로 사용할 실제 보유 상태
	bool bHasItem = false;
	// 유효한 아이템 정의와 양수 수량을 가진 슬롯만 채움 상태로 처리
	if (IsValid(ItemDefinition) == true && ItemCount > 0)
	{
		// 공통 표시와 본문 선택에 사용할 보유 상태 기록
		bHasItem = true;
	}
	// 부모 버튼에 실제 이름을 보관하고 동일 텍스트의 위젯 갱신 생략
	Text_ButtonText = bHasItem == true ? ItemDefinition->ItemName : FText::GetEmpty();
	// 부모 표시가 재구성된 경우에도 저장된 이름과 실제 위젯 동기화
	SetSlotText(Txt_ButtonText, Text_ButtonText);
	// 기존 숫자 서식을 재사용한 실제 보유 수량 표시
	SetSlotText(Txt_ToolCount, bHasItem == true ? ItemCountText : FText::GetEmpty());
	// 아이템이 없는 경우 이전 텍스처 참조를 해제할 이미지 리소스
	UTexture2D* ItemIcon = nullptr;
	// 비어 있는 이미지의 단색 사각형을 방지할 그리기 방식
	ESlateBrushDrawType::Type DrawType = ESlateBrushDrawType::NoDrawType;
	// 현재 사용할 수 있는 텍스처에만 이미지 그리기 허용
	if (bHasItem == true && IsValid(ItemDefinition->ItemIcon) == true)
	{
		// 유효한 텍스처만 부모 버튼 브러시에 연결
		ItemIcon = ItemDefinition->ItemIcon;
		// 유효한 아이템 아이콘의 기본 그리기 방식 선택
		DrawType = ESlateBrushDrawType::Image;
	}
	// 동일한 아이콘과 브러시를 반복 적용하는 비용 생략
	if (Brush_ButtonImage.GetResourceObject() != ItemIcon || Brush_ButtonImage.DrawAs != DrawType)
	{
		// 사용자 지정 이미지 크기와 색상을 유지한 리소스 교체
		Brush_ButtonImage.SetResourceObject(ItemIcon);
		// 아이콘 유무에 따라 실제 그리기만 전환
		Brush_ButtonImage.DrawAs = DrawType;
		// 부모 버튼의 공통 이미지 바인딩에 변경 브러시 반영
		ApplyContentImageBrush();
	}
	// 아이템 유효성과 수량 판정 결과를 BP의 본문 및 디자인 갱신에 전달
	OnSlotRefreshed(bHasItem);
	// 드래그 또는 남아 있는 강조가 있을 때만 교환 가능 상태 재검사
	if (HoveredDragOperation.IsValid() == true || bIsDropHighlighted == true)
	{
		// 아이템 소진과 교체 또는 종료된 작업에 맞춰 강조 갱신
		UpdateDropHighlight(HoveredDragOperation.Get());
	}
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
	// 비활성 화면과 같은 위치의 드롭은 출처와 아이템 조회 전에 차단
	if (IsValid(Operation) == false || IsValid(OwningInventory) == false || OwningInventory->IsActivated() == false
		|| Operation->SourceSlotIndex == SlotIndex)
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

	// 세션 식별자와 함께 원래 화면이 일치하는 작업만 허용
	if (Operation->SourceInventory.IsValid() == false || Operation->SourceInventory.Get() != OwningInventory)
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
	// 강조 중인 실제 외곽선이 달라질 때만 브러시 복사와 Slate 갱신 수행
	if (bIsDropHighlighted == true && IsValid(BDR_Root) == true
		&& BDR_Root->Background.OutlineSettings.Color != FSlateColor(DropHighlightColor))
	{
		// 강조 종료 시 복원할 원래 색상과 현재 배경 디자인 유지
		FSlateBrush Brush = BDR_Root->Background;
		// 외곽선에 새로 지정한 강조 색상 적용
		Brush.OutlineSettings.Color = FSlateColor(DropHighlightColor);
		// 현재 드래그가 끝나기 전에도 변경 색상 표시
		BDR_Root->SetBrush(Brush);
	}
}

bool UMD_InventorySlotWidget::UpdateDropHighlight(UMD_InventoryDragDropOperation* Operation)
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
		// 두 종료 알림에서 재사용할 작업을 약한 참조에서 한 번 조회
		UMD_InventoryDragDropOperation* Operation = HoveredDragOperation.Get();
		// 해당 카드가 등록한 드롭 완료 알림 제거
		Operation->OnDrop.RemoveDynamic(this, &UMD_InventorySlotWidget::HandleDragEnded);
		// 해당 카드가 등록한 취소 알림 제거
		Operation->OnDragCancelled.RemoveDynamic(this, &UMD_InventorySlotWidget::HandleDragEnded);
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
	if (IsValid(BDR_Root) == false)
	{
		// 실제 적용할 테두리가 없는 상태 기록
		bIsDropHighlighted = false;
		// 사용자 지정 테두리가 없는 카드의 표시 변경 생략
		return;
	}

	// 배경색과 모서리 및 두께를 보존할 현재 브러시 복사
	FSlateBrush Brush = BDR_Root->Background;
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
	BDR_Root->SetBrush(Brush);
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
	// 교환할 원본의 고정 Hotbar 위치 보관
	Operation->SourceSlotIndex = SlotIndex;
	// 드래그 시작 당시의 실제 아이템 정의 보관
	Operation->SourceItem = OwningInventory->GetItemAtSlot(SlotIndex);
	// 마우스 위치를 따라 이동할 카드 중심 기준 설정
	Operation->Pivot = EDragPivot::CenterCenter;
	// 원본 카드를 목록에서 떼어내지 않고 같은 BP의 독립 표시 인스턴스 생성
	UMD_InventorySlotWidget* DragSlot = CreateWidget<UMD_InventorySlotWidget>(this, GetClass());
	// 실제 슬롯 UI를 만들 수 없는 경우 불완전한 드래그 시작 차단
	if (IsValid(DragSlot) == false)
	{
		// 원본 슬롯을 그대로 유지한 생성 실패 반환
		return nullptr;
	}
	// 원본과 동일한 번호 및 아이템과 수량 연결
	DragSlot->InitializeSlot(OwningInventory, SlotIndex);
	// 사용자 BP의 현재 선택 표시를 미리보기에도 동일하게 적용
	DragSlot->SetIsSelected(GetSelected(), false);
	// 원본 카드에 적용한 아이콘 크기와 색상 보존
	DragSlot->SetContentImage(Brush_ButtonImage);
	// 글꼴과 내부 간격을 동일하게 배치할 원본 카드의 로컬 크기
	FVector2D SlotSize = GetCachedGeometry().GetLocalSize();
	// 화면 배율을 포함한 실제 카드 크기를 드래그 장식의 UI 좌표로 환산
	FVector2D DragSize = GetCachedGeometry().GetAbsoluteSize() / FMath::Max(UWidgetLayoutLibrary::GetViewportScale(this), UE_SMALL_NUMBER);
	// 아직 배치되지 않은 슬롯은 레이아웃 계산 결과를 크기로 사용
	if (SlotSize.X <= 0.0 || SlotSize.Y <= 0.0)
	{
		// 아이콘과 이름 및 배경의 실제 필요 크기 계산
		ForceLayoutPrepass();
		// 첫 프레임에도 크기가 있는 카드 미리보기 구성
		SlotSize = GetDesiredSize();
		// 배치 전에는 원래 필요한 크기를 기준으로 표시
		DragSize = SlotSize;
	}
	// 원본 로컬 크기에 맞춰 글꼴과 내부 여백을 동일하게 배치
	USizeBox* DragContent = NewObject<USizeBox>(Operation);
	// 화면 배율 적용 전의 카드 가로 크기 보존
	DragContent->SetWidthOverride(FMath::Max(1.0, SlotSize.X));
	// 화면 배율 적용 전의 카드 세로 크기 보존
	DragContent->SetHeightOverride(FMath::Max(1.0, SlotSize.Y));
	// 원본과 동일한 슬롯 BP를 로컬 크기 영역에 연결
	DragContent->AddChild(DragSlot);
	// 인벤토리의 화면 배율을 전체 카드에 동일하게 적용할 스케일 영역
	UScaleBox* DragScale = NewObject<UScaleBox>(Operation);
	// 카드 내부 배치를 바꾸지 않고 전체 비율을 유지한 크기 조절
	DragScale->SetStretch(EStretch::ScaleToFit);
	// 원본 크기의 슬롯을 화면 크기에 맞추는 스케일 영역에 연결
	DragScale->AddChild(DragContent);
	// 가로 컨테이너를 벗어난 미리보기의 크기를 원본과 동일하게 고정
	USizeBox* DragVisual = NewObject<USizeBox>(Operation);
	// 원본 카드의 가로 크기를 유지하며 영 크기 방지
	DragVisual->SetWidthOverride(FMath::Max(1.0, DragSize.X));
	// 원본 카드의 세로 크기를 유지하며 영 크기 방지
	DragVisual->SetHeightOverride(FMath::Max(1.0, DragSize.Y));
	// 슬롯 전체의 글꼴과 내부 간격을 포함한 화면 배율 적용
	DragVisual->AddChild(DragScale);
	// 배경과 번호 및 아이콘과 수량 전체의 불투명도 적용
	DragVisual->SetRenderOpacity(0.6f);
	// 미리보기 자식까지 입력 판정을 차단하여 목적지 드롭 허용
	DragVisual->SetVisibility(ESlateVisibility::HitTestInvisible);
	// 드래그 작업이 끝날 때까지 카드 미리보기 수명 유지
	Operation->DefaultDragVisual = DragVisual;
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
