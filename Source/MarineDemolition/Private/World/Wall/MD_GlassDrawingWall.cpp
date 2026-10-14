#include "World/Wall/MD_GlassDrawingWall.h"

#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/Drawing/MD_GlassDrawingProfile.h"
#include "Character/MD_Character.h"
#include "Component/Interaction/MD_InteractionComponent.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Subsystem/World/MD_GlassDrawingSubsystem.h"
#include "UObject/ConstructorHelpers.h"

AMD_GlassDrawingWall::AMD_GlassDrawingWall()
{
	static ConstructorHelpers::FObjectFinder<UMD_GlassDrawingProfile> DefaultProfile(
		TEXT("/Game/Data/Drawing/DA_GlassDrawingProfile.DA_GlassDrawingProfile"));
	DeskMesh->SetStaticMesh(DefaultProfile.Succeeded() ? DefaultProfile.Object->ApprovedMesh.Get() : nullptr);
	DisplayName = NSLOCTEXT("MDGlassDrawingWall", "DefaultName", "그림용 유리벽");
	ConfigureOwnGlass();
	RefreshPenDisplay();
}

void AMD_GlassDrawingWall::ConfigureOwnGlass()
{
	// Do not inherit a CDO/self reference or allow another pane to replace this wall's target.
	TargetGlassActor = this;
	TargetGlassComponent = FComponentReference();
	TargetGlassComponent.ComponentProperty = GET_MEMBER_NAME_CHECKED(AMD_GlassDrawingDesk, DeskMesh);
}

void AMD_GlassDrawingWall::OnConstruction(const FTransform& Transform)
{
	ConfigureOwnGlass();
	Super::OnConstruction(Transform);
}

void AMD_GlassDrawingWall::BeginPlay()
{
	ConfigureOwnGlass();
	Super::BeginPlay();
	RefreshPenDisplay();
}

void AMD_GlassDrawingWall::RefreshPenDisplay()
{
	if (!IsValid(PenDisplayMesh)) return;
	PenDisplayMesh->SetStaticMesh(nullptr);
	PenDisplayMesh->SetVisibility(false);
	PenDisplayMesh->SetHiddenInGame(true);
}

bool AMD_GlassDrawingWall::IsViewingFrontGlass(const AMD_Character* Character) const
{
	const UStaticMeshComponent* Glass = GetTargetGlassMesh();
	const UMD_GlassDrawingSubsystem* Drawing = GetWorld()
		? GetWorld()->GetSubsystem<UMD_GlassDrawingSubsystem>() : nullptr;
	const UMD_GlassDrawingProfile* Profile = Drawing ? Drawing->GetProfile() : nullptr;
	const UMD_InteractionComponent* Interaction = Character ? Character->GetInteractionComponent() : nullptr;
	const AController* Controller = Character ? Character->GetController() : nullptr;
	if (!IsValid(Glass) || !IsValid(Profile) || !IsValid(Interaction) || !IsValid(Controller)) return false;

	FVector Eye;
	FRotator View;
	if (Character->HasAuthority() && !Controller->IsLocalController())
	{
		// A stationary remote player's server camera cache may still contain the old
		// look direction. Validate using the authoritative pawn eye and control rotation.
		const UCameraComponent* Camera = Character->FindComponentByClass<UCameraComponent>();
		Eye = IsValid(Camera) ? Camera->GetComponentLocation() : Character->GetPawnViewLocation();
		View = Controller->GetControlRotation();
	}
	else
	{
		Controller->GetPlayerViewPoint(Eye, View);
	}
	const FTransform& Transform = Glass->GetComponentTransform();
	const FVector Scale = Transform.GetScale3D();
	if (Eye.ContainsNaN() || View.ContainsNaN() || Transform.ContainsNaN()
		|| FMath::Abs(Scale.Y) <= UE_SMALL_NUMBER) return false;
	const FVector LocalEye = Transform.InverseTransformPosition(Eye);
	if (LocalEye.ContainsNaN() || (LocalEye.Y - Profile->FrontFaceY) * FMath::Abs(Scale.Y)
		<= Profile->SurfaceToleranceCm) return false;

	// Use the same view and range on the owner and server. The booth's frame and sides
	// must not qualify just because the player happens to stand in the front half-space.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GlassDrawingFrontInteraction), true);
	Params.AddIgnoredActor(Character);
	Params.bReturnFaceIndex = true;
	FHitResult Hit;
	const float Reach = Interaction->GetInteractTraceDistance();
	if (!FMath::IsFinite(Reach) || Reach <= 0.0f || !GetWorld()->LineTraceSingleByChannel(Hit,
		Eye, Eye + View.Vector() * Reach, MDCollisionChannels::InteractTrace, Params)
		|| Hit.GetComponent() != Glass) return false;
	FVector2D UV;
	if (!Profile->ResolveHit(Hit, UV)) return false;
	const FVector LocalNormal = (Transform.InverseTransformVectorNoScale(Hit.ImpactNormal) * Scale).GetSafeNormal();
	return LocalNormal.Y >= FMath::Cos(FMath::DegreesToRadians(double(Profile->MaxSurfaceAngleDegrees)));
}

bool AMD_GlassDrawingWall::CanInteract_Implementation(AActor* Interactor) const
{
	return Super::CanInteract_Implementation(Interactor) && IsViewingFrontGlass(Cast<AMD_Character>(Interactor));
}

bool AMD_GlassDrawingWall::ResolveFocusView(const AMD_Character* Character, float Aspect,
	FVector& OutLocation, FRotator& OutRotation, float& OutFOV, float& OutTraceDistance) const
{
	OutLocation = FVector::ZeroVector;
	OutRotation = FRotator::ZeroRotator;
	OutFOV = 0.0f;
	OutTraceDistance = 0.0f;
	// Recheck the front before either server camera configuration; direct session calls
	// cannot fall back to the base desk's nearest-face selection from behind the booth.
	return IsViewingFrontGlass(Character)
		&& Super::ResolveFocusView(Character, Aspect, OutLocation, OutRotation, OutFOV, OutTraceDistance);
}

FText AMD_GlassDrawingWall::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	if (!HasValidConfiguration())
		return NSLOCTEXT("MDGlassDrawingWall", "Unconfigured", "유리 그리기 설정 없음");
	if (IsValid(GetOccupant()) && GetOccupant() != Interactor)
		return NSLOCTEXT("MDGlassDrawingWall", "Busy", "다른 대원이 사용 중");
	return GetOccupant() == Interactor
		? NSLOCTEXT("MDGlassDrawingWall", "Leave", "[E] 유리 그리기에서 나가기")
		: NSLOCTEXT("MDGlassDrawingWall", "Enter", "[E] 유리에 그리기");
}
