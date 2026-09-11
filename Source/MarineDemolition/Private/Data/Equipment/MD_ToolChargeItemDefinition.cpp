// Fill out your copyright notice in the Description page of Project Settings.

#include "Data/Equipment/MD_ToolChargeItemDefinition.h"
#include "Util/MD_LogUtility.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "GameFramework/Actor.h"

bool UMD_ToolChargeItemDefinition::CanGrant_Implementation(FMD_ItemGrantContext Context)
{
	// 즉시 적용 품목의 공통 대상 및 수량 검사
	if (Super::CanGrant_Implementation(Context) == false)
	{
		// 선행 검사에서 기록한 실패 결과의 전달
		return false;
	}
	// 대상 식별자와 양의 유한 충전량 설정 확인
	if (TargetItemAssetId.IsValid() == false || FMath::IsFinite(ChargeAmount) == false || ChargeAmount <= 0.0f)
	{
		// 실패 원인의 프로젝트 로그 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 충전 대상 Asset ID가 유효하지 않거나 ChargeAmount가 양의 유한 값이 아님 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
		// 충전물 설정 오류 전달
		return false;
	}
	// 충전물에 설정된 정확한 AssetId의 소지 도구 조회
	UMD_ToolDefinition* TargetTool = FindTargetTool(Context.Recipient);
	// 대상 미보유 시 기존 구매 이력과 무관하게 충전 차단
	if (IsValid(TargetTool) == false)
	{
		// 충전 대상 도구 부재 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 충전 대상 도구를 소지하지 않음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
		// 대상 도구가 없는 지급 요청 실패 전달
		return false;
	}
	// 실제 잔량 저장소에 대한 검사를 도구 정의에 위임
	return TargetTool->CanReceiveCharge(Context.Recipient, ChargeAmount);
}

UMD_ToolDefinition* UMD_ToolChargeItemDefinition::FindTargetTool(AActor* Recipient)
{
	// 실제 소지 도구를 조회할 공통 보관 컴포넌트 확인
	UMD_EquipHotbarComponent* Hotbar = IsValid(Recipient) == true ? Recipient->FindComponentByClass<UMD_EquipHotbarComponent>() : nullptr;
	// 보관 컴포넌트가 없는 대상의 슬롯 접근 차단
	if (IsValid(Hotbar) == false)
	{
		// 소지 도구 부재 전달
		return nullptr;
	}
	// 선택 여부와 무관하게 보관 중인 도구 검사
	for (int32 Index = 0; Index < Hotbar->GetSlotCount(); ++Index)
	{
		// 충전 동작을 제공하는 도구 정의 조회
		UMD_ToolDefinition* Tool = Cast<UMD_ToolDefinition>(Hotbar->GetItemAtSlot(Index));
		// 타입과 내부 식별자가 모두 일치하는 도구만 선택
		if (IsValid(Tool) == true && Tool->GetPrimaryAssetId() == TargetItemAssetId)
		{
			// 실제 보유 중인 충전 수신 도구 전달
			return Tool;
		}
	}
	// 호환되는 소지 도구 부재 전달
	return nullptr;
}

bool UMD_ToolChargeItemDefinition::Grant_Implementation(FMD_ItemGrantContext Context)
{
	// 충전물의 실제 지급을 서버에서만 허용
	if (IsValid(Context.Recipient) == false || Context.Recipient->HasAuthority() == false)
	{
		// 충전물 지급 권한 오류 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 충전물 지급 대상 또는 서버 권한이 유효하지 않음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
		// 권한 없는 충전물 지급 실패 전달
		return false;
	}
	// 실제 지급 직전 소지 도구와 잔량 조건 재검증
	if (CanGrant(Context) == false)
	{
		// 선행 검사에서 기록한 지급 실패 전달
		return false;
	}
	// BP 조건 검사 이후 현재 도구 재조회
	UMD_ToolDefinition* TargetTool = FindTargetTool(Context.Recipient);
	// 실제 도구 충전 결과를 결제 호출부에 전달
	return IsValid(TargetTool) == true && TargetTool->ReceiveCharge(Context.Recipient, ChargeAmount) == true;
}
