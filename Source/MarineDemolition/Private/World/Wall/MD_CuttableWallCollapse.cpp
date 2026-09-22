#include "World/Wall/MD_CuttableWall.h"

#include "Camera/CameraShakeBase.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "World/Wall/MD_WallPiece.h"

void AMD_CuttableWall::UpdateSupportState(bool bSupported, AActor* ResponsibleActor)
{
	if (!HasAuthority()) return;
	bHasSupport = bSupported;
	ForceNetUpdate();
	if (bAwaitingSupportCollapse && !bHasSupport) CollapseIfUnsupported(ResponsibleActor);
}

void AMD_CuttableWall::RestorePendingSupportCollapse()
{
	if (!HasAuthority() || WallType != EMD_WallType::Strut) return;
	bCut = true;
	bAwaitingSupportCollapse = true;
	bCollapseTriggered = false;
	bHasSupport = true;
	// Hidden, non-colliding actors otherwise lose relevance before a later collapse multicast.
	bAlwaysRelevant = true;
	SetLifeSpan(0.f);
	OnRep_Cut();
	ForceNetUpdate();
}

void AMD_CuttableWall::CollapseIfUnsupported(AActor* ResponsibleActor)
{
	if (!HasAuthority() || !bCut || WallType != EMD_WallType::Strut
		|| bHasSupport || bCollapseTriggered) return;
	// Consume the pending load before damage/delegates can cause another support change.
	bCollapseTriggered = true;
	bAwaitingSupportCollapse = false;
	TriggerUnsupportedCollapse(ResponsibleActor);
	// Penalty and wall utilities are independent of ceiling detection or cosmetic setup.
	OnStrutCollapsed.Broadcast(this, ResponsibleActor);
	ForceNetUpdate();
	SetLifeSpan(0.2f);
}

void AMD_CuttableWall::TriggerUnsupportedCollapse(AActor* Cutter)
{
	if (!HasAuthority() || !GetWorld() || !IsValid(Mesh)) return;
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_StrutLocalCollapse);

	UWorld* World = GetWorld();
	const FVector WallCenter = Mesh->Bounds.Origin;
	const double FloorZ = WallCenter.Z - Mesh->Bounds.BoxExtent.Z;
	const double Radius = FMath::Clamp(double(CollapseRadiusCm), 50.0, 1000.0);
	const double SearchHeight = FMath::Clamp(double(CollapseCeilingSearchHeightCm), 50.0, 1000.0);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(StrutLocalCollapse), false, this);

	const auto FindCeiling = [&](const FVector& Sample, FHitResult& Hit)
	{
		// Do not reach through another wall into a neighbouring room.
		FHitResult Obstruction;
		if (!Sample.Equals(WallCenter, 0.1)
			&& World->LineTraceSingleByChannel(Obstruction, WallCenter, Sample, ECC_Visibility, Params))
			return false;
		// Object-type queries also hit WorldStatic water and other overlap-only volumes.
		// Use blocking surfaces so those volumes cannot hide the real ceiling.
		return World->LineTraceSingleByChannel(Hit, Sample,
			Sample + FVector(0, 0, SearchHeight), ECC_Visibility, Params)
			&& !Hit.bStartPenetrating && Hit.ImpactNormal.Z < -0.3;
	};

	// A corner wall's bounds centre may sit below a seam or hole, so also sample nearby.
	double CeilingZ = TNumericLimits<double>::Max();
	for (const FVector& Offset : {FVector::ZeroVector, FVector(Radius * 0.5, 0, 0),
		FVector(-Radius * 0.5, 0, 0), FVector(0, Radius * 0.5, 0), FVector(0, -Radius * 0.5, 0)})
	{
		FHitResult Ceiling;
		if (FindCeiling(WallCenter + Offset, Ceiling))
			CeilingZ = FMath::Min(CeilingZ, Ceiling.ImpactPoint.Z);
	}
	if (CeilingZ == TNumericLimits<double>::Max())
	{
		UE_LOG(LogTemp, Warning, TEXT("Strut collapse [%s]: no ceiling above the wall within %.0f cm"),
			*GetName(), SearchHeight);
		return; // The unsupported-cut event still fires; do not invent a ceiling outdoors.
	}

	const double HalfHeight = FMath::Max(1.0, (CeilingZ - FloorZ) * 0.5);
	const FVector AreaCenter(WallCenter.X, WallCenter.Y, FloorZ + HalfHeight);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, AreaCenter, FQuat::Identity,
		FCollisionObjectQueryParams::AllDynamicObjects,
		FCollisionShape::MakeBox(FVector(Radius, Radius, HalfHeight)), Params);
	TSet<AActor*> Processed;
	int32 DamageTargets = 0;
	const APawn* CutterPawn = Cast<APawn>(Cutter);
	AController* InstigatorController = CutterPawn ? CutterPawn->GetController() : nullptr;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Victim = Overlap.GetActor();
		if (!IsValid(Victim) || Victim == this || Processed.Contains(Victim)) continue;
		Processed.Add(Victim);
		const FVector Target = Victim->GetActorLocation();
		if (Target.Z < FloorZ || Target.Z > CeilingZ
			|| FVector::DistSquared2D(Target, WallCenter) > FMath::Square(Radius)) continue;
		FHitResult Obstruction;
		if (World->LineTraceSingleByChannel(Obstruction, WallCenter, Target, ECC_Visibility, Params)
			&& Obstruction.GetActor() != Victim) continue;
		if (UGameplayStatics::ApplyDamage(Victim, FMath::Max(0.f, CollapseDamage),
			InstigatorController, this, nullptr) > 0.f)
			++DamageTargets;
	}

	// Physical, recoverable debris is authoritative. Each sample uses its own ceiling height.
	const int32 DebrisCount = FMath::Clamp(CollapseDebrisCount, 0, 24);
	const AMD_WallPiece* PieceDefaults = PieceClass.GetDefaultObject();
	const UStaticMeshComponent* PieceMesh = PieceDefaults
		? PieceDefaults->FindComponentByClass<UStaticMeshComponent>() : nullptr;
	const UStaticMesh* PieceAsset = PieceMesh ? PieceMesh->GetStaticMesh() : nullptr;
	if (!PieceAsset && DebrisCount > 0)
		UE_LOG(LogTemp, Warning, TEXT("Strut collapse [%s]: wall-piece mesh is not configured"), *GetName());
	for (int32 Index = 0; PieceAsset && Index < DebrisCount; ++Index)
	{
		const float Scale = FMath::FRandRange(0.2f, 0.5f);
		const FBox PieceBounds = PieceAsset->GetBoundingBox();
		const double Clearance = PieceBounds.GetExtent().Size() * Scale + 5.0;
		const double Angle = Index * 2.399963229728653; // Spread points without a global scan.
		const double Distance = FMath::Max(0.0, Radius - Clearance)
			* FMath::Sqrt(double(Index) / FMath::Max(1, DebrisCount - 1));
		const FVector Sample = WallCenter + FVector(FMath::Cos(Angle) * Distance,
			FMath::Sin(Angle) * Distance, 0);
		FHitResult Ceiling;
		if (!FindCeiling(Sample, Ceiling) || Ceiling.ImpactPoint.Z > CeilingZ + 100.0) continue;
		const FVector SpawnCenter = Ceiling.ImpactPoint - FVector(0, 0, Clearance);
		if (SpawnCenter.Z - Clearance <= FloorZ) continue;
		const FQuat Rotation = FRotator(FMath::FRandRange(0.f, 360.f),
			FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f)).Quaternion();
		// Ignore only this cut wall, which loses collision before the next physics step.
		// Other ceilings, walls, players and pieces must not be penetrated or push spawn outside the radius.
		if (World->OverlapBlockingTestByChannel(SpawnCenter, Rotation, ECC_PhysicsBody,
			FCollisionShape::MakeBox(PieceBounds.GetExtent() * Scale + FVector(1)), Params)) continue;
		const FVector SpawnLocation = SpawnCenter - Rotation.RotateVector(PieceBounds.GetCenter() * Scale);
		const FTransform SpawnTransform(Rotation, SpawnLocation, FVector(Scale));
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.TransformScaleMethod = ESpawnActorScaleMethod::OverrideRootScale;
		if (AMD_WallPiece* Piece = World->SpawnActor<AMD_WallPiece>(PieceClass, SpawnTransform, SpawnParams))
			Piece->SetContaminated(false);
	}

	// Reuse the level's authored collapse look, scaled from the original 10 m x 6 m volume.
	MulticastPlayCollapseEffect(AreaCenter,
		FVector(Radius / 500.0, Radius / 500.0, (CeilingZ - FloorZ) / 600.0));
	UE_LOG(LogTemp, Log, TEXT("Strut collapse [%s]: radius %.0f cm, ceiling Z %.1f, damaged actors %d"),
		*GetName(), Radius, CeilingZ, DamageTargets);
}

void AMD_CuttableWall::MulticastPlayCollapseEffect_Implementation(FVector EffectLocation, FVector EffectScale)
{
	if (GetNetMode() == NM_DedicatedServer) return;
	if (CollapseEffect)
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, CollapseEffect,
			EffectLocation, FRotator::ZeroRotator, EffectScale);
	else
		UE_LOG(LogTemp, Warning, TEXT("Strut collapse [%s]: CollapseEffect is not assigned in the wall Blueprint"), *GetName());
	if (CollapseShakeClass)
		UGameplayStatics::PlayWorldCameraShake(this, CollapseShakeClass,
			EffectLocation, 0.f, FMath::Clamp(CollapseRadiusCm, 50.f, 1000.f) * 2.f);
}

void AMD_CuttableWall::RestoreSupportState(bool bSupported)
{
    // 파일 복원의 직접 상태 교체를 서버로 제한
    if (HasAuthority() == false)
    {
        // 클라이언트의 권위 상태 변경 차단
        return;
    }
    // 이전 월드의 지지 유무를 저장값으로 교체
    bHasSupport = bSupported;
    // 붕괴 이벤트 없이 복원된 값을 클라이언트에 전송
    ForceNetUpdate();
}
