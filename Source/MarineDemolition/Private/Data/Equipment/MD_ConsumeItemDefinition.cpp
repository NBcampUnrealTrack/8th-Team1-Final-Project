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
	// 구매 묶음을 Hotbar의 실제 사용 수량으로 변환
	int32 GrantedQuantity = GetGrantedQuantity(Context.Quantity);
	// 보관 컴포넌트 부재 또는 슬롯 및 수량 조건 미충족 차단
	if (GrantedQuantity <= 0 || IsValid(Hotbar) == false || Hotbar->IsItemAbilityGrantValid(this) == false || Hotbar->CanAddItem(this, GrantedQuantity) == false)
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
	// BP 지급 검사 재정의 이후에도 실제 수량의 범위 재검증
	int32 GrantedQuantity = GetGrantedQuantity(Context.Quantity);
	// 검증된 보관 대상과 묶음 수량의 실제 지급
	if (IsValid(Hotbar) == true && GrantedQuantity > 0)
	{
		// 기본 소모품 한 개 또는 구매한 묶음의 사용 수량 합산
		return Hotbar->AddItem(this, GrantedQuantity);
	}
	// BP 조건 검사 이후 보관 대상이 사라진 원인 기록
	UE_LOG(MarineDemolition, Warning, TEXT("%s 지급 직전 Hotbar가 유효하지 않음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
	// 실제 지급이 없는 실패 결과 전달
	return false;
}

int32 UMD_ConsumeItemDefinition::GetGrantedQuantity(int32 PurchaseQuantity)
{
	// 곱셈 전에 유효한 묶음 수와 정수 수량 범위 검사
	if (PurchaseQuantity <= 0 || QuantityPerPurchase <= 0 || PurchaseQuantity > MAX_int32 / QuantityPerPurchase)
	{
		// 잘못된 묶음 설정이나 수량 넘침의 프로젝트 로그 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 소모품 묶음 수량이 유효하지 않음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
		// 보관 상태 변경 없이 지급 거절
		return 0;
	}
	// 구매 단위를 실제 사용 가능 수량으로 변환
	return PurchaseQuantity * QuantityPerPurchase;
}
