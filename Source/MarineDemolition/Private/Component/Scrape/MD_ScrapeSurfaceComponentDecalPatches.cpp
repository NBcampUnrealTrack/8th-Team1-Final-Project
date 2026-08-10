#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"

#include "CollisionQueryParams.h"
#include "Components/DecalComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Scrape/MD_ScrapeDecalLayout.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceGeometry.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RotationMatrix.h"

bool UMD_ScrapeSurfaceComponent::ConfigureContaminationDecalPatchesForFace(
	UDecalComponent* BaseDecalComponent,
	const EMD_ScrapeSurfaceFace Face,
	TArray<TObjectPtr<UDecalComponent>>& OutPatchComponents,
	TArray<TObjectPtr<UMaterialInstanceDynamic>>& OutPatchMIDs)
{
	OutPatchComponents.Reset();
	OutPatchMIDs.Reset();
	if (!BaseDecalComponent || !SurfaceMesh || !ContaminationDecalMaterial)
	{
		return false;
	}

	float& ResolvedDepthCm = Face == EMD_ScrapeSurfaceFace::Front
		? ResolvedFrontDecalDepthCm
		: ResolvedBackDecalDepthCm;
	FMD_ScrapeDecalFaceLayout FaceLayout;
	if (!FMD_ScrapeDecalLayout::TryCalculateFaceLayout(
			Face,
			ResolvedContaminationSurfaceNormalAxis,
			ResolvedContaminationSurfaceSizeCm,
			ContaminationMeshLocalBoundsMinimum,
			ContaminationMeshLocalBoundsMaximum,
			SurfaceMesh->GetComponentScale().GetAbs(),
			ResolvedDepthCm,
			ContaminationDecalSurfaceOffsetCm,
			ContaminationDecalEdgeInsetCm,
			FaceLayout))
	{
		return false;
	}

	ResolvedDepthCm = static_cast<float>(FaceLayout.AppliedInwardDepthCm);
	ResolvedContaminationDecalEdgeInsetCm =
		static_cast<float>(FaceLayout.AppliedEdgeInsetCm);
	const int32 PatchCount = FMD_ScrapeDecalLayout::
		CalculateHorizontalPatchCount(
			FaceLayout.DecalSize.Z * 2.0,
			ContaminationDecalPatchWidthCm,
			MaximumContaminationDecalPatchesPerFace);
	if (PatchCount <= 0)
	{
		return false;
	}

	OutPatchComponents.Reserve(PatchCount);
	OutPatchMIDs.Reserve(PatchCount);
	for (int32 PatchIndex = 0; PatchIndex < PatchCount; ++PatchIndex)
	{
		UDecalComponent* const DecalComponent = PatchIndex == 0
			? BaseDecalComponent
			: CreateAdditionalContaminationDecalPatch(Face, PatchIndex);
		if (!DecalComponent)
		{
			return false;
		}
		OutPatchComponents.Add(DecalComponent);

		FMD_ScrapeDecalFaceLayout PatchLayout;
		if (!FMD_ScrapeDecalLayout::TryCalculateHorizontalPatchLayout(
				FaceLayout,
				ResolvedContaminationSurfaceNormalAxis,
				SurfaceMesh->GetComponentScale().GetAbs(),
				PatchIndex,
				PatchCount,
				PatchLayout))
		{
			return false;
		}

		// Collision may be unavailable on a mesh. In that case the original
		// bounds-aligned patch remains valid instead of losing the decal.
		TryFitContaminationDecalPatchToSurface(Face, PatchLayout);

		TObjectPtr<UMaterialInstanceDynamic> PatchMID;
		if (!ConfigureContaminationDecalPatch(
				DecalComponent,
				PatchLayout,
				PatchMID))
		{
			return false;
		}
		OutPatchMIDs.Add(PatchMID);
	}

	return OutPatchComponents.Num() == PatchCount
		&& OutPatchMIDs.Num() == PatchCount;
}

UDecalComponent*
UMD_ScrapeSurfaceComponent::CreateAdditionalContaminationDecalPatch(
	const EMD_ScrapeSurfaceFace Face,
	const int32 PatchIndex)
{
	AActor* const OwnerActor = GetOwner();
	UWorld* const World = GetWorld();
	if (!OwnerActor || !World || !SurfaceMesh || PatchIndex <= 0)
	{
		return nullptr;
	}

	const TCHAR* const FaceName = Face == EMD_ScrapeSurfaceFace::Front
		? TEXT("Front")
		: TEXT("Back");
	const FName BaseName(*FString::Printf(
		TEXT("%sScrapeSurfaceDecalPatch%d"),
		FaceName,
		PatchIndex));
	UDecalComponent* const Patch = NewObject<UDecalComponent>(
		OwnerActor,
		MakeUniqueObjectName(
			OwnerActor,
			UDecalComponent::StaticClass(),
			BaseName),
		RF_Transient | RF_Transactional | RF_TextExportTransient);
	if (!Patch)
	{
		return nullptr;
	}

	Patch->SetupAttachment(SurfaceMesh);
	Patch->SetAbsolute(false, false, true);
	Patch->SetRelativeScale3D(FVector::OneVector);
	Patch->SetFadeScreenSize(0.0f);
	Patch->SetVisibility(false);
	Patch->SetHiddenInGame(true);
	Patch->CreationMethod = CreationMethod;
	Patch->RegisterComponentWithWorld(World);
	return Patch;
}

void UMD_ScrapeSurfaceComponent::DestroyAdditionalContaminationDecalPatches()
{
	auto DestroyAdditionalPatches = [](
		TArray<TObjectPtr<UDecalComponent>>& PatchComponents,
		UDecalComponent* BaseDecalComponent)
	{
		for (UDecalComponent* const PatchComponent : PatchComponents)
		{
			if (IsValid(PatchComponent)
				&& PatchComponent != BaseDecalComponent)
			{
				PatchComponent->DestroyComponent();
			}
		}
		PatchComponents.Reset();
	};

	DestroyAdditionalPatches(
		FrontContaminationDecalPatches,
		FrontContaminationDecal);
	DestroyAdditionalPatches(
		BackContaminationDecalPatches,
		BackContaminationDecal);
	FrontContaminationDecalPatchMIDs.Reset();
	BackContaminationDecalPatchMIDs.Reset();
	FrontContaminationDecalMID = nullptr;
	BackContaminationDecalMID = nullptr;
}

bool UMD_ScrapeSurfaceComponent::TryFitContaminationDecalPatchToSurface(
	const EMD_ScrapeSurfaceFace Face,
	FMD_ScrapeDecalFaceLayout& InOutLayout) const
{
	if (!SurfaceMesh
		|| (Face != EMD_ScrapeSurfaceFace::Front
			&& Face != EMD_ScrapeSurfaceFace::Back))
	{
		return false;
	}

	const FTransform MeshTransform = SurfaceMesh->GetComponentTransform();
	const FVector NominalProjectionDirection = MeshTransform
		.TransformVectorNoScale(InOutLayout.RelativeRotation.GetAxisX())
		.GetSafeNormal();
	const FVector NominalVerticalAxis = MeshTransform
		.TransformVectorNoScale(InOutLayout.RelativeRotation.GetAxisY())
		.GetSafeNormal();
	const FVector NominalHorizontalAxis = MeshTransform
		.TransformVectorNoScale(InOutLayout.RelativeRotation.GetAxisZ())
		.GetSafeNormal();
	if (NominalProjectionDirection.IsNearlyZero()
		|| NominalVerticalAxis.IsNearlyZero()
		|| NominalHorizontalAxis.IsNearlyZero())
	{
		return false;
	}

	const FVector NominalOutwardNormal = -NominalProjectionDirection;
	const FVector PlanarVolumeCenterWorld = MeshTransform.TransformPosition(
		InOutLayout.RelativeLocation);
	const double SurfaceFromVolumeCenterCm =
		(InOutLayout.AppliedInwardDepthCm
			- InOutLayout.AppliedOutwardMarginCm) * 0.5;
	const FVector PlanarSurfaceCenterWorld = PlanarVolumeCenterWorld
		+ NominalOutwardNormal * SurfaceFromVolumeCenterCm;

	FVector HorizontalAxisLocal;
	FVector FrontNormalLocal;
	FVector VerticalAxisLocal;
	if (!FMD_ScrapeSurfaceGeometry::TryGetSurfaceLocalAxes(
			ResolvedContaminationSurfaceNormalAxis,
			HorizontalAxisLocal,
			FrontNormalLocal,
			VerticalAxisLocal))
	{
		return false;
	}

	const FVector LocalBoundsSize = ContaminationMeshLocalBoundsMaximum
		- ContaminationMeshLocalBoundsMinimum;
	const double WorldThicknessCm = FMath::Abs(FVector::DotProduct(
		LocalBoundsSize,
		FrontNormalLocal)) * FMath::Abs(FVector::DotProduct(
		SurfaceMesh->GetComponentScale().GetAbs(),
		FrontNormalLocal));
	if (!FMath::IsFinite(WorldThicknessCm)
		|| WorldThicknessCm <= static_cast<double>(UE_SMALL_NUMBER))
	{
		return false;
	}

	const double ExteriorTraceDistanceCm = FMath::Max(
		InOutLayout.AppliedOutwardMarginCm + 1.0,
		1.0);
	const double InwardTraceDistanceCm = FMath::Max(
		WorldThicknessCm + ExteriorTraceDistanceCm,
		InOutLayout.AppliedInwardDepthCm + ExteriorTraceDistanceCm);
	// 패치는 가로로도 넓다. 세로 한 줄만 재면 담당 구간 안쪽의 곡률을 아예 측정하지
	// 못해, 그 줄이 우연히 평평하면 구간 전체를 평면으로 오판한다. 그러면 실제로는
	// 크게 돌아간 면이 각도 마스크에 걸려 그 패치만 통째로 사라진다.
	// 세로 끝(±0.85)은 벽 위/아래 몰딩처럼 튀어나온 다른 면을 물기 쉬우므로 안으로 당긴다.
	constexpr double HorizontalSampleFractions[] = {
		0.0, -0.85, -0.45, 0.45, 0.85 };
	constexpr double VerticalSampleFractions[] = { 0.0, -0.6, 0.6 };
	constexpr double MinimumPatchSurfaceNormalDot = 0.1;

	FCollisionQueryParams QueryParams(
		SCENE_QUERY_STAT(MD_ScrapeDecalPatchFit),
		true);
	QueryParams.bReturnFaceIndex = true;
	FVector SurfaceCenterSum = FVector::ZeroVector;
	FVector SurfaceNormalSum = FVector::ZeroVector;
	int32 ValidHitCount = 0;
	for (const double HorizontalFraction : HorizontalSampleFractions)
	{
		for (const double VerticalFraction : VerticalSampleFractions)
		{
			const FVector SamplePlanePoint = PlanarSurfaceCenterWorld
				+ NominalHorizontalAxis
					* (InOutLayout.DecalSize.Z * HorizontalFraction)
				+ NominalVerticalAxis
					* (InOutLayout.DecalSize.Y * VerticalFraction);
			const FVector TraceStart = SamplePlanePoint
				+ NominalOutwardNormal * ExteriorTraceDistanceCm;
			const FVector TraceEnd = SamplePlanePoint
				- NominalOutwardNormal * InwardTraceDistanceCm;
			FHitResult Hit;
			if (!SurfaceMesh->LineTraceComponent(
					Hit,
					TraceStart,
					TraceEnd,
					QueryParams))
			{
				continue;
			}

			FVector HitNormal = Hit.ImpactNormal.GetSafeNormal();
			if (HitNormal.IsNearlyZero())
			{
				HitNormal = Hit.Normal.GetSafeNormal();
			}
			const double FacingDot = FVector::DotProduct(
				HitNormal,
				NominalOutwardNormal);
			if (HitNormal.IsNearlyZero()
				|| FacingDot < MinimumPatchSurfaceNormalDot)
			{
				continue;
			}

			const FVector InwardTraceDirection = -NominalOutwardNormal;
			const double PlaneDenominator = FVector::DotProduct(
				InwardTraceDirection,
				HitNormal);
			if (FMath::Abs(PlaneDenominator)
				<= static_cast<double>(UE_SMALL_NUMBER))
			{
				continue;
			}

			// 히트를 지나는 접평면과, 패치 중심에서 안쪽으로 쏜 축의 교점까지 거리다.
			// 가로/세로로 벌어진 샘플도 같은 축 위의 깊이로 환산되어 평균에 들어간다.
			const double DistanceAlongInward = FVector::DotProduct(
				Hit.ImpactPoint - PlanarSurfaceCenterWorld,
				HitNormal) / PlaneDenominator;
			if (!FMath::IsFinite(DistanceAlongInward)
				|| DistanceAlongInward < -ExteriorTraceDistanceCm
				|| DistanceAlongInward > InwardTraceDistanceCm)
			{
				continue;
			}

			SurfaceCenterSum += PlanarSurfaceCenterWorld
				+ InwardTraceDirection * DistanceAlongInward;
			SurfaceNormalSum += HitNormal;
			++ValidHitCount;
		}
	}

	if (ValidHitCount <= 0)
	{
		return false;
	}

	const FVector SurfaceCenterWorld = SurfaceCenterSum
		/ static_cast<double>(ValidHitCount);
	const FVector OutwardNormal = SurfaceNormalSum.GetSafeNormal();
	const FVector TangentVerticalAxis = (
		NominalVerticalAxis
		- OutwardNormal * FVector::DotProduct(
			NominalVerticalAxis,
			OutwardNormal)).GetSafeNormal();
	if (OutwardNormal.IsNearlyZero()
		|| TangentVerticalAxis.IsNearlyZero()
		|| FVector::DotProduct(OutwardNormal, NominalOutwardNormal)
			< MinimumPatchSurfaceNormalDot)
	{
		return false;
	}

	const FVector ProjectionDirection = -OutwardNormal;
	const FQuat WorldRotation = FRotationMatrix::MakeFromXY(
		ProjectionDirection,
		TangentVerticalAxis).ToQuat();
	const FVector TangentHorizontalAxis = WorldRotation.GetAxisZ();
	const double HorizontalAlignment = FMath::Abs(FVector::DotProduct(
		TangentHorizontalAxis,
		NominalHorizontalAxis));
	if (FMath::IsFinite(HorizontalAlignment))
	{
		// 기울어진 상자는 담당 구간을 다 덮으려면 그만큼 넓어야 한다. 그런데 상자만
		// 넓히고 U 구간을 그대로 두면 데칼 로컬 U 한 칸이 가리키는 월드 폭이 패치마다
		// 달라져, 제거 마스크가 패치별로 다른 배율로 어긋난다. 넓힌 배율만큼 U 구간도
		// 넓히고, 구간 중심이 담당 구간 중심에 그대로 남도록 시작점을 되민다.
		const double WidthScale = 1.0
			/ FMath::Max(HorizontalAlignment, 0.35);
		const double PreviousUVScaleU =
			static_cast<double>(InOutLayout.PatchUVScaleU);
		InOutLayout.DecalSize.Z *= WidthScale;
		InOutLayout.PatchUVScaleU = static_cast<float>(
			PreviousUVScaleU * WidthScale);
		InOutLayout.PatchUVOffsetU = static_cast<float>(
			static_cast<double>(InOutLayout.PatchUVOffsetU)
			- PreviousUVScaleU * (WidthScale - 1.0) * 0.5);
	}

	const double VolumeCenterFromSurfaceCm =
		(InOutLayout.AppliedOutwardMarginCm
			- InOutLayout.AppliedInwardDepthCm) * 0.5;
	const FVector VolumeCenterWorld = SurfaceCenterWorld
		+ OutwardNormal * VolumeCenterFromSurfaceCm;
	InOutLayout.RelativeLocation = MeshTransform.InverseTransformPosition(
		VolumeCenterWorld);
	InOutLayout.RelativeRotation = (
		MeshTransform.GetRotation().Inverse() * WorldRotation).GetNormalized();
	return !InOutLayout.RelativeLocation.ContainsNaN()
		&& !InOutLayout.RelativeRotation.ContainsNaN()
		&& !InOutLayout.DecalSize.ContainsNaN();
}
