#include "Component/Scrape/MD_StainScraperComponent.h"

#include "AbilitySystemComponent.h"
#include "Character/MD_Character.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Component/Scrape/MD_ScrapeToolComponent.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "TimerManager.h"
#include "World/Contamination/MD_SurfaceStain.h"

UMD_StainScraperComponent::UMD_StainScraperComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

bool UMD_StainScraperComponent::CanUseEquippedScraper() const
{
	const AMD_Character* Character = Cast<AMD_Character>(GetOwner());
	if (!Character || !Character->GetController() || !Character->GetHotbarComponent()) return false;
	const UMD_ToolDefinition* Tool = Character->GetHotbarComponent()->GetCurrentTool();
	const UAbilitySystemComponent* AbilitySystem = Character->GetAbilitySystemComponent();
	return IsValid(Tool) && Tool->ToolAbilityTag.MatchesTagExact(MDGameplayTags::Ability_Scrape)
		&& AbilitySystem && !AbilitySystem->HasMatchingGameplayTag(MDGameplayTags::State_Dead)
		&& Character->GetScrapeToolComponent() && !Character->GetScrapeToolComponent()->IsScrapeFocusActive();
}

bool UMD_StainScraperComponent::StartStroke()
{
	if (!GetWorld() || !CanUseEquippedScraper()
		|| !FMath::IsFinite(StrokeDurationSeconds) || StrokeDurationSeconds < 0.2f || StrokeDurationSeconds > 2.0f
		|| GetWorld()->GetTimeSeconds() < NextStrokeAllowedAt) return false;
	AMD_Character* Character = CastChecked<AMD_Character>(GetOwner());
	StrokeStartedAt = GetWorld()->GetTimeSeconds();
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

AMD_SurfaceStain* UMD_StainScraperComponent::TraceTarget() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_SurfaceStain_Trace);
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->GetController() || !GetWorld() || !FMath::IsFinite(ReachCm) || ReachCm <= 0.0f) return nullptr;
	FVector Origin;
	FRotator Rotation;
	Pawn->GetController()->GetPlayerViewPoint(Origin, Rotation);
	const FVector Direction = Rotation.Vector();
	if (Origin.ContainsNaN() || Direction.ContainsNaN()) return nullptr;
	TArray<FHitResult> Hits;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MDStainStroke), true, Pawn);
	// Query volumes nominate candidates; only the actual visible surface point can receive a hit.
	GetWorld()->LineTraceMultiByChannel(Hits, Origin, Origin + Direction * FMath::Min(ReachCm, 250.0f), ECC_Visibility, Params);
	const FHitResult* Surface = Hits.FindByPredicate([](const FHitResult& Hit) { return Hit.bBlockingHit; });
	if (!Surface) return nullptr;
	AMD_SurfaceStain* Best = nullptr;
	double BestDistance = TNumericLimits<double>::Max();
	for (const FHitResult& Hit : Hits)
	{
		if (Hit.bBlockingHit) break;
		AMD_SurfaceStain* Stain = Cast<AMD_SurfaceStain>(Hit.GetActor());
		if (!Stain || !Stain->ContainsSurfacePoint(Surface->ImpactPoint, Surface->ImpactNormal)) continue;
		const double Distance = FVector::DistSquared(Stain->GetActorLocation(), Surface->ImpactPoint);
		if (Distance < BestDistance) { Best = Stain; BestDistance = Distance; }
	}
	return Best;
}

void UMD_StainScraperComponent::ApplyImpact()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_SurfaceStain_Impact);
	AMD_Character* Character = Cast<AMD_Character>(GetOwner());
	AMD_SurfaceStain* Target = StrokeTarget.Get();
	StrokeTarget.Reset(); // Consume before any callback; one stroke cannot hit twice.
	if (!Character || !Character->HasAuthority() || !CanUseEquippedScraper()
		|| Character->GetHotbarComponent()->GetCurrentTool() != StrokeTool.Get()) return;
	if (IsValid(Target) && TraceTarget() == Target) Target->ApplyScrapeStroke(StrokeTool->StainRemovalAmount);
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

void UMD_StainScraperComponent::CancelStroke()
{
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(ImpactTimer);
	StrokeTarget.Reset();
	StrokeTool.Reset();
	StrokeStartedAt = -10.0;
}

void UMD_StainScraperComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	CancelStroke();
	Super::EndPlay(Reason);
}
