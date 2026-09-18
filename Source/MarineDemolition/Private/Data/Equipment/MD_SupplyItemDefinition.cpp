// Fill out your copyright notice in the Description page of Project Settings.

#include "Data/Equipment/MD_SupplyItemDefinition.h"
#include "Component/Support/MD_SupplyComponent.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "GameFramework/Actor.h"
#include "Util/MD_LogUtility.h"

FGameplayTag UMD_SupplyItemDefinition::GetUseAbilityTag()
{
	return MDGameplayTags::Ability_PlaceSupport;
}

bool UMD_SupplyItemDefinition::CanGrant_Implementation(FMD_ItemGrantContext Context)
{
	// 즉시 지급 대상과 수량의 공통 조건 검사
	if (Super::CanGrant_Implementation(Context) == false)
	{
		// 선행 검사에서 기록한 실패 전달
		return false;
	}
	// 설치 GA와 공유하는 수량 저장소 조회
	UMD_SupplyComponent* Supply = Context.Recipient->FindComponentByClass<UMD_SupplyComponent>();
	// 저장소 부재 또는 정수 범위를 넘는 수량 적립 차단
	if (IsValid(Supply) == false || Supply->GetSupportCount() < 0 || Supply->GetSupportCount() > MAX_int32 - Context.Quantity)
	{
		// 수량 지급 불가 원인 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 지지대 수량 저장소가 없거나 적립 가능한 범위를 초과함 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
		// 보급 수량을 변경하지 않은 실패 결과 전달
		return false;
	}
	// A supply purchase also needs one selectable installation slot, only on first purchase.
	UMD_EquipHotbarComponent* Hotbar = Context.Recipient->FindComponentByClass<UMD_EquipHotbarComponent>();
	return IsValid(Hotbar) && Hotbar->IsItemAbilityGrantValid(this)
		&& (Hotbar->GetItemCount(this) > 0 || Hotbar->CanAddItem(this, 1));
}

bool UMD_SupplyItemDefinition::Grant_Implementation(FMD_ItemGrantContext Context)
{
	// 서버에서만 설치 수량 적립 허용
	if (IsValid(Context.Recipient) == false || Context.Recipient->HasAuthority() == false)
	{
		// 실제 보급 권한 오류 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 지지대 지급 대상 또는 서버 권한이 유효하지 않음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
		// 권한 없는 적립 요청 실패 전달
		return false;
	}
	// BP 확장을 포함한 최종 지급 가능 조건 확인
	if (CanGrant(Context) == false)
	{
		// 선행 검사에서 기록한 지급 실패 전달
		return false;
	}
	// 실제 적립 단계에서 수량 저장소 재조회
	UMD_SupplyComponent* Supply = Context.Recipient->FindComponentByClass<UMD_SupplyComponent>();
	// 조건 검사 이후 사라진 수량 저장소의 접근 차단
	if (IsValid(Supply) == false)
	{
		// 보급 저장소 부재 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 지급 직전 지지대 저장소가 유효하지 않음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
		// 실제 적립 없는 실패 전달
		return false;
	}
	UMD_EquipHotbarComponent* Hotbar = Context.Recipient->FindComponentByClass<UMD_EquipHotbarComponent>();
	if (!Hotbar || (Hotbar->GetItemCount(this) <= 0 && !Hotbar->AddItem(this, 1))) return false;
	// 기존 설치 GA가 소비할 수량으로 적립
	Supply->AddSupport(Context.Quantity);
	// 변경된 보급 수량의 빠른 복제 요청
	Context.Recipient->ForceNetUpdate();
	// 실제 수량 적립 성공 전달
	return true;
}
