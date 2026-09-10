// Fill out your copyright notice in the Description page of Project Settings.


#include "Player/MD_PlayerState.h"
#include "Character/MD_Character.h"
#include "Game/MD_GameStateBase.h"
#include "Gameplay/Attribute/MD_CharacterAttributeSet.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Online/Voice/MD_CharacterVOIPTalker.h"
#include "AbilitySystemComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

void AMD_PlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMD_PlayerState, ReturnStatus);
	DOREPLIFETIME_CONDITION(AMD_PlayerState, PurchasedShopItemIds, COND_OwnerOnly);
}

void AMD_PlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);

	// Seamless Travel이나 플레이어 연결 해제 후 재접속 시 보존되어야 하는 데이터 복사
	AMD_PlayerState* TargetPlayerState = Cast<AMD_PlayerState>(PlayerState);
	if (IsValid(TargetPlayerState) == true)
	{
		// 장비 해금 조건을 유지하기 위하여 플레이어가 구매한 장비 목록 복사
		TargetPlayerState->PurchasedShopItemIds = PurchasedShopItemIds;
	}
}

void AMD_PlayerState::BeginPlay()
{
	Super::BeginPlay();

	if (!IsValid(AbilitySystemComponent) || !IsValid(CharacterAttributeSet))
	{
		return;
	}

	// HP 변경 경로가 여러 개라 델리게이트 한 곳에서 받는다.
	// 서버·클라이언트 양쪽에서 호출되므로 각자 상태를 계산해 태그가 동일하게 유지된다.
	AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(CharacterAttributeSet->GetHealthAttribute()).AddUObject(this, &AMD_PlayerState::HandleHealthChanged);

	CachedHealthState = GetHealthState();
}

void AMD_PlayerState::OnRep_PlayerName()
{
	Super::OnRep_PlayerName();

	// 플레이어 이름 변경을 목록 UI에 전달할 현재 프로젝트 GameState 조회
	UWorld* World = GetWorld();
	if (IsValid(World) == true)
	{
		// 접속 플레이어 목록 변경 이벤트를 제공하는 GameState 조회
		AMD_GameStateBase* GameState = World->GetGameState<AMD_GameStateBase>();
		if (IsValid(GameState) == true)
		{
			// 이름 복제가 완료된 PlayerArray 변경 알림
			GameState->OnPlayerListChanged.Broadcast();
		}
	}

	// 이름이 변경된 PlayerState 소유 캐릭터 조회
	AMD_Character* PlayerCharacter = Cast<AMD_Character>(GetPawn());
	if (IsValid(PlayerCharacter) == false)
	{
		return;
	}

	// 새 플랫폼 닉네임의 캐릭터 머리 위 이름표 반영
	PlayerCharacter->RefreshNameTag();
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

	//캐릭터 AttributeSet
	CharacterAttributeSet = CreateDefaultSubobject<UMD_CharacterAttributeSet>(TEXT("CharacterAttributeSet"));
}

UAbilitySystemComponent* AMD_PlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AMD_PlayerState::SetVoiceVolumeMultiplier(float NewVoiceVolumeMultiplier)
{
	// 음성 채팅 UI와 Talker가 공유하는 허용 범위 적용
	float ClampedVoiceVolumeMultiplier = FMath::Clamp(NewVoiceVolumeMultiplier, 0.0f, UMD_CharacterVOIPTalker::MaxVolumeMultiplier);
	VoiceVolumeMultiplier = ClampedVoiceVolumeMultiplier;
}

EMD_HealthState AMD_PlayerState::GetHealthState() const
{
	if (!IsValid(CharacterAttributeSet))
	{
		return EMD_HealthState::Healthy;
	}

	const float CurrentHealth = CharacterAttributeSet->GetHealth();

	if (CurrentHealth <= 0.0f)
	{
		return EMD_HealthState::Dead;
	}

	if (CurrentHealth <= InjuredHealthThreshold)
	{
		return EMD_HealthState::Injured;
	}

	return EMD_HealthState::Healthy;
}

bool AMD_PlayerState::HasPurchasedShopItem(FPrimaryAssetId ItemAssetId) const
{
	// 타입과 이름이 모두 일치하는 유효한 구매 이력 확인
	return ItemAssetId.IsValid() == true && PurchasedShopItemIds.Contains(ItemAssetId) == true;
}

bool AMD_PlayerState::RecordPurchasedShopItem(FPrimaryAssetId ItemAssetId)
{
	// 서버 권한과 전체 식별자 및 중복 여부를 확인하여 잘못된 소유권 기록 차단
	if (HasAuthority() == false || ItemAssetId.IsValid() == false || PurchasedShopItemIds.Contains(ItemAssetId) == true)
	{
		// 유효하지 않거나 이미 기록된 구매 요청의 실패 전달
		return false;
	}

	// 구매가 완료된 품목의 전체 식별자 기록
	PurchasedShopItemIds.Add(ItemAssetId);
	// 복제 배열을 에셋 타입과 이름 순서로 유지하여 불필요한 변경 방지
	PurchasedShopItemIds.Sort([](FPrimaryAssetId Left, FPrimaryAssetId Right)
	{
		// 프로세스가 달라도 동일한 순서를 유지하는 타입 및 이름 비교
		return Left.LexicalLess(Right);
	});

	// 리슨 서버 화면과 소유 클라이언트의 구매 상태 갱신
	OnRep_PurchasedShopItemIds();
	// 새 구매 이력의 복제 갱신 요청
	ForceNetUpdate();
	// 구매 이력 기록 성공 전달
	return true;
}

void AMD_PlayerState::OnRep_PurchasedShopItemIds()
{
	// 플레이어의 화면에 보여지는 상점 UI를 갱신하도록 UI 소유 플레이어 변경 이벤트 호출
	OnShopOwnershipChanged.Broadcast();
}

void AMD_PlayerState::HandleHealthChanged(const FOnAttributeChangeData& ChangeData)
{
	RefreshHealthState();
}

void AMD_PlayerState::RefreshHealthState()
{
	const EMD_HealthState NewState = GetHealthState();

	// 같은 구간 안의 HP 변동(80 → 60)은 전이가 아니므로 무시한다.
	// 이 비교가 없으면 오염수 지속 피해에서 로그가 초당 수십 번 쏟아진다.
	if (NewState == CachedHealthState)
	{
		return;
	}

	CachedHealthState = NewState;

	if (IsValid(AbilitySystemComponent))
	{
		// 전이 경로가 여러 조합이라 매번 둘 다 지우고 현재 상태만 붙인다.
		AbilitySystemComponent->RemoveLooseGameplayTag(MDGameplayTags::State_Injured);
		AbilitySystemComponent->RemoveLooseGameplayTag(MDGameplayTags::State_Dead);

		if (NewState == EMD_HealthState::Injured)
		{
			AbilitySystemComponent->AddLooseGameplayTag(MDGameplayTags::State_Injured);
		}
		else if (NewState == EMD_HealthState::Dead)
		{
			AbilitySystemComponent->AddLooseGameplayTag(MDGameplayTags::State_Dead);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[Health][%s] 상태 전환 → %d (HP %.1f)"),
		HasAuthority() ? TEXT("Server") : TEXT("Client"),
		static_cast<int32>(NewState),
		IsValid(CharacterAttributeSet) ? CharacterAttributeSet->GetHealth() : -1.0f);

	OnHealthStateChanged.Broadcast(NewState);

	//부활, 타이머로직은 서버에서 처리한다.
	if (NewState == EMD_HealthState::Dead && HasAuthority())
	{
		// 사망 감점은 상태 전이 시점에 1회만 (기획 05 S-10 사망 -30)
		if (AMD_GameStateBase* GS = GetWorld() ? GetWorld()->GetGameState<AMD_GameStateBase>() : nullptr)
		{
			GS->AddScoreEvent(MDGameplayTags::Score_Penalty_Death);
		}

		StartReviveTimer();
	}
}

void AMD_PlayerState::StartReviveTimer()
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	World->GetTimerManager().SetTimer(
		ReviveTimerHandle,
		this,
		&AMD_PlayerState::Revive,
		ReviveDelaySeconds,
		false);
}

void AMD_PlayerState::Revive()
{
	if (!HasAuthority() || !IsValid(CharacterAttributeSet))
	{
		return;
	}

	const float ReviveAmount = CharacterAttributeSet->GetMaxHealth() * ReviveHealthRatio;
	CharacterAttributeSet->SetHealth(ReviveAmount);

	UE_LOG(LogTemp, Log, TEXT("[Health][Server] 부활 (HP %.1f)"), ReviveAmount);
}
