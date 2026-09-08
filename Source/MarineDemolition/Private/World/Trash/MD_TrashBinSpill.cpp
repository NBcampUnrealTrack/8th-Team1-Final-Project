#include "World/Trash/MD_TrashBin.h"

#include "Audio/MD_AudioSubsystem.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "World/Contamination/MD_WetContaminationPatch.h"
#include "World/Trash/MD_TrashBinLiquidSloshVisualComponent.h"

namespace MD_TrashBinSpillPrivate
{
	constexpr double LiquidTolerance = 1.0e-9;
	constexpr double RewardTolerance = 1.0e-12;
	constexpr double WetSurfaceOffsetCm = 0.15;
	constexpr float CosmeticRimInsetCm = 6.0f;

	bool AreLedgerTotalsEqual(
		TConstArrayView<FMD_ScrapeRecoveryLot> Lots,
		const double LiquidUnits,
		const double RewardUnits)
	{
		return FMath::IsNearlyEqual(
			FMD_ScrapeRecoveryLedger::GetTotalCarrierAmount(Lots),
			LiquidUnits,
			LiquidTolerance)
			&& FMath::IsNearlyEqual(
				FMD_ScrapeRecoveryLedger::GetTotalRewardUnits(Lots),
				RewardUnits,
				RewardTolerance);
	}

	int32 NextLiquidRevision(const int32 Revision)
	{
		return Revision == MAX_int32 ? 0 : Revision + 1;
	}

}

void AMD_TrashBin::HandleSpillMeshWake(
	UPrimitiveComponent* const WakingComponent,
	const FName BoneName)
{
	(void)BoneName;
	if (WakingComponent == Mesh.Get()
		&& IsValid(LiquidSloshVisualComponent))
	{
		LiquidSloshVisualComponent->NotifyPhysicsWake();
	}
	if (HasAuthority() && WakingComponent == Mesh.Get())
	{
		EnsureSpillSampleTimer();
	}
}

void AMD_TrashBin::HandleSpillMeshSleep(
	UPrimitiveComponent* const SleepingComponent,
	const FName BoneName)
{
	(void)BoneName;
	if (SleepingComponent == Mesh.Get()
		&& IsValid(LiquidSloshVisualComponent))
	{
		LiquidSloshVisualComponent->NotifyPhysicsSleep();
	}
	if (!HasAuthority()
		|| SleepingComponent != Mesh.Get()
		|| !IsValid(Mesh.Get()))
	{
		return;
	}

	const FMD_TrashBinTiltEvaluation Tilt =
		FMD_TrashBinSpillEvaluator::EvaluateTilt(
			Mesh->GetUpVector(),
			0.0,
			SpillTuning);
	if (LiquidState.LiquidUnits > 0.0
		&& Tilt.bInputValid
		&& Tilt.bMeetsAngle)
	{
		EnsureSpillSampleTimer();
		return;
	}

	StopSpillSampleTimer();
}

void AMD_TrashBin::EnsureSpillSampleTimer()
{
	UWorld* const World = GetWorld();
	if (!HasAuthority()
		|| !IsValid(World)
		|| !IsValid(Mesh.Get())
		|| !SpillTuning.IsValid()
		|| !LiquidState.IsValid(MaximumLiquidUnits)
		|| LiquidState.LiquidUnits <= 0.0)
	{
		return;
	}

	FTimerManager& TimerManager = World->GetTimerManager();
	if (!TimerManager.IsTimerActive(SpillSampleTimerHandle))
	{
		TimerManager.SetTimer(
			SpillSampleTimerHandle,
			this,
			&AMD_TrashBin::SampleSpillTilt,
			SpillTuning.SampleIntervalSeconds,
			/*bLoop=*/true);
	}
}

void AMD_TrashBin::StopSpillSampleTimer()
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SpillSampleTimerHandle);
	}
}

void AMD_TrashBin::RefreshSpillSamplingAfterLiquidMutation()
{
	if (!HasAuthority())
	{
		return;
	}
	if (LiquidState.IsValid(MaximumLiquidUnits)
		&& LiquidState.LiquidUnits > 0.0)
	{
		EnsureSpillSampleTimer();
	}
	else
	{
		StopSpillSampleTimer();
	}
}

void AMD_TrashBin::SampleSpillTilt()
{
	UWorld* const World = GetWorld();
	if (!HasAuthority()
		|| !IsValid(World)
		|| !IsValid(Mesh.Get())
		|| !LiquidState.IsValid(MaximumLiquidUnits)
		|| !SpillTuning.IsValid())
	{
		StopSpillSampleTimer();
		return;
	}
	if (LiquidState.LiquidUnits <= 0.0)
	{
		StopSpillSampleTimer();
		return;
	}

	const FMD_TrashBinTiltEvaluation Tilt =
		FMD_TrashBinSpillEvaluator::EvaluateTilt(
			Mesh->GetUpVector(),
			0.0,
			SpillTuning);
	if (!Tilt.bInputValid)
	{
		StopSpillSampleTimer();
		return;
	}

	if (Tilt.bMeetsAngle)
	{
		TryExecuteSpillPulse(EMD_TrashBinSpillTrigger::SustainedTilt);
	}

	if (!Mesh->IsAnyRigidBodyAwake() && !bCarryAccelerationActive)
	{
		StopSpillSampleTimer();
	}
}

AMD_TrashBin::ESpillAttemptResult AMD_TrashBin::TryExecuteSpillPulse(
	const EMD_TrashBinSpillTrigger Trigger)
{
	if (Trigger != EMD_TrashBinSpillTrigger::SustainedTilt)
	{
		return ESpillAttemptResult::Rejected;
	}
	if (!HasAuthority()
		|| bLiquidTransferInProgress
		|| !LiquidState.IsValid(MaximumLiquidUnits)
		|| LiquidState.LiquidUnits <= 0.0)
	{
		return LiquidState.LiquidUnits <= 0.0
			? ESpillAttemptResult::NoLiquid
			: ESpillAttemptResult::Rejected;
	}

	FMD_TrashBinSpillContact Contact;
	UPrimitiveComponent* SupportComponent = nullptr;
	FVector SourceWorldLocation = FVector::ZeroVector;
	if (!TryFindSpillContact(
			Contact,
			SupportComponent,
			SourceWorldLocation))
	{
		return ESpillAttemptResult::NoReceiver;
	}

	FMD_TrashBinSpillCandidate Candidate;
	FMD_TrashBinLiquidState RemainingState;
	TArray<FMD_ScrapeRecoveryLot> RemainingLots;
	if (!TryBuildSpillRemovalCandidate(
			Trigger,
			Candidate,
			RemainingState,
			RemainingLots))
	{
		return ESpillAttemptResult::Rejected;
	}
	// candidate를 만든 직후부터 spawn/attach/preflight/commit 전체를 한 guard로
	// 묶어, 동기 callback이 같은 aggregate의 다른 provenance를 복원해도
	// 준비한 remaining lots로 덮어쓰지 못하게 한다.
	TGuardValue<bool> MutationGuard(bLiquidTransferInProgress, true);

	UWorld* const World = GetWorld();
	if (!IsValid(World))
	{
		return ESpillAttemptResult::Rejected;
	}
	UClass* const PatchClass = WetContaminationPatchClass.Get();
	const AMD_WetContaminationPatch* const PatchDefaults =
		IsValid(PatchClass)
			? PatchClass->GetDefaultObject<AMD_WetContaminationPatch>()
			: nullptr;
	if (!IsValid(PatchDefaults)
		|| !PatchDefaults->HasCompleteVisualConfiguration())
	{
		return ESpillAttemptResult::Rejected;
	}
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMD_WetContaminationPatch* const PreparedPatch =
		World->SpawnActor<AMD_WetContaminationPatch>(
			PatchClass,
			FTransform(
				FRotationMatrix::MakeFromZ(Contact.WorldNormal).ToQuat(),
				Contact.WorldLocation),
			SpawnParameters);
	if (!IsValid(PreparedPatch))
	{
		return ESpillAttemptResult::Rejected;
	}

	FMD_TrashBinSpillPreparedSinkToken SinkToken;
	if (!PreparedPatch->PrepareForSpill(
			Candidate,
			Contact,
			SupportComponent,
			SinkToken)
		|| !CanCommitSpillRemoval(
			Candidate,
			RemainingState,
			RemainingLots,
			PreparedPatch,
			SinkToken))
	{
		PreparedPatch->AbortPreparedSpill();
		return ESpillAttemptResult::Rejected;
	}

	FMD_TrashBinSpillCosmeticResult CosmeticResult;
	CosmeticResult.TransactionId = Candidate.TransactionId;
	CosmeticResult.CommittedLiquidRevision = RemainingState.Revision;
	CosmeticResult.RemainingLiquidUnits = RemainingState.LiquidUnits;
	CosmeticResult.SourceWorldLocation = SourceWorldLocation;
	CosmeticResult.ContactWorldLocation = Contact.WorldLocation;
	CosmeticResult.ContactWorldNormal = Contact.WorldNormal;
	CosmeticResult.SpillUnits = static_cast<float>(Candidate.Payload.LiquidUnits);
	CosmeticResult.Trigger = Candidate.Trigger;
	if (!CosmeticResult.IsValid())
	{
		PreparedPatch->AbortPreparedSpill();
		return ESpillAttemptResult::Rejected;
	}

	if (!CommitNextSpillSequence(Candidate.TransactionId.Sequence))
	{
		PreparedPatch->AbortPreparedSpill();
		return ESpillAttemptResult::Rejected;
	}

	// 여기부터는 preflight가 잠긴 한 서버 호출 안의 실패 없는 대입 단계다.
	PreparedPatch->ApplyPreparedSpillNoNotify(Candidate, SinkToken);
	LiquidState = RemainingState;
	LiquidRecoveryLots = MoveTemp(RemainingLots);

	// 양쪽 원장이 존재한 뒤 Wet부터 완성한다. 외부 delegate는 완성된 상태만 본다.
	PreparedPatch->FinalizeCommittedSpill();
	NotifyLiquidStateChanged();
	ForceNetUpdate();
	MulticastCommittedSpillCosmetic(CosmeticResult);
	return ESpillAttemptResult::Committed;
}

void AMD_TrashBin::MulticastCommittedSpillCosmetic_Implementation(
	const FMD_TrashBinSpillCosmeticResult& Result)
{
	if (GetNetMode() == NM_DedicatedServer || !Result.IsValid())
	{
		return;
	}
	if (LastConsumedSpillCosmeticStableBinId.IsValid()
		&& LastConsumedSpillCosmeticStableBinId
			!= Result.TransactionId.StableBinId)
	{
		return;
	}
	if (Result.TransactionId.Sequence <= LastConsumedSpillCosmeticSequence)
	{
		return;
	}
	LastConsumedSpillCosmeticStableBinId = Result.TransactionId.StableBinId;
	LastConsumedSpillCosmeticSequence = Result.TransactionId.Sequence;


	UWorld* const World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}
	const FVector SourceWorldLocation = Result.SourceWorldLocation;
	if (SourceWorldLocation.ContainsNaN())
	{
		return;
	}
	FVector SpillDirection =
		(Result.ContactWorldLocation - SourceWorldLocation).GetSafeNormal();
	if (SpillDirection.IsNearlyZero())
	{
		SpillDirection = FVector::DownVector;
	}

	if (UNiagaraSystem* const Effect = SpillEffect.LoadSynchronous())
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this,
			Effect,
			SourceWorldLocation,
			SpillDirection.Rotation(),
			FVector::OneVector,
			/*bAutoDestroy=*/true,
			/*bAutoActivate=*/true,
			ENCPoolMethod::AutoRelease,
			/*bPreCullCheck=*/false);
	}

	if (SpillSoundTag.IsValid())
	{
		if (UMD_AudioSubsystem* const Audio =
			World->GetSubsystem<UMD_AudioSubsystem>())
		{
			Audio->PlayEventAtLocation(
				SpillSoundTag,
				Result.ContactWorldLocation);
		}
	}
}

bool AMD_TrashBin::TryBuildSpillRemovalCandidate(
	const EMD_TrashBinSpillTrigger Trigger,
	FMD_TrashBinSpillCandidate& OutCandidate,
	FMD_TrashBinLiquidState& OutRemainingState,
	TArray<FMD_ScrapeRecoveryLot>& OutRemainingLots) const
{
	OutCandidate = FMD_TrashBinSpillCandidate();
	OutRemainingState = FMD_TrashBinLiquidState();
	OutRemainingLots.Reset();
	if (!HasAuthority()
		|| bLiquidTransferInProgress
		|| !StableBinId.IsValid()
		|| !SpillTuning.IsValid()
		|| !LiquidState.IsValid(MaximumLiquidUnits)
		|| LiquidState.LiquidUnits <= 0.0
		|| !MD_TrashBinSpillPrivate::AreLedgerTotalsEqual(
			LiquidRecoveryLots,
			LiquidState.LiquidUnits,
			LiquidState.RewardUnits))
	{
		return false;
	}

	const double RequestedLiquidUnits =
		FMD_TrashBinSpillEvaluator::CalculateRequestedSpillUnits(
			LiquidState.LiquidUnits,
			SpillTuning);
	uint64 PreparedSequence = 0;
	if (RequestedLiquidUnits <= 0.0
		|| !TryPreviewNextSpillSequence(PreparedSequence))
	{
		return false;
	}

	TArray<FMD_ScrapeRecoveryLot> RemainingLots = LiquidRecoveryLots;
	TArray<FMD_ScrapeRecoveryLot> RemovedLots;
	const double RemovedLiquidUnits =
		FMD_ScrapeRecoveryLedger::ExtractProportional(
			RemainingLots,
			RequestedLiquidUnits,
			RemovedLots);
	if (!FMath::IsNearlyEqual(
			RemovedLiquidUnits,
			RequestedLiquidUnits,
			MD_TrashBinSpillPrivate::LiquidTolerance))
	{
		return false;
	}

	OutCandidate.TransactionId.StableBinId = StableBinId;
	OutCandidate.TransactionId.Sequence = PreparedSequence;
	OutCandidate.Trigger = Trigger;
	OutCandidate.ExpectedBinRevision = LiquidState.Revision;
	OutCandidate.Payload.LiquidUnits = RemovedLiquidUnits;
	OutCandidate.Payload.RewardUnits =
		FMD_ScrapeRecoveryLedger::GetTotalRewardUnits(RemovedLots);
	OutCandidate.Payload.RecoveryLots = MoveTemp(RemovedLots);

	OutRemainingState.LiquidUnits =
		FMD_ScrapeRecoveryLedger::GetTotalCarrierAmount(RemainingLots);
	OutRemainingState.RewardUnits =
		FMD_ScrapeRecoveryLedger::GetTotalRewardUnits(RemainingLots);
	OutRemainingState.Revision =
		MD_TrashBinSpillPrivate::NextLiquidRevision(LiquidState.Revision);
	if (!OutCandidate.IsValid()
		|| !OutRemainingState.IsValid(MaximumLiquidUnits)
		|| !FMath::IsNearlyEqual(
			OutRemainingState.LiquidUnits + OutCandidate.Payload.LiquidUnits,
			LiquidState.LiquidUnits,
			MD_TrashBinSpillPrivate::LiquidTolerance)
		|| !FMath::IsNearlyEqual(
			OutRemainingState.RewardUnits + OutCandidate.Payload.RewardUnits,
			LiquidState.RewardUnits,
			MD_TrashBinSpillPrivate::RewardTolerance))
	{
		OutCandidate = FMD_TrashBinSpillCandidate();
		OutRemainingState = FMD_TrashBinLiquidState();
		return false;
	}

	OutRemainingLots = MoveTemp(RemainingLots);
	return true;
}

bool AMD_TrashBin::TryFindSpillContact(
	FMD_TrashBinSpillContact& OutContact,
	UPrimitiveComponent*& OutSupportComponent,
	FVector& OutSourceWorldLocation) const
{
	OutContact = FMD_TrashBinSpillContact();
	OutSupportComponent = nullptr;
	OutSourceWorldLocation = FVector::ZeroVector;
	UWorld* const World = GetWorld();
	if (!HasAuthority()
		|| !IsValid(World)
		|| !IsValid(Mesh.Get())
		|| !SpillTuning.IsValid())
	{
		return false;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MD_TrashBinSpillFloor));
	QueryParams.AddIgnoredActor(this);
	for (const TWeakObjectPtr<AActor>& ContentPtr : Contents)
	{
		if (const AActor* const Content = ContentPtr.Get())
		{
			QueryParams.AddIgnoredActor(Content);
		}
	}

	const FVector MouthCenterWorldLocation =
		CalculateSpillMouthCenterWorldLocation();
	if (MouthCenterWorldLocation.ContainsNaN())
	{
		return false;
	}
	const FVector TraceEnd = MouthCenterWorldLocation
		+ FVector::DownVector * SpillTuning.FloorTraceDistanceCm;
	FHitResult FloorHit;
	if (!World->LineTraceSingleByChannel(
			FloorHit,
			MouthCenterWorldLocation,
			TraceEnd,
			MDCollisionChannels::SpillFloor,
			QueryParams))
	{
		return false;
	}
	if (!FloorHit.bBlockingHit)
	{
		return false;
	}

	UPrimitiveComponent* const SupportComponent =
		FloorHit.GetComponent();
	AActor* const SupportActor = FloorHit.GetActor();
	const FVector SupportNormal = FloorHit.ImpactNormal.GetSafeNormal();
	if (!IsValid(SupportComponent)
		|| !IsValid(SupportActor)
		|| SupportNormal.IsNearlyZero())
	{
		return false;
	}

	const FTransform WetWorldTransform(
		FRotationMatrix::MakeFromZ(SupportNormal).ToQuat(),
		FloorHit.ImpactPoint
			+ SupportNormal * MD_TrashBinSpillPrivate::WetSurfaceOffsetCm);
	OutContact.WorldLocation = WetWorldTransform.GetLocation();
	OutContact.WorldNormal = SupportNormal;
	OutContact.SupportActorPath =
		FMD_TrashBinSpillContact::CanonicalizeSupportActorPath(
			FSoftObjectPath(SupportActor));
	OutContact.SupportComponentName = SupportComponent->GetFName();
	OutContact.SupportLocalAnchor = WetWorldTransform.GetRelativeTransform(
		SupportComponent->GetComponentTransform());
	if (!OutContact.IsValid())
	{
		OutContact = FMD_TrashBinSpillContact();
		return false;
	}

	OutSupportComponent = SupportComponent;
	OutSourceWorldLocation = CalculateSpillMouthSourceWorldLocation();
	return true;
}

FVector AMD_TrashBin::CalculateSpillMouthCenterWorldLocation() const
{
	FVector MouthCenterWorld = IsValid(ScrapeUnloadEffectOrigin)
		? ScrapeUnloadEffectOrigin->GetComponentLocation()
		: IsValid(Mesh.Get())
			? Mesh->Bounds.Origin
			: GetActorLocation();
	if (MouthCenterWorld.ContainsNaN())
	{
		MouthCenterWorld = GetActorLocation();
	}
	return MouthCenterWorld;
}

FVector AMD_TrashBin::CalculateSpillMouthSourceWorldLocation() const
{
	const FVector MouthCenterWorld = CalculateSpillMouthCenterWorldLocation();
	if (!IsValid(Mesh.Get()))
	{
		return MouthCenterWorld;
	}

	const FTransform MeshTransform = Mesh->GetComponentTransform();
	const FVector LocalWorldDown = MeshTransform
		.InverseTransformVectorNoScale(FVector::DownVector);
	const FVector MeshScale = MeshTransform.GetScale3D();
	FVector RimDirectionLocal(
		LocalWorldDown.X * MeshScale.X,
		LocalWorldDown.Y * MeshScale.Y,
		0.0);
	RimDirectionLocal.Z = 0.0;
	RimDirectionLocal = RimDirectionLocal.GetSafeNormal();
	if (RimDirectionLocal.IsNearlyZero())
	{
		RimDirectionLocal = FVector::ForwardVector;
	}

	const float LocalRimRadiusCm = FMath::IsFinite(LiquidVisualRadiusCm)
		? FMath::Max(
			0.0f,
			LiquidVisualRadiusCm
				- MD_TrashBinSpillPrivate::CosmeticRimInsetCm)
		: 0.0f;
	const FVector WorldRimOffset = MeshTransform.TransformVector(
		RimDirectionLocal * LocalRimRadiusCm);
	const FVector SourceWorldLocation = MouthCenterWorld + WorldRimOffset;
	return SourceWorldLocation.ContainsNaN()
		? MouthCenterWorld
		: SourceWorldLocation;
}

bool AMD_TrashBin::CanCommitSpillRemoval(
	const FMD_TrashBinSpillCandidate& Candidate,
	const FMD_TrashBinLiquidState& RemainingState,
	const TConstArrayView<FMD_ScrapeRecoveryLot> RemainingLots,
	const AMD_WetContaminationPatch* const PreparedPatch,
	const FMD_TrashBinSpillPreparedSinkToken& SinkToken) const
{
	uint64 CurrentPreparedSequence = 0;
	return HasAuthority()
		&& bLiquidTransferInProgress
		&& Candidate.IsValid()
		&& Candidate.TransactionId.StableBinId == StableBinId
		&& Candidate.ExpectedBinRevision == LiquidState.Revision
		&& TryPreviewNextSpillSequence(CurrentPreparedSequence)
		&& Candidate.TransactionId.Sequence == CurrentPreparedSequence
		&& RemainingState.IsValid(MaximumLiquidUnits)
		&& RemainingState.Revision
			== MD_TrashBinSpillPrivate::NextLiquidRevision(LiquidState.Revision)
		&& MD_TrashBinSpillPrivate::AreLedgerTotalsEqual(
			LiquidRecoveryLots,
			LiquidState.LiquidUnits,
			LiquidState.RewardUnits)
		&& MD_TrashBinSpillPrivate::AreLedgerTotalsEqual(
			RemainingLots,
			RemainingState.LiquidUnits,
			RemainingState.RewardUnits)
		&& FMath::IsNearlyEqual(
			RemainingState.LiquidUnits + Candidate.Payload.LiquidUnits,
			LiquidState.LiquidUnits,
			MD_TrashBinSpillPrivate::LiquidTolerance)
		&& FMath::IsNearlyEqual(
			RemainingState.RewardUnits + Candidate.Payload.RewardUnits,
			LiquidState.RewardUnits,
			MD_TrashBinSpillPrivate::RewardTolerance)
		&& IsValid(PreparedPatch)
		&& PreparedPatch->CanCommitPreparedSpill(Candidate, SinkToken);
}
