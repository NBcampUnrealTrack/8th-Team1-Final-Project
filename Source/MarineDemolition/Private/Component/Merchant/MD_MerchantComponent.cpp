// Fill out your copyright notice in the Description page of Project Settings.


#include "Component/Merchant/MD_MerchantComponent.h"
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "Engine/GameInstance.h"
#include "Net/UnrealNetwork.h"
#include "Game/MD_GameStateBase.h"

UMD_MerchantComponent::UMD_MerchantComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);

}

void UMD_MerchantComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 상인 상태는 팀 공유 데이터라 전원에게 보낸다.
	DOREPLIFETIME(UMD_MerchantComponent, Credits);
	DOREPLIFETIME(UMD_MerchantComponent, DebtBalance);
	DOREPLIFETIME(UMD_MerchantComponent, LoanPrincipal);
	DOREPLIFETIME(UMD_MerchantComponent, OwnedItemIds);
}

void UMD_MerchantComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!GetOwner()->HasAuthority())
	{
		return;
	}

	UMD_MerchantSubsystem* Merchant = GetMerchantSubsystem();
	if (!IsValid(Merchant))
	{
		return;
	}

	SubsystemChangedHandle = Merchant->OnMerchantStateChanged.AddUObject(
		this, &UMD_MerchantComponent::RefreshFromSubsystem);

	// Day 트래블로 GameState가 새로 생성되었을 때 Subsystem에 남아 있는 상태를 즉시 반영한다.
	RefreshFromSubsystem();
}

void UMD_MerchantComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	//사라진 컴포넌트에 델리게이트가 또 호출되지 않도록 델리게이트 해제

	if (UMD_MerchantSubsystem* Merchant = GetMerchantSubsystem())
	{
		Merchant->OnMerchantStateChanged.Remove(SubsystemChangedHandle);
	}

	SubsystemChangedHandle.Reset();

	Super::EndPlay(EndPlayReason);
}

UMD_MerchantSubsystem* UMD_MerchantComponent::GetMerchantSubsystem() const
{
	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = IsValid(World) ? World->GetGameInstance() : nullptr;

	return IsValid(GameInstance) ? GameInstance->GetSubsystem<UMD_MerchantSubsystem>() : nullptr;
}

void UMD_MerchantComponent::RefreshFromSubsystem()
{
	const UMD_MerchantSubsystem* Merchant = GetMerchantSubsystem();
	if (!IsValid(Merchant))
	{
		return;
	}

	Credits = Merchant->GetCredits();
	DebtBalance = Merchant->GetDebtBalance();
	LoanPrincipal = Merchant->GetLoanPrincipal();
	OwnedItemIds = Merchant->GetOwnedItemIds();
    // 팀 원장을 소유한 GameState 확인
    AMD_GameStateBase* GameState = Cast<AMD_GameStateBase>(GetOwner());
    if (IsValid(GameState) == true)
    {
        // 착수금과 보상 및 지출의 최신 합계 반영
        GameState->RefreshDailyLedger();
    }


	// 저장 복원 직후 접속 클라이언트가 다음 일반 갱신 주기를 기다리지 않도록 하는 즉시 복제 요청
	GetOwner()->ForceNetUpdate();

	// 리슨서버 호스트는 OnRep이 오지 않으므로 동일한 UI 상태를 보장하기 위한 직접 방송
	OnMerchantStateChanged.Broadcast();
}

void UMD_MerchantComponent::OnRep_MerchantState()
{
	OnMerchantStateChanged.Broadcast();
}

FMD_MerchantMenuInfo UMD_MerchantComponent::BuildMenuInfo() const
{
	FMD_MerchantMenuInfo Info;

	Info.Credits = Credits;

	// 클라이언트에 표시할 현재 미상환 잔액 반영
	Info.DebtBalance = DebtBalance;
	// 클라이언트가 서버와 같은 고정 이자를 계산하도록 기준 원금 전달
	Info.LoanPrincipal = LoanPrincipal;
	// 서버와 동일한 기본 대출 한도를 클라이언트 UI에 적용
	Info.LoanLimit = UMD_MerchantSubsystem::CalculateLoanLimit();
	Info.DailyInterestRate = UMD_MerchantSubsystem::LoanInterestRate;
	return Info;
}
