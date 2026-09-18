#include "Ability/Support/MD_GA_PlaceSupport.h"

#include "AbilitySystemComponent.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Component/Interaction/MD_InteractionComponent.h"
#include "Component/Support/MD_SupplyComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/Equipment/MD_ItemDefinition.h"
#include "Engine/Engine.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "PhysicsEngine/BodyInstance.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "World/Support/MD_SupportBrace.h"
#include "World/Wall/MD_CuttableWall.h"

#define LOCTEXT_NAMESPACE "SupportPlacement"

namespace
{
	bool IsParallelCeilingContact(const FOverlapResult& Overlap, const FVector& BraceCenter,
		const FVector& BraceExtent, const FQuat& BraceRotation, const FVector& FloorNormal,
		float MaxAngleDegrees)
	{
		const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Overlap.GetComponent());
		const AActor* Actor = Overlap.GetActor();
		if (!IsValid(Mesh) || Mesh->IsSimulatingPhysics() || !IsValid(Actor)
			|| Actor->IsA<APawn>() || Actor->IsA<AMD_SupportBrace>() || Actor->IsA<AMD_CuttableWall>())
			return false;

		// Query only the body that overlapped, including the specific instance of an ISM.
		const FBodyInstance* Body = Mesh->GetBodyInstance(NAME_None, false, Overlap.GetItemIndex());
		if (!Body) return false;
		const double HalfProbeHeight = FMath::Min(0.5, BraceExtent.Z);
		const FVector VerticalOffset(0, 0, BraceExtent.Z - HalfProbeHeight);
		const FCollisionShape Probe = FCollisionShape::MakeBox(
			FVector(BraceExtent.X, BraceExtent.Y, HalfProbeHeight));
		FHitResult Ceiling;
		if (!Body->Sweep(Ceiling, BraceCenter - VerticalOffset, BraceCenter + VerticalOffset,
			BraceRotation, Probe, false)
			|| Ceiling.bStartPenetrating || Ceiling.ImpactPoint.Z <= BraceCenter.Z)
			return false; // Foot/body obstacles are not overhead ceiling contact.

		// Compare the actual collision faces. Thickness and penetration depth are not limits.
		// The underside normal must face the floor; a top face is not a ceiling.
		const double Alignment = FVector::DotProduct(FloorNormal.GetSafeNormal(),
			-Ceiling.ImpactNormal.GetSafeNormal());
		return Alignment + UE_DOUBLE_SMALL_NUMBER
			>= FMath::Cos(FMath::DegreesToRadians(double(MaxAngleDegrees)));
	}
}

UMD_GA_PlaceSupport::UMD_GA_PlaceSupport()
{
	ActivationBlockedTags.AddTag(MDGameplayTags::State_Swimming);
	ActivationBlockedTags.AddTag(MDGameplayTags::State_Climbing);
	ActivationBlockedTags.AddTag(MDGameplayTags::State_Dead);
}

const UStaticMeshComponent* UMD_GA_PlaceSupport::GetBraceMeshTemplate() const
{
	const AMD_SupportBrace* Brace = BraceClass.GetDefaultObject();
	return IsValid(Brace) ? Brace->GetBraceMesh() : nullptr;
}

void UMD_GA_PlaceSupport::EvaluatePlacement(APawn* Pawn, FMD_SupportPlacement& Out,
	bool bCollectSupportedWalls) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_SupportPlacement_Query);
	Out = FMD_SupportPlacement();
	const UStaticMeshComponent* Template = GetBraceMeshTemplate();
	if (!IsValid(Pawn) || !Pawn->GetController() || !Pawn->GetWorld()
		|| !Template || !Template->GetStaticMesh() || BraceClass->HasAnyClassFlags(CLASS_Abstract))
	{
		Out.FailureReason = LOCTEXT("MissingSetup", "지지대 설정을 확인하세요");
		return;
	}

	UWorld* World = Pawn->GetWorld();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SupportPlacement), false, Pawn);
	TArray<AActor*> Attachments;
	Pawn->GetAttachedActors(Attachments, true, true);
	Params.AddIgnoredActors(Attachments);
	if (const UMD_InteractionComponent* Interaction = Pawn->FindComponentByClass<UMD_InteractionComponent>())
	{
		if (AActor* Held = Interaction->GetHeldActor())
		{
			Params.AddIgnoredActor(Held);
			Held->GetAttachedActors(Attachments, true, true);
			Params.AddIgnoredActors(Attachments);
		}
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	Pawn->GetController()->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FBox Bounds = Template->GetStaticMesh()->GetBoundingBox();
	const FVector Scale = Template->GetRelativeScale3D();
	const FVector Extent = Bounds.GetExtent() * Scale.GetAbs();
	const FQuat Rotation = FRotator(0.f, ViewRotation.Yaw, 0.f).Quaternion();
	const FVector PlacementOrigin = Pawn->GetActorLocation();
	Out.Transform = FTransform(Rotation, PlacementOrigin, Scale);

	FHitResult Aim;
	const bool bAimHit = World->LineTraceSingleByChannel(Aim, ViewLocation,
		ViewLocation + ViewRotation.Vector() * MaxPlacementDistanceCm * 2.f, ECC_Visibility, Params);
	// Clamp distance along the camera's horizontal ray, not radially toward the pawn.
	// This preserves screen-centre direction even when the camera has a lateral offset.
	const FVector AimForward = FRotator(0.f, ViewRotation.Yaw, 0.f).Vector();
	FVector ViewOffset = ViewLocation - PlacementOrigin;
	ViewOffset.Z = 0.f;
	const double AlongRay = FVector::DotProduct(ViewOffset, AimForward);
	const double Discriminant = FMath::Square(double(MaxPlacementDistanceCm))
		- ViewOffset.SizeSquared() + FMath::Square(AlongRay);
	if (Discriminant < 0.0 || AlongRay > FMath::Sqrt(FMath::Max(0.0, Discriminant)))
	{
		Out.FailureReason = LOCTEXT("AimOutsideReach", "설치 가능한 바닥이 보이지 않습니다");
		return; // A camera outside the reach circle must still point into that circle.
	}
	const double RayHalfLength = FMath::Sqrt(Discriminant);
	const double MinAimDistance = FMath::Max(0.0, -AlongRay - RayHalfLength);
	const double MaxAimDistance = FMath::Max(0.0, -AlongRay + RayHalfLength);
	const double AimDistance = bAimHit
		? FVector::DotProduct(Aim.ImpactPoint - ViewLocation, AimForward) : MaxAimDistance;
	FVector Candidate = ViewLocation + AimForward
		* FMath::Clamp(AimDistance, MinAimDistance, MaxAimDistance);
	Candidate.Z = PlacementOrigin.Z;
	// Search at player height, not at the height of a remote wall/ceiling that was aimed at.
	FHitResult Floor;
	const bool bFloorHit = World->LineTraceSingleByChannel(Floor, Candidate + FVector(0, 0, 50),
		Candidate - FVector(0, 0, FloorSearchDepthCm), ECC_Visibility, Params);
	Out.bHasFloor = bFloorHit;
	Out.FloorLocation = bFloorHit ? Floor.ImpactPoint : Candidate;
	Out.FloorNormal = bFloorHit ? Floor.ImpactNormal : FVector::UpVector;
	const FVector MeshCenterOffset = Rotation.RotateVector(Bounds.GetCenter() * Scale);
	Out.Transform.SetLocation(Out.FloorLocation + FVector(0, 0, Extent.Z + 1.f) - MeshCenterOffset);

	if (!bFloorHit || !Floor.GetComponent() || Floor.GetComponent()->IsSimulatingPhysics()
		|| Cast<AMD_CuttableWall>(Floor.GetActor())
		|| Floor.ImpactNormal.Z < FMath::Cos(FMath::DegreesToRadians(MaxFloorSlopeDegrees)))
	{
		Out.FailureReason = LOCTEXT("Floor", "안정적인 바닥을 조준하세요");
		return;
	}
	const ACharacter* Character = Cast<ACharacter>(Pawn);
	if (!Character || !Character->GetCharacterMovement()->IsMovingOnGround())
	{
		Out.FailureReason = LOCTEXT("Grounded", "바닥에 서 있을 때 설치할 수 있습니다");
		return;
	}
	const UMD_SupplyComponent* Supply = Pawn->FindComponentByClass<UMD_SupplyComponent>();
	if (!Supply || Supply->GetSupportCount() <= 0)
	{
		Out.FailureReason = LOCTEXT("Empty", "지지대가 없습니다");
		return;
	}

	// Four foot probes reject ledges and uneven terrain; do not ignore the supporting floor.
	for (int32 Corner = 0; Corner < 4; ++Corner)
	{
		const FVector Offset = Rotation.RotateVector(FVector(
			(Corner & 1 ? 1.f : -1.f) * Extent.X * 0.85f,
			(Corner & 2 ? 1.f : -1.f) * Extent.Y * 0.85f, 0));
		FHitResult Foot;
		const FVector FootLocation = Out.FloorLocation + Offset;
		if (!World->LineTraceSingleByChannel(Foot, FootLocation + FVector(0, 0, 8),
			FootLocation - FVector(0, 0, 8), ECC_Visibility, Params)
			|| Foot.ImpactNormal.Z < FMath::Cos(FMath::DegreesToRadians(MaxFloorSlopeDegrees))
			|| !Foot.GetComponent() || Foot.GetComponent()->IsSimulatingPhysics()
			|| Cast<AMD_CuttableWall>(Foot.GetActor()))
		{
			Out.FailureReason = LOCTEXT("Ledge", "지지대 발판 전체가 바닥에 닿아야 합니다");
			return;
		}
	}
	const FVector Center = Out.Transform.TransformPosition(Bounds.GetCenter());
	const FVector Clearance = (Extent - FVector(0.5f)).ComponentMax(FVector(0.1f));
	FCollisionQueryParams ClearanceParams(SCENE_QUERY_STAT(SupportPlacementClearance), false);
	TArray<FOverlapResult> ClearanceOverlaps;
	World->OverlapMultiByChannel(ClearanceOverlaps, Center, Rotation, ECC_Pawn,
		FCollisionShape::MakeBox(Clearance), ClearanceParams);
	for (const FOverlapResult& Overlap : ClearanceOverlaps)
	{
		if (!Overlap.bBlockingHit) continue;
		if (IsParallelCeilingContact(Overlap, Center, Clearance, Rotation, Out.FloorNormal,
			CeilingParallelToleranceDegrees)) continue;
		Out.FailureReason = LOCTEXT("Blocked", "다른 물체와 겹치는 위치입니다");
		return;
	}
	// The floor must also be reachable from this side of any intervening wall.
	FHitResult Access;
	if (World->LineTraceSingleByChannel(Access, ViewLocation, Center, ECC_Visibility, Params))
	{
		Out.FailureReason = LOCTEXT("Occluded", "장애물 너머에는 설치할 수 없습니다");
		return;
	}

	// A stable, unobstructed floor is sufficient, even with no supportable walls nearby.
	Out.bCanPlace = true;
	if (!bCollectSupportedWalls) return;

	// Wall collection only runs for an actual server placement, never for the per-frame preview.
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_SupportPlacement_CollectWalls);
	TArray<FOverlapResult> Nearby;
	const FVector RangeCenter = Out.FloorLocation + FVector(0, 0, Extent.Z);
	World->OverlapMultiByObjectType(Nearby, RangeCenter, FQuat::Identity,
		FCollisionObjectQueryParams::AllObjects,
		FCollisionShape::MakeBox(FVector(SupportRadiusCm, SupportRadiusCm, Extent.Z)), Params);
	TSet<AMD_CuttableWall*> Candidates;
	for (const FOverlapResult& Overlap : Nearby)
		if (AMD_CuttableWall* Wall = Cast<AMD_CuttableWall>(Overlap.GetActor())) Candidates.Add(Wall);
	// Cut struts have no collision. Existing braces retain their pending loads so a new
	// brace can take over before the last old brace is removed. Placement only, never Tick.
	for (TActorIterator<AMD_SupportBrace> It(World); It; ++It)
	{
		if (It->IsActorBeingDestroyed() || It->IsWeakened()) continue;
		for (const TWeakObjectPtr<AMD_CuttableWall>& Wall : It->GetSupportedWalls())
			if (Wall.IsValid() && Wall->IsAwaitingSupportCollapse()) Candidates.Add(Wall.Get());
	}
	const FVector ContactOrigin = Out.FloorLocation + FVector(0, 0, FMath::Min(100.f, Extent.Z));
	for (AMD_CuttableWall* Wall : Candidates)
	{
		if (!IsValid(Wall) || Wall->GetWallType() != EMD_WallType::Strut
			|| (!Wall->CanCut() && !Wall->IsAwaitingSupportCollapse())) continue;
		const UStaticMeshComponent* WallMesh = Wall->GetScanMesh();
		if (!WallMesh || !WallMesh->GetStaticMesh()) continue;
		const FTransform WallTransform = WallMesh->GetComponentTransform();
		const FBox WallBounds = WallMesh->GetStaticMesh()->GetBoundingBox();
		const FVector Closest = WallTransform.TransformPosition(
			WallBounds.GetClosestPointTo(WallTransform.InverseTransformPosition(ContactOrigin)));
		// A horizontal circle at brace height must not connect a different storey.
		if (FMath::Abs(Closest.Z - ContactOrigin.Z) > 25.f
			|| FVector::DistSquared2D(Closest, Out.FloorLocation) > FMath::Square(SupportRadiusCm)) continue;
		FHitResult Obstacle;
		if (World->LineTraceSingleByChannel(Obstacle, ContactOrigin, Closest, ECC_Visibility, Params)
			&& Obstacle.GetActor() != Wall) continue;
		Out.Walls.Add(Wall);
	}
}

void UMD_GA_PlaceSupport::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	APawn* Pawn = ActorInfo ? Cast<APawn>(ActorInfo->AvatarActor.Get()) : nullptr;
	UMD_EquipHotbarComponent* Hotbar = Pawn ? Pawn->FindComponentByClass<UMD_EquipHotbarComponent>() : nullptr;
	if (!Hotbar || Hotbar->FindItemAbility(Hotbar->GetCurrentItem()) != Handle)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	FMD_SupportPlacement Placement;
	EvaluatePlacement(Pawn, Placement, Pawn && Pawn->HasAuthority());
	if (!Placement.bCanPlace)
	{
		if (Pawn->IsLocallyControlled() && GEngine)
			GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Yellow, Placement.FailureReason.ToString());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	if (Pawn->HasAuthority())
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.TransformScaleMethod = ESpawnActorScaleMethod::OverrideRootScale;
		// The validated transform must not be moved by a second, different placement policy.
		AMD_SupportBrace* Brace = Pawn->GetWorld()->SpawnActor<AMD_SupportBrace>(BraceClass, Placement.Transform, SpawnParams);
		UMD_SupplyComponent* Supply = Pawn->FindComponentByClass<UMD_SupplyComponent>();
		if (IsValid(Brace))
		{
			if (Supply && Supply->TryConsumeSupport())
			{
				Brace->SetSupportedWalls(Placement.Walls);
				Pawn->ForceNetUpdate();
			}
			else Brace->Destroy();
		}
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
#undef LOCTEXT_NAMESPACE
