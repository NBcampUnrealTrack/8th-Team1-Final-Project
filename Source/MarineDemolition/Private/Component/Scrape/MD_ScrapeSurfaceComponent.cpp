#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"

#include "Components/DecalComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "Component/Scrape/MD_ScrapeSurfaceComponentInternal.h"

DEFINE_STAT(STAT_MD_ScrapeStrokeTotal);
DEFINE_STAT(STAT_MD_ScrapeEvaluate);
DEFINE_STAT(STAT_MD_ScrapeCleanRatio);
DEFINE_STAT(STAT_MD_ScrapeMaskUpdate);
DEFINE_STAT(STAT_MD_ScrapeOverlayUpdate);
DEFINE_STAT(STAT_MD_ScrapePublish);
DEFINE_STAT(STAT_MD_ScrapeFullyRemovedCheck);

namespace MD_ScrapeSurfaceComponentInternal
{
	const FName ContaminationMaskTextureParameterName(TEXT("MaskTexture"));
	const FName ContaminationMaskVScaleParameterName(TEXT("MaskVScale"));
	const FName ContaminationMaskVOffsetParameterName(TEXT("MaskVOffset"));
	const FName ContaminationMaskUFlipParameterName(TEXT("MaskUFlip"));
	const FName ContaminationMaskHalfTexelUParameterName(TEXT("MaskHalfTexelU"));
	const FName ContaminationMaskHalfTexelVParameterName(TEXT("MaskHalfTexelV"));
}

UMD_ScrapeSurfaceComponent::UMD_ScrapeSurfaceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);

	ContaminationGridDimensions = FIntPoint(30, 25);
	InitialContaminationPerCell = 1.0f;
	bUseProceduralContamination = false;
	ContaminationNoiseScaleCm = 75.0f;
	ContaminationNoiseSeed = 1337;
	ContaminationCoverage = 0.55f;
	ContaminationMinimumAmount = 0.25f;
	ContaminationSurfaceSizeOverrideCm = FVector2D::ZeroVector;
	ContaminationSurfaceNormalAxis = EMD_ScrapeSurfaceNormalAxis::Auto;
	ContaminationOverlayOffsetCm = 0.25f;
	ContaminationOverlayThicknessCm = 0.35f;
	bShowContaminationOverlay = true;
	ContaminationOverlayMesh = nullptr;
	ContaminationOverlayMaterial = nullptr;
	ContaminationDecalMaterial = nullptr;
	bShowContaminationDecals = true;
	ContaminationDecalProjectionDepthCm = 1.0f;
	bAutoFitContaminationDecalDepth = true;
	ContaminationDecalDepthFitMarginCm = 1.0f;
	ContaminationDecalSurfaceOffsetCm = 0.05f;
	ContaminationDecalEdgeInsetCm = 2.0f;
	ContaminationDecalSortOrder = 0;

	ResolvedContaminationSurfaceSizeCm = FVector2D::ZeroVector;
	ResolvedContaminationSurfaceNormalAxis = EMD_ScrapeSurfaceNormalAxis::Y;
	ResolvedFrontDecalDepthCm = 0.0f;
	ResolvedBackDecalDepthCm = 0.0f;
	ResolvedContaminationDecalEdgeInsetCm = 0.0f;
	ContaminationSurfaceCenterLocal = FVector::ZeroVector;
	ContaminationMeshLocalBoundsMinimum = FVector::ZeroVector;
	ContaminationMeshLocalBoundsMaximum = FVector::ZeroVector;
	CachedFrontContaminationCleanRatio = 0.0f;
	CachedBackContaminationCleanRatio = 0.0f;
	ContaminationMaskAtlasDimensions = FIntPoint::ZeroValue;
	bContaminationDecalsConfigured = false;
	bContaminationSurfaceInitialized = false;
	bContaminationCompletionBroadcast = false;
	ReplicatedContaminationState.SetOwner(this);
}

UMD_ScrapeSurfaceComponent*
UMD_ScrapeSurfaceComponent::FindForPrimitiveComponent(
	const UPrimitiveComponent* const PrimitiveComponent)
{
	AActor* const OwnerActor = PrimitiveComponent
		? PrimitiveComponent->GetOwner()
		: nullptr;
	if (!IsValid(OwnerActor))
	{
		return nullptr;
	}

	TInlineComponentArray<UMD_ScrapeSurfaceComponent*> Surfaces(OwnerActor);
	for (UMD_ScrapeSurfaceComponent* const Surface : Surfaces)
	{
		if (IsValid(Surface)
			&& Surface->GetSurfaceMesh() == PrimitiveComponent)
		{
			return Surface;
		}
	}

	return nullptr;
}

void UMD_ScrapeSurfaceComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(
		UMD_ScrapeSurfaceComponent,
		ReplicatedContaminationState);
}

void UMD_ScrapeSurfaceComponent::ConfigureExternalComponents(
	UStaticMeshComponent* const InSurfaceMesh,
	UInstancedStaticMeshComponent* const InContaminationOverlay,
	UDecalComponent* const InFrontContaminationDecal,
	UDecalComponent* const InBackContaminationDecal)
{
	SurfaceMesh = InSurfaceMesh;
	ContaminationOverlay = InContaminationOverlay;
	FrontContaminationDecal = InFrontContaminationDecal;
	BackContaminationDecal = InBackContaminationDecal;
	bOwnsContaminationOverlay = false;
	bOwnsFrontContaminationDecal = false;
	bOwnsBackContaminationDecal = false;
}

bool UMD_ScrapeSurfaceComponent::HasAuthority() const
{
	const AActor* const OwnerActor = GetOwner();
	return OwnerActor && OwnerActor->HasAuthority();
}

void UMD_ScrapeSurfaceComponent::ResolveSurfaceMeshFromOwner()
{
	if (IsValid(SurfaceMesh))
	{
		return;
	}

	AActor* const OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	SurfaceMesh = Cast<UStaticMeshComponent>(OwnerActor->GetRootComponent());
	if (!SurfaceMesh)
	{
		SurfaceMesh = OwnerActor->FindComponentByClass<UStaticMeshComponent>();
	}
}

void UMD_ScrapeSurfaceComponent::EnsureVisualizationComponents()
{
	ResolveSurfaceMeshFromOwner();
	AActor* const OwnerActor = GetOwner();
	UWorld* const World = GetWorld();
	if (!OwnerActor || !World || !SurfaceMesh)
	{
		return;
	}

	if (!ContaminationOverlay)
	{
		ContaminationOverlay = NewObject<UInstancedStaticMeshComponent>(
			OwnerActor,
			MakeUniqueObjectName(
				OwnerActor,
				UInstancedStaticMeshComponent::StaticClass(),
				TEXT("ScrapeSurfaceOverlay")),
			RF_Transient | RF_Transactional | RF_TextExportTransient);
		ContaminationOverlay->SetupAttachment(SurfaceMesh);
		ContaminationOverlay->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ContaminationOverlay->SetGenerateOverlapEvents(false);
		ContaminationOverlay->SetCanEverAffectNavigation(false);
		ContaminationOverlay->SetReceivesDecals(false);
		ContaminationOverlay->CastShadow = false;
		ContaminationOverlay->SetStaticMesh(ContaminationOverlayMesh);
		if (ContaminationOverlayMaterial)
		{
			ContaminationOverlay->SetMaterial(0, ContaminationOverlayMaterial);
		}
		ContaminationOverlay->CreationMethod = CreationMethod;
		ContaminationOverlay->RegisterComponentWithWorld(World);
		bOwnsContaminationOverlay = true;
	}

	auto CreateDecal = [this, OwnerActor, World](
		TObjectPtr<UDecalComponent>& InOutDecal,
		bool& bOutOwned,
		const FName BaseName)
	{
		if (InOutDecal)
		{
			return;
		}

		InOutDecal = NewObject<UDecalComponent>(
			OwnerActor,
			MakeUniqueObjectName(
				OwnerActor,
				UDecalComponent::StaticClass(),
				BaseName),
			RF_Transient | RF_Transactional | RF_TextExportTransient);
		InOutDecal->SetupAttachment(SurfaceMesh);
		InOutDecal->SetAbsolute(false, false, true);
		InOutDecal->SetRelativeScale3D(FVector::OneVector);
		InOutDecal->SetFadeScreenSize(0.0f);
		InOutDecal->SetVisibility(false);
		InOutDecal->SetHiddenInGame(true);
		InOutDecal->CreationMethod = CreationMethod;
		InOutDecal->RegisterComponentWithWorld(World);
		bOutOwned = true;
	};

	CreateDecal(
		FrontContaminationDecal,
		bOwnsFrontContaminationDecal,
		TEXT("FrontScrapeSurfaceDecal"));
	CreateDecal(
		BackContaminationDecal,
		bOwnsBackContaminationDecal,
		TEXT("BackScrapeSurfaceDecal"));
}

void UMD_ScrapeSurfaceComponent::DestroyOwnedVisualizationComponents()
{
	if (bOwnsContaminationOverlay && ContaminationOverlay)
	{
		ContaminationOverlay->DestroyComponent();
		ContaminationOverlay = nullptr;
	}
	if (bOwnsFrontContaminationDecal && FrontContaminationDecal)
	{
		FrontContaminationDecal->DestroyComponent();
		FrontContaminationDecal = nullptr;
	}
	if (bOwnsBackContaminationDecal && BackContaminationDecal)
	{
		BackContaminationDecal->DestroyComponent();
		BackContaminationDecal = nullptr;
	}

	bOwnsContaminationOverlay = false;
	bOwnsFrontContaminationDecal = false;
	bOwnsBackContaminationDecal = false;
}

void UMD_ScrapeSurfaceComponent::OnRegister()
{
	Super::OnRegister();
	ReplicatedContaminationState.SetOwner(this);
	EnsureVisualizationComponents();
	ResolveContaminationSurfaceGeometry();
}

void UMD_ScrapeSurfaceComponent::OnUnregister()
{
	ShutdownRuntimeState();
	DestroyOwnedVisualizationComponents();
	Super::OnUnregister();
}

void UMD_ScrapeSurfaceComponent::BeginPlay()
{
	Super::BeginPlay();
	InitializeRuntimeState();
}

void UMD_ScrapeSurfaceComponent::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	ShutdownRuntimeState();
	Super::EndPlay(EndPlayReason);
}

void UMD_ScrapeSurfaceComponent::InitializeRuntimeState()
{
	ReplicatedContaminationState.SetOwner(this);
	InitializeContaminationSurface();
	InitializeContaminationVisualization();

	if (!HasAuthority())
	{
		ApplyAllReplicatedContaminationState();
	}
}

void UMD_ScrapeSurfaceComponent::ShutdownRuntimeState()
{
	SetContaminationDecalsVisible(false);
	ReleaseContaminationMask();
	FrontContaminationDecalMID = nullptr;
	BackContaminationDecalMID = nullptr;
}

void UMD_ScrapeSurfaceComponent::InitializeSurface()
{
	EnsureVisualizationComponents();
	InitializeRuntimeState();
}

bool UMD_ScrapeSurfaceComponent::RefreshSurfaceGeometry()
{
	ResolveSurfaceMeshFromOwner();
	return ResolveContaminationSurfaceGeometry();
}
