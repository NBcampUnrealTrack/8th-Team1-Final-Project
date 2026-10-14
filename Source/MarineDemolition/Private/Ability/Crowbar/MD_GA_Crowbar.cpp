#include "Ability/Crowbar/MD_GA_Crowbar.h"
#include "Component/Interaction/MD_InteractionComponent.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "GameplayEffectComponents/AbilitiesGameplayEffectComponent.h"
#include "GameFramework/Pawn.h"

UMD_GA_Crowbar::UMD_GA_Crowbar()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	FGameplayTagContainer Tags;
	Tags.AddTag(MDGameplayTags::Ability_UtilityRepair_Crowbar);
	SetAssetTags(Tags);
}

void UMD_GA_Crowbar::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	APawn* Pawn = ActorInfo ? Cast<APawn>(ActorInfo->AvatarActor.Get()) : nullptr;
	if (IsValid(Pawn) && Pawn->IsLocallyControlled())
		if (UMD_InteractionComponent* Interaction = Pawn->FindComponentByClass<UMD_InteractionComponent>())
			Interaction->TryUseCrowbar();
	// Permanent tool: no quantity, durability or charge consumption.
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

UMD_GE_CrowbarGrant::UMD_GE_CrowbarGrant()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
	FGameplayAbilitySpecConfig Config;
	Config.Ability = UMD_GA_Crowbar::StaticClass();
	Config.RemovalPolicy = EGameplayEffectGrantedAbilityRemovePolicy::CancelAbilityImmediately;
	auto* Grants = CreateDefaultSubobject<UAbilitiesGameplayEffectComponent>(TEXT("CrowbarAbilityGrant"));
	Grants->AddGrantedAbilityConfig(Config);
	GEComponents.Add(Grants);
}
