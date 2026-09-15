// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Equipment/MD_EquipHotbarWidget.h"
#include "Character/MD_Character.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"

void UMD_EquipHotbarWidget::NativeConstruct()
{
	// 같은 위젯의 재구성 이전에 남은 모든 장비 알림 구독 정리
	UnbindPlayer();
	Super::NativeConstruct();

	// 컨트롤러보다 오래 유지되는 로컬 플레이어의 변경 알림 등록
	if (IsValid(GetOwningLocalPlayer()) == true)
	{
		// 위젯 종료 시 원래 알림 제공자에서 구독 해제를 위한 보관
		BoundLocalPlayer = GetOwningLocalPlayer();
		// 로컬 플레이어의 델리게이트가 위젯 수명을 늘리지 않는 약한 참조
		TWeakObjectPtr<UMD_EquipHotbarWidget> WeakWidget(this);
		// UMG SetPlayerContext의 RemoveAll(this)에 함께 제거되지 않는 핸들 기반 구독
		PlayerControllerChangedHandle = BoundLocalPlayer->OnPlayerControllerChanged().AddLambda([WeakWidget](APlayerController* NewController)
		{
			// 이미 폐기된 위젯에 대한 월드 전환 알림 차단
			if (WeakWidget.IsValid() == true)
			{
				// 새 월드의 소유 컨트롤러와 Hotbar로 연결 교체
				WeakWidget->HandlePlayerControllerChanged(NewController);
			}
		});
	}

	// 최초 표시와 화면 재부착 시 현재 Pawn의 선택 상태 즉시 반영
	BindToController(GetOwningPlayer());
}

void UMD_EquipHotbarWidget::NativeDestruct()
{
	// 변경된 OwningPlayer 대신 실제 등록한 대상에서 모든 알림 해제
	UnbindPlayer();
	Super::NativeDestruct();
}

void UMD_EquipHotbarWidget::HandlePlayerControllerChanged(APlayerController* NewController)
{
	// LocalUIManager 처리 순서와 무관하게 조회 대상을 새 월드로 교체
	if (IsValid(NewController) == true)
	{
		// 내장 슬롯 위젯까지 새 플레이어 컨텍스트 전달
		SetPlayerContext(FLocalPlayerContext(NewController));
	}

	// 컨트롤러 준비 전 빈 상태를 포함한 장비 알림 재연결
	BindToController(NewController);
}

void UMD_EquipHotbarWidget::BindToController(APlayerController* NewController)
{
	// 소유 컨트롤러가 교체되어도 이전 Pawn 변경 알림을 남기지 않는 정리
	if (BoundController.IsValid() == true)
	{
		// 이전 컨트롤러의 리스폰 알림이 현재 UI에 간섭하지 않도록 해제
		BoundController->OnPossessedPawnChanged.RemoveDynamic(this, &UMD_EquipHotbarWidget::HandlePossessedPawnChanged);
	}

	// 이후 컨트롤러 교체와 위젯 종료에서 해제할 실제 구독 대상 보관
	BoundController = NewController;
	// 유효한 새 컨트롤러에 대한 리스폰 및 빙의 변경 추적
	if (IsValid(NewController) == true)
	{
		// 중복 초기화에도 Pawn 변경 이벤트의 단일 구독 유지
		NewController->OnPossessedPawnChanged.AddUniqueDynamic(this, &UMD_EquipHotbarWidget::HandlePossessedPawnChanged);
		// 이미 Pawn이 준비된 월드 전환의 즉시 장비 상태 동기화
		BindToPawn(NewController->GetPawn());
		// 연결 완료 후 빈 Pawn 처리 방지
		return;
	}

	// 컨트롤러가 없는 전환 구간에서 이전 월드의 장비 참조 해제
	BindToPawn(nullptr);
}

void UMD_EquipHotbarWidget::UnbindPlayer()
{
	// UMG의 소유 정보 변경 이후에도 원래 로컬 플레이어에서 구독 해제
	if (BoundLocalPlayer.IsValid() == true)
	{
		// 엔진 내부의 위젯 구독과 구분하여 사용자 정의 알림만 제거
		BoundLocalPlayer->OnPlayerControllerChanged().Remove(PlayerControllerChangedHandle);
	}
	// 재생성 시 이미 해제된 구독 핸들의 재사용 방지
	PlayerControllerChangedHandle.Reset();
	// 이전 로컬 플레이어 추적 종료
	BoundLocalPlayer.Reset();

	// 실제 바인딩했던 컨트롤러의 Pawn 교체 알림 정리
	if (BoundController.IsValid() == true)
	{
		// 제거된 위젯으로 전달되는 리스폰 이벤트 차단
		BoundController->OnPossessedPawnChanged.RemoveDynamic(this, &UMD_EquipHotbarWidget::HandlePossessedPawnChanged);
	}
	// 이전 컨트롤러 추적 종료
	BoundController.Reset();
	// 이전 월드의 Hotbar 알림 및 강한 참조 해제
	UnbindHotbar();
}

void UMD_EquipHotbarWidget::HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	BindToPawn(NewPawn);
}

void UMD_EquipHotbarWidget::HandleHotbarContentsChanged()
{
	// 내용 조회 전에 현재 소유 월드와 컨트롤러 동기화
	RefreshPlayerContext();
	// 슬롯 아이콘과 보관 수량의 갱신 알림 전달
	OnHotbarRefreshed();
}

void UMD_EquipHotbarWidget::HandleHotbarSelectionChanged()
{
	// 선택 전환만 발생하는 월드 이동에서도 현재 컨텍스트 복원
	RefreshPlayerContext();
	// 현재 선택 위치만 별도의 선택 표시 이벤트에 전달
	OnHotbarSelectionChanged(GetCurrentSlotIndex());
}

void UMD_EquipHotbarWidget::RefreshPlayerContext()
{
	// 같은 컨트롤러가 새 월드로 이동한 경우 남은 이전 컨텍스트 감지
	if (BoundController.IsValid() == true &&
		(GetWorld() != BoundController->GetWorld() || GetOwningPlayer() != BoundController.Get()))
	{
		// 내장 위젯까지 현재 월드의 플레이어 컨텍스트 동기화
		SetPlayerContext(FLocalPlayerContext(BoundController.Get()));
	}
}

void UMD_EquipHotbarWidget::BindToPawn(APawn* NewPawn)
{
	UnbindHotbar();

	AMD_Character* OwningCharacter = Cast<AMD_Character>(NewPawn);
	// 유효한 새 캐릭터에 한정한 장비 컴포넌트 연결
	if (IsValid(OwningCharacter) == false)
	{
		// 장비가 없는 상태의 내용 및 선택 표시 초기 동기화
		HandleHotbarContentsChanged();
		// 이전 선택 위치를 별도 선택 이벤트에서 해제
		HandleHotbarSelectionChanged();
		return;
	}

	UMD_EquipHotbarComponent* Hotbar = OwningCharacter->GetHotbarComponent();
	// 새 Pawn에 장비가 없는 상태의 이전 연결 재사용 방지
	if (IsValid(Hotbar) == false)
	{
		// 장비가 없는 상태의 내용 및 선택 표시 초기 동기화
		HandleHotbarContentsChanged();
		// 이전 선택 위치를 별도 선택 이벤트에서 해제
		HandleHotbarSelectionChanged();
		return;
	}

	BoundHotbar = Hotbar;
	// 아이템 및 수량 변경을 내용 표시 이벤트에만 연결
	BoundHotbar->OnHotbarContentsChanged.AddUniqueDynamic(this, &UMD_EquipHotbarWidget::HandleHotbarContentsChanged);
	// 선택 위치 변경을 선택 표시 이벤트에만 연결
	BoundHotbar->OnHotbarSelectionChanged.AddUniqueDynamic(this, &UMD_EquipHotbarWidget::HandleHotbarSelectionChanged);

	// 최초 생성과 장비 재연결 시 현재 슬롯 내용 동기화
	HandleHotbarContentsChanged();
	// 내용 갱신과 별도로 현재 선택 위치의 초기 표시 동기화
	HandleHotbarSelectionChanged();
}

void UMD_EquipHotbarWidget::UnbindHotbar()
{
	// 해제 시점에 살아 있는 장비 컴포넌트에서만 이벤트 제거
	if (BoundHotbar.IsValid() == true)
	{
		// 이전 장비의 아이템 및 수량 알림 해제
		BoundHotbar->OnHotbarContentsChanged.RemoveDynamic(this, &UMD_EquipHotbarWidget::HandleHotbarContentsChanged);
		// 이전 장비의 선택 변경 알림 해제
		BoundHotbar->OnHotbarSelectionChanged.RemoveDynamic(this, &UMD_EquipHotbarWidget::HandleHotbarSelectionChanged);
	}

	// 회수된 장비에도 안전한 약한 참조 초기화
	BoundHotbar.Reset();
}

int32 UMD_EquipHotbarWidget::GetSlotCount() const
{
	// 회수되지 않은 현재 장비의 슬롯 수 조회
	return BoundHotbar.IsValid() == true ? BoundHotbar->GetSlotCount() : 0;
}

int32 UMD_EquipHotbarWidget::GetCurrentSlotIndex() const
{
	// 장비 회수 또는 연결 해제 상태를 제외한 현재 선택 위치 조회
	return BoundHotbar.IsValid() == true ? BoundHotbar->GetCurrentSlotIndex() : INDEX_NONE;
}

UMD_ItemDefinition* UMD_EquipHotbarWidget::GetItemAtSlot(int32 SlotIndex)
{
	// 바인딩된 공통 Hotbar에서 해당 위치의 아이템 조회
	return BoundHotbar.IsValid() == true ? BoundHotbar->GetItemAtSlot(SlotIndex) : nullptr;
}

int32 UMD_EquipHotbarWidget::GetItemCountAtSlot(int32 SlotIndex)
{
	// 바인딩된 공통 Hotbar에서 해당 위치의 보유 수량 조회
	return BoundHotbar.IsValid() == true ? BoundHotbar->GetItemCountAtSlot(SlotIndex) : 0;
}
