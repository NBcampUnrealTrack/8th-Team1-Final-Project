// Fill out your copyright notice in the Description page of Project Settings.

#include "Data/Equipment/MD_ConsumeItemDefinition.h"
#include "Util/MD_LogUtility.h"
#include "GameFramework/Actor.h"

bool UMD_ConsumeItemDefinition::CanGrant_Implementation(FMD_ItemGrantContext Context)
{
	// 보관 조건에 앞서 공통 지급 데이터 검사
	if (Super::CanGrant_Implementation(Context) == false)
	{
		// 선행 검사에서 기록한 실패 결과의 전달
		return false;
	}
	// 실패 원인의 프로젝트 로그 기록
	UE_LOG(MarineDemolition, Warning, TEXT("%s 소모품의 공통 Hotbar 보관 기능이 연결되지 않음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
	// 공통 Hotbar 보관 API가 연결되기 전 지급 승인 차단
	return false;
}

bool UMD_ConsumeItemDefinition::CanUse_Implementation(FMD_ItemUseContext Context)
{
	// 사용 주체 또는 식별자가 유효하지 않은 소모품 사용의 차단
	if (IsValid(Context.User) == false || GetPrimaryAssetId().IsValid() == false)
	{
		// 사용 주체 또는 식별자 확인 실패의 프로젝트 로그 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 소모품 사용 주체 또는 AssetType/ItemId가 유효하지 않음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
		// 소모품 사용 요청의 검증 실패 전달
		return false;
	}
	// 실패 원인의 프로젝트 로그 기록
	UE_LOG(MarineDemolition, Warning, TEXT("%s 소모품의 개별 사용 조건이 구현되지 않음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
	// 개별 수리 및 소비 효과의 조건이 연결되기 전 승인 차단
	return false;
}

bool UMD_ConsumeItemDefinition::ApplyItemEffect_Implementation(FMD_ItemUseContext Context)
{
	// 유효한 사용 주체에 한해서 효과 적용 권한 확인
	if (IsValid(Context.User) == false || Context.User->HasAuthority() == false)
	{
		// 사용 주체 또는 서버 권한 확인 실패의 프로젝트 로그 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 소모품 사용 주체가 유효하지 않거나 효과 적용의 서버 권한이 없음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
		// 소모품 효과 실행 조건 실패 전달
		return false;
	}
	// BP 재정의를 포함한 사용 조건 확인
	if (CanUse(Context) == false)
	{
		// 선행 검사에서 기록한 실패 결과의 전달
		return false;
	}
	// 실패 원인의 프로젝트 로그 기록
	UE_LOG(MarineDemolition, Warning, TEXT("%s 소모품의 실제 사용 효과가 구현되지 않음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
	// 실제 효과와 Hotbar 소비가 연결되기 전 성공 차단
	return false;
}
