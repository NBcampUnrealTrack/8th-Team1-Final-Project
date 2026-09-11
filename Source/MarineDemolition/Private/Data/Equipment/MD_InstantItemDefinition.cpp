// Fill out your copyright notice in the Description page of Project Settings.

#include "Data/Equipment/MD_InstantItemDefinition.h"
#include "Util/MD_LogUtility.h"

bool UMD_InstantItemDefinition::CanGrant_Implementation(FMD_ItemGrantContext Context)
{
	// 즉시 적용 품목의 공통 지급 데이터 검사
	if (Super::CanGrant_Implementation(Context) == false)
	{
		// 선행 검사에서 기록한 실패 결과의 전달
		return false;
	}
	// 한 번의 구매에서 즉시 적용 효과 하나만 허용
	if (Context.Quantity != 1)
	{
		// 실패 원인의 프로젝트 로그 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 즉시 적용 아이템은 한 번에 1개만 지급 가능함 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
		// 지원하지 않는 일괄 적용 수량 전달
		return false;
	}
	// 구체 충전 및 플레이어 효과 정의의 추가 검증을 위한 공통 조건 통과
	return true;
}
