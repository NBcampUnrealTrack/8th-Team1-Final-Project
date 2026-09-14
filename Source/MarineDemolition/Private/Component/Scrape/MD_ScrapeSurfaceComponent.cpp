#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"
#include "World/Wall/MD_WallShadowBatchSubsystem.h"
#include "Gameplay/Scrape/MD_ScrapeInitializationSubsystem.h"
#include "Gameplay/Persistence/MD_DayPersistenceSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

#include "Components/DecalComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "Serialization/CustomVersion.h"

namespace
{
    const FGuid ContaminationSeedPolicyVersion(0xBC8E4F67, 0x6833487B, 0x9B6BA230, 0x9B7511AE);
    FCustomVersionRegistration RegisterContaminationSeedPolicyVersion(
        ContaminationSeedPolicyVersion, 1, TEXT("MDContaminationSeedPolicy"));
}

void UMD_ScrapeSurfaceComponent::Serialize(FArchive& Ar)
{
    Ar.UsingCustomVersion(ContaminationSeedPolicyVersion);
    Super::Serialize(Ar);
    // Preserve non-default seeds authored before automatic mode existed.
    // Old 1337 defaults intentionally adopt automatic mode on a new contract.
    if (Ar.IsLoading() && Ar.IsPersistent()
        && Ar.CustomVer(ContaminationSeedPolicyVersion) < 1 && ContaminationNoiseSeed != 1337)
    {
        bUseFixedContaminationSeed = true;
    }
}

#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
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
	const FName ContaminationEdgeMinParameterName(TEXT("EdgeMin"));
	const FName ContaminationEdgeMaxParameterName(TEXT("EdgeMax"));
	const FName ContaminationEdgeNoiseStrengthParameterName(
		TEXT("EdgeNoiseStrength"));
	const FName ContaminationVisualMaskGammaParameterName(
		TEXT("VisualMaskGamma"));
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
	const FName ContaminationPlanarSurfaceWidthParameterName(
		TEXT("PlanarSurfaceWidthCm"));
	const FName ContaminationUseCylindricalCanonicalUParameterName(
		TEXT("UseCylindricalCanonicalU"));
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
	const FName ContaminationCurveCenterParameterName(TEXT("CurveCenterWS"));
	const FName ContaminationCurveAxisParameterName(TEXT("CurveAxisWS"));
	const FName ContaminationCurveStartDirectionParameterName(
		TEXT("CurveStartDirectionWS"));
	const FName ContaminationCurveSweepRadiansParameterName(
		TEXT("CurveSweepRadians"));
	const FName ContaminationCurveRadiusParameterName(TEXT("CurveRadiusCm"));
	const FName ContaminationCurveFrontIsOuterParameterName(
		TEXT("CurveFrontIsOuter"));
}

UMD_ScrapeSurfaceComponent::UMD_ScrapeSurfaceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	bContaminationEnabled = true;
    DefaultContaminationSolidMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(
        TEXT("/Game/Materials/Wall/M_Contamination_Solid.M_Contamination_Solid")));
    ContaminationSolidMaterial = nullptr;

	// 12 x 4cm 헤라 도장을 실제 벽에서도 넓은 직사각형으로 표현할 수 있는 해상도.
	// 양면 아틀라스는 128 x 128(16,384셀)로 uint16 복제 인덱스 범위 안에 있다.
	ContaminationGridDimensions = FIntPoint(128, 64);
	TransferredPaintVisualTexelSizeCm = 0.75f;
	// 경계 전이 폭 약 1.8cm. 더 좁히면 선은 날카로워지지만 색조 단계가 사라져
	// 한 번 긁을 때마다 옅어지는 것이 안 보이고, 두 번째 통과에서 갑자기
	// 사라지는 것처럼 된다. 경계 위치 정확도는 이 값과 무관하다.
	ContaminationVisualEdgeSoftness = 0.55f;
	ContaminationVisualFilmOpacityGamma = 1.0f;
	ContaminationVisualEdgeNoiseAmount = 0.0f;
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
	CachedFrontInitialContaminationAmount = 0.0;
	CachedBackInitialContaminationAmount = 0.0;
	CachedFrontCurrentContaminationAmount = 0.0;
	CachedBackCurrentContaminationAmount = 0.0;
	CachedMeshUVProgress = FMD_ScrapeMeshUVProgress();
	ContaminationMaskAtlasDimensions = FIntPoint::ZeroValue;
	TransferredPaintMaskAtlasDimensions = FIntPoint::ZeroValue;
	bContaminationDecalsConfigured = false;
	bContaminationSurfaceInitialized = false;
	bContaminationCompletionBroadcast = false;
	bDeferredContaminationCompletionTransition = false;
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
    DOREPLIFETIME(UMD_ScrapeSurfaceComponent, ContaminationGeneration);
	DOREPLIFETIME(
		UMD_ScrapeSurfaceComponent,
		bContaminationEnabled);
	DOREPLIFETIME(
		UMD_ScrapeSurfaceComponent,
		ReplicatedContaminationVisualRevision);
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
	if (!OwnerActor || !World || !SurfaceMesh || World->GetNetMode() == NM_DedicatedServer)
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

void UMD_ScrapeSurfaceComponent::BeginDestroy()
{
    // Also cover initialized components that were never registered: final UObject
    // destruction may run in parallel, but these state owners are game-thread-only.
    ContaminationPagedCells.Reset();
    LocalPredictedPagedCells.Reset();
    Super::BeginDestroy();
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

#if WITH_EDITOR
void UMD_ScrapeSurfaceComponent::PostEditChangeProperty(
	FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// 마스크 텍셀 값 규약과 머티리얼 판정 구간은 한 쌍이다. 어느 쪽을 만져도
	// 같이 다시 만들어야 하므로, 에디터에서 값을 굴려 보며 맞출 수 있게 한다.
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(
			UMD_ScrapeSurfaceComponent,
			ContaminationVisualEdgeSoftness)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(
			UMD_ScrapeSurfaceComponent,
			ContaminationVisualFilmOpacityGamma)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(
			UMD_ScrapeSurfaceComponent,
			ContaminationVisualEdgeNoiseAmount))
	{
		RefreshContaminationAppearance();
	}
}
#endif

bool UMD_ScrapeSurfaceComponent::TryDeferContaminationInitialization()
{
    if (bCompletingDeferredInitialization || bRestoringContaminationGeneration || bContaminationSurfaceInitialized
        || !bContaminationEnabled || !UsesMeshUVSurface() || !ContaminationMeshUVData
        || !GetWorld() || !GetWorld()->IsGameWorld() || !HasAuthority()) return false;
    if (bDeferredInitializationPending) return true;
    const auto* GI = GetWorld()->GetGameInstance();
    const auto* Persistence = GI ? GI->GetSubsystem<UMD_DayPersistenceSubsystem>() : nullptr;
    if (!Persistence || !Persistence->IsContaminationContextReady(GetWorld())) return false;
    FMD_ContaminationGeneration Saved;
    bool Legacy = false;
    if (Persistence->TryGetSavedContaminationGeneration(this, Saved, Legacy) || Legacy
        || Persistence->ShouldDeferContaminationGeneration(this)) return false;
    PrepareContaminationGeneration();
    if (!ContaminationGeneration.bProcedural || ContaminationGeneration.Version < 2) return false;
    auto* Initializer = GetWorld()->GetSubsystem<UMD_ScrapeInitializationSubsystem>();
    bDeferredInitializationPending = Initializer && Initializer->Enqueue(*this);
    return bDeferredInitializationPending;
}

void UMD_ScrapeSurfaceComponent::InitializeRuntimeState()
{
	if (!bContaminationEnabled)
	{
		ShutdownRuntimeState();
		return;
	}

	ReplicatedContaminationState.SetOwner(this);
	if (TryDeferContaminationInitialization()) return;
	InitializeContaminationSurface();

	if (!HasAuthority())
	{
		ApplyAllReplicatedContaminationState();
	}
    if (SurfaceMesh && GetWorld() && GetWorld()->IsGameWorld())
        if (auto* Shadows = GetWorld()->GetSubsystem<UMD_WallShadowBatchSubsystem>()) Shadows->RegisterWall(*SurfaceMesh);
}

void UMD_ScrapeSurfaceComponent::ShutdownRuntimeState()
{
    ++DeferredInitializationEpoch;
    bDeferredInitializationPending = false;
    PreparedInitialState.Reset();
    if (SurfaceMesh && GetWorld())
        if (auto* Shadows = GetWorld()->GetSubsystem<UMD_WallShadowBatchSubsystem>()) Shadows->UnregisterWall(*SurfaceMesh);
    if (auto* Previous=GenerationTransformSource.Get()) Previous->TransformUpdated.Remove(GenerationTransformHandle);
    GenerationTransformSource.Reset();GenerationTransformHandle.Reset();
    ResetContaminationBoundary();
    PreservePendingContaminationDrops();
	ReleaseSolidContaminationVisual();
	SetContaminationDecalsVisible(false);
	ReleaseContaminationMask();
    ReleaseContaminationMeshUVOverlay();
    PendingBatchedContaminationVisualCellIndices.Empty();
    ContaminationVisualUpdateBatchDepth = 0;
	DestroyAdditionalContaminationDecalPatches();
	FrontContaminationDecalMID = nullptr;
	BackContaminationDecalMID = nullptr;
	bContaminationDecalsConfigured = false;
	bContaminationSurfaceInitialized = false;
	bContaminationCompletionBroadcast = false;
	bDeferredContaminationCompletionTransition = false;
	ContaminationCells.Empty();
    ContaminationPagedCells.Reset();
	SharedContaminationCellCarrierWeights.Reset();
    ContaminationCellLayout.Reset();
	LocalScrapePredictions.Empty();
	ReleaseLocalScrapePredictionMemoryBudget();
	ScrapeEvaluationScratch = FMD_ScrapeEvaluationScratch();
	LocalPredictedContaminationCells.Empty();
    LocalPredictedPagedCells.Reset();
	LocalScrapePredictionFaceScratch.Empty();
	PendingLocalScrapePredictionVisualCellIndices.Empty();
	PendingLocalScrapePredictionReconcileCells.Empty();
	ContaminationGeneratedInitialAmounts.Empty();
	PendingContaminationItemAmounts[0] = 0.0f;
	PendingContaminationItemAmounts[1] = 0.0f;
	for (int32 FaceSlot = 0; FaceSlot < 2; ++FaceSlot)
	{
		BaseContaminationRecoveryLots[FaceSlot].Empty();
		TransferredContaminationRecoveryLots[FaceSlot].Empty();
		PendingPhysicalRecoveryLots[FaceSlot].Empty();
		SpawnedPhysicalRecoveryLotsAwaitingCarrier[FaceSlot].Empty();
		CanonicalInitialContaminationCarrierAmounts[FaceSlot] = 0.0;
	}
	CachedFrontContaminationCleanRatio = 0.0f;
	CachedBackContaminationCleanRatio = 0.0f;
	CachedFrontInitialContaminationAmount = 0.0;
	CachedBackInitialContaminationAmount = 0.0;
	CachedFrontCurrentContaminationAmount = 0.0;
	CachedBackCurrentContaminationAmount = 0.0;
	CachedMeshUVProgress = FMD_ScrapeMeshUVProgress();
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
#if WITH_EDITOR
    // Construction can run continuously while a wall is resized. Retire the explicit
    // preview instead of rebuilding its large mesh on every editor interaction.
    if (bEditorSolidPreview && GetWorld() && !GetWorld()->IsGameWorld())
    {
        ClearSolidContaminationPreview();
    }
#endif
	if (!bContaminationEnabled)
	{
        ReleaseSolidContaminationVisual();
		return false;
	}

	ResolveSurfaceMeshFromOwner();
    const bool bResolved = ResolveContaminationSurfaceGeometry();
    if (ContaminationSolidVisual)
    {
        // This explicit runtime refresh also updates the scale-dependent display
        // cache and contact height without resetting authoritative cleaning state.
        if (bResolved) ConfigureSolidContaminationVisual();
        else ReleaseSolidContaminationVisual();
    }
    return bResolved;
}
