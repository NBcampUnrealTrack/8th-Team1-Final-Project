#include "Ability/Interaction/MD_GA_GlassMarker.h"

#include "Character/MD_Character.h"
#include "Component/Equipment/MD_GlassMarkerComponent.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "GameplayTags/MD_GameplayTags.h"

UMD_GA_GlassMarker::UMD_GA_GlassMarker()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ReplicationPolicy = EGameplayAbilityReplicationPolicy::ReplicateNo;
	bServerRespectsRemoteAbilityCancellation = true;
	SetAssetTags(FGameplayTagContainer(MDGameplayTags::Ability_UtilityRepair_GlassMarker));
	ActivationBlockedTags.AddTag(MDGameplayTags::State_Dead);
}

bool UMD_GA_GlassMarker::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags)) return false;
	const AMD_Character* Character = Cast<AMD_Character>(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr);
	const UMD_ToolDefinition* SourceTool = ActorInfo ? Cast<UMD_ToolDefinition>(GetSourceObject(Handle, ActorInfo)) : nullptr;
	const UMD_GlassMarkerComponent* Marker = Character ? Character->GetGlassMarkerComponent() : nullptr;
	return Marker && IsValid(SourceTool) && Marker->GetActiveToolDefinition() == SourceTool
		&& Marker->CanUseEquippedMarker();
}

void UMD_GA_GlassMarker::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	AMD_Character* Character = Cast<AMD_Character>(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr);
	UMD_GlassMarkerComponent* Marker = Character ? Character->GetGlassMarkerComponent() : nullptr;
	const UMD_ToolDefinition* SourceTool = ActorInfo ? Cast<UMD_ToolDefinition>(GetSourceObject(Handle, ActorInfo)) : nullptr;
	if (!Marker || !IsValid(SourceTool) || Marker->GetActiveToolDefinition() != SourceTool
		|| !CommitAbility(Handle, ActorInfo, ActivationInfo) || !Marker->BeginUse(Handle))
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
}

void UMD_GA_GlassMarker::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	AMD_Character* Character = Cast<AMD_Character>(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr);
	if (Character && Character->GetGlassMarkerComponent()) Character->GetGlassMarkerComponent()->FinishUse(Handle);
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
