// Fill out your copyright notice in the Description page of Project Settings.


#include "Component/Merchant/MD_MerchantComponent.h"
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "Data/Merchant/MD_SiteDefinition.h"
#include "Data/Merchant/MD_QuestDefinition.h"
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
	DebtBalance = Merchant->GetDebtBalance();
	CurrentDay = Merchant->GetCurrentDay();
	Affinity = Merchant->GetAffinity();
	QuestCompletions = Merchant->GetQuestCompletions();
	// 저장된 계약의 현장과 실제 표시 대상 월드 조회
	UMD_SiteDefinition* CurrentSite = Merchant->GetCurrentSite();
	// 서버에서 판정한 동일 목록을 호스트와 모든 클라이언트에 제공
	UWorld* World = GetWorld();
	// PIE 접두사를 제외한 전체 패키지 경로로 해당 현장의 맵만 허용
	if (IsValid(CurrentSite) == true && IsValid(World) == true && CurrentSite->QuestMap.IsNull() == false
		&& CurrentSite->QuestMap.ToSoftObjectPath().GetLongPackageFName() == FName(UWorld::RemovePIEPrefix(World->GetPackage()->GetName())))
	{
		// 현재 맵에서 표시할 진행 중인 퀘스트 보상 반영
		ActiveQuestReward = Merchant->GetActiveQuestReward();
		// 원본 순서를 유지하여 화면의 퀘스트 선택 인덱스 보존
		MainQuests = Merchant->GetMainQuests();
		// 현재 현장의 반복 퀘스트 목록을 클라이언트에도 동일하게 전달
		RepeatQuests = Merchant->GetRepeatQuests();
	}
	else
	{
		// 다른 맵에서 이전 현장의 보상 표시 제거
		ActiveQuestReward = 0;
		// 서버 원장의 진행도는 유지하고 표시용 메인 목록만 비우기
		MainQuests.Reset();
		// 서버 원장의 수락 상태는 유지하고 표시용 반복 목록만 비우기
		RepeatQuests.Reset();
	}
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

	// 실물 샘플 납품 완료 대사는 복제된 오늘의 메인 퀘스트 상태로 선택한다.
	// 핫바 카트리지는 기존 ReceiveQuestItemDelivery의 즉시 납품 대사를 유지한다.
	for (const FMD_MainQuestState& Quest : MainQuests)
	{
		if (Quest.UnlockDay != CurrentDay || Quest.Status != EMD_MainQuestStatus::Completed) continue;
		const UMD_QuestDefinition* Definition = UMD_QuestDefinition::LoadDefinition(Quest.DefinitionId);
		if (!Definition) continue;
		const int32 LastPhaseIndex = Definition->GetPhaseCount() - 1;
		const FMD_QuestPhaseData* Phase = Definition->GetPhase(LastPhaseIndex);
		if (!Phase || Phase->UsesHotbarDelivery()) continue;
		const FText Line = Definition->GetCompletionDialogue(LastPhaseIndex);
		if (Line.IsEmpty()) continue;
		Info.Greeting = Line;
		Info.TalkLines.Insert(Line, 0);
		break;
	}

	Info.Credits = Credits;
	Info.ContractDay = CurrentDay;
	Info.ActiveQuestReward = ActiveQuestReward;
	Info.Affinity = Affinity;

	// 다음 해금 임계값은 카탈로그별 값이므로 호출 위젯에서 계산하기 위한 기본 상태 유지
	Info.DebtBalance = DebtBalance;
	// 클라이언트와 서버의 대출 한도 값이 달라지지 않도록 복제 값 기반 계산
	Info.LoanLimit = UMD_MerchantSubsystem::CalculateLoanLimit(Affinity);
	Info.DailyInterestRate = 0.05f;
	return Info;
}
