#include "Customization/MD_CharacterCustomizationPreviewActor.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Customization/MD_CharacterCustomizationComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"

namespace MDPreviewActorPrivate
{
	static void ConfigureCaptureShowFlags(USceneCaptureComponent2D* Capture)
	{
		if (!Capture)
		{
			return;
		}

		Capture->ShowFlags.SetAtmosphere(false);
		Capture->ShowFlags.SetFog(false);
		Capture->ShowFlags.SetVolumetricFog(false);
		Capture->ShowFlags.SetTemporalAA(false);
		Capture->ShowFlags.SetEyeAdaptation(false);
		Capture->ShowFlags.SetBloom(false);
		Capture->ShowFlags.SetAmbientOcclusion(false);
		Capture->ShowFlags.SetLighting(true);
		Capture->ShowFlags.SetPostProcessing(true);

		Capture->PostProcessSettings.bOverride_AutoExposureMethod = true;
		Capture->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
		Capture->PostProcessSettings.bOverride_AutoExposureBias = true;
		Capture->PostProcessSettings.AutoExposureBias = 0.0f;
		Capture->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
		Capture->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
	}
}

AMD_CharacterCustomizationPreviewActor::AMD_CharacterCustomizationPreviewActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	PreviewMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("PreviewMesh"));
	SetRootComponent(PreviewMesh);
	PreviewMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PreviewMesh->SetCollisionObjectType(ECC_WorldDynamic);
	PreviewMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	PreviewMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	PreviewMesh->SetCastShadow(false);
	PreviewMesh->SetReceivesDecals(false);
	PreviewMesh->SetHiddenInGame(false);
	PreviewMesh->bOwnerNoSee = false;
	PreviewMesh->bVisibleInRayTracing = false;
	PreviewMesh->SetComponentTickEnabled(true);
	PreviewMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	CustomizationComponent = CreateDefaultSubobject<UMD_CharacterCustomizationComponent>(TEXT("CustomizationComponent"));

	// Soft key + fill only (no directional lux blowout).
	PreviewLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("PreviewLight"));
	PreviewLight->SetupAttachment(PreviewMesh);
	PreviewLight->SetRelativeLocation(FVector(40.f, -80.f, 120.f));
	PreviewLight->SetIntensityUnits(ELightUnits::Unitless);
	PreviewLight->SetIntensity(1.8f);
	PreviewLight->SetAttenuationRadius(2500.f);
	PreviewLight->SetCastShadows(false);
	PreviewLight->SetCastRaytracedShadow(false);
	PreviewLight->SetMobility(EComponentMobility::Movable);

	FillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLight"));
	FillLight->SetupAttachment(PreviewMesh);
	FillLight->SetRelativeLocation(FVector(-60.f, -40.f, 60.f));
	FillLight->SetIntensityUnits(ELightUnits::Unitless);
	FillLight->SetIntensity(0.9f);
	FillLight->SetAttenuationRadius(2500.f);
	FillLight->SetCastShadows(false);
	FillLight->SetCastRaytracedShadow(false);
	FillLight->SetMobility(EComponentMobility::Movable);

	KeyLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("KeyLight"));
	KeyLight->SetupAttachment(PreviewMesh);
	KeyLight->SetVisibility(false);
	KeyLight->SetCastShadows(false);
	KeyLight->SetMobility(EComponentMobility::Movable);

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(PreviewMesh);
	SkyLight->SetVisibility(false);
	SkyLight->SetMobility(EComponentMobility::Movable);

	SceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SceneCapture"));
	SceneCapture->SetupAttachment(PreviewMesh);
	SceneCapture->bCaptureEveryFrame = false;
	SceneCapture->bCaptureOnMovement = false;
	SceneCapture->TextureTarget = nullptr;
	SceneCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	SceneCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	SceneCapture->bAlwaysPersistRenderingState = true;
	SceneCapture->FOVAngle = 45.f;
	MDPreviewActorPrivate::ConfigureCaptureShowFlags(SceneCapture);

	FaceSceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("FaceSceneCapture"));
	FaceSceneCapture->SetupAttachment(PreviewMesh);
	FaceSceneCapture->bCaptureEveryFrame = false;
	FaceSceneCapture->bCaptureOnMovement = false;
	FaceSceneCapture->TextureTarget = nullptr;
	FaceSceneCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	FaceSceneCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	FaceSceneCapture->bAlwaysPersistRenderingState = true;
	FaceSceneCapture->FOVAngle = 30.f;
	MDPreviewActorPrivate::ConfigureCaptureShowFlags(FaceSceneCapture);
}

void AMD_CharacterCustomizationPreviewActor::ConfigureCapture(
	USceneCaptureComponent2D* Capture,
	UTextureRenderTarget2D* RenderTarget,
	const FVector& RelativeLocation,
	const FVector& FocusPoint) const
{
	if (!Capture)
	{
		return;
	}

	Capture->SetRelativeLocation(RelativeLocation);
	Capture->TextureTarget = RenderTarget;
	Capture->ShowOnlyActors.Reset();
	Capture->ShowOnlyComponents.Reset();
	Capture->ShowOnlyActors.Add(const_cast<AMD_CharacterCustomizationPreviewActor*>(this));
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->bCaptureEveryFrame = RenderTarget != nullptr;
	Capture->bCaptureOnMovement = RenderTarget != nullptr;
	Capture->bAlwaysPersistRenderingState = true;
	MDPreviewActorPrivate::ConfigureCaptureShowFlags(Capture);

	if (RenderTarget)
	{
		const FVector CaptureWorld = Capture->GetComponentLocation();
		const FVector ToFocus = FocusPoint - CaptureWorld;
		if (!ToFocus.IsNearlyZero())
		{
			Capture->SetWorldRotation(ToFocus.GetSafeNormal().Rotation());
		}
		Capture->CaptureScene();
	}
}

void AMD_CharacterCustomizationPreviewActor::ConfigurePreview(
	USkeletalMesh* InMesh,
	UMaterialInterface* InMaterial,
	UTextureRenderTarget2D* InBodyRenderTarget,
	UTextureRenderTarget2D* InFaceRenderTarget)
{
	if (!PreviewMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("MD PreviewActor: PreviewMesh missing"));
		return;
	}

	if (!InMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("MD PreviewActor: skeletal mesh failed to load — check Project Settings paths"));
	}
	else
	{
		PreviewMesh->SetSkeletalMeshAsset(InMesh);
		if (InMaterial && InMaterial->GetMaterial())
		{
			PreviewMesh->SetMaterial(0, InMaterial);
		}
		PreviewMesh->SetRelativeRotation(FRotator::ZeroRotator);
		PreviewMesh->SetForcedLOD(0);
		PreviewMesh->SetEnablePerPolyCollision(true);
		PreviewMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		PreviewMesh->SetCollisionObjectType(ECC_WorldDynamic);
		PreviewMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		PreviewMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		PreviewMesh->UpdateBounds();
		PreviewMesh->RefreshBoneTransforms();
		PreviewMesh->RecreatePhysicsState();
		PreviewMesh->MarkRenderStateDirty();
		UE_LOG(LogTemp, Log, TEXT("MD PreviewActor: mesh=%s boundsRadius=%.1f cam=+Y front"),
			*InMesh->GetName(), PreviewMesh->Bounds.SphereRadius);
	}

	const FBoxSphereBounds Bounds = PreviewMesh->Bounds.SphereRadius > KINDA_SMALL_NUMBER
		? PreviewMesh->Bounds
		: FBoxSphereBounds(GetActorLocation() + FVector(0.f, 0.f, 90.f), FVector(45.f, 45.f, 90.f), 90.f);

	const float Radius = FMath::Max(Bounds.SphereRadius, 50.f);
	const FVector BodyFocus = Bounds.Origin;

	// Prefer a head bone; bone pivots sit high in the helmet so nudge down toward the visor.
	FVector FaceFocus = Bounds.Origin + FVector(0.f, 0.f, Bounds.BoxExtent.Z * 0.58f);
	{
		static const FName HeadBoneCandidates[] = {
			FName(TEXT("CC_Base_Head")),
			FName(TEXT("head")),
			FName(TEXT("Head")),
			FName(TEXT("HeadTop_End")),
			FName(TEXT("mixamorig:Head")),
		};

		bool bFoundHeadBone = false;
		for (const FName BoneName : HeadBoneCandidates)
		{
			if (PreviewMesh->GetBoneIndex(BoneName) == INDEX_NONE)
			{
				continue;
			}

			FaceFocus = PreviewMesh->GetBoneLocation(BoneName);
			bFoundHeadBone = true;
			break;
		}

		// Pull focus onto the front visor plate (Tripo faces +Y) and down from the crown.
		const float DownNudge = bFoundHeadBone
			? Bounds.BoxExtent.Z * 0.18f
			: Bounds.BoxExtent.Z * 0.06f;
		FaceFocus += FVector(0.f, Bounds.BoxExtent.Y * 0.25f, -DownNudge);
	}

	const float BodyDistance = FMath::Clamp(Radius * 2.6f, 140.f, 1200.f);
	// Pull back so the full face plate fits the panel (previous 0.95 was too close).
	const float FaceDistance = FMath::Clamp(Radius * 1.55f, 90.f, 480.f);

	// Tripo faces +Y → camera in front on +Y, slightly below face focus.
	const FVector BodyCamRel(0.f, BodyDistance, Radius * 0.2f);
	const float FaceCamZ = (FaceFocus.Z - PreviewMesh->GetComponentLocation().Z)
		- (Bounds.BoxExtent.Z * 0.06f);
	const FVector FaceCamRel(0.f, FaceDistance, FaceCamZ);

	if (FaceSceneCapture)
	{
		FaceSceneCapture->FOVAngle = 34.f;
	}

	if (PreviewLight)
	{
		PreviewLight->SetVisibility(true);
		PreviewLight->SetIntensityUnits(ELightUnits::Unitless);
		PreviewLight->SetRelativeLocation(FVector(Radius * 0.3f, BodyDistance * 0.35f, Radius * 0.9f));
		PreviewLight->SetAttenuationRadius(BodyDistance * 5.f);
		PreviewLight->SetIntensity(1.8f);
	}
	if (FillLight)
	{
		FillLight->SetVisibility(true);
		FillLight->SetIntensityUnits(ELightUnits::Unitless);
		FillLight->SetRelativeLocation(FVector(-Radius * 0.4f, BodyDistance * 0.2f, Radius * 0.35f));
		FillLight->SetAttenuationRadius(BodyDistance * 5.f);
		FillLight->SetIntensity(0.9f);
	}
	if (KeyLight)
	{
		KeyLight->SetVisibility(false);
	}
	if (SkyLight)
	{
		SkyLight->SetVisibility(false);
	}

	ConfigureCapture(SceneCapture, InBodyRenderTarget, BodyCamRel, BodyFocus);
	ConfigureCapture(FaceSceneCapture, InFaceRenderTarget, FaceCamRel, FaceFocus);
}

void AMD_CharacterCustomizationPreviewActor::SetOrbitEnabled(bool bEnabled)
{
	bOrbitEnabled = bEnabled;
}

bool AMD_CharacterCustomizationPreviewActor::ProjectFaceViewToMeshUV(FVector2D ViewUV01, FVector2D& OutMeshUV) const
{
	if (!FaceSceneCapture || !PreviewMesh || !GetWorld())
	{
		return false;
	}

	ViewUV01.X = FMath::Clamp(ViewUV01.X, 0.f, 1.f);
	ViewUV01.Y = FMath::Clamp(ViewUV01.Y, 0.f, 1.f);

	const float Aspect = FaceSceneCapture->TextureTarget
		? FMath::Max(KINDA_SMALL_NUMBER, static_cast<float>(FaceSceneCapture->TextureTarget->SizeX)
			/ static_cast<float>(FMath::Max(1, FaceSceneCapture->TextureTarget->SizeY)))
		: 1.f;

	// SceneCapture FOV is horizontal; UE camera forward = +X, right = +Y, up = +Z.
	const float HalfFovRad = FMath::DegreesToRadians(FaceSceneCapture->FOVAngle * 0.5f);
	const float HalfWidth = FMath::Tan(HalfFovRad);
	const float HalfHeight = HalfWidth / Aspect;
	const float NdcX = ViewUV01.X * 2.f - 1.f;
	const float NdcY = 1.f - ViewUV01.Y * 2.f;

	FVector CamSpaceDir(1.f, NdcX * HalfWidth, NdcY * HalfHeight);
	CamSpaceDir.Normalize();

	const FTransform CamXform = FaceSceneCapture->GetComponentTransform();
	const FVector WorldStart = CamXform.GetLocation();
	const FVector WorldDir = CamXform.TransformVectorNoScale(CamSpaceDir).GetSafeNormal();

	FSkeletalMeshRenderData* RenderData = PreviewMesh->GetSkeletalMeshRenderData();
	if (!RenderData || !RenderData->LODRenderData.IsValidIndex(0))
	{
		return false;
	}

	FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[0];
	FRawStaticIndexBuffer16or32Interface* Indices = LODData.MultiSizeIndexContainer.GetIndexBuffer();
	if (!Indices)
	{
		return false;
	}

	const FStaticMeshVertexBuffer& VertexBuffer = LODData.StaticVertexBuffers.StaticMeshVertexBuffer;
	const FPositionVertexBuffer& PositionBuffer = LODData.StaticVertexBuffers.PositionVertexBuffer;
	const int32 NumIndices = Indices->Num();
	const int32 NumVerts = static_cast<int32>(PositionBuffer.GetNumVertices());
	if (NumIndices < 3 || NumVerts < 3)
	{
		return false;
	}

	// Ray in component space against bind-pose triangles (preview stays in ref pose).
	const FTransform CompXform = PreviewMesh->GetComponentTransform();
	const FVector LocalOrigin = CompXform.InverseTransformPosition(WorldStart);
	const FVector LocalDir = CompXform.InverseTransformVectorNoScale(WorldDir).GetSafeNormal();

	auto RayTriangle = [](const FVector& Origin, const FVector& Dir,
		const FVector& V0, const FVector& V1, const FVector& V2,
		float& OutT, FVector& OutBary) -> bool
	{
		const FVector Edge1 = V1 - V0;
		const FVector Edge2 = V2 - V0;
		const FVector PVec = FVector::CrossProduct(Dir, Edge2);
		const float Det = FVector::DotProduct(Edge1, PVec);
		if (FMath::Abs(Det) < KINDA_SMALL_NUMBER)
		{
			return false;
		}

		const float InvDet = 1.f / Det;
		const FVector TVec = Origin - V0;
		const float U = FVector::DotProduct(TVec, PVec) * InvDet;
		if (U < 0.f || U > 1.f)
		{
			return false;
		}

		const FVector QVec = FVector::CrossProduct(TVec, Edge1);
		const float V = FVector::DotProduct(Dir, QVec) * InvDet;
		if (V < 0.f || (U + V) > 1.f)
		{
			return false;
		}

		const float T = FVector::DotProduct(Edge2, QVec) * InvDet;
		if (T < KINDA_SMALL_NUMBER)
		{
			return false;
		}

		OutT = T;
		OutBary = FVector(1.f - U - V, U, V);
		return true;
	};

	float BestT = TNumericLimits<float>::Max();
	FVector BestBary = FVector::ZeroVector;
	int32 BestI0 = INDEX_NONE;
	int32 BestI1 = INDEX_NONE;
	int32 BestI2 = INDEX_NONE;

	const int32 TriCount = NumIndices / 3;
	for (int32 FaceIndex = 0; FaceIndex < TriCount; ++FaceIndex)
	{
		const int32 IndexBase = FaceIndex * 3;
		const int32 I0 = Indices->Get(IndexBase + 0);
		const int32 I1 = Indices->Get(IndexBase + 1);
		const int32 I2 = Indices->Get(IndexBase + 2);
		if (I0 < 0 || I1 < 0 || I2 < 0 || I0 >= NumVerts || I1 >= NumVerts || I2 >= NumVerts)
		{
			continue;
		}

		const FVector V0(PositionBuffer.VertexPosition(I0));
		const FVector V1(PositionBuffer.VertexPosition(I1));
		const FVector V2(PositionBuffer.VertexPosition(I2));

		float HitT = 0.f;
		FVector HitBary = FVector::ZeroVector;
		if (!RayTriangle(LocalOrigin, LocalDir, V0, V1, V2, HitT, HitBary) || HitT >= BestT)
		{
			continue;
		}

		BestT = HitT;
		BestBary = HitBary;
		BestI0 = I0;
		BestI1 = I1;
		BestI2 = I2;
	}

	if (BestI0 == INDEX_NONE)
	{
		return false;
	}

	const FVector2D UV0(VertexBuffer.GetVertexUV(BestI0, 0));
	const FVector2D UV1(VertexBuffer.GetVertexUV(BestI1, 0));
	const FVector2D UV2(VertexBuffer.GetVertexUV(BestI2, 0));
	OutMeshUV = (BestBary.X * UV0) + (BestBary.Y * UV1) + (BestBary.Z * UV2);
	OutMeshUV.X = FMath::Clamp(OutMeshUV.X, 0.f, 1.f);
	OutMeshUV.Y = FMath::Clamp(OutMeshUV.Y, 0.f, 1.f);
	return true;
}

void AMD_CharacterCustomizationPreviewActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bOrbitEnabled && PreviewMesh)
	{
		AddActorWorldRotation(FRotator(0.f, OrbitYawSpeed * DeltaSeconds, 0.f));
	}
}
