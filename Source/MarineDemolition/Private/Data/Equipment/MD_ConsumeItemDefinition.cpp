// Fill out your copyright notice in the Description page of Project Settings.

#include "Data/Equipment/MD_ConsumeItemDefinition.h"
#include "Util/MD_LogUtility.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "GameFramework/Actor.h"

bool UMD_ConsumeItemDefinition::CanGrant_Implementation(FMD_ItemGrantContext Context)
{
	// 보관 조건에 앞서 공통 지급 데이터 검사
	if (Super::CanGrant_Implementation(Context) == false)
	{
		// 선행 검사에서 기록한 실패 결과의 전달
		return false;
	}
	// 지급 대상의 공통 보관 컴포넌트 조회
	UMD_EquipHotbarComponent* Hotbar = Context.Recipient->FindComponentByClass<UMD_EquipHotbarComponent>();
	// 보관 컴포넌트 부재 또는 슬롯 및 수량 조건 미충족 차단
	if (IsValid(Hotbar) == false || Hotbar->CanAddItem(this, Context.Quantity) == false)
	{
		// 실제 보관 조건 실패의 프로젝트 로그 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s Hotbar에 아이템을 지급할 수 없음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
		// 상태 변경 없는 지급 불가 결과 전달
		return false;
	}
	// 공통 슬롯의 보관 조건 통과 결과 전달
	return true;
}

bool UMD_ConsumeItemDefinition::Grant_Implementation(FMD_ItemGrantContext Context)
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
		// 단일 도구 교체 또는 일반 아이템 수량 합산 결과 전달
		return Hotbar->AddItem(this, Context.Quantity);
	}
	// BP 조건 검사 이후 보관 대상이 사라진 원인 기록
	UE_LOG(MarineDemolition, Warning, TEXT("%s 지급 직전 Hotbar가 유효하지 않음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
	// 실제 지급이 없는 실패 결과 전달
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
