// Fill out your copyright notice in the Description page of Project Settings.


#include "Component/Merchant/MD_MerchantComponent.h"
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "Engine/GameInstance.h"
#include "Net/UnrealNetwork.h"

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
	DOREPLIFETIME(UMD_MerchantComponent, CurrentDay);
	DOREPLIFETIME(UMD_MerchantComponent, QuestCompletions);
	DOREPLIFETIME(UMD_MerchantComponent, ActiveQuestReward);
	DOREPLIFETIME(UMD_MerchantComponent, MainQuests);
	DOREPLIFETIME(UMD_MerchantComponent, RepeatQuests);
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
	CurrentDay = Merchant->GetCurrentDay();
	QuestCompletions = Merchant->GetQuestCompletions();
	ActiveQuestReward = Merchant->GetActiveQuestReward();
	MainQuests = Merchant->GetMainQuests();
	RepeatQuests = Merchant->GetRepeatQuests();

	// 리슨서버 호스트는 OnRep이 오지 않으므로 명시적 호출.
	OnMerchantStateChanged.Broadcast();
}

void UMD_MerchantComponent::OnRep_MerchantState()
{
	OnMerchantStateChanged.Broadcast();
}
