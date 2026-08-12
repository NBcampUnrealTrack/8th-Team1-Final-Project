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
	DOREPLIFETIME(UMD_MerchantComponent, Affinity);
	DOREPLIFETIME(UMD_MerchantComponent, QuestCompletions);
	DOREPLIFETIME(UMD_MerchantComponent, ActiveQuestReward);
	DOREPLIFETIME(UMD_MerchantComponent, MainQuests);
	DOREPLIFETIME(UMD_MerchantComponent, RepeatQuests);
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
	CurrentDay = Merchant->GetCurrentDay();
	Affinity = Merchant->GetAffinity();
	QuestCompletions = Merchant->GetQuestCompletions();
	ActiveQuestReward = Merchant->GetActiveQuestReward();
	MainQuests = Merchant->GetMainQuests();
	RepeatQuests = Merchant->GetRepeatQuests();
	OwnedItemIds = Merchant->GetOwnedItemIds();

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

	// DisplayName·Greeting·TalkLines는 구조체 기본값(06 §4 대사)을 그대로 쓴다.
	// 상인 액터가 다른 값을 쓰고 싶으면 호출 후 덮어쓰면 된다.

	Info.Credits = Credits;
	Info.ContractDay = CurrentDay;
	Info.ActiveQuestReward = ActiveQuestReward;
	Info.Affinity = Affinity;

	// 다음 해금 임계값은 카탈로그별 값이므로 호출 위젯에서 계산하기 위한 기본 상태 유지
	// TODO(김동연): 대출 단계에서 DebtBalance / LoanLimit / DailyInterestRate를 채운다.
	// 그때까지는 구조체 기본값(착수금 500 / 한도 800 / 이자 5%)이 표시된다.

	return Info;
}
