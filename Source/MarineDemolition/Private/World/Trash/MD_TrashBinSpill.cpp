#include "World/Trash/MD_TrashBin.h"

#include "Algo/Sort.h"
#include "Audio/MD_AudioSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "World/Contamination/MD_WetContaminationPatch.h"
#include "World/Trash/MD_TrashBinLiquidSloshVisualComponent.h"
#include "World/Trash/MD_TrashBinSpillStreamVisualComponent.h"

namespace MD_TrashBinSpillPrivate
{
	constexpr double LiquidTolerance = 1.0e-9;
	constexpr double RewardTolerance = 1.0e-12;
	constexpr double WetSurfaceOffsetCm = 0.15;
	constexpr float CosmeticRimInsetCm = 6.0f;
	const FName NoSpillSupportTag(TEXT("MD.NoSpillSupport"));

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

	bool IsPointInsideContentsVolume(
		const UBoxComponent* const ContentsVolume,
		const FVector& WorldPoint)
	{
		if (!IsValid(ContentsVolume))
		{
			return false;
		}

		const FVector LocalPoint = ContentsVolume->GetComponentTransform()
			.InverseTransformPosition(WorldPoint);
		const FVector Extent = ContentsVolume->GetUnscaledBoxExtent();
		return FMath::Abs(LocalPoint.X) <= Extent.X
			&& FMath::Abs(LocalPoint.Y) <= Extent.Y
			&& FMath::Abs(LocalPoint.Z) <= Extent.Z;
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
		&& Tilt.bMeetsAngle
		&& !bTiltBlockedUntilUpright)
	{
		// 넘어진 채 잠든 통도 계속 샌다. Actor Tick 대신 이 timer만 유지한다.
		EnsureSpillSampleTimer();
		return;
	}

	StopSpillSampleTimer(/*bResetTiltState=*/!Tilt.bMeetsAngle);
}

void AMD_TrashBin::HandleSpillMeshHit(
	UPrimitiveComponent* const HitComponent,
	AActor* const OtherActor,
	UPrimitiveComponent* const OtherComponent,
	const FVector NormalImpulse,
	const FHitResult& Hit)
{
	(void)OtherActor;
	(void)Hit;
	if (!HasAuthority()
		|| HitComponent != Mesh.Get()
		|| !IsValid(OtherComponent)
		|| bLiquidTransferInProgress
		|| LiquidState.LiquidUnits <= 0.0
		|| !SpillTuning.IsValid())
	{
		return;
	}

	const UWorld* const World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	const double SecondsSincePickup = LastPickupStartedWorldSeconds >= 0.0
		? FMath::Max(0.0, Now - LastPickupStartedWorldSeconds)
		: TNumericLimits<double>::Max();
	const FMD_TrashBinImpactEvaluation Impact =
		FMD_TrashBinSpillEvaluator::EvaluateImpact(
			NormalImpulse.Size(),
			SecondsSincePickup,
			SpillTuning);
	if (!Impact.bCanPulse)
	{
		return;
	}

	if (LastAcceptedImpactOtherComponent.Get() == OtherComponent
		&& LastAcceptedImpactWorldSeconds >= 0.0
		&& Now - LastAcceptedImpactWorldSeconds
			< SpillTuning.ImpactDebounceSeconds)
	{
		return;
	}

	// 한 Chaos contact의 여러 Hit는 receiver 결과와 무관하게 먼저 묶는다.
	LastAcceptedImpactWorldSeconds = Now;
	LastAcceptedImpactOtherComponent = OtherComponent;
	TryExecuteSpillPulse(EMD_TrashBinSpillTrigger::Impact);
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

void AMD_TrashBin::StopSpillSampleTimer(const bool bResetTiltState)
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SpillSampleTimerHandle);
	}
	if (bResetTiltState)
	{
		ResetSustainedTiltState(/*bClearReceiverBlock=*/true);
	}
}

void AMD_TrashBin::ResetSustainedTiltState(
	const bool bClearReceiverBlock)
{
	TiltCandidateStartedWorldSeconds = -1.0;
	NextSustainedPulseWorldSeconds = -1.0;
	if (bClearReceiverBlock)
	{
		bTiltBlockedUntilUpright = false;
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
		StopSpillSampleTimer(/*bResetTiltState=*/true);
	}
}

void AMD_TrashBin::SampleSpillTilt()
{
	UWorld* const World = GetWorld();
	if (!HasAuthority()
		|| !IsValid(World)
		|| !IsValid(Mesh.Get())
		|| !LiquidState.IsValid(MaximumLiquidUnits)
		|| LiquidState.LiquidUnits <= 0.0
		|| !SpillTuning.IsValid())
	{
		StopSpillSampleTimer(/*bResetTiltState=*/true);
		return;
	}

	const double Now = World->GetTimeSeconds();
	const double HeldSeconds = TiltCandidateStartedWorldSeconds >= 0.0
		? FMath::Max(0.0, Now - TiltCandidateStartedWorldSeconds)
		: 0.0;
	const FMD_TrashBinTiltEvaluation Tilt =
		FMD_TrashBinSpillEvaluator::EvaluateTilt(
			Mesh->GetUpVector(),
			HeldSeconds,
			SpillTuning);
	if (!Tilt.bInputValid)
	{
		StopSpillSampleTimer(/*bResetTiltState=*/true);
		return;
	}

	if (!Tilt.bMeetsAngle)
	{
		ResetSustainedTiltState(/*bClearReceiverBlock=*/true);
		if (!Mesh->IsAnyRigidBodyAwake() && !bCarryAccelerationActive)
		{
			StopSpillSampleTimer(/*bResetTiltState=*/false);
		}
		return;
	}

	if (bTiltBlockedUntilUpright)
	{
		// 깨어 있는 동안에는 세워졌는지만 계속 본다. tipped+sleep이면 다음 Wake가 재평가한다.
		if (!Mesh->IsAnyRigidBodyAwake())
		{
			StopSpillSampleTimer(/*bResetTiltState=*/false);
		}
		return;
	}

	if (TiltCandidateStartedWorldSeconds < 0.0)
	{
		TiltCandidateStartedWorldSeconds = Now;
		NextSustainedPulseWorldSeconds = Now + SpillTuning.TiltHoldSeconds;
		return;
	}

	if (NextSustainedPulseWorldSeconds < 0.0
		|| Now + UE_DOUBLE_SMALL_NUMBER < NextSustainedPulseWorldSeconds)
	{
		return;
	}

	const ESpillAttemptResult Result = TryExecuteSpillPulse(
		EMD_TrashBinSpillTrigger::SustainedTilt);
	if (Result == ESpillAttemptResult::Committed)
	{
		// 긴 frame 뒤에도 catch-up을 여러 번 하지 않고 현재부터 한 간격만 예약한다.
		NextSustainedPulseWorldSeconds =
			Now + SpillTuning.SustainedPulseIntervalSeconds;
		if (LiquidState.LiquidUnits <= 0.0)
		{
			StopSpillSampleTimer(/*bResetTiltState=*/true);
		}
	}
	else if (Result == ESpillAttemptResult::NoReceiver)
	{
		bTiltBlockedUntilUpright = true;
		TiltCandidateStartedWorldSeconds = -1.0;
		NextSustainedPulseWorldSeconds = -1.0;
	}
	else if (Result == ESpillAttemptResult::Rejected)
	{
		// 잘못된 class/token/state를 20Hz spawn 실패 폭풍으로 만들지 않는다.
		bTiltBlockedUntilUpright = true;
		TiltCandidateStartedWorldSeconds = -1.0;
		NextSustainedPulseWorldSeconds = -1.0;
	}
	else if (Result == ESpillAttemptResult::NoLiquid)
	{
		StopSpillSampleTimer(/*bResetTiltState=*/true);
	}
}

AMD_TrashBin::ESpillAttemptResult AMD_TrashBin::TryExecuteSpillPulse(
	const EMD_TrashBinSpillTrigger Trigger)
{
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
	if (!TryFindSpillContact(Contact, SupportComponent))
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
	UClass* const PatchClass =
		WetContaminationPatchClass
			? WetContaminationPatchClass.Get()
			: AMD_WetContaminationPatch::StaticClass();
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
	const FVector SourceWorldLocation =
		CalculateSpillCosmeticSourceWorldLocation(Contact);
	CosmeticResult.SourceActorLocalLocation = GetActorTransform()
		.InverseTransformPosition(SourceWorldLocation);
	CosmeticResult.ContactWorldLocation = Contact.WorldLocation;
	CosmeticResult.ContactWorldNormal = Contact.WorldNormal;
	CosmeticResult.SpillUnits = static_cast<float>(Candidate.Payload.LiquidUnits);
	CosmeticResult.Trigger = Candidate.Trigger;
	CosmeticResult.StreamLeaseSeconds = Candidate.Trigger
		== EMD_TrashBinSpillTrigger::SustainedTilt
		? SpillTuning.SustainedPulseIntervalSeconds
			+ static_cast<float>(
				UMD_TrashBinSpillStreamVisualComponent::DiameterShrinkSeconds)
		: 0.0f;
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


	UWorld* const World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}
	if (Result.Trigger == EMD_TrashBinSpillTrigger::SustainedTilt
		&& IsValid(SpillStreamVisualComponent))
	{
		SpillStreamVisualComponent->PlayCommittedPulse(Result);
	}

	if (UNiagaraSystem* const Effect = SpillEffect.LoadSynchronous())
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this,
			Effect,
			Result.ContactWorldLocation,
			Result.ContactWorldNormal.Rotation(),
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
	UPrimitiveComponent*& OutSupportComponent) const
{
	OutContact = FMD_TrashBinSpillContact();
	OutSupportComponent = nullptr;
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

	const FVector TraceStart = Mesh->Bounds.Origin + FVector::UpVector * 2.0;
	const FVector TraceEnd = TraceStart
		- FVector::UpVector * SpillTuning.FloorTraceDistanceCm;
	TArray<FHitResult> Hits;
	if (!World->LineTraceMultiByChannel(
			Hits,
			TraceStart,
			TraceEnd,
			ECC_Visibility,
			QueryParams))
	{
		return false;
	}
	Algo::SortBy(Hits, &FHitResult::Distance);

	// 첫 blocking 물체가 수용 불가면 그 아래 바닥으로 관통하지 않는다.
	const FHitResult* FirstBlockingHit = Hits.FindByPredicate(
		[](const FHitResult& CandidateHit)
		{
			return CandidateHit.bBlockingHit;
		});
	if (!FirstBlockingHit)
	{
		return false;
	}

	UPrimitiveComponent* const SupportComponent =
		FirstBlockingHit->GetComponent();
	AActor* const SupportActor = FirstBlockingHit->GetActor();
	const FVector SupportNormal = FirstBlockingHit->ImpactNormal.GetSafeNormal();
	if (!IsValid(SupportComponent)
		|| !IsValid(SupportActor)
		|| SupportActor == this
		|| SupportActor->IsA<APawn>()
		|| SupportActor->IsA<AMD_TrashBin>()
		|| SupportActor->IsA<AMD_WetContaminationPatch>()
		|| (IsValid(ContentsVolume)
			&& ContentsVolume->IsOverlappingComponent(SupportComponent))
		|| MD_TrashBinSpillPrivate::IsPointInsideContentsVolume(
			ContentsVolume,
			FirstBlockingHit->ImpactPoint)
		|| SupportComponent->ComponentHasTag(
			MD_TrashBinSpillPrivate::NoSpillSupportTag)
		|| SupportNormal.IsNearlyZero()
		|| FVector::DotProduct(SupportNormal, FVector::UpVector)
			< SpillTuning.MinimumSupportUpDot)
	{
		return false;
	}

	const FTransform WetWorldTransform(
		FRotationMatrix::MakeFromZ(SupportNormal).ToQuat(),
		FirstBlockingHit->ImpactPoint
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
	return true;
}

FVector AMD_TrashBin::CalculateSpillCosmeticSourceWorldLocation(
	const FMD_TrashBinSpillContact& Contact) const
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
	if (!IsValid(Mesh.Get()))
	{
		return MouthCenterWorld;
	}

	const FTransform MeshTransform = Mesh->GetComponentTransform();
	const FVector RimPlaneNormal = Mesh->GetUpVector().GetSafeNormal();
	auto ProjectToRimPlane = [RimPlaneNormal](const FVector& Direction)
	{
		return FVector::VectorPlaneProject(Direction, RimPlaneNormal)
			.GetSafeNormal();
	};

	// 기울어진 통에서 월드 아래쪽이 실제 낮은 입구다. upright/수치 퇴화 때만
	// 접점 방향, 마지막으로 메시 forward를 사용한다.
	FVector RimDirectionWorld = ProjectToRimPlane(FVector::DownVector);
	if (RimDirectionWorld.IsNearlyZero())
	{
		RimDirectionWorld = ProjectToRimPlane(
			Contact.WorldLocation - MouthCenterWorld);
	}
	if (RimDirectionWorld.IsNearlyZero())
	{
		RimDirectionWorld = ProjectToRimPlane(Mesh->GetForwardVector());
	}

	FVector RimDirectionLocal = MeshTransform
		.InverseTransformVectorNoScale(RimDirectionWorld);
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
	// TransformVector를 사용해 BP/Actor에 설정된 Mesh scale도 실제 world offset에
	// 포함한다. 이 점은 payload cosmetic에만 쓰이며 접점·Wet 권위에는 영향이 없다.
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
