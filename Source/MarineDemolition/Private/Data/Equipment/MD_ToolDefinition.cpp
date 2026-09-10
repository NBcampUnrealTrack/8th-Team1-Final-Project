// Fill out your copyright notice in the Description page of Project Settings.

#include "Data/Equipment/MD_ToolDefinition.h"
#include "Util/MD_LogUtility.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "GameFramework/Actor.h"

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
		// 단일 도구 교체 또는 일반 아이템 수량 합산 결과 전달
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
