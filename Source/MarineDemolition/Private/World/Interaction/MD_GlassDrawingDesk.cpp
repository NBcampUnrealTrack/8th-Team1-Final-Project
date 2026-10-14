#include "World/Interaction/MD_GlassDrawingDesk.h"

#include "Camera/CameraComponent.h"
#include "Character/MD_Character.h"
#include "Component/Interaction/MD_GlassDrawingFocusComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/Drawing/MD_GlassDrawingProfile.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "GameplayEffect.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Net/UnrealNetwork.h"
#include "Subsystem/World/MD_GlassDrawingSubsystem.h"
#include "UObject/ConstructorHelpers.h"
#include "World/Wall/MD_CuttableWall.h"

AMD_GlassDrawingDesk::AMD_GlassDrawingDesk()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetNetUpdateFrequency(10.0f);

	DeskMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DeskMesh"));
	SetRootComponent(DeskMesh);
	DeskMesh->SetCollisionProfileName(TEXT("BlockAll"));
	DeskMesh->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);
	DeskMesh->SetSimulatePhysics(false);

	PenDisplayMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PenDisplayMesh"));
	PenDisplayMesh->SetupAttachment(DeskMesh);
	PenDisplayMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PenDisplayMesh->SetGenerateOverlapEvents(false);
	PenDisplayMesh->SetCanEverAffectNavigation(false);
	PenDisplayMesh->SetRelativeTransform(PenDisplayTransform);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> DefaultDesk(
		TEXT("/Game/Maps/TutorialV2/SM_Tutorial_ResearchDesk_Station.SM_Tutorial_ResearchDesk_Station"));
	if (DefaultDesk.Succeeded()) DeskMesh->SetStaticMesh(DefaultDesk.Object);
	static ConstructorHelpers::FObjectFinder<UMD_ToolDefinition> DefaultMarker(
		TEXT("/Game/Equipment/Tools/DA_Tool_GlassMarker.DA_Tool_GlassMarker"));
	if (DefaultMarker.Succeeded()) MarkerTool = DefaultMarker.Object;
	DisplayName = NSLOCTEXT("MDGlassDrawingDesk", "DefaultName", "그림 책상");
	RefreshPenDisplay();
}

void AMD_GlassDrawingDesk::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMD_GlassDrawingDesk, TargetGlassActor);
	DOREPLIFETIME(AMD_GlassDrawingDesk, TargetGlassComponent);
	DOREPLIFETIME(AMD_GlassDrawingDesk, Occupant);
}

void AMD_GlassDrawingDesk::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshPenDisplay();
}

void AMD_GlassDrawingDesk::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsValid(Occupant))
	{
		if (UMD_GlassDrawingFocusComponent* Focus = Occupant->GetGlassDrawingFocusComponent())
		{
			Focus->EndFocus(true);
		}
	}
	if (UMD_GlassDrawingSubsystem* Drawing = GetWorld() ? GetWorld()->GetSubsystem<UMD_GlassDrawingSubsystem>() : nullptr)
	{
		Drawing->ReleaseDeskSurface(this);
	}
	Occupant = nullptr;
	Super::EndPlay(EndPlayReason);
}

const UMD_GlassDrawingProfile* AMD_GlassDrawingDesk::GetDrawingProfile() const
{
	const UMD_GlassDrawingSubsystem* Drawing = GetWorld()
		? GetWorld()->GetSubsystem<UMD_GlassDrawingSubsystem>() : nullptr;
	return Drawing ? Drawing->GetProfile() : nullptr;
}

UStaticMeshComponent* AMD_GlassDrawingDesk::GetTargetGlassMesh() const
{
	if (!IsValid(TargetGlassActor) || TargetGlassActor->IsActorBeingDestroyed()
		|| TargetGlassActor->GetWorld() != GetWorld()) return nullptr;
	const UMD_GlassDrawingProfile* Profile = GetDrawingProfile();
	if (!IsValid(Profile) || !Profile->IsValidForDrawing()) return nullptr;
	const auto IsApproved = [this, Profile](const UStaticMeshComponent* Mesh)
	{
		return IsValid(Mesh) && Mesh->GetOwner() == TargetGlassActor
			&& Mesh->GetStaticMesh() == Profile->ApprovedMesh;
	};
	const bool bHasComponentSelector = !TargetGlassComponent.ComponentProperty.IsNone()
		|| !TargetGlassComponent.PathToComponent.IsEmpty()
		|| !TargetGlassComponent.OtherActor.IsExplicitlyNull()
		|| !TargetGlassComponent.OverrideComponent.IsExplicitlyNull();
	if (bHasComponentSelector)
	{
		// Stale selectors and references to a different actor must not silently choose another pane.
		if (!TargetGlassComponent.OtherActor.IsExplicitlyNull()
			&& TargetGlassComponent.OtherActor.Get() != TargetGlassActor) return nullptr;
		if (!TargetGlassComponent.OverrideComponent.IsExplicitlyNull()
			&& !TargetGlassComponent.OverrideComponent.IsValid()) return nullptr;
		UStaticMeshComponent* Selected = Cast<UStaticMeshComponent>(TargetGlassComponent.GetComponent(TargetGlassActor));
		return IsApproved(Selected) ? Selected : nullptr;
	}
	if (const AMD_CuttableWall* Wall = Cast<AMD_CuttableWall>(TargetGlassActor))
	{
		UStaticMeshComponent* Mesh = Wall->GetScanMesh();
		return IsApproved(Mesh) ? Mesh : nullptr;
	}
	TInlineComponentArray<UStaticMeshComponent*> Meshes(TargetGlassActor.Get());
	UStaticMeshComponent* Result = nullptr;
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		if (!IsApproved(Mesh)) continue;
		if (Result) return nullptr; // An actor with multiple eligible panes needs the explicit selector.
		Result = Mesh;
	}
	return Result;
}

bool AMD_GlassDrawingDesk::HasValidConfiguration() const
{
	const UStaticMeshComponent* Glass = GetTargetGlassMesh();
	if (!IsValid(Glass) || !Glass->IsRegistered() || !Glass->IsVisible()
		|| Glass->bHiddenInGame || TargetGlassActor->IsHidden()
		|| !IsValid(MarkerTool) || !IsValid(MarkerTool->ToolMesh)
		|| !MarkerTool->ToolAbilityTag.MatchesTagExact(MDGameplayTags::Ability_UtilityRepair_GlassMarker)
		|| !MarkerTool->GetAbilityGrantEffect()) return false;
	const FTransform& Transform = Glass->GetComponentTransform();
	const FVector Scale = Transform.GetScale3D().GetAbs();
	if (Transform.ContainsNaN() || Scale.X <= UE_SMALL_NUMBER || Scale.Y <= UE_SMALL_NUMBER || Scale.Z <= UE_SMALL_NUMBER)
		return false;
	return FMath::IsFinite(BlendSeconds) && BlendSeconds >= 0.0f && BlendSeconds <= 3.0f
		&& FMath::IsFinite(FramePadding) && FramePadding >= 1.0f && FramePadding <= 2.0f
		&& FMath::IsFinite(FocusFieldOfView) && FocusFieldOfView >= 40.0f && FocusFieldOfView <= 100.0f
		&& FMath::IsFinite(CursorSpeed) && CursorSpeed >= 0.0001f && CursorSpeed <= 0.05f
		&& FMath::IsFinite(CursorDepthCm) && CursorDepthCm >= 5.0f && CursorDepthCm <= 300.0f
		&& FMath::IsFinite(CursorScale) && CursorScale >= 0.1f && CursorScale <= 2.0f
		&& !CursorRotationOffset.ContainsNaN() && !CursorTipLocalOffset.ContainsNaN()
		&& !CursorEraserLocalOffset.ContainsNaN();
}

bool AMD_GlassDrawingDesk::ResolveFocusView(const AMD_Character* Character, float Aspect,
	FVector& OutLocation, FRotator& OutRotation, float& OutFOV, float& OutTraceDistance) const
{
	OutLocation = FVector::ZeroVector;
	OutRotation = FRotator::ZeroRotator;
	OutFOV = 0.0f;
	OutTraceDistance = 0.0f;
	if (!IsValid(Character) || Character->GetWorld() != GetWorld()
		|| !FMath::IsFinite(Aspect) || Aspect < 0.5f || Aspect > 4.0f
		|| !HasValidConfiguration()) return false;
	const UStaticMeshComponent* Glass = GetTargetGlassMesh();
	const UMD_GlassDrawingProfile* Profile = GetDrawingProfile();
	const FTransform& Transform = Glass->GetComponentTransform();
	const FVector Scale = Transform.GetScale3D();
	const FVector2D Size = Profile->LocalMax - Profile->LocalMin;
	const double HalfWidth = Size.X * FMath::Abs(Scale.X) * 0.5;
	const double HalfHeight = Size.Y * FMath::Abs(Scale.Z) * 0.5;
	if (!FMath::IsFinite(HalfWidth) || !FMath::IsFinite(HalfHeight)
		|| HalfWidth <= UE_SMALL_NUMBER || HalfHeight <= UE_SMALL_NUMBER
		|| HalfWidth > 1000.0 || HalfHeight > 1000.0) return false;
	const UCameraComponent* Camera = Character->FindComponentByClass<UCameraComponent>();
	const FVector Eye = IsValid(Camera) ? Camera->GetComponentLocation() : Character->GetPawnViewLocation();
	if (Eye.ContainsNaN()) return false;
	const FVector FrontCenter = Transform.TransformPosition(Profile->UVToLocalPoint(FVector2D(0.5, 0.5), Profile->FrontFaceY));
	const FVector BackCenter = Transform.TransformPosition(Profile->UVToLocalPoint(FVector2D(0.5, 0.5), Profile->BackFaceY));
	const bool bFront = FVector::DistSquared(Eye, FrontCenter) <= FVector::DistSquared(Eye, BackCenter);
	const FVector Center = bFront ? FrontCenter : BackCenter;
	// Inverse-transpose surface normal and signed +Z preserve the actual reflected pane basis.
	const FVector Normal = Transform.TransformVectorNoScale(FVector(0.0, (bFront ? 1.0 : -1.0) / Scale.Y, 0.0)).GetSafeNormal();
	const FVector Up = Transform.TransformVector(FVector::UpVector).GetSafeNormal();
	if (Normal.IsNearlyZero() || Up.IsNearlyZero()) return false;
	const double TanHalfHorizontalFOV = FMath::Tan(FMath::DegreesToRadians(double(FocusFieldOfView) * 0.5));
	const double Distance = FMath::Max(HalfWidth, HalfHeight * double(Aspect)) * double(FramePadding) / TanHalfHorizontalFOV;
	const FVector Location = Center + Normal * Distance;
	const FRotator Rotation = FRotationMatrix::MakeFromXZ(-Normal, Up).Rotator();
	if (Location.ContainsNaN() || Rotation.ContainsNaN()) return false;
	double TraceDistance = 0.0;
	for (const double FaceY : { double(Profile->FrontFaceY), double(Profile->BackFaceY) })
	{
		for (const double U : { 0.0, 1.0 })
		{
			for (const double V : { 0.0, 1.0 })
			{
				const FVector Corner = Transform.TransformPosition(Profile->UVToLocalPoint(FVector2D(U, V), FaceY));
				TraceDistance = FMath::Max(TraceDistance, FVector::Distance(Location, Corner));
			}
		}
	}
	TraceDistance += FMath::Max(10.0, double(Profile->SurfaceToleranceCm + Profile->SurfaceOffsetCm));
	if (!FMath::IsFinite(TraceDistance) || TraceDistance > 4000.0) return false;
	OutLocation = Location;
	OutRotation = Rotation;
	OutFOV = FocusFieldOfView;
	OutTraceDistance = static_cast<float>(TraceDistance);
	return true;
}

void AMD_GlassDrawingDesk::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
	if (!IsValid(DeskMesh)) return;
	DeskMesh->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue);
	DeskMesh->SetRenderCustomDepth(bHighlighted);
}

FText AMD_GlassDrawingDesk::GetInteractionDisplayName_Implementation() const
{
	return DisplayName;
}

FText AMD_GlassDrawingDesk::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	if (!HasValidConfiguration()) return NSLOCTEXT("MDGlassDrawingDesk", "Unassigned", "연결된 유리 없음");
	if (IsValid(Occupant) && Occupant != Interactor)
		return NSLOCTEXT("MDGlassDrawingDesk", "Busy", "다른 대원이 사용 중");
	return Occupant == Interactor
		? NSLOCTEXT("MDGlassDrawingDesk", "Leave", "[E] 그림 책상에서 나가기")
		: NSLOCTEXT("MDGlassDrawingDesk", "Enter", "[E] 그림 책상 사용");
}

bool AMD_GlassDrawingDesk::CanInteract_Implementation(AActor* Interactor) const
{
	const AMD_Character* Character = Cast<AMD_Character>(Interactor);
	if (!IsValid(Character) || !HasValidConfiguration() || (IsValid(Occupant) && Occupant != Character)) return false;
	const UMD_GlassDrawingSubsystem* Drawing = GetWorld()->GetSubsystem<UMD_GlassDrawingSubsystem>();
	const AMD_Character* SurfaceOccupant = Drawing ? Drawing->GetSurfaceOccupant(GetTargetGlassMesh()) : nullptr;
	if (IsValid(SurfaceOccupant) && SurfaceOccupant != Character) return false;
	const UMD_GlassDrawingFocusComponent* Focus = Character->GetGlassDrawingFocusComponent();
	return IsValid(Focus) && Focus->CanBeginFocus(this);
}

void AMD_GlassDrawingDesk::Interact_Implementation(AActor* Interactor)
{
	AMD_Character* Character = Cast<AMD_Character>(Interactor);
	if (!HasAuthority() || !CanInteract_Implementation(Interactor) || IsValid(Occupant)) return;
	UMD_GlassDrawingSubsystem* Drawing = GetWorld()->GetSubsystem<UMD_GlassDrawingSubsystem>();
	if (!Drawing || !Drawing->TryReserveDeskSurface(GetTargetGlassMesh(), this)) return;
	UMD_GlassDrawingFocusComponent* Focus = Character->GetGlassDrawingFocusComponent();
	// Server interaction calls are serialized. Reserve before any focus/ability callbacks execute.
	Occupant = Character;
	OnRep_Occupant();
	ForceNetUpdate();
	if (!Focus->BeginFocus(this)) ReleaseOccupant(Character);
}

void AMD_GlassDrawingDesk::ReleaseOccupant(AMD_Character* Character)
{
	if (!HasAuthority() || Occupant != Character) return;
	if (UMD_GlassDrawingSubsystem* Drawing = GetWorld() ? GetWorld()->GetSubsystem<UMD_GlassDrawingSubsystem>() : nullptr)
	{
		Drawing->ReleaseDeskSurface(this);
	}
	Occupant = nullptr;
	OnRep_Occupant();
	ForceNetUpdate();
}

void AMD_GlassDrawingDesk::RefreshPenDisplay()
{
	if (!IsValid(PenDisplayMesh)) return;
	PenDisplayMesh->SetStaticMesh(IsValid(MarkerTool) ? MarkerTool->ToolMesh.Get() : nullptr);
	if (!PenDisplayTransform.ContainsNaN()) PenDisplayMesh->SetRelativeTransform(PenDisplayTransform);
	PenDisplayMesh->SetVisibility(!IsValid(Occupant) && IsValid(PenDisplayMesh->GetStaticMesh()));
}

void AMD_GlassDrawingDesk::OnRep_Occupant()
{
	RefreshPenDisplay();
}
