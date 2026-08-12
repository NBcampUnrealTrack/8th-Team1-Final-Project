#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"

#include "Components/DecalComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "Component/Scrape/MD_ScrapeSurfaceComponentInternal.h"

DEFINE_STAT(STAT_MD_ScrapeStrokeTotal);
DEFINE_STAT(STAT_MD_ScrapeEvaluate);
DEFINE_STAT(STAT_MD_ScrapeCleanRatio);
DEFINE_STAT(STAT_MD_ScrapeMaskUpdate);
DEFINE_STAT(STAT_MD_ScrapePublish);
DEFINE_STAT(STAT_MD_ScrapeFullyRemovedCheck);

namespace MD_ScrapeSurfaceComponentInternal
{
	const FName ContaminationMaskTextureParameterName(TEXT("MaskTexture"));
	const FName TransferredPaintMaskTextureParameterName(
		TEXT("PaintMaskTexture"));
	const FName ContaminationBaseColorTextureParameterName(
		TEXT("ContamBaseColor"));
	const FName ContaminationShapeMaskTextureParameterName(
		TEXT("ContaminationShapeMask"));
	const FName ContaminationBaseCoverageParameterName(TEXT("ShapeFloorMin"));
	const FName ContaminationOpacityParameterName(TEXT("ContaminationOpacity"));
	const FName ContaminationMaskVScaleParameterName(TEXT("MaskVScale"));
	const FName ContaminationMaskVOffsetParameterName(TEXT("MaskVOffset"));
	const FName ContaminationMaskUFlipParameterName(TEXT("MaskUFlip"));
	const FName ContaminationMaskHalfTexelUParameterName(TEXT("MaskHalfTexelU"));
	const FName ContaminationMaskHalfTexelVParameterName(TEXT("MaskHalfTexelV"));
	const FName ContaminationPatchUVScaleUParameterName(TEXT("PatchUVScaleU"));
	const FName ContaminationPatchUVOffsetUParameterName(TEXT("PatchUVOffsetU"));
	const FName ContaminationPatchUVScaleVParameterName(TEXT("PatchUVScaleV"));
	const FName ContaminationPatchUVOffsetVParameterName(TEXT("PatchUVOffsetV"));
	const FName ContaminationSurfaceWidthParameterName(TEXT("SurfaceWidthCm"));
	const FName ContaminationSurfaceHeightParameterName(TEXT("SurfaceHeightCm"));
	const FName ContaminationSurfaceThicknessParameterName(
		TEXT("SurfaceThicknessCm"));
	const FName ContaminationDetailTileWorldSizeParameterName(
		TEXT("DetailTileWorldSizeCm"));
	const FName ContaminationDecalProjectionDirectionParameterName(
		TEXT("DecalProjectionDirectionWS"));
	const FName ContaminationAngleFadeStartParameterName(
		TEXT("AngleFadeStartDot"));
	const FName ContaminationAngleFadeEndParameterName(
		TEXT("AngleFadeEndDot"));
	const FName ContaminationFaceSplitOriginParameterName(
		TEXT("ContaminationFaceSplitOriginWS"));
	const FName ContaminationFaceSplitNormalParameterName(
		TEXT("ContaminationFaceSplitNormalWS"));
	const FName ContaminationFaceIsFrontParameterName(
		TEXT("ContaminationFaceIsFront"));
	const FName ContaminationFaceOwnershipEnabledParameterName(
		TEXT("ContaminationFaceOwnershipEnabled"));
	const FName ContaminationSurfaceOriginParameterName(
		TEXT("SurfaceOriginWS"));
	const FName ContaminationSurfaceTangentUParameterName(
		TEXT("SurfaceTangentUWS"));
	const FName ContaminationSurfaceTangentVParameterName(
		TEXT("SurfaceTangentVWS"));
}

UMD_ScrapeSurfaceComponent::UMD_ScrapeSurfaceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	bContaminationEnabled = true;

	// 12 x 4cm 헤라 도장을 실제 벽에서도 넓은 직사각형으로 표현할 수 있는 해상도.
	// 양면 아틀라스는 128 x 128(16,384셀)로 uint16 복제 인덱스 범위 안에 있다.
	ContaminationGridDimensions = FIntPoint(128, 64);
	TransferredPaintVisualTexelSizeCm = 0.75f;
	InitialContaminationPerCell = 1.0f;
	bUseProceduralContamination = false;
	ContaminationNoiseScaleCm = 75.0f;
	ContaminationNoiseSeed = 1337;
	ContaminationCoverage = 0.55f;
	ContaminationMinimumAmount = 0.25f;
	ContaminationSurfaceSizeOverrideCm = FVector2D::ZeroVector;
	ContaminationSurfaceNormalAxis = EMD_ScrapeSurfaceNormalAxis::Auto;
	ContaminationDecalMaterial = nullptr;
	ContaminationBaseColorTexture = nullptr;
	ContaminationShapeMaskTexture = nullptr;
	ContaminationBaseCoverage = 0.85f;
	ContaminationOpacity = 0.90f;
	bShowContaminationDecals = true;
	ContaminationDecalProjectionDepthCm = 1.0f;
	bAutoFitContaminationDecalDepth = true;
	bUseFullCoverageContaminationDecals = false;
	ContaminationDecalDepthFitMarginCm = 1.0f;
	FullCoverageContaminationDecalOuterMarginCm = 35.0f;
	ContaminationDecalSurfaceOffsetCm = 0.05f;
	ContaminationDecalEdgeInsetCm = 2.0f;
	bFitContaminationDecalPatchesToSurface = true;
	// 패치 하나는 평면 한 장이므로 구간이 넓을수록 곡면을 못 따라가고 각도 마스크에
	// 걸려 사라진다. Wall_Inter_A 기준으로 150 cm는 앞면의 87.6%, 50 cm는 100%를 덮는다.
	ContaminationDecalPatchWidthCm = 50.0f;
	ContaminationDetailTileWorldSizeCm = 150.0f;
	ContaminationDecalSortOrder = 0;
	ResolvedContaminationSurfaceSizeCm = FVector2D::ZeroVector;
	ResolvedContaminationSurfaceNormalAxis = EMD_ScrapeSurfaceNormalAxis::Y;
	ResolvedFrontDecalDepthCm = 0.0f;
	ResolvedBackDecalDepthCm = 0.0f;
	bResolvedFullCoverageContaminationDecals = false;
	ResolvedContaminationFaceSplitCoordinateLocal = 0.0;
	ResolvedContaminationDecalEdgeInsetCm = 0.0f;
	ContaminationSurfaceCenterLocal = FVector::ZeroVector;
	ContaminationMeshLocalBoundsMinimum = FVector::ZeroVector;
	ContaminationMeshLocalBoundsMaximum = FVector::ZeroVector;
	CachedFrontContaminationCleanRatio = 0.0f;
	CachedBackContaminationCleanRatio = 0.0f;
	ContaminationMaskAtlasDimensions = FIntPoint::ZeroValue;
	TransferredPaintMaskAtlasDimensions = FIntPoint::ZeroValue;
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
			&& Surface->IsContaminationEnabled()
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
		bContaminationEnabled);
	DOREPLIFETIME(
		UMD_ScrapeSurfaceComponent,
		ReplicatedContaminationState);
}

void UMD_ScrapeSurfaceComponent::ConfigureExternalComponents(
	UStaticMeshComponent* const InSurfaceMesh,
	UDecalComponent* const InFrontContaminationDecal,
	UDecalComponent* const InBackContaminationDecal)
{
	DestroyAdditionalContaminationDecalPatches();
	SurfaceMesh = InSurfaceMesh;
	FrontContaminationDecal = InFrontContaminationDecal;
	BackContaminationDecal = InBackContaminationDecal;
	bOwnsFrontContaminationDecal = false;
	bOwnsBackContaminationDecal = false;
	if (!bContaminationEnabled)
	{
		SetContaminationDecalsVisible(false);
	}
}

void UMD_ScrapeSurfaceComponent::SetContaminationEnabled(const bool bEnabled)
{
	if (!HasAuthority() || bContaminationEnabled == bEnabled)
	{
		return;
	}

	bContaminationEnabled = bEnabled;
	if (!bContaminationEnabled)
	{
		ReplicatedContaminationState.ResetState();
	}
	ApplyContaminationEnabledState();

	if (AActor* const OwnerActor = GetOwner())
	{
		OwnerActor->ForceNetUpdate();
	}
}

void UMD_ScrapeSurfaceComponent::OnRep_ContaminationEnabled()
{
	ApplyContaminationEnabledState();
}

void UMD_ScrapeSurfaceComponent::ApplyContaminationEnabledState()
{
	if (!bContaminationEnabled)
	{
		ShutdownRuntimeState();
		DestroyOwnedVisualizationComponents();
		return;
	}

	if (!IsRegistered())
	{
		return;
	}

	EnsureVisualizationComponents();
	ResolveContaminationSurfaceGeometry();
	const AActor* const OwnerActor = GetOwner();
	if (OwnerActor && OwnerActor->HasActorBegunPlay())
	{
		InitializeRuntimeState();
	}
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
	DestroyAdditionalContaminationDecalPatches();

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

	bOwnsFrontContaminationDecal = false;
	bOwnsBackContaminationDecal = false;
}

void UMD_ScrapeSurfaceComponent::OnRegister()
{
	Super::OnRegister();
	ReplicatedContaminationState.SetOwner(this);
	if (bContaminationEnabled)
	{
		EnsureVisualizationComponents();
		ResolveContaminationSurfaceGeometry();
	}
	else
	{
		ShutdownRuntimeState();
	}
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
	ApplyContaminationEnabledState();
}

void UMD_ScrapeSurfaceComponent::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	ShutdownRuntimeState();
	Super::EndPlay(EndPlayReason);
}

void UMD_ScrapeSurfaceComponent::InitializeRuntimeState()
{
	if (!bContaminationEnabled)
	{
		ShutdownRuntimeState();
		return;
	}

	ReplicatedContaminationState.SetOwner(this);
	InitializeContaminationSurface();

	if (!HasAuthority())
	{
		ApplyAllReplicatedContaminationState();
	}
}

void UMD_ScrapeSurfaceComponent::ShutdownRuntimeState()
{
	SetContaminationDecalsVisible(false);
	ReleaseContaminationMask();
	DestroyAdditionalContaminationDecalPatches();
	FrontContaminationDecalMID = nullptr;
	BackContaminationDecalMID = nullptr;
	bContaminationDecalsConfigured = false;
	bContaminationSurfaceInitialized = false;
	bContaminationCompletionBroadcast = false;
	ContaminationCells.Reset();
	ContaminationGeneratedInitialAmounts.Reset();
	PendingContaminationItemAmounts[0] = 0.0f;
	PendingContaminationItemAmounts[1] = 0.0f;
	CachedFrontContaminationCleanRatio = 0.0f;
	CachedBackContaminationCleanRatio = 0.0f;
}

void UMD_ScrapeSurfaceComponent::InitializeSurface()
{
	if (!bContaminationEnabled)
	{
		ShutdownRuntimeState();
		DestroyOwnedVisualizationComponents();
		return;
	}

	EnsureVisualizationComponents();
	InitializeRuntimeState();
}

bool UMD_ScrapeSurfaceComponent::RefreshSurfaceGeometry()
{
	if (!bContaminationEnabled)
	{
		return false;
	}

	ResolveSurfaceMeshFromOwner();
	return ResolveContaminationSurfaceGeometry();
}
