// Fill out your copyright notice in the Description page of Project Settings.


#include "Player/MD_PlayerState.h"
#include "AbilitySystemComponent.h"
#include "Net/UnrealNetwork.h"

void AMD_PlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMD_PlayerState, ReturnStatus);
}

void AMD_PlayerState::SetReturnStatus(EMD_ReturnStatus NewStatus)
{
	if (!HasAuthority())
	{
		return;
	}

	if (ReturnStatus == NewStatus)
	{
		return;
	}

	ReturnStatus = NewStatus;

	HandleReturnStatusChanged();

	ForceNetUpdate();
}

EMD_ReturnStatus AMD_PlayerState::GetReturnStatus() const
{
	return ReturnStatus;
}

void AMD_PlayerState::OnRep_ReturnStatus()
{
	HandleReturnStatusChanged();
}

void AMD_PlayerState::HandleReturnStatusChanged()
{
	switch (ReturnStatus)
	{
	case EMD_ReturnStatus::Pending:
		UE_LOG(
			LogTemp,
			Log,
			TEXT("복귀 상태 : 가는중")
		);
		break;

	case EMD_ReturnStatus::Returned:
		UE_LOG(LogTemp,
			Log,
			TEXT("복귀 상태 : 복귀 완료")
		);
		break;

	case EMD_ReturnStatus::Failed:
		UE_LOG(
			LogTemp,
			Error,
			TEXT("복귀 상태 : 실패")
		);
		break;

	default:
		break;
	}
}

AMD_PlayerState::AMD_PlayerState()
{
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));

	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
}

UAbilitySystemComponent* AMD_PlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}
