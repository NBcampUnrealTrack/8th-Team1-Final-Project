// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ability/Scrape/MD_GA_Scrape.h"

#include "AbilitySystemComponent.h"
#include "Character/MD_Character.h"
#include "Component/Scrape/MD_ScrapeToolComponent.h"
#include "Component/Scrape/MD_StainScraperComponent.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "Engine/World.h"
#include "GameplayPrediction.h"
#include "TimerManager.h"

FString FGameplayAbilityTargetData_ScrapeFirstAim::ToString() const
{
	return FString::Printf(
		TEXT("ScrapeFirstAim(Session=%llu, Sequence=%u)"),
		static_cast<unsigned long long>(FocusSessionId),
		AimSequence);
}

bool FGameplayAbilityTargetData_ScrapeFirstAim::NetSerialize(
	FArchive& Ar,
	UPackageMap* Map,
	bool& bOutSuccess)
{
	Ar << FocusSessionId;
	Ar.SerializeIntPacked(AimSequence);
	Ar << SampleTimeSeconds;

	bool bDirectionSuccess = true;
	CameraRelativeAimDirection.NetSerialize(Ar, Map, bDirectionSuccess);
	bOutSuccess = !Ar.IsError() && bDirectionSuccess;
	return true;
}

UMD_GA_Scrape::UMD_GA_Scrape()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ReplicationPolicy = EGameplayAbilityReplicationPolicy::ReplicateNo;
	bServerRespectsRemoteAbilityCancellation = true;
}

void UMD_GA_Scrape::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	bWaitingForFirstAim = false;
	RegisteredAbilityHandle = FGameplayAbilitySpecHandle();
	RegisteredActivationPredictionKey = FPredictionKey();
	RegisteredFocusSessionId = 0;

	AMD_Character* const Character =
		Cast<AMD_Character>(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr);
	UMD_ScrapeToolComponent* const ScrapeTool =
		Character ? Character->GetScrapeToolComponent() : nullptr;
	UAbilitySystemComponent* const AbilitySystem =
		ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (Character && ScrapeTool && AbilitySystem && !ScrapeTool->IsScrapeFocusActive())
	{
		UMD_StainScraperComponent* StainScraper = Character->GetStainScraperComponent();
		bStainStroke = true;
		// A predicted activation must still belong to the currently equipped tool on the server.
		const UMD_ToolDefinition* SourceTool = Cast<UMD_ToolDefinition>(GetSourceObject(Handle, ActorInfo));
		if (!StainScraper || !SourceTool || !Character->GetHotbarComponent()
			|| Character->GetHotbarComponent()->GetCurrentTool() != SourceTool
			|| !CommitAbility(Handle, ActorInfo, ActivationInfo) || !StainScraper->StartStroke(Handle))
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}
		GetWorld()->GetTimerManager().SetTimer(StainStrokeTimer, this,
			&UMD_GA_Scrape::FinishStainStroke, StainScraper->GetStrokeDuration(), false);
		return;
	}
	if (!Character
		|| !ScrapeTool
		|| !AbilitySystem
		|| !ScrapeTool->IsScrapeFocusReady())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	const bool bIsAuthority = ActorInfo->IsNetAuthority();
	const bool bIsLocallyControlled = ActorInfo->IsLocallyControlled();
	uint64 FocusSessionId = ScrapeTool->GetActiveScrapeFocusSessionId();
	if (FocusSessionId == 0
		|| (bIsAuthority
			&& !ScrapeTool->CanActivateAuthoritativeScrape(FocusSessionId)))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	RegisteredAbilityHandle = Handle;
	RegisteredActivationPredictionKey = ActivationInfo.GetActivationPredictionKey();
	RegisteredFocusSessionId = FocusSessionId;
	if (!Character->RegisterActiveScrapeAbility(Handle, FocusSessionId))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (bIsAuthority && !bIsLocallyControlled)
	{
		// The activation and TargetData use the ASC channel but can still be
		// processed in either order. GAS caches early TargetData for this key.
		WaitForReplicatedFirstAim(Handle, ActorInfo, ActivationInfo);
		return;
	}

	uint32 AimSequence = 0;
	double SampleTimeSeconds = 0.0;
	FVector CameraRelativeAimDirection = FVector::ZeroVector;
	uint64 FirstAimSessionId = 0;
	if (!ScrapeTool->BuildScrapeAbilityFirstAim(
			FirstAimSessionId,
			AimSequence,
			SampleTimeSeconds,
			CameraRelativeAimDirection)
		|| FirstAimSessionId != FocusSessionId)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (bIsAuthority)
	{
		if (!ScrapeTool->AuthorizeScrapeAbilityFirstAim(
				FocusSessionId,
				AimSequence,
				SampleTimeSeconds,
				CameraRelativeAimDirection)
			|| !ScrapeTool->IsAuthoritativeFirstScrapeAimApproved(FocusSessionId)
			|| !CommitAndStartScraping(Handle, ActorInfo, ActivationInfo, ScrapeTool))
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		}
		return;
	}

	// Prediction is visual/local only. No contamination, load, or barnacle work is
	// authoritative on this path. The server remains paused until TargetData passes.
	if (!CommitAndStartScraping(Handle, ActorInfo, ActivationInfo, ScrapeTool)
		|| !SendFirstAimTargetData(
			AbilitySystem,
			Handle,
			ActivationInfo,
			FocusSessionId,
			AimSequence,
			SampleTimeSeconds,
			CameraRelativeAimDirection))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
	}
}

bool UMD_GA_Scrape::CommitAndStartScraping(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo& ActivationInfo,
	UMD_ScrapeToolComponent* const ScrapeTool)
{
	if (!ScrapeTool || !CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		return false;
	}

	ScrapeTool->StartScraping();
	return ScrapeTool->IsScraping();
}

bool UMD_GA_Scrape::SendFirstAimTargetData(
	UAbilitySystemComponent* const AbilitySystem,
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActivationInfo& ActivationInfo,
	const uint64 FocusSessionId,
	const uint32 AimSequence,
	const double SampleTimeSeconds,
	const FVector& CameraRelativeAimDirection)
{
	const double DirectionSizeSquared = CameraRelativeAimDirection.SizeSquared();
	if (!AbilitySystem
		|| !Handle.IsValid()
		|| !ActivationInfo.GetActivationPredictionKey().IsValidKey()
		|| FocusSessionId == 0
		|| AimSequence == 0
		|| !FMath::IsFinite(SampleTimeSeconds)
		|| SampleTimeSeconds < 0.0
		|| CameraRelativeAimDirection.ContainsNaN()
		|| !FMath::IsFinite(DirectionSizeSquared)
		|| !FMath::IsNearlyEqual(DirectionSizeSquared, 1.0, 0.01))
	{
		return false;
	}

	FGameplayAbilityTargetData_ScrapeFirstAim* const FirstAim =
		new FGameplayAbilityTargetData_ScrapeFirstAim();
	FirstAim->FocusSessionId = FocusSessionId;
	FirstAim->AimSequence = AimSequence;
	FirstAim->SampleTimeSeconds = SampleTimeSeconds;
	FirstAim->CameraRelativeAimDirection =
		FVector_NetQuantizeNormal(CameraRelativeAimDirection);

	FGameplayAbilityTargetDataHandle TargetData(FirstAim);
	FScopedPredictionWindow PredictionWindow(AbilitySystem, true);
	AbilitySystem->CallServerSetReplicatedTargetData(
		Handle,
		ActivationInfo.GetActivationPredictionKey(),
		TargetData,
		FGameplayTag(),
		AbilitySystem->ScopedPredictionKey);
	return true;
}

void UMD_GA_Scrape::WaitForReplicatedFirstAim(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo& ActivationInfo)
{
	UAbilitySystemComponent* const AbilitySystem =
		ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!AbilitySystem
		|| !Handle.IsValid()
		|| !ActivationInfo.GetActivationPredictionKey().IsValidKey())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	bWaitingForFirstAim = true;
	FirstAimSetDelegateHandle = AbilitySystem->AbilityTargetDataSetDelegate(
		Handle,
		ActivationInfo.GetActivationPredictionKey()).AddUObject(
			this,
			&UMD_GA_Scrape::HandleReplicatedFirstAim);
	FirstAimCancelledDelegateHandle =
		AbilitySystem->AbilityTargetDataCancelledDelegate(
			Handle,
			ActivationInfo.GetActivationPredictionKey()).AddUObject(
				this,
				&UMD_GA_Scrape::HandleReplicatedFirstAimCancelled);

	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			FirstAimTimeoutHandle,
			this,
			&UMD_GA_Scrape::HandleFirstAimTimeout,
			FMath::Max(StartAimTimeoutSeconds, 0.05f),
			false);
	}

	// Handles TargetData that arrived before the server activation delegate existed.
	AbilitySystem->CallReplicatedTargetDataDelegatesIfSet(
		Handle,
		ActivationInfo.GetActivationPredictionKey());
}

void UMD_GA_Scrape::HandleReplicatedFirstAim(
	const FGameplayAbilityTargetDataHandle& TargetData,
	const FGameplayTag ApplicationTag)
{
	if (!bWaitingForFirstAim || !IsActive())
	{
		return;
	}

	const FGameplayAbilityActorInfo* const ActorInfo = GetCurrentActorInfo();
	UAbilitySystemComponent* const AbilitySystem =
		ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	AMD_Character* const Character =
		Cast<AMD_Character>(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr);
	UMD_ScrapeToolComponent* const ScrapeTool =
		Character ? Character->GetScrapeToolComponent() : nullptr;

	const FGameplayAbilityTargetData* const RawData =
		TargetData.Num() == 1 ? TargetData.Get(0) : nullptr;
	const FGameplayAbilityTargetData_ScrapeFirstAim* const FirstAim =
		RawData && RawData->GetScriptStruct()
			== FGameplayAbilityTargetData_ScrapeFirstAim::StaticStruct()
			? static_cast<const FGameplayAbilityTargetData_ScrapeFirstAim*>(RawData)
			: nullptr;
	// ConsumeClientReplicatedTargetData may release the cached handle that owns
	// FirstAim, so copy every value before consuming it.
	const bool bHasExpectedFirstAim = FirstAim != nullptr;
	const uint64 ReceivedSessionId = FirstAim ? FirstAim->FocusSessionId : 0;
	const uint32 ReceivedAimSequence = FirstAim ? FirstAim->AimSequence : 0;
	const double ReceivedSampleTimeSeconds = FirstAim
		? FirstAim->SampleTimeSeconds
		: -1.0;
	const FVector AimDirection = FirstAim
		? FVector(FirstAim->CameraRelativeAimDirection)
		: FVector::ZeroVector;

	if (AbilitySystem)
	{
		AbilitySystem->ConsumeClientReplicatedTargetData(
			RegisteredAbilityHandle,
			RegisteredActivationPredictionKey);
		RemoveFirstAimDelegates(AbilitySystem);
	}
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FirstAimTimeoutHandle);
	}
	bWaitingForFirstAim = false;

	// A different session is a harmless stale packet. For the current session,
	// structural, freshness, view-cone, and contact checks belong to the component's
	// single authoritative validation core so the reliable and lossy paths cannot drift.
	const bool bTargetsRegisteredSession = bHasExpectedFirstAim
		&& ReceivedSessionId == RegisteredFocusSessionId;
	if (ActorInfo
		&& ActorInfo->IsNetAuthority()
		&& ScrapeTool
		&& !bHasExpectedFirstAim
		&& ScrapeTool->CanActivateAuthoritativeScrape(
			RegisteredFocusSessionId))
	{
		// A payload for this active prediction key with the wrong structure is not
		// a stale session packet: it violates the first-aim protocol itself.
		ScrapeTool->RejectScrapeAbilityFirstAimProtocol(
			RegisteredFocusSessionId);
	}
	if (!ActorInfo
		|| !ActorInfo->IsNetAuthority()
		|| !ScrapeTool
		|| !bTargetsRegisteredSession
		|| !ScrapeTool->CanActivateAuthoritativeScrape(RegisteredFocusSessionId)
		|| !ScrapeTool->AuthorizeScrapeAbilityFirstAim(
			RegisteredFocusSessionId,
			ReceivedAimSequence,
			ReceivedSampleTimeSeconds,
			AimDirection)
		|| !ScrapeTool->IsAuthoritativeFirstScrapeAimApproved(
			RegisteredFocusSessionId)
		|| !CommitAndStartScraping(
			RegisteredAbilityHandle,
			ActorInfo,
			GetCurrentActivationInfo(),
			ScrapeTool))
	{
		EndCurrentAbilityAsCancelled();
	}
}

void UMD_GA_Scrape::HandleReplicatedFirstAimCancelled()
{
	if (!bWaitingForFirstAim)
	{
		return;
	}

	if (UAbilitySystemComponent* const AbilitySystem =
		GetCurrentActorInfo()
			? GetCurrentActorInfo()->AbilitySystemComponent.Get()
			: nullptr)
	{
		AbilitySystem->ConsumeClientReplicatedTargetData(
			RegisteredAbilityHandle,
			RegisteredActivationPredictionKey);
		RemoveFirstAimDelegates(AbilitySystem);
	}
	bWaitingForFirstAim = false;
	EndCurrentAbilityAsCancelled();
}

void UMD_GA_Scrape::HandleFirstAimTimeout()
{
	if (!bWaitingForFirstAim)
	{
		return;
	}

	if (UAbilitySystemComponent* const AbilitySystem =
		GetCurrentActorInfo()
			? GetCurrentActorInfo()->AbilitySystemComponent.Get()
			: nullptr)
	{
		AbilitySystem->ConsumeClientReplicatedTargetData(
			RegisteredAbilityHandle,
			RegisteredActivationPredictionKey);
		RemoveFirstAimDelegates(AbilitySystem);
	}
	bWaitingForFirstAim = false;
	EndCurrentAbilityAsCancelled();
}

void UMD_GA_Scrape::RemoveFirstAimDelegates(
	UAbilitySystemComponent* const AbilitySystem)
{
	if (!AbilitySystem
		|| !RegisteredAbilityHandle.IsValid()
		|| !RegisteredActivationPredictionKey.IsValidKey())
	{
		FirstAimSetDelegateHandle.Reset();
		FirstAimCancelledDelegateHandle.Reset();
		return;
	}

	if (FirstAimSetDelegateHandle.IsValid())
	{
		AbilitySystem->AbilityTargetDataSetDelegate(
			RegisteredAbilityHandle,
			RegisteredActivationPredictionKey).Remove(FirstAimSetDelegateHandle);
		FirstAimSetDelegateHandle.Reset();
	}
	if (FirstAimCancelledDelegateHandle.IsValid())
	{
		AbilitySystem->AbilityTargetDataCancelledDelegate(
			RegisteredAbilityHandle,
			RegisteredActivationPredictionKey).Remove(
				FirstAimCancelledDelegateHandle);
		FirstAimCancelledDelegateHandle.Reset();
	}
}

void UMD_GA_Scrape::EndCurrentAbilityAsCancelled()
{
	if (IsActive())
	{
		EndAbility(
			GetCurrentAbilitySpecHandle(),
			GetCurrentActorInfo(),
			GetCurrentActivationInfo(),
			true,
			true);
	}
}

void UMD_GA_Scrape::FinishStainStroke()
{
	if (IsActive()) EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
}

void UMD_GA_Scrape::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility,
	const bool bWasCancelled)
{
	if (bStainStroke)
	{
		if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(StainStrokeTimer);
		AMD_Character* Character = Cast<AMD_Character>(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr);
		if (Character && Character->GetStainScraperComponent()) Character->GetStainScraperComponent()->CancelStroke(false);
		bStainStroke = false;
		Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
		return;
	}
	UAbilitySystemComponent* const AbilitySystem =
		ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	RemoveFirstAimDelegates(AbilitySystem);
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FirstAimTimeoutHandle);
	}

	AMD_Character* const Character =
		Cast<AMD_Character>(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr);
	if (UMD_ScrapeToolComponent* const ScrapeTool =
		Character ? Character->GetScrapeToolComponent() : nullptr)
	{
		if (ActorInfo && ActorInfo->IsNetAuthority())
		{
			ScrapeTool->ClearScrapeAbilityFirstAimApproval(
				RegisteredFocusSessionId);
		}
		ScrapeTool->StopScraping();
	}
	if (Character)
	{
		Character->UnregisterActiveScrapeAbility(
			Handle,
			RegisteredFocusSessionId);
	}

	bWaitingForFirstAim = false;
	RegisteredAbilityHandle = FGameplayAbilitySpecHandle();
	RegisteredActivationPredictionKey = FPredictionKey();
	RegisteredFocusSessionId = 0;

	Super::EndAbility(
		Handle,
		ActorInfo,
		ActivationInfo,
		bReplicateEndAbility,
		bWasCancelled);
}
