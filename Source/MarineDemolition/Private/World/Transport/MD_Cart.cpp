#include "World/Transport/MD_Cart.h"
#include "Component/Interaction/MD_InteractionPromptComponent.h"

#include "Component/Interaction/MD_InteractionComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Gameplay/Interaction/MD_InteractionPresentationLibrary.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Net/UnrealNetwork.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

AMD_Cart::AMD_Cart()
{
	// 인스턴스와 파생 BP가 행동 유형을 선택하는 기본 구성 생성
	InteractionPrompt = CreateDefaultSubobject<UMD_InteractionPromptComponent>(TEXT("InteractionPrompt"));
	// 기존 상호작용 동작에 대응하는 공통 문구 유형 지정
	InteractionPrompt->PromptType = EMD_InteractionPromptType::PushCart;

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.TickInterval = 1.f / 30.f;
	bReplicates = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(30.f);
	SetMinNetUpdateFrequency(10.f);

	CartBody = CreateDefaultSubobject<UBoxComponent>(TEXT("CartBody"));
	SetRootComponent(CartBody);
	CartBody->SetMobility(EComponentMobility::Movable);
	CartBody->SetCollisionProfileName(TEXT("PhysicsActor"));
	CartBody->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);
	CartBody->CanCharacterStepUpOn = ECB_No;
	CartBody->SetWalkableSlopeOverride(FWalkableSlopeOverride(WalkableSlope_Unwalkable, 0.f));
	CartBody->SetLinearDamping(1.f);
	CartBody->SetAngularDamping(2.f);
	CartBody->SetUseCCD(true);
	CartBody->SetIsReplicated(true);

	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
	WheelVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WheelVisual"));
	for (UStaticMeshComponent* Mesh : { Visual.Get(), WheelVisual.Get() })
	{
		Mesh->SetupAttachment(CartBody);
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	for (int32 Index = 0; Index < 4; ++Index)
	{
		UBoxComponent* Wall = CreateDefaultSubobject<UBoxComponent>(*FString::Printf(TEXT("Wall%d"), Index));
		Wall->SetupAttachment(CartBody);
		Wall->SetMobility(EComponentMobility::Movable);
		Wall->SetCollisionProfileName(TEXT("PhysicsActor"));
		Wall->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);
		Wall->CanCharacterStepUpOn = ECB_No;
		Wall->SetWalkableSlopeOverride(FWalkableSlopeOverride(WalkableSlope_Unwalkable, 0.f));
		Wall->BodyInstance.bAutoWeld = true;
		Walls.Add(Wall);
	}
	Handle = CreateDefaultSubobject<UBoxComponent>(TEXT("Handle"));
	Handle->SetupAttachment(CartBody);
	Handle->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Handle->SetCollisionResponseToAllChannels(ECR_Ignore);
	Handle->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);
	Handle->SetGenerateOverlapEvents(false);
	CargoSensor = CreateDefaultSubobject<UBoxComponent>(TEXT("CargoSensor"));
	CargoSensor->SetupAttachment(CartBody);
	CargoSensor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CargoSensor->SetCollisionResponseToAllChannels(ECR_Ignore);
	CargoSensor->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
	CargoSensor->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	CargoSensor->SetGenerateOverlapEvents(true);

	SteeringProfile.LinearStiffness = 2400.f;
	SteeringProfile.LinearDamping = 800.f;
	SteeringProfile.AngularStiffness = 1600.f;
	SteeringProfile.AngularDamping = 650.f;
	SteeringProfile.InterpolationSpeed = 20.f;
	ApplyGeometry();
}

void AMD_Cart::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyGeometry();
}

void AMD_Cart::ApplyGeometry()
{
	if (HasActorBegunPlay()) for (UBoxComponent* Wall : Walls) Wall->UnWeldFromParent();
	const float X = TrayHalfExtent.X;
	const float Y = TrayHalfExtent.Y;
	const float Z = TrayHalfExtent.Z;
	CartBody->SetBoxExtent(FVector(X, Y, FloorHalfThickness));
	for (int32 Index = 0; Index < Walls.Num(); ++Index)
	{
		const bool bSideX = Index < 2;
		Walls[Index]->SetBoxExtent(bSideX ? FVector(WallHalfThickness, Y, Z) : FVector(X, WallHalfThickness, Z));
		Walls[Index]->SetRelativeLocation(FVector(
			bSideX ? (Index == 0 ? -X : X) : 0.f,
			bSideX ? 0.f : (Index == 2 ? -Y : Y), FloorHalfThickness + Z));
	}
	const FVector Outward = HandleDirection.GetSafeNormal2D(UE_SMALL_NUMBER, FVector(0.f, 1.f, 0.f));
	const bool bHandleOnY = FMath::Abs(Outward.Y) >= FMath::Abs(Outward.X);
	Handle->SetBoxExtent(bHandleOnY ? FVector(FMath::Max(16.f, X * .6f), 12.f, 13.f)
		: FVector(12.f, FMath::Max(16.f, Y * .6f), 13.f));
	Handle->SetRelativeLocation(FVector(Outward.X * (X + 9.f), Outward.Y * (Y + 9.f), FloorHalfThickness + Z * 2.f));
	CargoSensor->SetBoxExtent(FVector(X, Y, Z));
	CargoSensor->SetRelativeLocation(FVector(0.f, 0.f, FloorHalfThickness + Z));
	if (HasActorBegunPlay() && CartBody->IsSimulatingPhysics())
		for (UBoxComponent* Wall : Walls) Wall->WeldTo(CartBody);
}

void AMD_Cart::OnRep_Geometry()
{
	ApplyGeometry();
}

void AMD_Cart::BeginPlay()
{
	Super::BeginPlay();
	ApplyGeometry();
	CartBody->SetSimulatePhysics(true);
	for (UBoxComponent* Wall : Walls) Wall->WeldTo(CartBody);
	CartBody->SetMassOverrideInKg(NAME_None, CartMass);
}

void AMD_Cart::EndPlay(const EEndPlayReason::Type Reason)
{
	if (APawn* Pawn = GripPawn.Get())
	{
		if (UMD_InteractionComponent* Interaction = Pawn->FindComponentByClass<UMD_InteractionComponent>();
			Interaction && Interaction->GetHeldActor() == this) Interaction->ReleaseHeldActor();
	}
	OnCarryEnded_Implementation();
	Super::EndPlay(Reason);
}

void AMD_Cart::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AMD_Cart, TrayHalfExtent, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AMD_Cart, FloorHalfThickness, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AMD_Cart, WallHalfThickness, COND_InitialOnly);
	DOREPLIFETIME(AMD_Cart, bSteering);
	DOREPLIFETIME_CONDITION(AMD_Cart, HandleDirection, COND_InitialOnly);
}

void AMD_Cart::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
	TagContainer.Reset();
	TagContainer.AddTag(MDGameplayTags::Interaction_Pickup_Physics);
}

void AMD_Cart::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
	for (UStaticMeshComponent* Mesh : { Visual.Get(), WheelVisual.Get() })
	{
		Mesh->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue);
		Mesh->SetRenderCustomDepth(bHighlighted);
	}
}

FText AMD_Cart::GetInteractionDisplayName_Implementation() const
{
	FText DisplayName;
	UMD_InteractionPresentationLibrary::FindDisplayName(TEXT("Cart"), DisplayName);
	return DisplayName;
}

FText AMD_Cart::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	// 선택한 행동 유형의 설정 문구와 현재 입력 키 표시
	return UMD_InteractionPresentationLibrary::FindActorActionPrompt(this, Interactor);
}

bool AMD_Cart::IsPawnInsideTray(const APawn* Pawn) const
{
	if (!IsValid(Pawn)) return false;
	FVector Origin, Extent;
	Pawn->GetActorBounds(true, Origin, Extent);
	const FVector Local = GetActorTransform().InverseTransformPosition(Origin);
	const FVector Up = GetActorUpVector().GetAbs();
	const float Feet = Local.Z - FVector::DotProduct(Up, Extent) / FMath::Max(FMath::Abs(GetActorScale3D().Z), UE_SMALL_NUMBER);
	return FMath::Abs(Local.X) < TrayHalfExtent.X && FMath::Abs(Local.Y) < TrayHalfExtent.Y
		&& Feet > FloorHalfThickness - 25.f && Feet < FloorHalfThickness + TrayHalfExtent.Z * 2.f
		&& Local.Z > -FloorHalfThickness;
}

bool AMD_Cart::CanInteract_Implementation(AActor* Interactor) const
{
	const APawn* Pawn = Cast<APawn>(Interactor);
	return IsValid(Pawn) && (!GripPawn.IsValid() || GripPawn.Get() == Pawn)
		&& !IsPawnInsideTray(Pawn) && IsAimingAtHandle(Pawn);
}

bool AMD_Cart::IsAimingAtHandle(const APawn* Pawn) const
{
	if (!IsValid(Pawn) || !IsValid(Pawn->GetController()) || !GetWorld()) return false;
	FVector Origin;
	FRotator Aim;
	Pawn->GetController()->GetPlayerViewPoint(Origin, Aim);
	const FVector Up = GetActorUpVector();
	const FVector Outward = GetActorTransform().TransformVector(
		HandleDirection.GetSafeNormal2D(UE_SMALL_NUMBER, FVector(0.f, 1.f, 0.f))).GetSafeNormal();
	const double MinimumCosine = FMath::Cos(FMath::DegreesToRadians(GripApproachHalfAngle));
	auto IsInFront = [&](const FVector& Position)
	{
		const FVector Approach = FVector::VectorPlaneProject(Position - Handle->GetComponentLocation(), Up);
		return FVector::DotProduct(Approach, Outward) > 1.f
			&& FVector::DotProduct(Approach.GetSafeNormal(), Outward) >= MinimumCosine;
	};
	if (!IsInFront(Pawn->GetActorLocation()) || !IsInFront(Origin)) return false;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MDCartGrip), false, Pawn);
	return GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + Aim.Vector() * 200.f,
		MDCollisionChannels::InteractTrace, Params) && Hit.GetActor() == this && Hit.GetComponent() == Handle;
}

bool AMD_Cart::PrepareGrip(APawn* Pawn)
{
	if (!HasAuthority() || !CanInteract_Implementation(Pawn)) return false;
	const UMD_InteractionComponent* Interaction = Pawn->FindComponentByClass<UMD_InteractionComponent>();
	if (!Interaction || !Interaction->CanOwnerInteract() || Interaction->IsHolding()) return false;
	bSteering = true;
	GripPawn = Pawn;
	return true;
}

bool AMD_Cart::PreparePhysicsGrip_Implementation(APawn* Pawn)
{
	return PrepareGrip(Pawn);
}

EMD_PhysicsHoldTargetResult AMD_Cart::ResolvePhysicsHoldTarget_Implementation(APawn* Pawn, FTransform& OutTarget) const
{
	return ResolveSteeringTarget(Pawn, OutTarget) ? EMD_PhysicsHoldTargetResult::Resolved : EMD_PhysicsHoldTargetResult::Release;
}

UPrimitiveComponent* AMD_Cart::GetGrabComponent_Implementation() const { return CartBody; }
USceneComponent* AMD_Cart::GetGrabPoint_Implementation() const
{
	return Handle;
}
FMD_HoldProfile AMD_Cart::GetHoldProfile_Implementation() const
{
	return SteeringProfile;
}

bool AMD_Cart::ResolveSteeringTarget(APawn* Pawn, FTransform& OutTarget) const
{
	if (!bSteering || !IsValid(Pawn) || GripPawn.Get() != Pawn || IsPawnInsideTray(Pawn)
		|| FVector::DistSquared(Pawn->GetActorLocation(), Handle->GetComponentLocation()) > FMath::Square(TetherDistance)) return false;
	const FVector Forward = FRotator(0.f, Pawn->GetBaseAimRotation().Yaw, 0.f).Vector();
	const FVector Desired = Pawn->GetActorLocation() + Forward * HandleDistance;
	const FVector ActorScale = GetActorScale3D();
	const float WorldFloorHalfThickness = FloorHalfThickness * FMath::Abs(ActorScale.Z);
	const FVector Inward = -(HandleDirection * ActorScale).GetSafeNormal2D(UE_SMALL_NUMBER, FVector(0.f, 1.f, 0.f));
	const FQuat YawOffset(FVector::UpVector, FMath::DegreesToRadians(-Inward.Rotation().Yaw));
	const FVector HandleOffset = Handle->GetRelativeLocation() * ActorScale;
	const FVector Center = CartBody->GetComponentLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MDCartFloor), false, this);
	Params.AddIgnoredActor(Pawn);
	for (const auto& Entry : Cargo)
	{
		if (const UPrimitiveComponent* Primitive = Entry.Key.Get()) Params.AddIgnoredActor(Primitive->GetOwner());
	}
	// Find support beneath the actual floor, not a distant obstacle's top beneath the goal.
	const FVector Foot = Center - GetActorUpVector() * WorldFloorHalfThickness;
	const FVector Probe = Foot + FVector(0, 0, 2);
	TArray<FHitResult> Hits;
	GetWorld()->LineTraceMultiByObjectType(Hits, Probe, Probe - FVector(0, 0, 180),
		FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects), Params);
	const FHitResult* Support = nullptr;
	for (const FHitResult& Hit : Hits)
	{
		const UPrimitiveComponent* Other = Hit.GetComponent();
		if (Hit.bStartPenetrating || !IsValid(Other) || !Other->IsPhysicsCollisionEnabled()
			|| CartBody->GetCollisionResponseToChannel(Other->GetCollisionObjectType()) != ECR_Block
			|| Other->GetCollisionResponseToChannel(CartBody->GetCollisionObjectType()) != ECR_Block) continue;
		if (!Support || Hit.Distance < Support->Distance) Support = &Hit;
	}
	FVector Up = GetActorUpVector();
	double CenterZ = Center.Z;
	bool bSupported = false;
	if (Support && Support->ImpactNormal.Z >= .55f)
	{
		const FVector Normal = Support->ImpactNormal;
		const double SupportedZ = Support->ImpactPoint.Z + (WorldFloorHalfThickness
			- Normal.X * (Center.X - Support->ImpactPoint.X) - Normal.Y * (Center.Y - Support->ImpactPoint.Y)) / Normal.Z;
		// A missing or inconsistent support estimate keeps the grip and current height.
		if (FMath::Abs(SupportedZ - Center.Z) <= MaximumStepHeight)
		{
			Up = Normal;
			CenterZ = SupportedZ;
			bSupported = true;
		}
	}
	const FQuat Rotation = FRotationMatrix::MakeFromZX(Up, Forward).ToQuat() * YawOffset;
	FVector TargetHandle(Desired.X, Desired.Y, Handle->GetComponentLocation().Z);
	if (bSupported) TargetHandle.Z = CenterZ + Rotation.RotateVector(HandleOffset).Z;
	OutTarget = FTransform(Rotation, TargetHandle);
	LimitSteeringTarget(OutTarget);
	return true;
}

void AMD_Cart::LimitSteeringTarget(FTransform& Target) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_Cart_Target);
	const FVector Point = Handle->GetComponentLocation();
	const FVector Error = Target.GetLocation() - Point;
	const double PlanarDistance = Error.Size2D();
	const double PlanarLimit = FMath::Max(1.f, MaximumSteeringStretch);
	const double VerticalLimit = FMath::Max(.1f, MaximumVerticalStretch);
	FVector Stretch(Error.X, Error.Y, 0);
	// Continuous saturation retains contact pressure and tangential motion. Chaos resolves
	// the actual contact; no broad obstruction query switches steering completely off.
	if (PlanarDistance > UE_SMALL_NUMBER)
		Stretch *= PlanarLimit * FMath::Tanh(PlanarDistance / PlanarLimit) / PlanarDistance;
	Stretch.Z = VerticalLimit * FMath::Tanh(Error.Z / VerticalLimit);
	Target.SetLocation(Point + Stretch);
	// Preserve the requested orientation. The existing handle interpolates the shortest
	// quaternion path, including a small input change across the opposite heading.
}

void AMD_Cart::OnCarryStarted_Implementation()
{
	if (!HasAuthority() || !GripPawn.IsValid()) return;
	CargoSensor->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CargoRefreshSeconds = 0.f;
	RefreshCargo();
	SetActorTickEnabled(true);
	ForceNetUpdate();
}

void AMD_Cart::OnCarryEnded_Implementation()
{
	if (!HasAuthority()) return;
	GripPawn.Reset();
	bSteering = false;
	Cargo.Reset();
	CargoSensor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetActorTickEnabled(false);
	ForceNetUpdate();
}

void AMD_Cart::RefreshCargo()
{
	TArray<UPrimitiveComponent*, TInlineAllocator<64>> Candidates;
	int32 Checked = 0;
	for (const FOverlapInfo& Overlap : CargoSensor->GetOverlapInfos())
	{
		if (++Checked > 128 || Candidates.Num() >= 64) break;
		UPrimitiveComponent* Primitive = Overlap.OverlapInfo.GetComponent();
		if (IsValid(Primitive) && Primitive->GetOwner() != this && Primitive->IsSimulatingPhysics()) Candidates.AddUnique(Primitive);
	}
	for (auto It = Cargo.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || !Candidates.Contains(It.Key().Get())) It.RemoveCurrent();
	}
	for (UPrimitiveComponent* Primitive : Candidates) Cargo.FindOrAdd(Primitive, 0.f);
}

void AMD_Cart::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_Cart_Cargo);
	if (!HasAuthority()) return;
	APawn* Pawn = GripPawn.Get();
	UMD_InteractionComponent* Interaction = IsValid(Pawn) ? Pawn->FindComponentByClass<UMD_InteractionComponent>() : nullptr;
	if (!Interaction || !Interaction->CanOwnerInteract() || Interaction->GetHeldActor() != this
		|| IsPawnInsideTray(Pawn) || FVector::DistSquared(Pawn->GetActorLocation(), Handle->GetComponentLocation()) > FMath::Square(TetherDistance))
	{
		if (Interaction && Interaction->GetHeldActor() == this) Interaction->ReleaseHeldActor();
		else OnCarryEnded_Implementation();
		return;
	}
	CargoRefreshSeconds += DeltaSeconds;
	if (CargoRefreshSeconds >= .1f) { CargoRefreshSeconds = 0.f; RefreshCargo(); }
	TArray<AActor*, TInlineAllocator<8>> Held;
	if (const AGameStateBase* State = GetWorld()->GetGameState())
	{
		for (const APlayerState* Player : State->PlayerArray)
		{
			if (Held.Num() >= 64) break;
			const APawn* Other = IsValid(Player) ? Player->GetPawn() : nullptr;
			const UMD_InteractionComponent* OtherInteraction = Other ? Other->FindComponentByClass<UMD_InteractionComponent>() : nullptr;
			if (OtherInteraction && OtherInteraction->GetHeldActor()) Held.Add(OtherInteraction->GetHeldActor());
		}
	}
	const FVector Up = GetActorUpVector();
	const FTransform Tray = GetActorTransform();
	const float VerticalScale = FMath::Max(FMath::Abs(Tray.GetScale3D().Z), UE_SMALL_NUMBER);
	for (auto& Entry : Cargo)
	{
		UPrimitiveComponent* Primitive = Entry.Key.Get();
		if (!IsValid(Primitive) || !Primitive->IsSimulatingPhysics() || Held.Contains(Primitive->GetOwner())) { Entry.Value = 0.f; continue; }
		const FVector Local = Tray.InverseTransformPosition(Primitive->Bounds.Origin);
		const float Feet = Local.Z - FVector::DotProduct(Up.GetAbs(), Primitive->Bounds.BoxExtent) / VerticalScale;
		const FVector RelativeVelocity = Primitive->GetPhysicsLinearVelocity()
			- CartBody->GetPhysicsLinearVelocityAtPoint(Primitive->Bounds.Origin);
		const bool bSettled = Up.Z >= .55f && FMath::Abs(Local.X) < TrayHalfExtent.X - WallHalfThickness
			&& FMath::Abs(Local.Y) < TrayHalfExtent.Y - WallHalfThickness && Local.Z < FloorHalfThickness + TrayHalfExtent.Z * 2.f
			&& FMath::Abs(Feet - FloorHalfThickness) <= 12.f && RelativeVelocity.SizeSquared() <= FMath::Square(SettledRelativeSpeed);
		if (!bSettled) { Entry.Value = 0.f; continue; }
		Entry.Value += DeltaSeconds;
		if (Entry.Value < .15f) continue;
		// Only slow tangential slip after natural floor contact. Gravity and spilling remain physical.
		const FVector Acceleration = FVector::VectorPlaneProject(-RelativeVelocity * CargoDamping, Up).GetClampedToMaxSize(MaxCargoAcceleration);
		Primitive->AddForce(Acceleration, NAME_None, true);
	}
}
