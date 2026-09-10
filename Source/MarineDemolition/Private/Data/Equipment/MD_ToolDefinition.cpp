// Fill out your copyright notice in the Description page of Project Settings.

#include "Data/Equipment/MD_ToolDefinition.h"
#include "Util/MD_LogUtility.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "GameFramework/Actor.h"

bool UMD_ToolDefinition::CanGrant_Implementation(FMD_ItemGrantContext Context)
{
	// 도구 추가 및 업그레이드 조건에 앞서 공통 지급 데이터 검사
	if (Super::CanGrant_Implementation(Context) == false)
	{
		// 선행 검사에서 기록한 실패 결과의 전달
		return false;
	}
	// 도구 수량을 하나로 제한하는 정의 수준의 검사
	if (Context.Quantity != 1)
	{
		// 실패 원인의 프로젝트 로그 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 도구는 한 번에 1개만 지급 가능함 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
		// 도구의 일괄 지급 요청 거부
		return false;
	}
	// 도구 사용에 필요한 메시와 GA 식별 태그 확인
	if (IsValid(ToolMesh) == false || ToolAbilityTag.IsValid() == false)
	{
		// 실패 원인의 프로젝트 로그 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s ToolMesh 또는 ToolAbilityTag가 유효하지 않음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
		// 장착 및 실행 설정 오류 전달
		return false;
	}
	// 최대 충전량과 초기 충전량의 유한 값 및 허용 범위 검사
	if (FMath::IsFinite(MaxCharge) == false || MaxCharge < 0.0f
		// 최대 충전량이 유효한 경우에만 초기 충전량 범위 확인
		|| FMath::IsFinite(InitialCharge) == false || InitialCharge < 0.0f || InitialCharge > MaxCharge)
	{
		// 충전량 설정 실패 원인의 프로젝트 로그 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s MaxCharge 또는 InitialCharge가 유한 값이 아니거나 0 <= InitialCharge <= MaxCharge 범위를 벗어남 (Item=%s)"),
			*UE_CALL_LOCATIONINFO, *GetPathName());
		// 최대 충전량 또는 초기 충전량 설정 오류 전달
		return false;
	}
	// 지급 대상의 공통 보관 컴포넌트 조회
	UMD_EquipHotbarComponent* Hotbar = Context.Recipient->FindComponentByClass<UMD_EquipHotbarComponent>();
	// 이미 보유한 도구의 재지급 및 유효하지 않은 보관 구조 차단
	if (IsValid(Hotbar) == false || Hotbar->GetSlotCount() != 4 || Hotbar->GetItemCount(this) > 0)
	{
		// 소지 중인 도구는 구매 이력과 무관하게 중복 구매 거부
		UE_LOG(MarineDemolition, Warning, TEXT("%s 이미 보유한 도구이거나 Hotbar가 유효하지 않음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
		// 실제 보관 상태에 따른 지급 실패 전달
		return false;
	}
	// 현재 선행 도구가 있으면 빈 슬롯 없이 해당 위치를 업그레이드 가능
	if (FindUpgradeSourceSlot(Hotbar) != INDEX_NONE)
	{
		// 보유 선행 도구를 교체할 수 있는 지급 조건 전달
		return true;
	}
	// 처음 구매하는 업그레이드만 현재 선행 도구 필요
	if (UpgradeSourceAssetId.IsValid() == true && Context.PurchasedItemIds.Contains(GetPrimaryAssetId()) == false)
	{
		// 최초 업그레이드 구매의 선행 도구 미보유 원인 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 최초 업그레이드에 필요한 선행 도구가 없음 (Item=%s, Source=%s)"),
			*UE_CALL_LOCATIONINFO, *GetPrimaryAssetId().ToString(), *UpgradeSourceAssetId.ToString());
		// 구매 이력이 없는 업그레이드의 직접 지급 차단
		return false;
	}
	// 일반 도구와 선행 도구 없는 재구매는 남은 공통 슬롯에 추가
	return Hotbar->CanAddItem(this, Context.Quantity);
}

bool UMD_ToolDefinition::Grant_Implementation(FMD_ItemGrantContext Context)
{
	// 유효한 서버 대상만 실제 보관 상태 변경 허용
	if (IsValid(Context.Recipient) == false || Context.Recipient->HasAuthority() == false)
	{
		// 지급 대상 또는 서버 권한 오류 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 아이템 지급 대상 또는 서버 권한이 유효하지 않음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
		// 서버 지급 요청 실패 전달
		return false;
	}
	// BP 재정의를 포함한 최신 지급 조건 검증
	if (CanGrant(Context) == false)
	{
		// 선행 검사에서 기록한 지급 실패 전달
		return false;
	}
	// 조건 검사 이후 실제 지급에 사용할 보관 컴포넌트 재조회
	UMD_EquipHotbarComponent* Hotbar = Context.Recipient->FindComponentByClass<UMD_EquipHotbarComponent>();
	// 재조회한 컴포넌트가 유효한 경우에만 실제 지급 요청
	if (IsValid(Hotbar) == true)
	{
		// 지급 직전 현재 보유한 선행 도구 위치 재조회
		int32 SourceSlotIndex = FindUpgradeSourceSlot(Hotbar);
		// 실제 선행 도구가 있을 때만 해당 슬롯을 업그레이드로 교체
		if (SourceSlotIndex != INDEX_NONE)
		{
			// 다른 도구와 소모품을 보존하는 선행 도구 슬롯 갱신
			return Hotbar->SetItemAtSlot(SourceSlotIndex, this, Context.Quantity);
		}
		// 일반 구매 또는 선행 도구 없는 재구매는 빈 슬롯에 추가
		return Hotbar->AddItem(this, Context.Quantity);
	}
	// BP 조건 검사 이후 보관 대상이 사라진 원인 기록
	UE_LOG(MarineDemolition, Warning, TEXT("%s 지급 직전 Hotbar가 유효하지 않음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
	// 실제 지급이 없는 실패 결과 전달
	return false;
}

bool UMD_ToolDefinition::CanReceiveCharge_Implementation(AActor* Recipient, float Amount)
{
	// 보유 도구와 충전 잔량을 관리하는 대상 컴포넌트 조회
	UMD_EquipHotbarComponent* Hotbar = IsValid(Recipient) == true ? Recipient->FindComponentByClass<UMD_EquipHotbarComponent>() : nullptr;
	// 도구를 보관할 수 없는 대상의 충전 차단
	if (IsValid(Hotbar) == false)
	{
		// 충전 수신 대상 오류 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 도구 충전 대상의 Hotbar가 없음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
		// 충전 수신 불가 결과 전달
		return false;
	}
	// 해당 도구의 실제 보유 상태와 충전 상한 검사 결과 전달
	return Hotbar->CanAddToolCharge(this, Amount);
}

bool UMD_ToolDefinition::ReceiveCharge_Implementation(AActor* Recipient, float Amount)
{
	// 서버에서만 실제 충전 상태 변경 허용
	if (IsValid(Recipient) == false || Recipient->HasAuthority() == false)
	{
		// 서버 충전 권한 오류 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 도구 충전 대상 또는 서버 권한이 유효하지 않음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
		// 권한 없는 충전 요청 실패 전달
		return false;
	}
	// BP 재정의를 포함한 충전 수신 조건 검증
	if (CanReceiveCharge(Recipient, Amount) == false)
	{
		// 선행 조건 검사에서 기록한 충전 실패 전달
		return false;
	}
	// 실제 잔량 변경 직전 보관 컴포넌트 조회
	UMD_EquipHotbarComponent* Hotbar = Recipient->FindComponentByClass<UMD_EquipHotbarComponent>();
	// 조건 검사 이후에도 유효한 저장소로 충전 요청 전달
	return IsValid(Hotbar) == true && Hotbar->AddToolCharge(this, Amount) == true;
}

int32 UMD_ToolDefinition::FindUpgradeSourceSlot(UMD_EquipHotbarComponent* Hotbar)
{
	// 보관 대상 또는 선행 식별자가 없는 상태의 슬롯 접근 차단
	if (IsValid(Hotbar) == false || UpgradeSourceAssetId.IsValid() == false)
	{
		// 교체할 선행 도구 위치 부재 전달
		return INDEX_NONE;
	}
	// 선택된 슬롯에 한정하지 않고 전체 보관 위치의 선행 도구 탐색
	for (int32 SlotIndex = 0; SlotIndex < Hotbar->GetSlotCount(); ++SlotIndex)
	{
		// 같은 식별자의 일반 아이템을 선행 도구로 인정하지 않는 타입 확인
		UMD_ToolDefinition* SourceTool = Cast<UMD_ToolDefinition>(Hotbar->GetItemAtSlot(SlotIndex));
		// 현재 보유한 도구의 전체 식별자로 업그레이드 대상 확인
		if (IsValid(SourceTool) == true && SourceTool->GetPrimaryAssetId() == UpgradeSourceAssetId)
		{
			// 실제 업그레이드가 교체해야 할 슬롯 위치 전달
			return SlotIndex;
		}
	}
	// 일치하는 선행 도구가 없는 결과 전달
	return INDEX_NONE;
}
