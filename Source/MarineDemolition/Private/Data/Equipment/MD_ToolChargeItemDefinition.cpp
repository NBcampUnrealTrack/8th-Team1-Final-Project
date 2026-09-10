// Fill out your copyright notice in the Description page of Project Settings.

#include "Data/Equipment/MD_ToolChargeItemDefinition.h"
#include "Util/MD_LogUtility.h"

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
	// 실패 원인의 프로젝트 로그 기록
	UE_LOG(MarineDemolition, Warning, TEXT("%s 대상 도구 소지 검사 및 GAS 충전 상태 연동이 구현되지 않음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
	// 대상 도구 소지 및 GAS 충전 상태 연결 전 지급 승인 차단
	return false;
}
