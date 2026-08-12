// Fill out your copyright notice in the Description page of Project Settings.


#include "Component/Oxygen/MD_OxygenComponent.h"

#include "AbilitySystemComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

#include "Character/MD_Character.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Gameplay/Attribute/MD_CharacterAttributeSet.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Player/MD_PlayerState.h"
#include "Util/MD_LogUtility.h"
#include "Data/Equipment/MD_ToolDefinition.h"

UMD_OxygenComponent::UMD_OxygenComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	SetIsReplicatedByDefault(true);
}

void UMD_OxygenComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UMD_OxygenComponent, BreathState);
}


void UMD_OxygenComponent::BeginPlay()
{
	Super::BeginPlay();

	const AActor* OwnerActor = GetOwner();

	//GE로직과 판정은 서버에서 관리
	if (!IsValid(OwnerActor) || !OwnerActor->HasAuthority())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	// ASC는 PlayerState에 있어 PossessedBy 이후에야 유효하다.
	// 재시도 타이머를 따로 두지 않고, 판정 타이머 안에서 늦은 초기화까지 같이 처리한다.
	World->GetTimerManager().SetTimer(
		EvaluationTimerHandle,
		this,
		&UMD_OxygenComponent::EvaluateBreathState,
		EvaluationInterval,
		true);
}

void UMD_OxygenComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(EvaluationTimerHandle);
	}


	//어트리뷰트 변경 델리게이트 제거
	if (UAbilitySystemComponent* ASC = BoundASC.Get())
	{
		ASC->GetGameplayAttributeValueChangeDelegate(UMD_CharacterAttributeSet::GetOxygenAttribute()).Remove(OxygenChangedHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void UMD_OxygenComponent::RefillOxygen()
{
	UMD_CharacterAttributeSet* Attributes = GetAttributeSet();
	if (!IsValid(Attributes))
	{
		return;
	}

	Attributes->SetOxygen(Attributes->GetMaxOxygen());
}

void UMD_OxygenComponent::UseOxygenCan()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	AMD_Character* OwnerCharacter = Cast<AMD_Character>(GetOwner());
	UMD_EquipHotbarComponent* HotbarComponent = IsValid(OwnerCharacter) == true ? OwnerCharacter->GetHotbarComponent() : nullptr;

	// 효과를 적용할 수 없거나 구매한 산소 캔이 없으면 무료 사용을 막기 위한 검사
	if (IsValid(ASC) == false || OxygenCanEffectClass == nullptr || IsValid(OxygenCanToolDefinition) == false || IsValid(HotbarComponent) == false)
	{
		return;
	}

	// 산소 캔 소비 처리
	bool bConsumeSuccess = HotbarComponent->TryConsumeTool(OxygenCanToolDefinition, 1);
	if (bConsumeSuccess == false)
	{
		return;
	}

	ApplyPassiveEffect(ASC, OxygenCanEffectClass);
}

void UMD_OxygenComponent::SetOxygenTankEquipped(bool bEquipped)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!IsValid(ASC))
	{
		return;
	}

	if (bEquipped)
	{
		// 중복 장착 방지
		if (OxygenTankEffectHandle.IsValid())
		{
			return;
		}

		OxygenTankEffectHandle = ApplyPassiveEffect(ASC, OxygenTankEffectClass);
	}

	else
	{
		if (!OxygenTankEffectHandle.IsValid())
		{
			return;
		}

		ASC->RemoveActiveGameplayEffect(OxygenTankEffectHandle);

		OxygenTankEffectHandle = FActiveGameplayEffectHandle();
	}
}

void UMD_OxygenComponent::EvaluateBreathState()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!IsValid(ASC))
	{
		return;
	}

	EnsureInitialized(ASC);

	const EMD_BreathState NewState = CalculateBreathState();

	if (NewState == BreathState)
	{
		return;
	}

	BreathState = NewState;

	ApplyBreathStateTag();

	// 모선 갑판도 물 밖이라 이 조건에 자연히 포함된다.
	if (NewState == EMD_BreathState::Surface)
	{
		RefillOxygen();
	}

	if (bLogBreathState)
	{
		const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());

		const UCharacterMovementComponent* Movement = IsValid(OwnerCharacter) ? OwnerCharacter->GetCharacterMovement() : nullptr;

		UE_LOG(MarineDemolition, Log,
			TEXT("[Oxygen][Server] 호흡 상태 → %s (InWater %d, ImmersionDepth %.2f)"),
			NewState == EMD_BreathState::Submerged ? TEXT("잠수") : TEXT("수면 위"),
			IsValid(Movement) ? Movement->IsInWater() : -1,
			IsValid(Movement) ? Movement->ImmersionDepth() : -1.0f);
	}

	OnBreathStateChanged.Broadcast(NewState);
}

EMD_BreathState UMD_OxygenComponent::CalculateBreathState() const
{
	const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());

	const UCharacterMovementComponent* Movement =
		IsValid(OwnerCharacter) ? OwnerCharacter->GetCharacterMovement() : nullptr;

	if (!IsValid(Movement) || !Movement->IsInWater())
	{
		return EMD_BreathState::Surface;
	}

	// ImmersionDepth()는 캡슐 컴포넌트가 물 볼륨에 잠긴 비율.
	// IsInWater()만 보면 머리를 수면 위로 내밀어도 계속 소모되므로 잠긴 깊이까지 본다.
	return Movement->ImmersionDepth() >= SubmergedImmersionThreshold
		? EMD_BreathState::Submerged
		: EMD_BreathState::Surface;
}

void UMD_OxygenComponent::EnsureInitialized(UAbilitySystemComponent* ASC)
{
	if (bInitialized)
	{
		return;
	}

	bInitialized = true;
	BoundASC = ASC;

	//Oxygen 0 상태 알림.
	OxygenChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(
		UMD_CharacterAttributeSet::GetOxygenAttribute())
		.AddUObject(this, &UMD_OxygenComponent::HandleOxygenChanged);

	// 두 GE는 여기서 한 번만 붙이고, 이후에는 태그로만 켜고 끈다.
	// 잠수할 때마다 Apply/Remove 하면 주기 타이머가 리셋돼 소모를 회피할 수 있다.
	ApplyPassiveEffect(ASC, OxygenDrainEffectClass);
	ApplyPassiveEffect(ASC, OxygenDepletedDamageEffectClass);

	// ASC 준비 전에 구매한 확장 탱크 아이템이 존재하면 해당 효과를 적용
	const AMD_Character* OwnerCharacter = Cast<AMD_Character>(GetOwner());
	const UMD_EquipHotbarComponent* HotbarComponent = IsValid(OwnerCharacter) == true ? OwnerCharacter->GetHotbarComponent() : nullptr;
	if (IsValid(HotbarComponent) == true && IsValid(OxygenTankToolDefinition) == true && HotbarComponent->GetToolCount(OxygenTankToolDefinition) > 0)
	{
		SetOxygenTankEquipped(true);
	}
}

FActiveGameplayEffectHandle UMD_OxygenComponent::ApplyPassiveEffect(UAbilitySystemComponent* ASC, const TSubclassOf<UGameplayEffect>& EffectClass)
{
	if (!EffectClass)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("[Oxygen] 지속 GE 클래스가 비어 있다. BP_MDPlayerCharacter의 OxygenComponent에서 지정할 것"));
		return FActiveGameplayEffectHandle();
	}

	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(this);

	const FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(EffectClass, 1.0f, Context);

	if (!SpecHandle.IsValid())
	{
		return FActiveGameplayEffectHandle();
	}

	return ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
}
void UMD_OxygenComponent::HandleOxygenChanged(const FOnAttributeChangeData& ChangeData)
{
	UAbilitySystemComponent* ASC = BoundASC.Get();
	if (!IsValid(ASC))
	{
		return;
	}

	const bool bDepleted = ChangeData.NewValue <= 0.0f;

	// 같은 구간 안의 변동(200 → 199)에서는 태그를 다시 붙이지 않는다
	if (bDepleted == bOxygenDepleted)
	{
		return;
	}

	bOxygenDepleted = bDepleted;

	if (bDepleted)
	{
		ASC->AddLooseGameplayTag(MDGameplayTags::State_Oxygen_Depleted);
	}
	else
	{
		ASC->RemoveLooseGameplayTag(MDGameplayTags::State_Oxygen_Depleted);
	}

	UE_LOG(MarineDemolition, Log, TEXT("[Oxygen][Server] 질식 상태 %s (산소 %.1f)"),
		bDepleted ? TEXT("진입") : TEXT("해제"), ChangeData.NewValue);
}

void UMD_OxygenComponent::ApplyBreathStateTag()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!IsValid(ASC))
	{
		return;
	}

	if (BreathState == EMD_BreathState::Submerged)
	{
		ASC->AddLooseGameplayTag(MDGameplayTags::State_Submerged);
	}
	else
	{
		ASC->RemoveLooseGameplayTag(MDGameplayTags::State_Submerged);
	}
}

void UMD_OxygenComponent::OnRep_BreathState()
{
	ApplyBreathStateTag();

	OnBreathStateChanged.Broadcast(BreathState);
}

UAbilitySystemComponent* UMD_OxygenComponent::GetAbilitySystemComponent() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());

	AMD_PlayerState* MDPlayerState =
		IsValid(OwnerPawn) ? OwnerPawn->GetPlayerState<AMD_PlayerState>() : nullptr;

	return IsValid(MDPlayerState) ? MDPlayerState->GetAbilitySystemComponent() : nullptr;
}

UMD_CharacterAttributeSet* UMD_OxygenComponent::GetAttributeSet() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());

	AMD_PlayerState* MDPlayerState =
		IsValid(OwnerPawn) ? OwnerPawn->GetPlayerState<AMD_PlayerState>() : nullptr;

	return IsValid(MDPlayerState) ? MDPlayerState->GetCharacterAttributeSet() : nullptr;
}

