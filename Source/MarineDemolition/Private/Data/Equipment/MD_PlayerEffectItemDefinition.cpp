// Fill out your copyright notice in the Description page of Project Settings.

#include "Data/Equipment/MD_PlayerEffectItemDefinition.h"
#include "Util/MD_LogUtility.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Actor.h"
#include "GameplayEffect.h"
#include "GameplayEffectComponents/ChanceToApplyGameplayEffectComponent.h"
#include "GameplayEffectComponents/TargetTagRequirementsGameplayEffectComponent.h"

bool UMD_PlayerEffectItemDefinition::CanGrant_Implementation(FMD_ItemGrantContext Context)
{
	// 즉시 적용 품목의 공통 대상 및 수량 검사
	if (Super::CanGrant_Implementation(Context) == false)
	{
		// 선행 검사에서 기록한 실패 결과의 전달
		return false;
	}
	// 효과 클래스가 없는 정의의 적용 차단
	if (IsValid(EffectClass) == false)
	{
		// 실패 원인의 프로젝트 로그 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 적용할 GameplayEffect 클래스가 유효하지 않음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
		// 효과 설정 오류 전달
		return false;
	}
	// 호출부가 제공한 이력에 따른 일회성 업그레이드 제한
	if (bUniqueUpgrade == true && Context.PurchasedItemIds.Contains(GetPrimaryAssetId()) == true)
	{
		// 실패 원인의 프로젝트 로그 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 이미 구매한 일회성 업그레이드의 중복 적용 요청 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
		// 이미 구매한 업그레이드의 중복 적용 제한 전달
		return false;
	}
	// 구매자의 실제 효과 수신 컴포넌트 조회
	UAbilitySystemComponent* AbilitySystem = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Context.Recipient);
	// GAS 효과를 받을 수 없는 대상의 차단
	if (IsValid(AbilitySystem) == false)
	{
		// 실패 원인의 프로젝트 로그 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 지급 대상의 AbilitySystemComponent가 유효하지 않음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
		// 효과 수신 대상의 부적합 전달
		return false;
	}
	// 실행 없이 설정된 효과 조건을 확인할 기본 객체 조회
	UGameplayEffect* Effect = EffectClass.GetDefaultObject();
	// 유효하지 않은 효과 기본 객체의 접근 차단
	if (IsValid(Effect) == false)
	{
		// 실패 원인의 프로젝트 로그 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s GameplayEffect 기본 객체가 유효하지 않음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
		// 효과 정의 오류 전달
		return false;
	}
	// 효과가 변경하는 모든 속성의 수신 가능 여부 확인
	for (FGameplayModifierInfo& Modifier : Effect->Modifiers)
	{
		// 필요한 AttributeSet이 없는 효과의 지급 차단
		if (AbilitySystem->HasAttributeSetForAttribute(Modifier.Attribute) == false)
		{
			// 실패 원인의 프로젝트 로그 기록
			UE_LOG(MarineDemolition, Warning, TEXT("%s 효과가 변경할 AttributeSet이 지급 대상에 없음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
			// 속성 수신 조건 실패 전달
			return false;
		}
	}
	// 엔진의 읽기 전용 API로 대상 태그 조건 조회
	const UTargetTagRequirementsGameplayEffectComponent* TagRequirements = Effect->FindComponent<UTargetTagRequirementsGameplayEffectComponent>();
	// 설정된 태그 조건만 평가하는 무적용 검사
	if (IsValid(TagRequirements) == true)
	{
		// 현재 구매자 태그의 비교용 컨테이너
		FGameplayTagContainer OwnedTags;
		// 실제 태그 상태를 변경하지 않고 조회
		AbilitySystem->GetOwnedGameplayTags(OwnedTags);
		// 적용 태그 조건 미충족 또는 설정된 제거 조건 충족 시 지급 차단
		if (TagRequirements->ApplicationTagRequirements.RequirementsMet(OwnedTags) == false
			// 제거 조건이 설정된 경우에만 현재 태그와 비교
			|| (TagRequirements->RemovalTagRequirements.IsEmpty() == false
				&& TagRequirements->RemovalTagRequirements.RequirementsMet(OwnedTags) == true))
		{
			// 효과 적용을 막는 태그 조건의 프로젝트 로그 기록
			UE_LOG(MarineDemolition, Warning, TEXT("%s GameplayEffect의 적용 태그 조건 미충족 또는 제거 태그 조건 충족 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
			// 효과 적용 또는 유지에 필요한 태그 조건 실패 전달
			return false;
		}
	}
	// 엔진의 읽기 전용 API로 적용 확률 설정 조회
	const UChanceToApplyGameplayEffectComponent* Chance = Effect->FindComponent<UChanceToApplyGameplayEffectComponent>();
	// 사전 검사에서는 추첨 없이 적용 가능성이 없는 설정만 차단
	if (IsValid(Chance) == true && Chance->GetChanceToApplyToTarget().GetValueAtLevel(1.0f) <= 0.0f)
	{
		// 실패 원인의 프로젝트 로그 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s GameplayEffect의 적용 확률이 0 이하임 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
		// 효과가 적용될 가능성이 없는 설정의 실패 전달
		return false;
	}
	// 실제 효과를 생성하거나 적용하지 않은 조건 검사 통과 결과
	return true;
}

bool UMD_PlayerEffectItemDefinition::Grant_Implementation(FMD_ItemGrantContext Context)
{
	// 유효한 효과 수신 대상에 한해서 서버 적용 권한 확인
	if (IsValid(Context.Recipient) == false || Context.Recipient->HasAuthority() == false)
	{
		// 효과 수신 대상 또는 서버 권한 확인 실패의 프로젝트 로그 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 효과를 받을 지급 대상이 유효하지 않거나 GameplayEffect 적용의 서버 권한이 없음 (Item=%s)"),
			*UE_CALL_LOCATIONINFO, *GetPathName());
		// GameplayEffect 실행 조건 실패 전달
		return false;
	}
	// BP 재정의를 포함한 최신 지급 조건 확인
	if (CanGrant(Context) == false)
	{
		// 선행 검사에서 기록한 실패 결과의 전달
		return false;
	}
	// 실제 효과 적용에 사용할 구매자 ASC 재조회
	UAbilitySystemComponent* AbilitySystem = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Context.Recipient);
	// BP 조건 검사 이후 사라진 대상의 접근 차단
	if (IsValid(AbilitySystem) == false)
	{
		// 실패 원인의 프로젝트 로그 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 지급 조건 검사 이후 AbilitySystemComponent가 유효하지 않음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
		// 효과 수신 대상 확인 실패 전달
		return false;
	}
	// 실제 적용 단계에서만 효과 문맥 생성
	FGameplayEffectContextHandle EffectContext = AbilitySystem->MakeEffectContext();
	// 구매 아이템을 효과 출처로 기록
	EffectContext.AddSourceObject(this);
	// 현재 효과 설정으로 실행할 Spec 생성
	FGameplayEffectSpecHandle Spec = AbilitySystem->MakeOutgoingSpec(EffectClass, 1.0f, EffectContext);
	// 유효한 Spec에 한해서 효과를 적용하고 두 단계 중 하나라도 실패하면 지급 차단
	if (Spec.IsValid() == false || AbilitySystem->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get()).WasSuccessfullyApplied() == false)
	{
		// 효과 생성 또는 실제 적용 실패의 프로젝트 로그 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s GameplayEffect Spec 생성 또는 실제 적용 실패 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
		// 효과 생성 및 적용 단계의 실패 전달
		return false;
	}
	// 실제 효과 적용을 마친 성공 결과 전달
	return true;
}
