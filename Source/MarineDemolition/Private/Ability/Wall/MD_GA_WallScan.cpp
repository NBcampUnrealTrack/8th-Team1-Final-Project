#include "Ability/Wall/MD_GA_WallScan.h"

#include "AbilitySystemComponent.h"
#include "Character/MD_Character.h"
#include "Component/Scanning/MD_ScannerComponent.h"
#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"
#include "World/Contamination/MD_Barnacle.h"
#include "World/Contamination/MD_SurfaceStain.h"
#include "UObject/UObjectIterator.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "Gameplay/Scanning/MD_Scannable.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "GameplayPrediction.h"
#include "Kismet/KismetSystemLibrary.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "TimerManager.h"
#include "World/Wall/MD_CuttableWall.h"
#include "World/Wall/MD_WallUtility.h"

UMD_GA_WallScan::UMD_GA_WallScan()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ActivationBlockedTags.AddTag(MDGameplayTags::State_Dead);
	ScanPulseSoundTag = FGameplayTag::RequestGameplayTag(TEXT("SFX.Tool.Scanner.PulseLoop"), false);
	RequiredTargetTags.AddTag(MDGameplayTags::Interaction_Scannable);
	ScanObjectTypes = { UEngineTypes::ConvertToObjectType(ECC_WorldStatic),
		UEngineTypes::ConvertToObjectType(ECC_WorldDynamic), UEngineTypes::ConvertToObjectType(ECC_PhysicsBody) };
}

bool UMD_GA_WallScan::FindScanTargets(const FGameplayAbilityActorInfo* ActorInfo, EMD_ScanMode Mode,
	TArray<AActor*>& OutTargets, FVector& OutOrigin, FVector& OutForward) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scanner_FindTargets);
	OutTargets.Reset();
	OutOrigin = FVector::ZeroVector;
	OutForward = FVector::ForwardVector;
	const APawn* Pawn = ActorInfo ? Cast<APawn>(ActorInfo->AvatarActor.Get()) : nullptr;
	const AController* Controller = Pawn ? Pawn->GetController() : nullptr;
	if (!MDScanning::IsValidMode(Mode) || !Controller || !Pawn->GetWorld() || !FMath::IsFinite(ScanRadius) || ScanRadius <= 0
		|| !FMath::IsFinite(ScanConeHalfAngleDegrees)
		|| (Mode == EMD_ScanMode::InternalStructure && ScanObjectTypes.IsEmpty())) return false;
	FRotator Rotation;
	Controller->GetPlayerViewPoint(OutOrigin, Rotation);
	OutForward = Rotation.Vector().GetSafeNormal();
	TArray<AActor*> Candidates;
	if (Mode == EMD_ScanMode::WallType)
	{
		// Some authored walls have complex collision only and are absent from simple
		// overlap results. Classify their rendered bounds once per scan, without Tick.
		for (TActorIterator<AMD_CuttableWall> It(Pawn->GetWorld()); It; ++It) Candidates.Add(*It);
	}
	else if (Mode == EMD_ScanMode::Cleanup)
	{
		// Surfaces also exist on floors/equipment; the collision query is not authoritative.
		for (TObjectIterator<UMD_ScrapeSurfaceComponent> It; It; ++It)
			if (It->GetWorld() == Pawn->GetWorld() && It->IsRegistered() && IsValid(It->GetOwner()))
				Candidates.AddUnique(It->GetOwner());
		for (TActorIterator<AMD_Barnacle> It(Pawn->GetWorld()); It; ++It) Candidates.AddUnique(*It);
		for (TActorIterator<AMD_SurfaceStain> It(Pawn->GetWorld()); It; ++It) Candidates.Add(*It);
	}
	else
	{
		const TArray<AActor*> Ignore { const_cast<APawn*>(Pawn) };
		UKismetSystemLibrary::SphereOverlapActors(Pawn->GetWorld(), OutOrigin, ScanRadius,
			ScanObjectTypes, AActor::StaticClass(), Ignore, Candidates);
		// Pipes/conduits can have no simple collision or no host-wall link.
		// Enumerate only this infrastructure class, once per activation.
		for (TActorIterator<AMD_WallUtility> It(Pawn->GetWorld()); It; ++It)
			Candidates.AddUnique(*It);
	}
	for (AActor* Candidate : Candidates)
	{
		if (!IsValid(Candidate) || Candidate->IsHidden()
			|| (Mode != EMD_ScanMode::Cleanup && !ActorHasAnyTargetTag(Candidate, RequiredTargetTags)))
			continue;
		FVector Center = Candidate->GetActorLocation();
		double BoundRadius = 0;
		if (Mode == EMD_ScanMode::Cleanup)
		{
			const UStaticMeshComponent* Mesh = nullptr;
			if (const AMD_Barnacle* Barnacle = Cast<AMD_Barnacle>(Candidate))
			{
				if (Barnacle->IsFullyRemoved()) continue;
				Mesh = Barnacle->GetScanMesh();
			}
			else if (const auto* Surface = Candidate->FindComponentByClass<UMD_ScrapeSurfaceComponent>())
			{
				if (!Surface->IsContaminationEnabled() || !Surface->IsSurfaceInitialized()
					|| !Surface->UsesMeshUVSurface() || Surface->IsContaminationFullyRemoved()) continue;
				Mesh = Surface->GetSurfaceMesh();
			}
			if (const AMD_SurfaceStain* Stain = Cast<AMD_SurfaceStain>(Candidate))
			{
				if (Stain->GetRemainingSize() <= 0) continue;
				const FBoxSphereBounds Bounds = Stain->GetStainBounds();
				Center = Bounds.Origin; BoundRadius = Bounds.SphereRadius;
			}
			else
			{
				if (!IsValid(Mesh) || !Mesh->GetStaticMesh() || !Mesh->IsVisible() || !Mesh->bRenderInMainPass) continue;
				Center = Mesh->Bounds.Origin; BoundRadius = Mesh->Bounds.SphereRadius;
			}
		}
		else if (Mode == EMD_ScanMode::WallType)
		{
			const AMD_CuttableWall* Wall = Cast<AMD_CuttableWall>(Candidate);
			if (!Wall || !Wall->CanCut() || !IsValid(Wall->GetScanMesh())) continue;
			if (Wall->GetWallType() != EMD_WallType::Partition && Wall->GetWallType() != EMD_WallType::Strut) continue;
			const UStaticMeshComponent* Mesh = Wall->GetScanMesh();
			if (!Mesh->IsVisible() || !Mesh->bRenderInMainPass) continue;
			Center = Mesh->Bounds.Origin;
			BoundRadius = Mesh->Bounds.SphereRadius;
		}
		else if (const AMD_WallUtility* Utility = Cast<AMD_WallUtility>(Candidate))
		{
			const UStaticMeshComponent* Mesh = Utility->GetScanMesh();
			if (!IsValid(Mesh) || !Mesh->GetStaticMesh() || !Mesh->IsVisible() || !Mesh->bRenderInMainPass)
				continue;
			Center = Mesh->Bounds.Origin;
			BoundRadius = Mesh->Bounds.SphereRadius;
		}
		else if (!Candidate->GetClass()->ImplementsInterface(UMD_Scannable::StaticClass()))
			continue;
		if (MDScanning::SphereIntersectsScanCone(OutOrigin, OutForward, Center, BoundRadius,
			ScanRadius, ScanConeHalfAngleDegrees)) OutTargets.AddUnique(Candidate);
	}
	UE_LOG(LogTemp, VeryVerbose, TEXT("[Scanner] Find %s mode=%d candidates=%d targets=%d"), *GetNameSafe(Pawn), int32(Mode), Candidates.Num(), OutTargets.Num());
	return true;
}

void UMD_GA_WallScan::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData*)
{
	ClearModeWait();
	LocalRequestId = 0;
	AMD_Character* Character = ActorInfo ? Cast<AMD_Character>(ActorInfo->AvatarActor.Get()) : nullptr;
	UMD_ScannerComponent* Scanner = Character ? Character->GetScannerComponent() : nullptr;
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	UE_LOG(LogTemp, VeryVerbose, TEXT("[Scanner] Activate %s authority=%d local=%d key=%d"), *GetNameSafe(Character), ActorInfo && ActorInfo->IsNetAuthority(), ActorInfo && ActorInfo->IsLocallyControlled(), ActivationInfo.GetActivationPredictionKey().Current);
	if (!Scanner || !ASC || !Scanner->IsScannerEquipped())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	PendingHandle = Handle;
	PendingKey = ActivationInfo.GetActivationPredictionKey();
	if (ActorInfo->IsNetAuthority() && !ActorInfo->IsLocallyControlled())
	{
		if (!PendingKey.IsValidKey())
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}
		bWaitingForMode = true;
		ModeDelegate = ASC->AbilityTargetDataSetDelegate(Handle, PendingKey).AddUObject(this, &ThisClass::ReceiveMode);
		CancelDelegate = ASC->AbilityTargetDataCancelledDelegate(Handle, PendingKey).AddUObject(this, &ThisClass::CancelPendingMode);
		GetWorld()->GetTimerManager().SetTimer(ModeTimeout, this, &ThisClass::CancelPendingMode,
			FMath::IsFinite(ModeRequestTimeoutSeconds) ? FMath::Max(ModeRequestTimeoutSeconds, 0.1f) : 2.f, false);
		ASC->CallReplicatedTargetDataDelegatesIfSet(Handle, PendingKey);
		return;
	}
	LocalRequestId = Scanner->BeginLocalRequest();
	const EMD_ScanMode Mode = Scanner->GetScanMode();
	if (!PerformScan(Mode, LocalRequestId))
	{
		Scanner->ClearScan();
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	if (!ActorInfo->IsNetAuthority())
	{
		if (!PendingKey.IsValidKey())
		{
			Scanner->ClearScan();
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}
		PendingKey.NewRejectedDelegate().BindWeakLambda(Scanner, [Scanner, RequestId = LocalRequestId]
		{
			Scanner->FinishLocalRequest(RequestId);
		});
		auto* Request = new FGameplayAbilityTargetData_ScanMode();
		Request->Mode = Mode;
		Request->RequestId = LocalRequestId;
		FGameplayAbilityTargetDataHandle Data(Request);
		FScopedPredictionWindow Prediction(ASC, true);
		ASC->CallServerSetReplicatedTargetData(Handle, PendingKey, Data, FGameplayTag(), ASC->ScopedPredictionKey);
	}
	// The server ends its own request after consuming mode data. Do not race that wait
	// with an instant client EndAbility RPC; the display belongs to ScannerComponent.
	EndAbility(Handle, ActorInfo, ActivationInfo, ActorInfo->IsNetAuthority(), false);
}

bool UMD_GA_WallScan::PerformScan(EMD_ScanMode Mode, uint32 RequestId)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scanner_Activate);
	const FGameplayAbilityActorInfo* Info = GetCurrentActorInfo();
	const AMD_Character* Character = Info ? Cast<AMD_Character>(Info->AvatarActor.Get()) : nullptr;
	UMD_ScannerComponent* Scanner = Character ? Character->GetScannerComponent() : nullptr;
	if (!Scanner || !MDScanning::IsValidMode(Mode) || !Scanner->IsScannerEquipped()
		|| !FMath::IsFinite(ScanDuration) || ScanDuration <= 0) return false;
	if (Info->IsNetAuthority() && !Scanner->AcceptAuthoritativeRequest(RequestId)) return false;
	if (!CommitAbility(GetCurrentAbilitySpecHandle(), Info, GetCurrentActivationInfo())) return false;
	TArray<AActor*> Targets;
	FVector Origin, Forward;
	FindScanTargets(Info, Mode, Targets, Origin, Forward);
	if (!Targets.IsEmpty())
	{
		if (Info->IsLocallyControlled())
			Scanner->PresentLocalScan(RequestId, Mode, Targets, ScanDuration, ScanMPC, Origin,
				Forward, ScanRadius, ScanConeHalfAngleDegrees);
		Scanner->PlayScanPulse(RequestId, ScanPulseSoundTag, ScanDuration, ScanPulseFadeOutSeconds);
	}
	return true;
}

void UMD_GA_WallScan::ReceiveMode(const FGameplayAbilityTargetDataHandle& Data, FGameplayTag)
{
	if (!bWaitingForMode || !IsActive()) return;
	const FGameplayAbilityTargetData* Raw = Data.Num() == 1 ? Data.Get(0) : nullptr;
	const auto* Request = Raw && Raw->GetScriptStruct() == FGameplayAbilityTargetData_ScanMode::StaticStruct()
		? static_cast<const FGameplayAbilityTargetData_ScanMode*>(Raw) : nullptr;
	const EMD_ScanMode Mode = Request ? Request->Mode : static_cast<EMD_ScanMode>(255);
	const uint32 RequestId = Request ? Request->RequestId : 0;
	ClearModeWait(); // Copy before consuming GAS's owning handle.
	const bool bSuccess = RequestId != 0 && PerformScan(Mode, RequestId);
	UE_LOG(LogTemp, VeryVerbose, TEXT("[Scanner] Receive mode=%d request=%u accepted=%d"), int32(Mode), RequestId, bSuccess);
	if (!bSuccess)
	{
		const AMD_Character* Character = Cast<AMD_Character>(GetAvatarActorFromActorInfo());
		if (Character && Character->GetScannerComponent())
			Character->GetScannerComponent()->RejectAuthoritativeRequest(RequestId);
	}
	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, !bSuccess);
}

void UMD_GA_WallScan::CancelPendingMode()
{
	if (!bWaitingForMode || !IsActive()) return;
	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
}

void UMD_GA_WallScan::ClearModeWait()
{
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(ModeTimeout);
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo(); ASC && PendingHandle.IsValid() && PendingKey.IsValidKey())
	{
		if (ModeDelegate.IsValid()) ASC->AbilityTargetDataSetDelegate(PendingHandle, PendingKey).Remove(ModeDelegate);
		if (CancelDelegate.IsValid()) ASC->AbilityTargetDataCancelledDelegate(PendingHandle, PendingKey).Remove(CancelDelegate);
		if (bWaitingForMode) ASC->ConsumeClientReplicatedTargetData(PendingHandle, PendingKey);
	}
	ModeDelegate.Reset();
	CancelDelegate.Reset();
	bWaitingForMode = false;
	PendingHandle = FGameplayAbilitySpecHandle();
	PendingKey = FPredictionKey();
}

void UMD_GA_WallScan::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	ClearModeWait();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
