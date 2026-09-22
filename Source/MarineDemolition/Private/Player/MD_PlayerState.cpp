// Fill out your copyright notice in the Description page of Project Settings.


#include "Player/MD_PlayerState.h"
#include "Types/SaveGame/MD_SaveDataTypes.h"
#include "Audio/MD_AudioSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Character/MD_Character.h"
#include "Data/Customization/MD_CharacterRosterData.h"
#include "Gameplay/Customization/MD_CharacterAppearanceSettings.h"
#include "Game/MD_GameStateBase.h"
#include "Gameplay/Attribute/MD_CharacterAttributeSet.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Online/Voice/MD_CharacterVOIPTalker.h"
#include "AbilitySystemComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Util/MD_LogUtility.h"

void AMD_PlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMD_PlayerState, ReturnStatus);
	DOREPLIFETIME(AMD_PlayerState, ReplicatedAppearance);
	DOREPLIFETIME_CONDITION(AMD_PlayerState, PurchasedShopItemIds, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AMD_PlayerState, BlockedShopItemIds, COND_OwnerOnly);
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
		// 구매 이력과 같은 수명으로 업그레이드 소스 구매 금지 유지
		TargetPlayerState->BlockedShopItemIds = BlockedShopItemIds;

		// 캐릭터 외형을 Seamless Travel 이후에도 유지하기 위한 복사
		TargetPlayerState->ReplicatedAppearance = ReplicatedAppearance;
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

bool AMD_PlayerState::RecordPurchasedShopItem(FPrimaryAssetId ItemAssetId, FPrimaryAssetId SourceAssetId)
{
	// 성공한 서버 구매의 유효한 식별자만 이력에 반영
	if (HasAuthority() == false || ItemAssetId.IsValid() == false)
	{
		// 구매 상태를 기록할 수 없는 원인의 프로젝트 로그 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 구매 기록의 서버 권한 또는 AssetId가 유효하지 않음"), *UE_CALL_LOCATIONINFO);
		// 구매 상태를 변경하지 않은 기록 실패 전달
		return false;
	}
	// 실제로 추가할 구매 이력만 정렬과 통지 대상으로 구분
	bool bPurchaseAdded = PurchasedShopItemIds.Contains(ItemAssetId) == false;
	// 유효하고 아직 등록되지 않은 소스만 변경 대상으로 구분
	bool bSourceAdded = SourceAssetId.IsValid() == true && BlockedShopItemIds.Contains(SourceAssetId) == false;
	// 두 목록이 같은 재구매에서는 정렬과 복제 통지 생략
	if (bPurchaseAdded == false && bSourceAdded == false)
	{
		// 기존 기록을 유지한 재구매 성공 전달
		return true;
	}
	// 새 구매 이력이 생긴 경우에만 구매 목록 변경
	if (bPurchaseAdded == true)
	{
		// 중복 여부 검사를 통과한 전체 식별자 추가
		PurchasedShopItemIds.Add(ItemAssetId);
		// 실제 변경된 구매 목록만 고정 순서로 정렬
		PurchasedShopItemIds.Sort([](FPrimaryAssetId Left, FPrimaryAssetId Right)
		{
			// 타입과 이름을 함께 고려한 고정 정렬 순서
			return Left.LexicalLess(Right);
		});
	}
	// 새 업그레이드 소스가 있는 경우에만 구매 금지 목록 변경
	if (bSourceAdded == true)
	{
		// 중복 여부 검사를 통과한 소스 식별자 추가
		BlockedShopItemIds.Add(SourceAssetId);
		// 실제 변경된 구매 금지 목록만 고정 순서로 정렬
		BlockedShopItemIds.Sort([](FPrimaryAssetId Left, FPrimaryAssetId Right)
		{
			// 전체 식별자에 따른 구매 금지 목록 정렬
			return Left.LexicalLess(Right);
		});
	}
	// 두 목록의 변경을 마친 상태를 리슨 서버 UI에 한 번 전달
	OnRep_ShopPurchaseState();
	// 변경된 구매 상태의 소유 클라이언트 복제 요청
	ForceNetUpdate();
	// 첫 구매와 재구매의 상태 기록 성공 전달
	return true;
}

void AMD_PlayerState::ResetShopToolProgress(TArray<FPrimaryAssetId>& ToolAssetIds)
{
	// 서버에서 확정한 도구 판매에만 개인 진행 상태 변경 허용
	if (HasAuthority() == false)
	{
		// 클라이언트의 구매 이력 초기화 차단
		return;
	}
	// 해당 도구 단계들의 무료 재구매 이력 제거
	int32 RemovedCount = PurchasedShopItemIds.RemoveAll([&ToolAssetIds](FPrimaryAssetId ItemAssetId)
	{
		// 판매 도구의 업그레이드 경로에 포함된 이력만 선택
		return ToolAssetIds.Contains(ItemAssetId);
	});
	// 기본 도구부터 다시 구매할 수 있도록 같은 경로의 구매 금지 제거
	RemovedCount += BlockedShopItemIds.RemoveAll([&ToolAssetIds](FPrimaryAssetId ItemAssetId)
	{
		// 다른 도구 계열의 구매 금지를 보존할 초기화 대상 검사
		return ToolAssetIds.Contains(ItemAssetId);
	});
	// 실제 이력 변경이 있을 때만 화면 갱신과 복제 요청
	if (RemovedCount > 0)
	{
		// 두 목록을 함께 초기화한 상태를 서버 상점 화면에 전달
		OnRep_ShopPurchaseState();
		// 소유 클라이언트에 변경된 구매 및 잠금 이력 복제 요청
		ForceNetUpdate();
	}
}

void AMD_PlayerState::CollectShopPurchaseState(FMD_PlayerData& SaveData) const
{
	// 구매하지 않았거나 판매로 초기화된 빈 이력까지 명시적으로 저장
	SaveData.bHasShopPurchaseState = true;
	// 상위 도구 해금과 무료 재구매 기준 보존
	SaveData.PurchasedShopItemIds = PurchasedShopItemIds;
	// 업그레이드로 대체된 선행 도구 구매 금지 보존
	SaveData.BlockedShopItemIds = BlockedShopItemIds;
}

bool AMD_PlayerState::RestoreShopPurchaseState(FMD_PlayerData& SaveData)
{
	// 준비되지 않은 호환 기록이나 손상된 이력의 서버 상태 변경 차단
	if (HasAuthority() == false || SaveData.bHasShopPurchaseState == false || SaveData.IsStructurallyValid() == false)
	{
		// 기존 개인 이력을 유지한 실패 반환
		return false;
	}
	// 같은 파일 재적용의 불필요한 화면 갱신과 복제 생략
	if (PurchasedShopItemIds == SaveData.PurchasedShopItemIds && BlockedShopItemIds == SaveData.BlockedShopItemIds)
	{
		// 동일한 이력 복원 성공 반환
		return true;
	}
	// 다른 슬롯이나 이전 세션의 구매 이력을 저장된 값으로 교체
	PurchasedShopItemIds = SaveData.PurchasedShopItemIds;
	// 구매 이력과 같은 시점의 재구매 제한 교체
	BlockedShopItemIds = SaveData.BlockedShopItemIds;
	// 두 목록을 함께 반영한 서버 상점 화면 갱신
	OnRep_ShopPurchaseState();
	// 기존 소유자 전용 복제를 통한 클라이언트 상점 상태 동기화
	ForceNetUpdate();
	// 구매 효과나 결제 없이 개인 진행 복원 완료
	return true;
}

bool AMD_PlayerState::IsShopItemPurchaseBlocked(FPrimaryAssetId ItemAssetId)
{
	// 무효 식별자를 제외하고 등록된 구매 금지 목록만 확인
	return ItemAssetId.IsValid() == true && BlockedShopItemIds.Contains(ItemAssetId) == true;
}

void AMD_PlayerState::OnRep_ShopPurchaseState()
{
	// 플레이어의 화면에 보여지는 상점 UI를 갱신하도록 UI 소유 플레이어 변경 이벤트 호출
	OnShopOwnershipChanged.Broadcast();
}

void AMD_PlayerState::HandleHealthChanged(const FOnAttributeChangeData& ChangeData)
{
	// 실제 HP 감소마다 소유 플레이어에게만 재생한다. 부상 상태 전이와는 별개다.
	const APlayerController* PlayerController = GetPlayerController();
	if (ChangeData.NewValue < ChangeData.OldValue && ChangeData.OldValue > 0.0f
		&& PlayerController && PlayerController->IsLocalController()
		&& GetNetMode() != NM_DedicatedServer)
	{
		if (UMD_AudioSubsystem* Audio = GetWorld()->GetSubsystem<UMD_AudioSubsystem>())
		{
			Audio->PlayEvent2D(FGameplayTag::RequestGameplayTag(TEXT("SFX.Character.Damage.Tick")));
		}
	}

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

	// 부활 상태 전이 전에 확장 탱크가 반영된 현재 최대 산소량으로 완충
	CharacterAttributeSet->SetOxygen(CharacterAttributeSet->GetMaxOxygen());

	const float ReviveAmount = CharacterAttributeSet->GetMaxHealth() * ReviveHealthRatio;
	CharacterAttributeSet->SetHealth(ReviveAmount);

	UE_LOG(LogTemp, Log, TEXT("[Health][Server] 부활 (HP %.1f)"), ReviveAmount);
}

void AMD_PlayerState::SetAppearance(const FMD_CharacterAppearance& NewAppearance)
{
	if (HasAuthority() == false)
	{
		return;
	}

	// 클라이언트가 보낸 인덱스는 로스터 범위로 정리한다
	const UMD_CharacterAppearanceSettings* Settings = UMD_CharacterAppearanceSettings::Get();
	const UMD_CharacterRosterData* Roster = IsValid(Settings) == true ? Settings->GetRoster() : nullptr;
	ReplicatedAppearance = IsValid(Roster) == true ? Roster->Sanitize(NewAppearance) : NewAppearance;

	// Listen Server 호스트에서도 즉시 캐릭터에 반영
	OnRep_ReplicatedAppearance();
	ForceNetUpdate();
}

void AMD_PlayerState::OnRep_ReplicatedAppearance()
{
	OnAppearanceChanged.Broadcast();
}
