// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Equipment/MD_InventoryWidget.h"

#include "Character/MD_Character.h"
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

void UMD_InventoryWidget::SelectSlot(UMD_InventorySlotWidget* SlotWidget)
{
	// 비활성 화면과 해제된 보관함 및 제거된 카드의 입력 차단
	if (IsActivated() == false || IsValid(BoundHotbar) == false || IsValid(SlotWidget) == false)
	{
		// 현재 화면에 적용할 수 없는 요청 종료
		return;
	}
	// 실제 카드 목록과 장비 선택에서 공유할 고정 위치 조회
	int32 SlotIndex = SlotWidget->GetSlotIndex();
	// 드래그 미리보기와 재생성 이전 카드의 장비 선택 차단
	if (SlotWidgets.IsValidIndex(SlotIndex) == true && SlotWidgets[SlotIndex] == SlotWidget)
	{
		// 숫자키 입력을 처리하는 현재 소유 캐릭터 조회
		AMD_Character* Character = Cast<AMD_Character>(GetOwningPlayerPawn());
		// 빙의가 바뀐 직후 이전 보관함에 입력하는 상태 차단
		if (IsValid(Character) == true && Character->GetHotbarComponent() == BoundHotbar)
		{
			// 조준 종료와 장비 변경 및 서버 요청의 기존 경로 재사용
			Character->SelectHotbarSlot(SlotIndex);
		}
	}
	// 같은 슬롯 재클릭과 거절된 요청도 실제 선택 상태로 표시 복원
	HandleHotbarSelectionChanged();
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
	// 화면의 입력 가능 상태만 검사하고 슬롯 조합은 Hotbar에서 최종 검사
	if (IsActivated() == false || IsValid(BoundHotbar) == false)
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
	// Pawn 연결 대기 중에는 디자이너의 카드 배치를 빈 상태로 유지
	if (IsValid(BoundHotbar) == false)
	{
		// 재연결 시 같은 카드와 여백을 복원할 기존 자리 수 보존
		SlotCount = HB_Slots->GetChildrenCount();
	}
	// 실제 컨테이너와 저장된 카드 목록의 개수 일치 여부 확인
	bool bRebuildSlots = SlotWidgets.Num() != SlotCount || HB_Slots->GetChildrenCount() != SlotCount;
	// 개수가 같아도 외부에서 제거하거나 교체한 카드의 연결 검사
	if (bRebuildSlots == false)
	{
		// Hotbar 순서와 실제 컨테이너 순서의 일치 여부 확인
		for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
		{
			// 잘못된 인스턴스 또는 다른 위치에 연결된 카드 검사
			if (IsValid(SlotWidgets[SlotIndex]) == false || HB_Slots->GetChildAt(SlotIndex) != SlotWidgets[SlotIndex]
				|| SlotWidgets[SlotIndex]->GetClass() != SlotWidgetClass || SlotWidgets[SlotIndex]->GetSlotIndex() != SlotIndex)
			{
				// 불일치가 있는 목록의 전체 연결 복원 요청
				bRebuildSlots = true;
				// 한 개의 불일치만으로 재생성 필요 여부 확정
				break;
			}
		}
	}
	// 디자이너에 이미 배치된 슬롯은 간격과 인스턴스 설정을 보존하여 재사용
	if (bRebuildSlots == true && HB_Slots->GetChildrenCount() == SlotCount)
	{
		// 실제 가로 컨테이너 순서로 카드 참조 다시 구성
		SlotWidgets.Reset(SlotCount);
		// 기존 자식이 모두 현재 슬롯 클래스인지 확인
		for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
		{
			// 원본 위젯 트리의 슬롯 인스턴스 조회
			UMD_InventorySlotWidget* SlotWidget = Cast<UMD_InventorySlotWidget>(HB_Slots->GetChildAt(SlotIndex));
			// 다른 위젯이나 변경된 슬롯 클래스가 섞인 배치의 재사용 중단
			if (IsValid(SlotWidget) == false || SlotWidget->GetClass() != SlotWidgetClass)
			{
				// 불완전한 참조 목록으로 재사용하는 상태 방지
				break;
			}
			// 실제 배치 순서와 Hotbar 번호 연결
			SlotWidget->InitializeSlot(this, SlotIndex);
			// 이후 내용 변경에서 재사용할 카드 보관
			SlotWidgets.Add(SlotWidget);
		}
		// 모든 슬롯의 연결이 유효한 경우에만 기존 배치 유지
		bRebuildSlots = SlotWidgets.Num() != SlotCount;
	}
	// 표시 갱신 중에는 카드를 재사용하고 목록 연결이 바뀐 경우에만 재생성
	if (bRebuildSlots == true)
	{
		// 이전 Pawn 또는 다른 보관 공간의 슬롯 제거
		HB_Slots->ClearChildren();
		// 새 카드 수만큼 용량을 확보하고 기존 배열 저장 공간 재사용
		SlotWidgets.Reset(SlotCount);
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
			// 새로 생성한 슬롯에도 현재 디자이너의 자동 폭과 여백 적용
			if (IsValid(HorizontalSlot) == true)
			{
				// 각 카드의 원래 표시 크기를 유지하는 자동 폭 적용
				HorizontalSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
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
		// 구매와 소비 및 재배치에서만 슬롯 내용 갱신 알림 구독
		BoundHotbar->OnHotbarContentsChanged.AddUniqueDynamic(this, &UMD_InventoryWidget::HandleHotbarChanged);
		// 숫자키와 복제 및 슬롯 교환에 따른 사용 위치 변경 추적
		BoundHotbar->OnHotbarSelectionChanged.AddUniqueDynamic(this, &UMD_InventoryWidget::HandleHotbarSelectionChanged);
	}
}

void UMD_InventoryWidget::UnbindHotbar()
{
	// 화면 종료와 Pawn 변경 시 남아 있는 카드 강조 정리
	for (UMD_InventorySlotWidget* SlotWidget : SlotWidgets)
	{
		// 현재 남아 있는 카드에만 드래그 정리 요청
		if (IsValid(SlotWidget) == true)
		{
			// 재활성화 후 이전 드롭 대상 표시가 남는 상태 방지
			SlotWidget->ClearDropHighlight();
			// 이전 Pawn의 사용 슬롯 표시가 남는 상태 방지
			if (SlotWidget->GetSelected() == true)
			{
				// 공통 버튼의 선택 해제 이벤트로 기존 BP 표시 정리
				SlotWidget->ClearSelection();
			}
		}
	}
	// 이전 연결이나 닫기 이전에 시작한 드래그의 재사용 차단
	DragSessionId = FGuid::NewGuid();
	// 기존 Pawn의 컴포넌트가 살아 있는 경우에만 연결 해제
	if (IsValid(BoundHotbar) == true)
	{
		// 해당 인벤토리 화면이 등록한 변경 알림 제거
		BoundHotbar->OnHotbarContentsChanged.RemoveDynamic(this, &UMD_InventoryWidget::HandleHotbarChanged);
		// 닫힌 화면에 전달되는 선택 알림 연결 해제
		BoundHotbar->OnHotbarSelectionChanged.RemoveDynamic(this, &UMD_InventoryWidget::HandleHotbarSelectionChanged);
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
		// 최초 활성화와 카드 재생성 및 슬롯 이동 이후 사용 위치 반영
		HandleHotbarSelectionChanged();
	}
}

void UMD_InventoryWidget::HandleHotbarSelectionChanged()
{
	// 비활성 인벤토리의 불필요한 선택 표시 갱신 방지
	if (IsActivated() == false)
	{
		// 닫힌 화면의 갱신 종료
		return;
	}
	// Pawn이 없는 상태에서는 모든 카드의 선택 해제
	int32 SelectedSlotIndex = INDEX_NONE;
	// 예측 입력과 서버 복제가 공유하는 실제 사용 위치 조회
	if (IsValid(BoundHotbar) == true)
	{
		// 현재 보관함의 선택 위치 반영
		SelectedSlotIndex = BoundHotbar->GetCurrentSlotIndex();
	}
	// 내용과 Slate 인스턴스를 유지하며 선택 표시만 동기화
	for (UMD_InventorySlotWidget* SlotWidget : SlotWidgets)
	{
		// 제거된 카드의 선택 이벤트 호출 차단
		if (IsValid(SlotWidget) == true)
		{
			// 현재 사용 위치에 해당하는 카드 판정
			bool bSelected = SlotWidget->GetSlotIndex() == SelectedSlotIndex;
			// 변경된 카드에만 기존 BP 선택 및 해제 이벤트 전달
			if (SlotWidget->GetSelected() != bSelected)
			{
				// 실제 사용 슬롯의 선택 표시 활성화
				if (bSelected == true)
				{
					// 입력 피드백 없이 실제 장착 상태만 표시
					SlotWidget->SetIsSelected(true, false);
				}
				else
				{
					// 토글 불가능한 카드도 실제 장비 전환에 따라 선택 해제
					SlotWidget->ClearSelection();
				}
			}
		}
	}
}
