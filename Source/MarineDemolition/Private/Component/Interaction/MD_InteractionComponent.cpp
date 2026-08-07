#include "Component/Interaction/MD_InteractionComponent.h"

#include "Camera/CameraComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Net/UnrealNetwork.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"

UMD_InteractionComponent::UMD_InteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UMD_InteractionComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UMD_InteractionComponent, HeldActor);
}

void UMD_InteractionComponent::SetInteractionDependencies(
	UCameraComponent* InCamera,
	USceneComponent* InHoldPoint,
	UPhysicsHandleComponent* InPhysicsHandle)
{
	Camera = InCamera;
	HoldPoint = InHoldPoint;
	PhysicsHandle = InPhysicsHandle;
}

void UMD_InteractionComponent::BeginPlay()
{
	Super::BeginPlay();
	CaptureDefaultHoldProfile();
}

void UMD_InteractionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetCurrentTarget(nullptr);
	ReleaseTarget();
	StopFollowingHeldActor();

	Super::EndPlay(EndPlayReason);
}

void UMD_InteractionComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	APawn* OwnerPawn = GetOwnerPawn();
	if (!IsValid(OwnerPawn)) return;

	UpdateInteractionFocus();

	if (OwnerPawn->HasAuthority())
	{
		// 대상이 파괴되면 핸들이 조용히 비므로 복제 상태도 같이 정리한다.
		if (HeldActor && !IsHolding())
		{
			ReleaseTarget();
		}

		UpdateHeldTarget();
		UpdateCarrySettle(DeltaTime);
	}
	else
	{
		UpdateFollowedHeldActor(DeltaTime);
	}
}

void UMD_InteractionComponent::Interact()
{
	APawn* OwnerPawn = GetOwnerPawn();
	if (!IsValid(OwnerPawn)) return;

	AActor* Target = CurrentTarget.Get();

	// 물리 픽업 또는 대상이 서버 실행을 요구하는 상호작용은 검증된 ServerInteract 경로로 보낸다.
	// 이동 콘솔·상인처럼 로컬 UI만 여는 대상은 기존처럼 로컬에서 즉시 실행한다.
	if (IsValid(Target) && Target->Implements<UMD_Interactable>())
	{
		const IGameplayTagAssetInterface* TagAsset = Cast<IGameplayTagAssetInterface>(Target);
		const bool bPickupTarget = TagAsset
			&& TagAsset->HasMatchingGameplayTag(MDGameplayTags::Interaction_Pickup_Physics);
		const bool bRequiresServerInteraction =
			IMD_Interactable::Execute_RequiresServerInteraction(Target);

		if (!bPickupTarget && !bRequiresServerInteraction)
		{
			IMD_Interactable::Execute_Interact(Target, OwnerPawn);
			return;
		}
	}

	ServerInteract(Target);
}

void UMD_InteractionComponent::ServerInteract_Implementation(AActor* RequestedActor)
{
	APawn* OwnerPawn = GetOwnerPawn();
	if (!IsValid(OwnerPawn) || !OwnerPawn->HasAuthority()) return;

	if (IsHolding())
	{
		ReleaseTarget();
		return;
	}

	if (!IsValid(RequestedActor)
		|| !RequestedActor->Implements<UMD_Interactable>()
		|| TraceInteractionTarget() != RequestedActor) return;

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

bool UMD_InteractionComponent::TryHoldTarget(AActor* Target)
{
	APawn* OwnerPawn = GetOwnerPawn();
	if (!IsValid(OwnerPawn) || !OwnerPawn->HasAuthority()
		|| !IsValid(Target) || !IsValid(PhysicsHandle)) return false;

	// 요청이 서버에 도착하기 전에 다른 플레이어가 먼저 잡았을 수 있다.
	if (IsHeldByOtherPawn(Target)) return false;

	UPrimitiveComponent* Component = nullptr;
	FVector GrabLocation = FVector::ZeroVector;
	FVector HoldOffset = FVector::ZeroVector;
	FMD_HoldProfile HoldProfile = DefaultHoldProfile;

	if (Target->Implements<UMD_Grabbable>())
	{
		Component = IMD_Grabbable::Execute_GetGrabComponent(Target);
		const USceneComponent* GrabPoint = IMD_Grabbable::Execute_GetGrabPoint(Target);

		if (!IsValid(Component) || !Component->IsSimulatingPhysics() || !IsValid(GrabPoint)) return false;

		GrabLocation = GrabPoint->GetComponentLocation();
		HoldOffset = IMD_Grabbable::Execute_GetHoldOffset(Target);
		HoldProfile = IMD_Grabbable::Execute_GetHoldProfile(Target);
	}
	else
	{
		Component = Cast<UPrimitiveComponent>(Target->GetRootComponent());
		if (!IsValid(Component) || !Component->IsSimulatingPhysics()) return false;

		GrabLocation = Component->GetCenterOfMass();
	}

	ApplyHoldProfile(HoldProfile);

	if (HoldProfile.bConstrainRotation)
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

	if (PhysicsHandle->GetGrabbedComponent() != Component)
	{
		ApplyHoldProfile(DefaultHoldProfile);
		ActiveHoldProfile = DefaultHoldProfile;
		return false;
	}

	HeldTargetOffset = HoldOffset;
	HeldGrabPointOffset = Component->GetComponentTransform().InverseTransformPositionNoScale(GrabLocation);
	ActiveHoldProfile = HoldProfile;
	HeldActor = Target;
	bHeldActorSettled = false;
	HeldCarrySeconds = 0.f;

	// 운반 중에는 위치를 복제하지 않고, 각 머신이 복제된 HeldActor를 HoldPoint로 따라가게 한다.
	Target->SetReplicateMovement(false);

	if (Target->Implements<UMD_Grabbable>())
	{
		IMD_Grabbable::Execute_OnCarryStarted(Target);
	}

	UpdateHeldTarget();
	return true;
}

bool UMD_InteractionComponent::IsHolding() const
{
	return IsValid(PhysicsHandle) && IsValid(PhysicsHandle->GetGrabbedComponent());
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

void UMD_InteractionComponent::ReleaseTarget()
{
	HeldTargetOffset = FVector::ZeroVector;
	HeldGrabPointOffset = FVector::ZeroVector;
	bHeldActorSettled = false;
	HeldCarrySeconds = 0.f;

	if (AActor* PreviousHeldActor = HeldActor)
	{
		HeldActor = nullptr;

		if (IsValid(PreviousHeldActor))
		{
			if (PreviousHeldActor->Implements<UMD_Grabbable>())
			{
				IMD_Grabbable::Execute_OnCarryEnded(PreviousHeldActor);
			}

			PreviousHeldActor->SetReplicateMovement(true);
			PreviousHeldActor->ForceNetUpdate();
		}
	}

	if (!IsValid(PhysicsHandle)) return;

	if (PhysicsHandle->GetGrabbedComponent())
	{
		PhysicsHandle->ReleaseComponent();
	}

	ApplyHoldProfile(DefaultHoldProfile);
	ActiveHoldProfile = DefaultHoldProfile;
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

void UMD_InteractionComponent::UpdateHeldTarget()
{
	const APawn* OwnerPawn = GetOwnerPawn();
	if (!IsValid(OwnerPawn) || !OwnerPawn->HasAuthority()
		|| !IsHolding() || !IsValid(HoldPoint)) return;

	const FTransform HoldTransform = GetHoldPointTransform();
	const FVector TargetLocation = HoldTransform.TransformPositionNoScale(HeldTargetOffset);

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
	const FVector TargetLocation =
		GetHoldPointTransform().TransformPositionNoScale(HeldTargetOffset);

	const bool bReachedTarget =
		FVector::DistSquared(CurrentGrabPoint, TargetLocation) <= FMath::Square(CarrySettleTolerance);
	const bool bTimedOut = HeldCarrySeconds >= CarrySettleTimeout;

	if (!bReachedTarget && !bTimedOut) return;

	bHeldActorSettled = true;
	IMD_Grabbable::Execute_OnCarrySettled(HeldActor);
}

void UMD_InteractionComponent::OnRep_HeldActor()
{
	StopFollowingHeldActor();
	StartFollowingHeldActor();
}

void UMD_InteractionComponent::StartFollowingHeldActor()
{
	const APawn* OwnerPawn = GetOwnerPawn();
	if (!IsValid(OwnerPawn) || OwnerPawn->HasAuthority() || !IsValid(HeldActor)) return;

	UPrimitiveComponent* Component = nullptr;
	FVector GrabLocation = FVector::ZeroVector;

	if (HeldActor->Implements<UMD_Grabbable>())
	{
		Component = IMD_Grabbable::Execute_GetGrabComponent(HeldActor);
		const USceneComponent* GrabPoint = IMD_Grabbable::Execute_GetGrabPoint(HeldActor);
		if (!IsValid(Component) || !IsValid(GrabPoint)) return;

		GrabLocation = GrabPoint->GetComponentLocation();
		HeldTargetOffset = IMD_Grabbable::Execute_GetHoldOffset(HeldActor);
		ActiveHoldProfile = IMD_Grabbable::Execute_GetHoldProfile(HeldActor);
	}
	else
	{
		Component = Cast<UPrimitiveComponent>(HeldActor->GetRootComponent());
		if (!IsValid(Component)) return;

		GrabLocation = Component->GetCenterOfMass();
		HeldTargetOffset = FVector::ZeroVector;
		ActiveHoldProfile = DefaultHoldProfile;
	}

	HeldGrabPointOffset = Component->GetComponentTransform().InverseTransformPositionNoScale(GrabLocation);
	Component->SetSimulatePhysics(false);
	FollowedComponent = Component;
}

void UMD_InteractionComponent::StopFollowingHeldActor()
{
	UPrimitiveComponent* Component = FollowedComponent.Get();

	FollowedComponent = nullptr;
	HeldTargetOffset = FVector::ZeroVector;
	HeldGrabPointOffset = FVector::ZeroVector;
	ActiveHoldProfile = DefaultHoldProfile;

	if (!IsValid(Component)) return;

	Component->SetSimulatePhysics(true);
}

void UMD_InteractionComponent::UpdateFollowedHeldActor(float DeltaSeconds)
{
	UPrimitiveComponent* Component = FollowedComponent.Get();
	if (!IsValid(Component) || !IsValid(HoldPoint)) return;

	const FTransform HoldTransform = GetHoldPointTransform();
	const FVector GrabTargetLocation = HoldTransform.TransformPositionNoScale(HeldTargetOffset);

	FQuat TargetRotation = ActiveHoldProfile.bConstrainRotation
		? HoldTransform.GetRotation()
		: Component->GetComponentQuat();
	FVector TargetLocation = GrabTargetLocation - TargetRotation.RotateVector(HeldGrabPointOffset);

	if (ActiveHoldProfile.RemoteFollowSpeed > 0.f)
	{
		const float Alpha = FMath::Clamp(DeltaSeconds * ActiveHoldProfile.RemoteFollowSpeed, 0.f, 1.f);
		TargetLocation = FMath::Lerp(Component->GetComponentLocation(), TargetLocation, Alpha);
		TargetRotation = FQuat::Slerp(Component->GetComponentQuat(), TargetRotation, Alpha).GetNormalized();
	}

	// Teleport가 아닌 이동이라야 키네마틱 바디가 속도를 갖고 통 안의 내용물도 함께 이동한다.
	Component->SetWorldLocationAndRotation(
		TargetLocation, TargetRotation, false, nullptr, ETeleportType::None);
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

	FHitResult Hit;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(OwnerPawn);

	if (!GetWorld()->LineTraceSingleByChannel(
		Hit,
		ViewLocation,
		TraceEnd,
		MDCollisionChannels::InteractTrace,
		QueryParams)) return nullptr;

	AActor* HitActor = Hit.GetActor();
	if (!IsValid(HitActor) || !HitActor->Implements<UMD_Interactable>()) return nullptr;
	if (IsHeldByOtherPawn(HitActor)) return nullptr;

	return HitActor;
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
