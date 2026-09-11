// Fill out your copyright notice in the Description page of Project Settings.

#include "Data/Equipment/MD_FoamToolDefinition.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Component/Equipment/MD_FoamGunComponent.h"
#include "GameFramework/Actor.h"
#include "Util/MD_LogUtility.h"

bool UMD_FoamToolDefinition::CanReceiveCharge_Implementation(AActor* Recipient, float Amount)
{
	// 정수 게이지에 적용할 수 있는 양의 유한 충전량 검사
	if (IsValid(Recipient) == false || FMath::IsFinite(Amount) == false || Amount <= 0.0f
		|| static_cast<double>(Amount) > MAX_int32 || FMath::FloorToFloat(Amount) != Amount)
	{
		// 충전 대상 또는 단위 설정 오류 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 폼건 충전 대상 또는 정수 충전량이 유효하지 않음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
		// 유효하지 않은 충전 요청 실패 전달
		return false;
	}
	// 폼건의 실제 소지 여부 확인을 위한 보관 컴포넌트 조회
	UMD_EquipHotbarComponent* Hotbar = Recipient->FindComponentByClass<UMD_EquipHotbarComponent>();
	// 사격과 공유하는 실제 충전 저장소 조회
	UMD_FoamGunComponent* FoamGun = Recipient->FindComponentByClass<UMD_FoamGunComponent>();
	// 미보유 또는 만충인 폼건의 충전물 구매 차단
	if (IsValid(Hotbar) == false || Hotbar->GetItemCount(this) <= 0 || IsValid(FoamGun) == false || FoamGun->IsChargeFull() == true)
	{
		// 충전할 수 없는 폼건 상태 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 폼건 미보유, 충전 컴포넌트 부재 또는 만충 상태 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
		// 상태 변경 없는 충전 불가 결과 전달
		return false;
	}
	// 실제 폼건 충전 가능 결과 전달
	return true;
}

bool UMD_FoamToolDefinition::ReceiveCharge_Implementation(AActor* Recipient, float Amount)
{
	// 실제 사격 잔량 변경의 서버 권한 확인
	if (IsValid(Recipient) == false || Recipient->HasAuthority() == false)
	{
		// 충전 실행 권한 오류 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 폼건 충전 대상 또는 서버 권한이 유효하지 않음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
		// 권한 없는 잔량 변경 실패 전달
		return false;
	}
	// BP 확장과 현재 소지 및 잔량 상태 재검증
	if (CanReceiveCharge(Recipient, Amount) == false)
	{
		// 선행 검사에서 기록한 충전 실패 전달
		return false;
	}
	// 실제 사격 잔량을 보관한 컴포넌트 재조회
	UMD_FoamGunComponent* FoamGun = Recipient->FindComponentByClass<UMD_FoamGunComponent>();
	// 조건 검사 이후 사라진 저장소의 접근 차단
	if (IsValid(FoamGun) == false)
	{
		// 실제 충전 저장소 부재 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 지급 직전 폼건 컴포넌트가 유효하지 않음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
		// 실제 잔량 변경 없는 실패 전달
		return false;
	}
	// 기존 정수 덧셈의 범위를 보장하면서 충전 상한까지 보충
	FoamGun->AddCharge(FMath::Min(static_cast<int32>(Amount), MAX_int32 - FoamGun->GetFoamCharge()));
	// 변경된 실제 사격 잔량의 빠른 복제 요청
	Recipient->ForceNetUpdate();
	// 충전 저장소 변경 성공 전달
	return true;
}
