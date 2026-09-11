// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Equipment/MD_InventoryWidget.h"

#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Data/Equipment/MD_ItemDefinition.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "UI/Equipment/MD_InventorySlotWidget.h"

void UMD_InventoryWidget::NativeOnActivated()
{
	// 재활성화 시 이전 알림 연결의 중복 등록 방지
	UnbindInventory();
	// 인벤토리 입력과 Pawn 교체 알림에 사용할 소유 컨트롤러 조회
	APlayerController* OwningController = GetOwningPlayer();
	// 로컬 플레이어 화면에서만 소유 Pawn 변경 추적
	if (IsValid(OwningController) == true && OwningController->IsLocalController() == true)
	{
		// 화면 종료 시 동일 컨트롤러의 알림 해제를 위한 참조 보관
		BoundController = OwningController;
		// 화면이 열린 동안 리스폰과 빙의 변경 추적
		OwningController->OnPossessedPawnChanged.AddUniqueDynamic(this, &UMD_InventoryWidget::HandlePossessedPawnChanged);
		// 활성화 이벤트 전에 현재 보관 상태 연결
		BindToPawn(OwningController->GetPawn());
	}

	Super::NativeOnActivated();
	// 초기 빈 상태를 포함한 인벤토리 슬롯 표시 갱신
	HandleHotbarChanged();
}

void UMD_InventoryWidget::NativeOnDeactivated()
{
	// 닫힌 화면의 불필요한 데이터 갱신 방지
	UnbindInventory();
	Super::NativeOnDeactivated();
}

void UMD_InventoryWidget::NativeDestruct()
{
	// 비활성화 없이 제거되는 화면의 알림 연결 정리
	UnbindInventory();
	Super::NativeDestruct();
}

int32 UMD_InventoryWidget::GetSlotCount()
{
	// Pawn 해제 상태에서 빈 인벤토리 표시
	if (IsValid(BoundHotbar) == false)
	{
		// 연결된 보관 공간이 없는 상태 반환
		return 0;
	}

	// 실제 Hotbar와 동일한 슬롯 개수 반환
	return BoundHotbar->GetSlotCount();
}

int32 UMD_InventoryWidget::GetCurrentSlotIndex()
{
	// 연결된 Hotbar가 없는 상태의 선택 표시 해제
	if (IsValid(BoundHotbar) == false)
	{
		// 장착 위치가 없는 상태 반환
		return INDEX_NONE;
	}

	// 재배치 이후에도 실제 장착 아이템의 위치 반환
	return BoundHotbar->GetCurrentSlotIndex();
}

UMD_ItemDefinition* UMD_InventoryWidget::GetItemAtSlot(int32 SlotIndex)
{
	// Pawn 해제 이후 아이템 데이터 접근 차단
	if (IsValid(BoundHotbar) == false)
	{
		// 표시할 아이템이 없는 상태 반환
		return nullptr;
	}

	// 실제 슬롯의 표시용 아이템 정의 반환
	return BoundHotbar->GetItemAtSlot(SlotIndex);
}

int32 UMD_InventoryWidget::GetItemCountAtSlot(int32 SlotIndex)
{
	// 보관 공간이 없을 때 이전 수량 표시 제거
	if (IsValid(BoundHotbar) == false)
	{
		// 표시할 보유 수량 초기값 반환
		return 0;
	}

	// 아이템 이동과 함께 보존된 실제 슬롯 수량 반환
	return BoundHotbar->GetItemCountAtSlot(SlotIndex);
}

bool UMD_InventoryWidget::CanSwapSlots(int32 FromSlotIndex, int32 ToSlotIndex)
{
	// 비활성 화면과 해제된 Pawn의 재배치 입력 차단
	if (IsActivated() == false || IsValid(BoundHotbar) == false)
	{
		// 현재 입력을 적용할 수 없는 상태 반환
		return false;
	}

	// Hotbar와 동일한 원본 아이템 및 목적지 범위 검사 결과 반환
	return BoundHotbar->CanSwapItemSlots(FromSlotIndex, ToSlotIndex);
}

bool UMD_InventoryWidget::SwapSlots(int32 FromSlotIndex, int32 ToSlotIndex)
{
	// 드롭 또는 교환 버튼이 전달한 슬롯 조합 검사
	if (CanSwapSlots(FromSlotIndex, ToSlotIndex) == false)
	{
		// 처리할 수 없는 위치 교환 거절
		return false;
	}

	// 수량을 포함한 두 슬롯의 서버 교환 요청 전달
	return BoundHotbar->SwapItemSlots(FromSlotIndex, ToSlotIndex);
}

void UMD_InventoryWidget::RefreshSlots()
{
	// 가로 컨테이너와 슬롯 클래스 준비 여부 확인
	if (IsValid(HB_Slots) == false || IsValid(SlotWidgetClass) == false)
	{
		// 필수 위젯 구성 전 자동 슬롯 생성 중단
		return;
	}

	// 슬롯 생성과 표시 갱신에 공통 사용할 실제 슬롯 수 조회
	int32 SlotCount = GetSlotCount();
	// 실제 컨테이너와 저장된 카드 목록의 개수 일치 여부 확인
	bool bRebuildSlots = SlotWidgets.Num() != SlotCount || HB_Slots->GetChildrenCount() != SlotCount;
	// 개수가 같아도 외부에서 제거하거나 교체한 카드의 연결 검사
	if (bRebuildSlots == false)
	{
		// Hotbar 순서와 실제 컨테이너 순서의 일치 여부 확인
		for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
		{
			// 잘못된 인스턴스 또는 다른 위치에 연결된 카드 검사
			if (IsValid(SlotWidgets[SlotIndex]) == false || HB_Slots->GetChildAt(SlotIndex) != SlotWidgets[SlotIndex])
			{
				// 불일치가 있는 목록의 전체 연결 복원 요청
				bRebuildSlots = true;
				// 한 개의 불일치만으로 재생성 필요 여부 확정
				break;
			}
		}
	}
	// 표시 갱신 중에는 카드를 재사용하고 목록 연결이 바뀐 경우에만 재생성
	if (bRebuildSlots == true)
	{
		// 이전 Pawn 또는 다른 보관 공간의 슬롯 제거
		HB_Slots->ClearChildren();
		// 새 슬롯 인스턴스 구성을 위한 참조 초기화
		SlotWidgets.Reset();
		// Hotbar 인덱스와 동일한 순서로 가로 카드 생성
		for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
		{
			// 사용자가 지정한 슬롯 위젯 인스턴스 생성
			UMD_InventorySlotWidget* SlotWidget = CreateWidget<UMD_InventorySlotWidget>(this, SlotWidgetClass);
			// 생성할 수 없는 슬롯 위젯의 후속 접근 차단
			if (IsValid(SlotWidget) == false)
			{
				// 일부 슬롯만 남는 잘못된 표시 제거
				HB_Slots->ClearChildren();
				// 다음 데이터 갱신에서 전체 슬롯 구성을 다시 시도하도록 참조 초기화
				SlotWidgets.Reset();
				// 불완전한 슬롯 생성 종료
				return;
			}

			// 슬롯의 데이터 조회와 드롭 대상 인덱스 연결
			SlotWidget->InitializeSlot(this, SlotIndex);
			// Hotbar 번호 순서의 가로 컨테이너 배치
			UHorizontalBoxSlot* HorizontalSlot = HB_Slots->AddChildToHorizontalBox(SlotWidget);
			// 생성한 컨테이너 슬롯의 카드 폭 균등 분배
			if (IsValid(HorizontalSlot) == true)
			{
				// 가로 공간을 전체 Hotbar 슬롯에 동일 비율로 할당
				HorizontalSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				// 화면 기본 설정에 지정한 카드 사이 여백 적용
				HorizontalSlot->SetPadding(SlotPadding);
			}
			// 이후 구매와 선택 갱신에 사용할 카드 인스턴스 보관
			SlotWidgets.Add(SlotWidget);
		}
	}

	// 기존 슬롯 인스턴스를 유지하며 아이템과 수량 표시 갱신
	for (UMD_InventorySlotWidget* SlotWidget : SlotWidgets)
	{
		// 제거된 카드의 데이터 접근 차단
		if (IsValid(SlotWidget) == true)
		{
			// 실제 Hotbar의 현재 슬롯 데이터 표시
			SlotWidget->RefreshSlot();
		}
	}
}

void UMD_InventoryWidget::BindToPawn(APawn* NewPawn)
{
	// 이전 Pawn에서 받던 보관 상태 알림 해제
	UnbindHotbar();
	// Pawn이 없는 리스폰 대기 상태의 접근 차단
	if (IsValid(NewPawn) == false)
	{
		// 비어 있는 인벤토리 연결 상태 유지
		return;
	}

	// 캐릭터 구현에 종속되지 않는 실제 Hotbar 컴포넌트 연결
	BoundHotbar = NewPawn->FindComponentByClass<UMD_EquipHotbarComponent>();
	// Hotbar를 보유한 Pawn의 변경 알림만 등록
	if (IsValid(BoundHotbar) == true)
	{
		// 구매와 소비 및 선택과 재배치의 공통 갱신 알림 구독
		BoundHotbar->OnHotbarChanged.AddUniqueDynamic(this, &UMD_InventoryWidget::HandleHotbarChanged);
	}
}

void UMD_InventoryWidget::UnbindHotbar()
{
	// 이전 연결이나 닫기 이전에 시작한 드래그의 재사용 차단
	DragSessionId = FGuid::NewGuid();
	// 기존 Pawn의 컴포넌트가 살아 있는 경우에만 연결 해제
	if (IsValid(BoundHotbar) == true)
	{
		// 해당 인벤토리 화면이 등록한 변경 알림 제거
		BoundHotbar->OnHotbarChanged.RemoveDynamic(this, &UMD_InventoryWidget::HandleHotbarChanged);
	}

	// 해제된 보관 공간의 후속 조회와 입력 차단
	BoundHotbar = nullptr;
}

void UMD_InventoryWidget::UnbindInventory()
{
	// 기존 Hotbar 변경 알림과 데이터 참조 정리
	UnbindHotbar();
	// 실제 등록했던 컨트롤러가 남아 있는 경우에만 연결 해제
	if (BoundController.IsValid() == true)
	{
		// 화면이 닫힌 동안의 Pawn 변경 콜백 제거
		BoundController->OnPossessedPawnChanged.RemoveDynamic(this, &UMD_InventoryWidget::HandlePossessedPawnChanged);
	}

	// 다음 활성화에서 새 소유 컨트롤러 연결을 위한 참조 초기화
	BoundController.Reset();
}

void UMD_InventoryWidget::HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	// 빙의 변경 이후 새 Pawn의 보관 상태 연결
	BindToPawn(NewPawn);
	// Pawn 해제로 비어진 상태까지 화면에 전달
	HandleHotbarChanged();
}

void UMD_InventoryWidget::HandleHotbarChanged()
{
	// 활성 화면에서만 실제 데이터를 조회하여 표시 갱신
	if (IsActivated() == true)
	{
		// 가로 카드 목록과 슬롯별 기본 표시 갱신
		RefreshSlots();
		// 슬롯 이름과 아이콘 및 수량 표시의 갱신 요청
		OnInventoryRefreshed();
	}
}
