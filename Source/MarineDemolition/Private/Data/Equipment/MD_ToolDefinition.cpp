// Fill out your copyright notice in the Description page of Project Settings.

#include "Data/Equipment/MD_ToolDefinition.h"
#include "Util/MD_LogUtility.h"

bool UMD_ToolDefinition::CanGrant_Implementation(FMD_ItemGrantContext Context)
{
	// 도구 교체 조건에 앞서 공통 지급 데이터 검사
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
	// 실패 원인의 프로젝트 로그 기록
	UE_LOG(MarineDemolition, Warning, TEXT("%s 도구의 Hotbar 교체 및 GAS 상태 연동이 구현되지 않음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
	// 공통 Hotbar 교체와 GAS 상태 연결 전 지급 승인 차단
	return false;
}
