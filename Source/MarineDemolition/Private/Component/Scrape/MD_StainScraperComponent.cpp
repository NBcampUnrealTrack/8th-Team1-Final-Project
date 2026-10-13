#include "Component/Scrape/MD_StainScraperComponent.h"

#include "AbilitySystemComponent.h"
#include "Audio/MD_AudioRelayComponent.h"
#include "Camera/CameraComponent.h"
#include "Character/MD_Character.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Component/Interaction/MD_InteractionComponent.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Gameplay/Contamination/MD_StainTargeting.h"
#include "Gameplay/Contamination/MD_SurfaceStainSize.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "TimerManager.h"
#include "World/Contamination/MD_SurfaceStain.h"
#include "World/Contamination/MD_Barnacle.h"

UMD_StainScraperComponent::UMD_StainScraperComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

bool UMD_StainScraperComponent::CanUseEquippedScraper() const
{
	const AMD_Character* Character = Cast<AMD_Character>(GetOwner());
	if (!Character || !Character->GetController() || !Character->GetHotbarComponent()
		|| !GetWorld() || GetWorld()->IsPaused()
		|| (Character->GetInteractionComponent() && Character->GetInteractionComponent()->IsHolding())) return false;
	const UMD_ToolDefinition* Tool = Character->GetHotbarComponent()->GetCurrentTool();
	const UAbilitySystemComponent* AbilitySystem = Character->GetAbilitySystemComponent();
	return IsValid(Tool) && Tool->ToolAbilityTag.MatchesTagExact(MDGameplayTags::Ability_Scrape)
		&& AbilitySystem && !AbilitySystem->HasMatchingGameplayTag(MDGameplayTags::State_Dead);
}

bool UMD_StainScraperComponent::StartStroke(FGameplayAbilitySpecHandle AbilityHandle)
{
	if (!AbilityHandle.IsValid() || StrokeAbility.IsValid() || !GetWorld() || !CanUseEquippedScraper()
		|| !FMath::IsFinite(StrokeDurationSeconds) || StrokeDurationSeconds < 0.2f || StrokeDurationSeconds > 2.0f
		|| GetWorld()->GetTimeSeconds() < NextStrokeAllowedAt) return false;
	AMD_Character* Character = CastChecked<AMD_Character>(GetOwner());
	StrokeAbility = AbilityHandle;
	StrokeStartedAt = GetWorld()->GetTimeSeconds();
	ChargeUpdatedAt = StrokeStartedAt;
	NextStrokeAllowedAt = StrokeStartedAt + StrokeDurationSeconds;
	StrokeTool = Character->GetHotbarComponent()->GetCurrentTool();
	PlaySwing();
	if (Character->HasAuthority())
	{
		StrokeTarget = TraceTarget();
		GetWorld()->GetTimerManager().SetTimer(ImpactTimer, this,
			&UMD_StainScraperComponent::ApplyImpact, StrokeDurationSeconds * 0.5f, false);
		MulticastSwing();
	}
	return true;
}

AActor* UMD_StainScraperComponent::TraceTarget() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_SurfaceStain_Trace);
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->GetController() || !GetWorld() || !FMath::IsFinite(ReachCm) || ReachCm <= 0.0f) return nullptr;
	// Remote player-camera caches accept client positions. Keep reach anchored to the
	// server's pawn while retaining the authored first-person camera offset.
	const UCameraComponent* Camera = Pawn->FindComponentByClass<UCameraComponent>();
	const FVector Origin = IsValid(Camera) ? Camera->GetComponentLocation() : Pawn->GetPawnViewLocation();
	const FRotator Rotation = Pawn->GetController()->GetControlRotation();
	if (Origin.ContainsNaN() || Rotation.ContainsNaN()) return nullptr;
	const FVector Direction = Rotation.Vector();
	if (Direction.ContainsNaN()) return nullptr;
	FHitResult Hit;
	return FMD_StainTargeting::Trace(*GetWorld(), Origin, Direction, FMath::Min(ReachCm, 250.0f), Hit, Pawn, true)
		? Hit.GetActor() : nullptr;
}

void UMD_StainScraperComponent::UpdateStrokeCharge()
{
	AMD_Character* Character = Cast<AMD_Character>(GetOwner());
	UMD_ToolDefinition* Tool = StrokeTool.Get();
	if (!Character || !Character->HasAuthority() || !GetWorld() || !IsValid(Tool)
		|| Tool->MaxCharge <= 0.0f || !Character->GetHotbarComponent()) return;
	// Settle at impact and finish/cancel, without an idle tick or a per-frame UI refresh.
	const double Now = FMath::Min(double(GetWorld()->GetTimeSeconds()), StrokeStartedAt + StrokeDurationSeconds);
	const float Elapsed = float(FMath::Max(0.0, Now - ChargeUpdatedAt));
	ChargeUpdatedAt = Now; // Advance before inventory callbacks can cancel this stroke.
	if (Elapsed > 0.0f) Character->GetHotbarComponent()->ConsumeToolChargeAmount(Tool, Elapsed);
}

void UMD_StainScraperComponent::ApplyImpact()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_SurfaceStain_Impact);
	UpdateStrokeCharge();
	AMD_Character* Character = Cast<AMD_Character>(GetOwner());
	AActor* Target = StrokeTarget.Get();
	StrokeTarget.Reset(); // Consume before any callback; one stroke cannot hit twice.
	if (!Character || !Character->HasAuthority() || !CanUseEquippedScraper()
		|| Character->GetHotbarComponent()->GetCurrentTool() != StrokeTool.Get()) return;
	if (!IsValid(Target) || TraceTarget() != Target) return;
	const float Charge = Character->GetHotbarComponent()->GetToolCharge(StrokeTool.Get());
	if (AMD_SurfaceStain* Stain = Cast<AMD_SurfaceStain>(Target))
	{
		if (Stain->ApplyScrapeStroke(FMD_SurfaceStainSize::RemovalFromToolStrength(StrokeTool->GetScrapeStainRemovalAmount(Charge)))
			&& Stain->GetRemainingSize() == 0 && Character->GetAudioRelayComponent())
			Character->GetAudioRelayComponent()->PlayConfirmedAtOwnerLocationForAll(FGameplayTag::RequestGameplayTag(TEXT("SFX.Clean.Sludge.Drop"), false));
	}
	else if (AMD_Barnacle* Barnacle = Cast<AMD_Barnacle>(Target))
		Barnacle->ApplyScrapeClickDamage(StrokeTool->GetScrapeBarnacleDamage(Charge));
}

void UMD_StainScraperComponent::PlaySwing()
{
	AMD_Character* Character = Cast<AMD_Character>(GetOwner());
	if (Character && GetWorld() && GetWorld()->GetNetMode() != NM_DedicatedServer) Character->PlayBarnacleScrapeMontage();
}

void UMD_StainScraperComponent::MulticastSwing_Implementation()
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	// The owning client predicted its swing; the server played during StartStroke.
	if (Pawn && !Pawn->HasAuthority() && !Pawn->IsLocallyControlled()) PlaySwing();
}

FVector UMD_StainScraperComponent::GetHandSwingOffset() const
{
	if (!GetWorld() || !StrokeTool.IsValid()) return FVector::ZeroVector;
	const float Phase = (GetWorld()->GetTimeSeconds() - StrokeStartedAt) / StrokeDurationSeconds;
	if (Phase <= 0.0f || Phase >= 1.0f || !CanUseEquippedScraper()) return FVector::ZeroVector;
	// Smooth closed path shared by the arms and their tool socket.
	const float Envelope = FMath::Square(FMath::Sin(PI * Phase));
	return FVector(FMath::Sin(2.0f * PI * Phase) * Envelope * SwingDistanceCm, 0.0, 0.0);
}

void UMD_StainScraperComponent::CancelStroke(bool bCancelAbility)
{
	// Release ability ownership before charge-change delegates can re-enter cancellation.
	const FGameplayAbilitySpecHandle AbilityToCancel = StrokeAbility;
	StrokeAbility = FGameplayAbilitySpecHandle();
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(ImpactTimer);
	UpdateStrokeCharge();
	StrokeTarget.Reset();
	StrokeTool.Reset();
	StrokeStartedAt = -10.0;
	ChargeUpdatedAt = -10.0;
	if (bCancelAbility && AbilityToCancel.IsValid())
	{
		if (AMD_Character* Character = Cast<AMD_Character>(GetOwner()))
			if (UAbilitySystemComponent* AbilitySystem = Character->GetAbilitySystemComponent())
				AbilitySystem->CancelAbilityHandle(AbilityToCancel);
	}
}

void UMD_StainScraperComponent::FinishStroke(FGameplayAbilitySpecHandle AbilityHandle)
{
	// An ability from another item must not clear the currently active stroke.
	if (StrokeAbility == AbilityHandle) CancelStroke(false);
}

void UMD_StainScraperComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	CancelStroke();
	Super::EndPlay(Reason);
}
