#include "Component/Interaction/MD_InteractionComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Camera/CameraComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "Net/UnrealNetwork.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"
#include "World/Sample/MD_QuestItem.h"

UMD_InteractionComponent::UMD_InteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
}

void UMD_InteractionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UMD_InteractionComponent, HeldActor);
}

void UMD_InteractionComponent::SetInteractionDependencies(UCameraComponent* InCamera, USceneComponent* InHoldPoint, UPhysicsHandleComponent* InPhysicsHandle)
{
	Camera = InCamera;
	HoldPoint = InHoldPoint;
	PhysicsHandle = InPhysicsHandle;
}

void UMD_InteractionComponent::BeginPlay()
{
	Super::BeginPlay();
	CaptureDefaultHoldProfile();
	RefreshTickEnabled();
}

void UMD_InteractionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelInteraction();

	Super::EndPlay(EndPlayReason);
}

void UMD_InteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	APawn* OwnerPawn = GetOwnerPawn();
	if (!IsValid(OwnerPawn))
	{
		SetComponentTickEnabled(false);
		return;
	}

	if (!CanOwnerInteract())
	{
		SetCurrentTarget(nullptr);

		if (OwnerPawn->HasAuthority() && (HeldActor || HasAuthorityPhysicsGrip()))
		{
			ReleaseHeldActor();
		}
		else
		{
			RefreshTickEnabled();
		}
		return;
	}

	if (OwnerPawn->IsLocallyControlled())
	{
		UpdateInteractionFocus();
	}

	if (OwnerPawn->HasAuthority() && (HeldActor || HasAuthorityPhysicsGrip()))
	{
		// 대상이 파괴되면 핸들이 조용히 비므로 복제 상태도 같이 정리한다.
		if (!IsValid(HeldActor) || !HasAuthorityPhysicsGrip())
		{
			ReleaseHeldActor();
			return;
		}

		UpdateHeldTarget();
		UpdateCarrySettle(DeltaTime);
	}
}

void UMD_InteractionComponent::Interact()
{
	APawn* OwnerPawn = GetOwnerPawn();
	if (!IsValid(OwnerPawn) || !CanOwnerInteract())
	{
		SetCurrentTarget(nullptr);
		return;
	}

	AActor* Target = CurrentTarget.Get();

	// New interactables are server-authoritative by default. UI-only targets must explicitly opt out.
	if (IsValid(Target) && Target->Implements<UMD_Interactable>())
	{
		const bool bRequiresServerInteraction =
			IMD_Interactable::Execute_RequiresServerInteraction(Target);

		if (!bRequiresServerInteraction)
		{
			IMD_Interactable::Execute_Interact(Target, OwnerPawn);
			return;
		}
	}

	ServerInteract(Target);
}

void UMD_InteractionComponent::CancelInteraction()
{
	SetCurrentTarget(nullptr);

	const APawn* OwnerPawn = GetOwnerPawn();
	if (IsValid(OwnerPawn) && OwnerPawn->HasAuthority())
	{
		ReleaseHeldActor();
	}
}

void UMD_InteractionComponent::SetInteractionBlocked(bool bBlocked)
{
	bInteractionBlocked = bBlocked;

	if (bInteractionBlocked)
	{
		CancelInteraction();
	}

	RefreshTickEnabled();
}

void UMD_InteractionComponent::ServerInteract_Implementation(AActor* RequestedActor)
{
	APawn* OwnerPawn = GetOwnerPawn();
	if (!IsValid(OwnerPawn) || !OwnerPawn->HasAuthority()) return;
	if (!CanOwnerInteract())
	{
		ReleaseHeldActor();
		return;
	}

	if (HeldActor || HasAuthorityPhysicsGrip())
	{
		ReleaseHeldActor();
		return;
	}

	const UWorld* World = GetWorld();
	if (!World) return;

	const double ServerTimeSeconds = World->GetTimeSeconds();
	const double ElapsedSeconds = ServerTimeSeconds - LastServerInteractionTimeSeconds;
	const double MinIntervalSeconds = FMath::Clamp(ServerInteractionMinInterval, 0.05f, 0.1f);
	if (bHasServerInteractionTimestamp && ElapsedSeconds >= 0.0
		&& ElapsedSeconds < MinIntervalSeconds)
	{
		return;
	}

	bHasServerInteractionTimestamp = true;
	LastServerInteractionTimeSeconds = ServerTimeSeconds;

	if (!IsValid(RequestedActor) || !RequestedActor->Implements<UMD_Interactable>())
	{
		return;
	}

	if (!ValidateInteractionTarget(RequestedActor))
	{
		return;
	}

	// 핫바는 장비 전용 — 월드 오브젝트 픽업은 물리 운반만 허용 (기획 02 §2.5).
	const IGameplayTagAssetInterface* TagAsset = Cast<IGameplayTagAssetInterface>(RequestedActor);
	if (TagAsset
		&& TagAsset->HasMatchingGameplayTag(MDGameplayTags::Interaction_Pickup_Physics))
	{
		TryHoldTarget(RequestedActor);
		return;
	}

	IMD_Interactable::Execute_Interact(RequestedActor, OwnerPawn);
}

bool UMD_InteractionComponent::TryGrabPhysicsComponent(
	UPrimitiveComponent* Component,
	const FVector& PreferredGrabLocation,
	const FMD_HoldProfile& HoldProfile)
{
	if (!IsValid(Component) || !IsValid(PhysicsHandle))
	{
		return false;
	}

	// 물리를 켜기 전에 끊는다. SetSimulatePhysics 직후 첫 솔버 스텝부터 캡슐과의 접촉이
	// 풀리기 시작하므로, 그랩이 끝난 뒤에 끊으면 이미 한 번 밀린 뒤가 된다.
	BeginCarryCollisionIsolation(Component);

	if (!Component->IsSimulatingPhysics())
	{
		Component->SetSimulatePhysics(true);
		Component->RecreatePhysicsState();
	}

	if (UGeometryCollectionComponent* Collection = Cast<UGeometryCollectionComponent>(Component))
	{
		Collection->WakeAllRigidBodies();
	}

	TArray<FVector, TInlineAllocator<3>> GrabCandidates;
	GrabCandidates.Add(PreferredGrabLocation);
	GrabCandidates.Add(Component->GetCenterOfMass());
	GrabCandidates.Add(Component->GetComponentLocation());

	auto AttemptGrab = [this, Component](const FVector& GrabLocation, bool bConstrainRotation) -> bool
	{
		if (bConstrainRotation)
		{
			PhysicsHandle->GrabComponentAtLocationWithRotation(
				Component,
				NAME_None,
				GrabLocation,
				Component->GetComponentRotation());
		}
		else
		{
			PhysicsHandle->GrabComponentAtLocation(Component, NAME_None, GrabLocation);
		}

		return PhysicsHandle->GetGrabbedComponent() == Component;
	};

	ApplyHoldProfile(HoldProfile);
	for (const FVector& GrabLocation : GrabCandidates)
	{
		if (AttemptGrab(GrabLocation, HoldProfile.bConstrainRotation))
		{
			return true;
		}
	}

	// GC는 회전 고정 Grab이 실패하는 경우가 많다.
	FMD_HoldProfile LooseProfile = HoldProfile;
	LooseProfile.bConstrainRotation = false;
	ApplyHoldProfile(LooseProfile);
	for (const FVector& GrabLocation : GrabCandidates)
	{
		if (AttemptGrab(GrabLocation, false))
		{
			return true;
		}
	}

	ApplyHoldProfile(DefaultHoldProfile);
	EndCarryCollisionIsolation();
	return false;
}

UPrimitiveComponent* UMD_InteractionComponent::GetOwnerCollisionComponent() const
{
	const APawn* OwnerPawn = GetOwnerPawn();
	if (!IsValid(OwnerPawn))
	{
		return nullptr;
	}

	return Cast<UPrimitiveComponent>(OwnerPawn->GetRootComponent());
}

void UMD_InteractionComponent::BeginCarryCollisionIsolation(UPrimitiveComponent* Component)
{
	// 이전 운반이 정리되지 않은 채로 들어와도 상태가 겹치지 않게 한다.
	EndCarryCollisionIsolation();

	if (!IsValid(Component))
	{
		return;
	}

	CarryIsolatedPawnResponse = Component->GetCollisionResponseToChannel(ECC_Pawn);
	bHasCarryCollisionIsolation = true;
	CarryIsolatedComponent = Component;

	// 이게 발사를 막는 본체다. MoveIgnoreComponents는 InitSweepCollisionParams를 타고
	// CMC의 바닥 탐지에까지 반영되므로, 캐릭터가 운반물을 무브먼트 베이스로 삼지 못한다.
	// 물체가 튕겨나가도 캐릭터를 끌고 갈 경로가 없어진다.
	if (UPrimitiveComponent* OwnerCollision = GetOwnerCollisionComponent())
	{
		OwnerCollision->IgnoreComponentWhenMoving(Component, true);
	}

	// 솔버 접촉까지 끊는 건 기본적으로 하지 않는다. 끊으면 물체가 캐릭터를 통과해 머리 뒤로 빠진다.
	if (bIsolateCarriedFromPawnChannel)
	{
		Component->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}
}

void UMD_InteractionComponent::EndCarryCollisionIsolation()
{
	if (!bHasCarryCollisionIsolation)
	{
		return;
	}

	bHasCarryCollisionIsolation = false;

	UPrimitiveComponent* Component = CarryIsolatedComponent.Get();
	CarryIsolatedComponent = nullptr;

	// 운반 중 파괴된 물체는 되돌릴 대상이 없다. 플래그만 내리고 끝낸다.
	if (!IsValid(Component))
	{
		return;
	}

	if (UPrimitiveComponent* OwnerCollision = GetOwnerCollisionComponent())
	{
		OwnerCollision->IgnoreComponentWhenMoving(Component, false);
	}

	Component->SetCollisionResponseToChannel(ECC_Pawn, CarryIsolatedPawnResponse);
}

bool UMD_InteractionComponent::TryHoldTarget(AActor* Target)
{
	APawn* OwnerPawn = GetOwnerPawn();
	if (!IsValid(OwnerPawn) || !OwnerPawn->HasAuthority()
		|| !CanOwnerInteract() || !IsValid(Target) || !IsValid(PhysicsHandle)) return false;

	// 요청이 서버에 도착하기 전에 다른 플레이어가 먼저 잡았을 수 있다.
	if (IsHeldByOtherPawn(Target))
	{
		return false;
	}

	UPrimitiveComponent* Component = nullptr;
	FVector GrabLocation = FVector::ZeroVector;
	FVector HoldOffset = FVector::ZeroVector;
	FMD_HoldProfile HoldProfile = DefaultHoldProfile;

	if (Target->Implements<UMD_Grabbable>())
	{
		Component = IMD_Grabbable::Execute_GetGrabComponent(Target);
		const USceneComponent* GrabPoint = IMD_Grabbable::Execute_GetGrabPoint(Target);

		if (!IsValid(Component) || !IsValid(GrabPoint))
		{
			return false;
		}

		GrabLocation = GrabPoint->GetComponentLocation();
		HoldOffset = IMD_Grabbable::Execute_GetHoldOffset(Target);
		HoldProfile = IMD_Grabbable::Execute_GetHoldProfile(Target);
	}
	else
	{
		Component = Cast<UPrimitiveComponent>(Target->GetRootComponent());
		if (!IsValid(Component))
		{
			return false;
		}

		GrabLocation = Component->GetCenterOfMass();
	}

	if (!TryGrabPhysicsComponent(Component, GrabLocation, HoldProfile))
	{
		return false;
	}

	HeldTargetOffset = HoldOffset;
	HeldGrabPointOffset = Component->GetComponentTransform().InverseTransformPositionNoScale(GrabLocation);
	ActiveHoldProfile = HoldProfile;
	HeldActor = Target;
	bHeldActorSettled = false;
	HeldCarrySeconds = 0.f;

	// The server remains the sole physics authority; preserve the actor's existing movement replication strategy.
	// Clients never move the carried actor from their local HoldPoint.

	// PhysicsHandle이 HoldPoint로 급가속하는 첫 순간의 오탐 충격을 흡수한다.
	if (AMD_QuestItem* QuestItem = Cast<AMD_QuestItem>(Target))
	{
		QuestItem->NotifyGrabbed();
	}

	if (Target->Implements<UMD_Grabbable>())
	{
		IMD_Grabbable::Execute_OnCarryStarted(Target);
	}

	UpdateHeldTarget();
	Target->ForceNetUpdate();
	OwnerPawn->ForceNetUpdate();
	RefreshTickEnabled();
	return true;
}

bool UMD_InteractionComponent::IsHolding() const
{
	return IsValid(HeldActor);
}

bool UMD_InteractionComponent::CanOwnerInteract() const
{
	APawn* OwnerPawn = GetOwnerPawn();
	if (!IsValid(OwnerPawn) || bInteractionBlocked)
	{
		return false;
	}

	const AController* Controller = OwnerPawn->GetController();
	if (!IsValid(Controller) || Controller->GetPawn() != OwnerPawn)
	{
		return false;
	}

	const IAbilitySystemInterface* AbilitySystemOwner = Cast<IAbilitySystemInterface>(OwnerPawn);
	const UAbilitySystemComponent* AbilitySystem = AbilitySystemOwner
		? AbilitySystemOwner->GetAbilitySystemComponent()
		: nullptr;

	return !IsValid(AbilitySystem)
		|| !AbilitySystem->HasMatchingGameplayTag(MDGameplayTags::State_Dead);
}

bool UMD_InteractionComponent::HasAuthorityPhysicsGrip() const
{
	const APawn* OwnerPawn = GetOwnerPawn();
	return IsValid(OwnerPawn) && OwnerPawn->HasAuthority()
		&& IsValid(PhysicsHandle) && IsValid(PhysicsHandle->GetGrabbedComponent());
}

void UMD_InteractionComponent::RefreshTickEnabled()
{
	const APawn* OwnerPawn = GetOwnerPawn();
	const bool bNeedsLocalFocus = IsValid(OwnerPawn)
		&& OwnerPawn->IsLocallyControlled() && CanOwnerInteract();
	const bool bNeedsAuthorityCarry = IsValid(OwnerPawn)
		&& OwnerPawn->HasAuthority() && (HeldActor || HasAuthorityPhysicsGrip());

	SetComponentTickEnabled(bNeedsLocalFocus || bNeedsAuthorityCarry);
}

bool UMD_InteractionComponent::IsHeldByOtherPawn(const AActor* Target) const
{
	const APawn* OwnerPawn = GetOwnerPawn();
	const UWorld* World = GetWorld();
	if (!IsValid(OwnerPawn) || !IsValid(Target) || !World) return false;

	const AGameStateBase* GameState = World->GetGameState();
	if (!IsValid(GameState)) return false;

	for (const APlayerState* OtherPlayerState : GameState->PlayerArray)
	{
		if (!IsValid(OtherPlayerState)) continue;

		const APawn* OtherPawn = OtherPlayerState->GetPawn();
		if (!IsValid(OtherPawn) || OtherPawn == OwnerPawn) continue;

		const UMD_InteractionComponent* OtherInteraction =
			OtherPawn->FindComponentByClass<UMD_InteractionComponent>();
		if (IsValid(OtherInteraction) && OtherInteraction->HeldActor == Target)
		{
			return true;
		}
	}

	return false;
}

void UMD_InteractionComponent::ReleaseHeldActor()
{
	APawn* OwnerPawn = GetOwnerPawn();
	if (!IsValid(OwnerPawn) || !OwnerPawn->HasAuthority()) return;

	// 그립이 이미 사라진 경우(운반 중 대상 파괴 등)에도 끊어둔 충돌은 반드시 원복한다.
	EndCarryCollisionIsolation();

	if (!HeldActor && !HasAuthorityPhysicsGrip())
	{
		RefreshTickEnabled();
		return;
	}

	AActor* PreviousHeldActor = HeldActor;
	HeldActor = nullptr;
	HeldTargetOffset = FVector::ZeroVector;
	HeldGrabPointOffset = FVector::ZeroVector;
	bHeldActorSettled = false;
	HeldCarrySeconds = 0.f;

	// Release server physics before notifying gameplay code or replicating the empty HeldActor state.
	if (IsValid(PhysicsHandle) && PhysicsHandle->GetGrabbedComponent())
	{
		PhysicsHandle->ReleaseComponent();
	}

	ApplyHoldProfile(DefaultHoldProfile);
	ActiveHoldProfile = DefaultHoldProfile;

	if (IsValid(PreviousHeldActor))
	{
		if (PreviousHeldActor->Implements<UMD_Grabbable>())
		{
			IMD_Grabbable::Execute_OnCarryEnded(PreviousHeldActor);
		}

		PreviousHeldActor->ForceNetUpdate();
	}

	OwnerPawn->ForceNetUpdate();
	RefreshTickEnabled();
}

FTransform UMD_InteractionComponent::GetHoldPointTransform() const
{
	const APawn* OwnerPawn = GetOwnerPawn();
	if (!IsValid(OwnerPawn) || !IsValid(HoldPoint)) return FTransform::Identity;

	// 원격 캐릭터의 카메라 회전은 낡을 수 있어 복제되는 시점 회전으로 다시 만든다.
	if (!IsValid(Camera)) return HoldPoint->GetComponentTransform();

	const FTransform ViewTransform(OwnerPawn->GetBaseAimRotation(), Camera->GetComponentLocation());
	return HoldPoint->GetRelativeTransform() * ViewTransform;
}

FVector UMD_InteractionComponent::ResolveHoldTargetLocation(const FTransform& HoldTransform) const
{
	const FVector Desired = HoldTransform.TransformPositionNoScale(HeldTargetOffset);

	const UPrimitiveComponent* Held = IsValid(PhysicsHandle) ? PhysicsHandle->GetGrabbedComponent() : nullptr;
	const UWorld* World = GetWorld();
	if (!IsValid(Held) || !IsValid(Camera) || !World)
	{
		return Desired;
	}

	const FVector Origin = Camera->GetComponentLocation();
	const float ProbeRadius = FMath::Max(
		HoldProbeMinRadius,
		static_cast<float>(Held->Bounds.SphereRadius) * HoldProbeRadiusScale);

	// 폰과 다른 물리 프롭은 제외한다. 목표점을 막아야 할 건 월드 지오메트리뿐이다.
	// 채널 스윕으로 양쪽 충돌 응답을 함께 확인해야 Overlap 감지 볼륨을 벽으로 오인하지 않는다.
	FCollisionResponseParams ProbeResponses(ECR_Ignore);
	ProbeResponses.CollisionResponse.SetResponse(
		ECC_WorldStatic,
		Held->GetCollisionResponseToChannel(ECC_WorldStatic));
	ProbeResponses.CollisionResponse.SetResponse(
		ECC_WorldDynamic,
		Held->GetCollisionResponseToChannel(ECC_WorldDynamic));

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MDHoldProbe), false, GetOwner());
	QueryParams.AddIgnoredActor(Held->GetOwner());

	FHitResult Hit;
	const bool bBlocked = World->SweepSingleByChannel(
		Hit, Origin, Desired, FQuat::Identity, Held->GetCollisionObjectType(),
		FCollisionShape::MakeSphere(ProbeRadius), QueryParams, ProbeResponses);

	const FVector ToDesired = Desired - Origin;
	const double DesiredDistance = ToDesired.Size();

	// 시작 지점이 이미 묻혀 있으면 Distance가 0이라 물체가 얼굴로 끌려온다. 기존 동작을 유지한다.
	if (!bBlocked || Hit.bStartPenetrating || DesiredDistance <= UE_KINDA_SMALL_NUMBER)
	{
		return Desired;
	}

	// 하한이 상한을 넘지 않게 먼저 접는다. HoldMinDistance를 HoldPoint 거리보다 크게 잡아도 안전하다.
	const double MinDistance = FMath::Min(static_cast<double>(HoldMinDistance), DesiredDistance);
	const double ClampedDistance = FMath::Clamp(static_cast<double>(Hit.Distance), MinDistance, DesiredDistance);

	return Origin + (ToDesired / DesiredDistance) * ClampedDistance;
}

void UMD_InteractionComponent::UpdateHeldTarget()
{
	const APawn* OwnerPawn = GetOwnerPawn();
	if (!IsValid(OwnerPawn) || !OwnerPawn->HasAuthority()
		|| !HasAuthorityPhysicsGrip() || !IsValid(HoldPoint)) return;

	const FTransform HoldTransform = GetHoldPointTransform();
	const FVector TargetLocation = ResolveHoldTargetLocation(HoldTransform);

	if (ActiveHoldProfile.bConstrainRotation)
	{
		PhysicsHandle->SetTargetLocationAndRotation(TargetLocation, HoldTransform.Rotator());
	}
	else
	{
		PhysicsHandle->SetTargetLocation(TargetLocation);
	}
}

void UMD_InteractionComponent::UpdateCarrySettle(float DeltaSeconds)
{
	const APawn* OwnerPawn = GetOwnerPawn();
	if (!IsValid(OwnerPawn) || !OwnerPawn->HasAuthority()
		|| bHeldActorSettled || !IsValid(HeldActor) || !IsValid(HoldPoint)) return;

	if (!HeldActor->Implements<UMD_Grabbable>())
	{
		bHeldActorSettled = true;
		return;
	}

	HeldCarrySeconds += DeltaSeconds;

	const UPrimitiveComponent* Component = PhysicsHandle ? PhysicsHandle->GetGrabbedComponent() : nullptr;
	if (!IsValid(Component)) return;

	const FVector CurrentGrabPoint =
		Component->GetComponentTransform().TransformPositionNoScale(HeldGrabPointOffset);
	// 정착 판정은 스프링이 실제로 쫓는 좌표와 같아야 한다. 보정 전 좌표로 재면
	// 시선을 내린 채 집었을 때 영영 도달하지 못해 OnCarrySettled가 불리지 않는다.
	const FVector TargetLocation = ResolveHoldTargetLocation(GetHoldPointTransform());

	const bool bReachedTarget =
		FVector::DistSquared(CurrentGrabPoint, TargetLocation) <= FMath::Square(CarrySettleTolerance);
	const bool bTimedOut = HeldCarrySeconds >= CarrySettleTimeout;

	if (bTimedOut && !bReachedTarget)
	{
		// Timeout ends monitoring only. OnCarrySettled is reserved for a real physical arrival.
		bHeldActorSettled = true;
		return;
	}

	if (!bReachedTarget) return;

	bHeldActorSettled = true;
	IMD_Grabbable::Execute_OnCarrySettled(HeldActor);
}

void UMD_InteractionComponent::OnRep_HeldActor()
{
	// HeldActor is only presentation state on clients. Actor/physics movement stays server-authoritative.
	if (IsHolding())
	{
		SetCurrentTarget(nullptr);
	}

	RefreshTickEnabled();
}

void UMD_InteractionComponent::CaptureDefaultHoldProfile()
{
	if (!IsValid(PhysicsHandle)) return;

	DefaultHoldProfile.bConstrainRotation = true;
	DefaultHoldProfile.bSoftLinear = PhysicsHandle->bSoftLinearConstraint;
	DefaultHoldProfile.bSoftAngular = PhysicsHandle->bSoftAngularConstraint;
	DefaultHoldProfile.bInterpolate = PhysicsHandle->bInterpolateTarget;
	DefaultHoldProfile.LinearStiffness = PhysicsHandle->LinearStiffness;
	DefaultHoldProfile.LinearDamping = PhysicsHandle->LinearDamping;
	DefaultHoldProfile.AngularStiffness = PhysicsHandle->AngularStiffness;
	DefaultHoldProfile.AngularDamping = PhysicsHandle->AngularDamping;
	DefaultHoldProfile.InterpolationSpeed = PhysicsHandle->InterpolationSpeed;
	ActiveHoldProfile = DefaultHoldProfile;
}

void UMD_InteractionComponent::ApplyHoldProfile(const FMD_HoldProfile& Profile)
{
	if (!IsValid(PhysicsHandle)) return;

	PhysicsHandle->bSoftLinearConstraint = Profile.bSoftLinear;
	PhysicsHandle->bSoftAngularConstraint = Profile.bSoftAngular;
	PhysicsHandle->bInterpolateTarget = Profile.bInterpolate;
	PhysicsHandle->SetLinearStiffness(FMath::Max(0.f, Profile.LinearStiffness));
	PhysicsHandle->SetLinearDamping(FMath::Max(0.f, Profile.LinearDamping));
	PhysicsHandle->SetAngularStiffness(FMath::Max(0.f, Profile.AngularStiffness));
	PhysicsHandle->SetAngularDamping(FMath::Max(0.f, Profile.AngularDamping));
	PhysicsHandle->SetInterpolationSpeed(FMath::Max(0.f, Profile.InterpolationSpeed));
}

void UMD_InteractionComponent::UpdateInteractionFocus()
{
	APawn* OwnerPawn = GetOwnerPawn();
	if (!IsValid(OwnerPawn) || !OwnerPawn->IsLocallyControlled()
		|| !OwnerPawn->GetController() || !GetWorld())
	{
		SetCurrentTarget(nullptr);
		return;
	}

	// 들고 있는 물체 너머의 대상이 반짝이지 않게 하고, 놓기는 대상이 없어도 서버 RPC로 처리한다.
	if (HeldActor)
	{
		SetCurrentTarget(nullptr);
		return;
	}

	SetCurrentTarget(TraceInteractionTarget());
}

AActor* UMD_InteractionComponent::TraceInteractionTarget() const
{
	const APawn* OwnerPawn = GetOwnerPawn();
	AController* Controller = IsValid(OwnerPawn) ? OwnerPawn->GetController() : nullptr;
	if (!IsValid(Controller) || !GetWorld()) return nullptr;

	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);

	const FVector TraceEnd = ViewLocation + ViewRotation.Vector() * InteractTraceDistance;

	// GeometryCollection은 복합(complex) 콜리전이 없다. bTraceComplex를 켜면 GC를 통째로 놓친다.
	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(InteractTrace), /*bTraceComplex*/ false);
	QueryParams.AddIgnoredActor(OwnerPawn);

	if (!GetWorld()->LineTraceSingleByChannel(
		Hit,
		ViewLocation,
		TraceEnd,
		MDCollisionChannels::InteractTrace,
		QueryParams))
	{
		return nullptr;
	}

	AActor* HitActor = Hit.GetActor();
	if (!IsValid(HitActor) && IsValid(Hit.GetComponent()))
	{
		HitActor = Hit.GetComponent()->GetOwner();
	}

	if (!IsValid(HitActor) || !HitActor->Implements<UMD_Interactable>())
	{
		return nullptr;
	}
	if (IsHeldByOtherPawn(HitActor)) return nullptr;

	return HitActor;
}

bool UMD_InteractionComponent::ValidateInteractionTarget(AActor* RequestedActor) const
{
	if (!IsValid(RequestedActor))
	{
		return false;
	}

	if (TraceInteractionTarget() == RequestedActor)
	{
		return true;
	}

	// 클라이언트는 조준·하이라이트가 맞는데 서버 trace가 빗나가는 경우가 있다 (지연, GC 콜리전).
	const APawn* OwnerPawn = GetOwnerPawn();
	const AController* Controller = IsValid(OwnerPawn) ? OwnerPawn->GetController() : nullptr;
	if (!Controller || !GetWorld())
	{
		return false;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);

	const FVector ViewDirection = ViewRotation.Vector();
	const FBox Bounds = RequestedActor->GetComponentsBoundingBox(true);
	const FVector ClosestPoint = Bounds.GetClosestPointTo(ViewLocation);
	const FVector ToClosest = ClosestPoint - ViewLocation;
	const float Distance = ToClosest.Size();

	static constexpr float RangeToleranceCm = 50.f;
	if (Distance > InteractTraceDistance + RangeToleranceCm)
	{
		return false;
	}

	const IGameplayTagAssetInterface* TagAsset = Cast<IGameplayTagAssetInterface>(RequestedActor);
	const bool bPickupTarget = TagAsset
		&& TagAsset->HasMatchingGameplayTag(MDGameplayTags::Interaction_Pickup_Physics);

	// 픽업 대상은 GC 콜리전·멀티 지연 때문에 조준 각도를 조금 더 완화한다.
	// 다만 조준하지 않은 물체까지 서버가 잡아주면 desync·악용 여지가 생기므로
	// 콘 각도는 사람이 "그걸 보고 있었다"고 인정할 범위로 제한한다.
	static constexpr float MinAlignmentPickup = 0.5f;    // 약 60°
	static constexpr float MinAlignmentDefault = 0.7f;   // 약 45°
	const float MinAlignment = bPickupTarget ? MinAlignmentPickup : MinAlignmentDefault;
	if (Distance > KINDA_SMALL_NUMBER)
	{
		const float Alignment = FVector::DotProduct(ViewDirection, ToClosest / Distance);
		if (Alignment < MinAlignment)
		{
			return false;
		}
	}

	// The relaxed bounds/aim fallback must not authorize an actor hidden by world geometry
	// or another interactable. A clear trace may either hit the requested actor itself or hit nothing
	// (for example, a GeometryCollection without a matching simple collision representation).
	const auto IsOccludedOnChannel = [this, OwnerPawn, RequestedActor, &ViewLocation, &ClosestPoint](
		ECollisionChannel Channel) -> bool
	{
		FHitResult OcclusionHit;
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(InteractFallbackOcclusion), /*bTraceComplex*/ false);
		QueryParams.AddIgnoredActor(OwnerPawn);

		if (!GetWorld()->LineTraceSingleByChannel(
			OcclusionHit,
			ViewLocation,
			ClosestPoint,
			Channel,
			QueryParams))
		{
			return false;
		}

		AActor* BlockingActor = OcclusionHit.GetActor();
		if (!IsValid(BlockingActor) && IsValid(OcclusionHit.GetComponent()))
		{
			BlockingActor = OcclusionHit.GetComponent()->GetOwner();
		}

		return BlockingActor != RequestedActor;
	};

	if (IsOccludedOnChannel(ECC_Visibility)
		|| IsOccludedOnChannel(MDCollisionChannels::InteractTrace))
	{
		return false;
	}

	return true;
}

void UMD_InteractionComponent::SetCurrentTarget(AActor* NewTarget)
{
	if (CurrentTarget.Get() == NewTarget) return;

	if (AActor* PreviousTarget = CurrentTarget.Get())
	{
		if (PreviousTarget->Implements<UMD_Interactable>())
		{
			IMD_Interactable::Execute_SetInteractionHighlighted(PreviousTarget, false);
		}
	}

	CurrentTarget = NewTarget;

	if (IsValid(NewTarget) && NewTarget->Implements<UMD_Interactable>())
	{
		IMD_Interactable::Execute_SetInteractionHighlighted(NewTarget, true);
	}
}

APawn* UMD_InteractionComponent::GetOwnerPawn() const
{
	return Cast<APawn>(GetOwner());
}
