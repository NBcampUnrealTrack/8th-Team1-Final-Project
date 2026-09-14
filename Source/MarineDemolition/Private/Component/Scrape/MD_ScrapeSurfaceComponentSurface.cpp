#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"
#include "Engine/GameInstance.h"
#include "Gameplay/Persistence/MD_DayPersistenceSubsystem.h"

#include "Components/StaticMeshComponent.h"
#include "Gameplay/Scrape/MD_ContaminationNoise.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceGeometry.h"
#include "Gameplay/Scrape/MD_ScrapeMeshUVData.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Gameplay/Scrape/MD_ScrapeMeshUVSyncSubsystem.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceLayout.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/SecureHash.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

// 파일 분할 기준은 MD_ScrapeSurfaceComponentInternal.h 표 참고.
// 책임: 작업면 기하 해석 · 셀 초기화 · 좌표 변환

namespace
{
	const FName ContaminationRewardClass(TEXT("Contamination"));

	uint32 ReadRecoverySourceHashWord(const uint8* const Bytes)
	{
		return (static_cast<uint32>(Bytes[0]) << 24)
			| (static_cast<uint32>(Bytes[1]) << 16)
			| (static_cast<uint32>(Bytes[2]) << 8)
			| static_cast<uint32>(Bytes[3]);
	}

	FGuid BuildCanonicalContaminationSourceId(
		const UMD_ScrapeSurfaceComponent* const Surface,
		const EMD_ScrapeSurfaceFace Face)
	{
		const AActor* const Owner = Surface ? Surface->GetOwner() : nullptr;
		const FString StableSourceName = FString::Printf(
			TEXT("%s|%s|%s"),
			Owner ? *Owner->GetPathName() : TEXT("None"),
			Surface ? *Surface->GetName() : TEXT("None"),
			Face == EMD_ScrapeSurfaceFace::Back
				? TEXT("Back")
				: TEXT("Front"));
		const FTCHARToUTF8 Utf8SourceName(*StableSourceName);
		uint8 Hash[FSHA1::DigestSize];
		FSHA1::HashBuffer(
			Utf8SourceName.Get(),
			Utf8SourceName.Length(),
			Hash);

		FGuid SourceId(
			ReadRecoverySourceHashWord(Hash),
			ReadRecoverySourceHashWord(Hash + 4),
			ReadRecoverySourceHashWord(Hash + 8),
			ReadRecoverySourceHashWord(Hash + 12));
		if (!SourceId.IsValid())
		{
			// SHA-1의 앞 128 bit가 전부 0일 이론적 경우에도 reward lot은
			// 항상 유효한 source id를 가져야 한다.
			SourceId.D = 1;
		}
		return SourceId;
	}
}

bool UMD_ScrapeSurfaceComponent::TryGetScrapeSurfaceFaceFromHit(
	const FHitResult& Hit,
	EMD_ScrapeSurfaceFace& OutSurfaceFace)
{
	OutSurfaceFace = EMD_ScrapeSurfaceFace::Front;
	if (!bContaminationEnabled)
	{
		return false;
	}
	if (!bContaminationSurfaceInitialized)
	{
		InitializeContaminationSurface();
	}
	if (!bContaminationSurfaceInitialized || !AreContaminationCarrierWeightsCurrent())
	{
		return false;
	}

	// MeshUV에서는 unwrap이 앞뒤를 서로 다른 아일랜드로 이미 갈라 놓았다.
	// 여기서 Back을 돌려주면 ToCombinedCellIndex가 셀을 아틀라스 아래쪽
	// 절반으로 밀어내는데, 오버레이 머티리얼은 위쪽 절반만 읽는다. 그러면
	// 화면과 셀이 서로 다른 텍셀을 보게 되어 이 모드를 도입한 이유가 무의미해진다.
	if (ContaminationSurfaceMapping.Mode
		== EMD_ScrapeSurfaceMappingMode::MeshUV)
	{
		OutSurfaceFace = EMD_ScrapeSurfaceFace::Front;
		FVector2D Address;
		return TryProjectHitToContaminationMeshUVSurface(Hit, Address);
	}

	return TryGetContaminationFaceFromHit(Hit, OutSurfaceFace);
}


bool UMD_ScrapeSurfaceComponent::TryGetScrapeSurfacePositionFromHit(
	const FHitResult& Hit,
	FVector2D& OutSurfacePositionCm,
	EMD_ScrapeSurfaceFace& OutSurfaceFace)
{
	OutSurfacePositionCm = FVector2D::ZeroVector;
	if (!TryGetScrapeSurfaceFaceFromHit(Hit, OutSurfaceFace))
	{
		return false;
	}

	if (ContaminationSurfaceMapping.Mode
		== EMD_ScrapeSurfaceMappingMode::MeshUV)
	{
		return TryProjectHitToContaminationMeshUVSurface(
			Hit,
			OutSurfacePositionCm);
	}

	return TryProjectWorldPointToContaminationSurface(
		Hit.ImpactPoint,
		OutSurfaceFace,
		OutSurfacePositionCm);
}


bool UMD_ScrapeSurfaceComponent::TryProjectHitToContaminationMeshUVSurface(
	const FHitResult& Hit,
	FVector2D& OutSurfacePositionCm) const
{
    OutSurfacePositionCm = FVector2D::ZeroVector;
    if (!bContaminationEnabled || !bContaminationSurfaceInitialized || !SurfaceMesh
        || !AreContaminationCarrierWeightsCurrent() || !ContaminationMeshUVData || !Hit.bBlockingHit || Hit.GetActor() != GetOwner()
        || Hit.GetComponent() != SurfaceMesh.Get() || Hit.ImpactPoint.ContainsNaN()
        || Hit.ImpactNormal.ContainsNaN()) { return false; }
    const FTransform& Transform = SurfaceMesh->GetComponentTransform();
    FMD_ScrapeMeshUVSample Sample;
    bool bFound = false;
    if (!Hit.TraceStart.ContainsNaN() && !Hit.TraceEnd.ContainsNaN()
        && !Hit.TraceStart.Equals(Hit.TraceEnd, UE_SMALL_NUMBER))
    {
        // The first source surface is authoritative. A cap never rays through to a side.
        bFound = FMD_ScrapeMeshUVGeometry::RaycastSource(*ContaminationMeshUVData,
            Transform.InverseTransformPosition(Hit.TraceStart),
            Transform.InverseTransformPosition(Hit.TraceEnd), Sample);
    }
    else
    {
        bFound = FMD_ScrapeMeshUVGeometry::FindNearestSource(*ContaminationMeshUVData,
            Transform.InverseTransformPosition(Hit.ImpactPoint),
            (Transform.InverseTransformVectorNoScale(Hit.ImpactNormal) * Transform.GetScale3D()).GetSafeNormal(),
            0.25, Sample);
    }
    return bFound && FMD_ScrapeSurfaceGeometry::TryConvertMeshUVToSurfacePositionCm(
        Sample.CanonicalUV, ResolvedContaminationSurfaceSizeCm, OutSurfacePositionCm);
}

bool UMD_ScrapeSurfaceComponent::TryGetContaminationStateFromHit(
	const FHitResult& Hit,
	bool& OutContaminated)
{
	OutContaminated = false;
	if (!SurfaceMesh
		|| !Hit.bBlockingHit
		|| Hit.GetActor() != GetOwner()
		|| Hit.ImpactPoint.ContainsNaN()
		|| Hit.ImpactNormal.ContainsNaN())
	{
		return false;
	}

	if (!bContaminationEnabled)
	{
		return true;
	}

	if (!bContaminationSurfaceInitialized)
	{
		InitializeContaminationSurface();
	}
	if (!bContaminationSurfaceInitialized)
	{
		return false;
	}

	EMD_ScrapeSurfaceFace SurfaceFace = EMD_ScrapeSurfaceFace::Front;
	FVector2D SurfacePositionCm = FVector2D::ZeroVector;
	return TryGetScrapeSurfacePositionFromHit(
			Hit,
			SurfacePositionCm,
			SurfaceFace)
		&& TryGetContaminationStateAtSurfacePosition(
			SurfacePositionCm,
			SurfaceFace,
			OutContaminated);
}

bool UMD_ScrapeSurfaceComponent::TryGetContaminationStateAtSurfacePosition(
	const FVector2D& SurfacePositionCm,
	const EMD_ScrapeSurfaceFace SurfaceFace,
	bool& OutContaminated) const
{
	OutContaminated = false;
	if (!bContaminationEnabled)
	{
		return true;
	}
	if (!bContaminationSurfaceInitialized
		|| SurfacePositionCm.ContainsNaN()
		|| ContaminationGridDimensions.X <= 0
		|| ContaminationGridDimensions.Y <= 0
		|| ResolvedContaminationSurfaceSizeCm.X <= UE_SMALL_NUMBER
		|| ResolvedContaminationSurfaceSizeCm.Y <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const FVector2D SurfaceHalfSizeCm =
		ResolvedContaminationSurfaceSizeCm * 0.5;
	const FVector2D NormalizedPosition(
		(SurfacePositionCm.X + SurfaceHalfSizeCm.X)
			/ ResolvedContaminationSurfaceSizeCm.X,
		(SurfacePositionCm.Y + SurfaceHalfSizeCm.Y)
			/ ResolvedContaminationSurfaceSizeCm.Y);
	if (NormalizedPosition.X < -UE_KINDA_SMALL_NUMBER
		|| NormalizedPosition.X > 1.0 + UE_KINDA_SMALL_NUMBER
		|| NormalizedPosition.Y < -UE_KINDA_SMALL_NUMBER
		|| NormalizedPosition.Y > 1.0 + UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const int32 CellX = FMath::Clamp(
		FMath::FloorToInt(NormalizedPosition.X * ContaminationGridDimensions.X),
		0,
		ContaminationGridDimensions.X - 1);
	const int32 CellY = FMath::Clamp(
		FMath::FloorToInt(NormalizedPosition.Y * ContaminationGridDimensions.Y),
		0,
		ContaminationGridDimensions.Y - 1);
	const int32 CellIndex = CellX + CellY * ContaminationGridDimensions.X;
	const FMD_ScrapeConstCellView FaceCells =
		GetContaminationCellsForFace(SurfaceFace);
	if (!FaceCells.IsValidIndex(CellIndex))
	{
		return false;
	}

	const FMD_ScrapeSurfaceCell& Cell = FaceCells[CellIndex];
	OutContaminated =
		Cell.RemainingContamination > ScrapeSettings.CleanTolerance
		|| Cell.TransferredContamination > ScrapeSettings.CleanTolerance;
	return true;
}


bool UMD_ScrapeSurfaceComponent::TryGetScrapeSurfacePositionFromWorldPoint(
	const FVector& WorldPoint,
	const FVector& WorldNormal,
	FVector2D& OutSurfacePositionCm,
	EMD_ScrapeSurfaceFace& OutSurfaceFace)
{
	OutSurfacePositionCm = FVector2D::ZeroVector;
	OutSurfaceFace = EMD_ScrapeSurfaceFace::Front;
	if (!bContaminationEnabled)
	{
		return false;
	}

	if (!bContaminationSurfaceInitialized)
	{
		InitializeContaminationSurface();
	}
	if (!bContaminationSurfaceInitialized || !AreContaminationCarrierWeightsCurrent())
	{
		return false;
	}

	if (!SurfaceMesh || WorldPoint.ContainsNaN() || WorldNormal.ContainsNaN())
	{
		return false;
	}

    if (UsesMeshUVSurface())
    {
        FMD_ScrapeMeshUVSample Sample;
        const FTransform& Transform = SurfaceMesh->GetComponentTransform();
        return ContaminationMeshUVData
            && FMD_ScrapeMeshUVGeometry::FindNearestSource(*ContaminationMeshUVData,
                Transform.InverseTransformPosition(WorldPoint),
                (Transform.InverseTransformVectorNoScale(WorldNormal) * Transform.GetScale3D()).GetSafeNormal(),
                0.25, Sample)
            && FMD_ScrapeSurfaceGeometry::TryConvertMeshUVToSurfacePositionCm(
                Sample.CanonicalUV, ResolvedContaminationSurfaceSizeCm, OutSurfacePositionCm);
    }

	return TryResolveContaminationFace(
			WorldPoint,
			WorldNormal,
			OutSurfaceFace)
		&& TryProjectWorldPointToContaminationSurface(
			WorldPoint,
			OutSurfaceFace,
			OutSurfacePositionCm);
}


bool UMD_ScrapeSurfaceComponent::TryGetScrapeSurfaceWorldPosition(
	const FVector2D& SurfacePositionCm,
	const EMD_ScrapeSurfaceFace SurfaceFace,
	FVector& OutWorldPosition) const
{
	OutWorldPosition = FVector::ZeroVector;
    if (UsesMeshUVSurface())
    {
        FVector2D UV;
        FMD_ScrapeMeshUVSample Sample;
        if (!bContaminationEnabled || !bContaminationSurfaceInitialized || !SurfaceMesh
            || !AreContaminationCarrierWeightsCurrent() || !ContaminationMeshUVData || SurfaceFace != EMD_ScrapeSurfaceFace::Front
            || !FMD_ScrapeSurfaceGeometry::TryConvertSurfacePositionCmToMeshUV(
                SurfacePositionCm, ResolvedContaminationSurfaceSizeCm, UV)
            || !FMD_ScrapeMeshUVGeometry::SampleCanonicalUV(*ContaminationMeshUVData, UV, Sample))
        { return false; }
        OutWorldPosition = SurfaceMesh->GetComponentTransform().TransformPosition(Sample.PositionLocal);
        return true;
    }
	if (!bContaminationEnabled
		|| !bContaminationSurfaceInitialized
		|| !SurfaceMesh
		|| !FMD_ScrapeSurfaceGeometry::TryCalculateSurfaceWorldPosition(
			SurfacePositionCm,
			SurfaceFace,
			SurfaceMesh->GetComponentTransform(),
			ResolvedContaminationSurfaceNormalAxis,
			ContaminationSurfaceCenterLocal,
			ContaminationMeshLocalBoundsMinimum,
			ContaminationMeshLocalBoundsMaximum,
			ContaminationSurfaceMapping,
			ResolvedContaminationSurfaceSizeCm,
			OutWorldPosition))
	{
		return false;
	}

	return true;
}


bool UMD_ScrapeSurfaceComponent::TryGetScrapeSurfaceWorldPositionAtDepth(
	const FVector2D& SurfacePositionCm,
	const EMD_ScrapeSurfaceFace SurfaceFace,
	const FVector& ReferenceWorldPosition,
	FVector& OutWorldPosition) const
{
	if (!TryGetScrapeSurfaceWorldPosition(
			SurfacePositionCm,
			SurfaceFace,
			OutWorldPosition))
	{
		return false;
	}

	if (UsesMeshUVSurface()) { return true; }

	FVector OutWorldNormal = FVector::ZeroVector;
	if (ReferenceWorldPosition.ContainsNaN()
		|| !FMD_ScrapeSurfaceGeometry::TryCalculateSurfaceWorldNormal(
			SurfacePositionCm,
			SurfaceFace,
			SurfaceMesh->GetComponentTransform(),
			ResolvedContaminationSurfaceNormalAxis,
			ContaminationSurfaceMapping,
			ResolvedContaminationSurfaceSizeCm,
			OutWorldNormal))
	{
		// 깊이 보정만 실패한 경우이므로 Bounds 평면 위치를 그대로 사용한다.
		return true;
	}

	// 앞/뒤 축 성분만 참조 위치의 깊이로 바꾼다. 가로/세로 위치는 유지한다.
	const double DepthDifference = FVector::DotProduct(
		ReferenceWorldPosition - OutWorldPosition,
		OutWorldNormal);
	if (!FMath::IsFinite(DepthDifference))
	{
		return true;
	}

	OutWorldPosition += OutWorldNormal * DepthDifference;
	return !OutWorldPosition.ContainsNaN();
}


bool UMD_ScrapeSurfaceComponent::SelectCompatibleContaminationLayout(const FGuid& LayoutId, const bool bInitialize)
{
    if (!UsesMeshUVSurface() || !ContaminationMeshUVData) return false;
    if (ContaminationMeshUVData->LayoutId == LayoutId) return true;
    UMD_ScrapeMeshUVData* Compatible = ContaminationMeshUVData->ResolveCompatibleLayout(LayoutId);
    if (!Compatible || !SurfaceMesh) return false;
    if (HasAuthority() && GetWorld())
        if (auto* Sync = GetWorld()->GetSubsystem<UMD_ScrapeMeshUVSyncSubsystem>()) Sync->InvalidateSurface(*this);
    ShutdownRuntimeState();
    ContaminationMeshUVData = Compatible;
    SurfaceMesh->SetStaticMesh(Compatible->SourceMesh);
    ContaminationGridDimensions = Compatible->AtlasDimensions;
    if (bInitialize)
    {
        TGuardValue<bool> Completing(bCompletingDeferredInitialization, true);
        InitializeSurface();
        return bContaminationSurfaceInitialized;
    }
    return true;
}

bool UMD_ScrapeSurfaceComponent::ResolveContaminationSurfaceGeometry()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scrape_ResolveSurfaceGeometry);
	if (!SurfaceMesh)
	{
		ResolvedContaminationSurfaceSizeCm = FVector2D::ZeroVector;
		ResolvedContaminationSurfaceNormalAxis =
			EMD_ScrapeSurfaceNormalAxis::Y;
		ContaminationSurfaceCenterLocal = FVector::ZeroVector;
		return false;
	}

    if (UsesMeshUVSurface())
    {
        if (HasAuthority() && GetWorld() && GetWorld()->IsGameWorld())
            if (auto* GI = GetWorld()->GetGameInstance())
                if (auto* Persistence = GI->GetSubsystem<UMD_DayPersistenceSubsystem>())
                {
                    FGuid SavedLayout;
                    if (Persistence->TryGetSavedContaminationLayout(this, SavedLayout)
                        && !SelectCompatibleContaminationLayout(SavedLayout, false)) return false;
                }
        FString Error;
        if (!ContaminationMeshUVData || !ContaminationMeshUVData->ValidateForRuntime(&Error)
            || !ContaminationMeshUVData->IsValidForMesh(SurfaceMesh->GetStaticMesh())
            || (SurfaceMesh->GetStaticMesh() != ContaminationMeshUVData->SourceMesh
                && SurfaceMesh->GetStaticMesh() != ContaminationMeshUVData->BakedRenderMesh)
            || SurfaceMesh->GetComponentScale().ContainsNaN()
            || SurfaceMesh->GetComponentScale().GetAbs().GetMin() <= UE_SMALL_NUMBER)
        {
            UE_LOG(LogTemp, Warning, TEXT("MeshUV surface %s requires matching baked data: %s"), *GetName(), *Error);
            return false;
        }
        SurfaceMesh->SetStaticMesh(ContaminationMeshUVData->BakedRenderMesh);
        ContaminationGridDimensions = ContaminationMeshUVData->AtlasDimensions;
        ContaminationSurfaceMapping.MeshUVChannel = ContaminationMeshUVData->CanonicalUVChannel;
        // Legacy FVector2D API carries an address. It is never used as physical brush distance.
        ResolvedContaminationSurfaceSizeCm = FVector2D(ContaminationGridDimensions)
            * ContaminationMeshUVData->TargetCellSizeCm;
        SurfaceMesh->GetLocalBounds(ContaminationMeshLocalBoundsMinimum, ContaminationMeshLocalBoundsMaximum);
        ContaminationSurfaceCenterLocal = (ContaminationMeshLocalBoundsMinimum + ContaminationMeshLocalBoundsMaximum) * 0.5;
        ResolvedContaminationSurfaceNormalAxis = EMD_ScrapeSurfaceNormalAxis::Y;
        return true;
    }

	SurfaceMesh->GetLocalBounds(
		ContaminationMeshLocalBoundsMinimum,
		ContaminationMeshLocalBoundsMaximum);

	// 피벗과 무관하게 Bounds 중심 사용
	ContaminationSurfaceCenterLocal =
		(ContaminationMeshLocalBoundsMinimum
			+ ContaminationMeshLocalBoundsMaximum)
		* 0.5f;
	const bool bHasValidOverride =
		FMath::IsFinite(ContaminationSurfaceSizeOverrideCm.X)
		&& FMath::IsFinite(ContaminationSurfaceSizeOverrideCm.Y)
		&& ContaminationSurfaceSizeOverrideCm.X > UE_SMALL_NUMBER
		&& ContaminationSurfaceSizeOverrideCm.Y > UE_SMALL_NUMBER;
	const FVector AbsoluteComponentScale =
		SurfaceMesh->GetComponentScale().GetAbs();
	if (!FMD_ScrapeSurfaceGeometry::TryResolveSurfaceNormalAxis(
			ContaminationSurfaceNormalAxis,
			ContaminationMeshLocalBoundsMinimum,
			ContaminationMeshLocalBoundsMaximum,
			AbsoluteComponentScale,
			ResolvedContaminationSurfaceNormalAxis))
	{
		// 메시 없는 계산 테스트처럼 크기 Override만 제공한 기존 경로는 Y축으로 유지한다.
		if (!bHasValidOverride
			|| ContaminationSurfaceNormalAxis
				!= EMD_ScrapeSurfaceNormalAxis::Auto)
		{
			ResolvedContaminationSurfaceSizeCm = FVector2D::ZeroVector;
			return false;
		}
		ResolvedContaminationSurfaceNormalAxis =
			EMD_ScrapeSurfaceNormalAxis::Y;
	}

	if (bHasValidOverride)
	{
		if (ContaminationSurfaceMapping.Mode
				== EMD_ScrapeSurfaceMappingMode::CylindricalArc
			|| ContaminationSurfaceMapping.Mode
				== EMD_ScrapeSurfaceMappingMode::CurvedFrontPlanarBack)
		{
			FVector2D CurveSurfaceSizeCm = FVector2D::ZeroVector;
			if (!FMD_ScrapeSurfaceGeometry::TryCalculateMappedSurfaceSize(
					SurfaceMesh->GetComponentTransform(),
					ContaminationSurfaceMapping,
					ContaminationMeshLocalBoundsMinimum,
					ContaminationMeshLocalBoundsMaximum,
					CurveSurfaceSizeCm))
			{
				ResolvedContaminationSurfaceSizeCm = FVector2D::ZeroVector;
				return false;
			}

			// U의 cm 단위는 실제 중심선 호 길이여야 스크레이퍼 폭과 렌더링 각도가
			// 일치한다. 높이 Override만 유지하고 X Override는 곡면에서 사용하지 않는다.
			ResolvedContaminationSurfaceSizeCm = FVector2D(
				CurveSurfaceSizeCm.X,
				ContaminationSurfaceSizeOverrideCm.Y);
			return true;
		}

		ResolvedContaminationSurfaceSizeCm =
			ContaminationSurfaceSizeOverrideCm;
		return true;
	}

	if (ContaminationSurfaceMapping.Mode
			== EMD_ScrapeSurfaceMappingMode::CylindricalArc
		|| ContaminationSurfaceMapping.Mode
			== EMD_ScrapeSurfaceMappingMode::CurvedFrontPlanarBack)
	{
		return FMD_ScrapeSurfaceGeometry::TryCalculateMappedSurfaceSize(
			SurfaceMesh->GetComponentTransform(),
			ContaminationSurfaceMapping,
			ContaminationMeshLocalBoundsMinimum,
			ContaminationMeshLocalBoundsMaximum,
			ResolvedContaminationSurfaceSizeCm);
	}
	// MeshUV의 U/V는 이미 0~1 무차원 주소다. 여기서 정하는 cm 크기는 물리적
	// 치수가 아니라 격자 해상도와 헤라 폭의 기준일 뿐이므로 Bounds 크기를
	// 그대로 쓴다. Planar와 같은 경로를 타는 이유가 이것이다.
	if (ContaminationSurfaceMapping.Mode
			!= EMD_ScrapeSurfaceMappingMode::Planar
		&& ContaminationSurfaceMapping.Mode
			!= EMD_ScrapeSurfaceMappingMode::MeshUV)
	{
		ResolvedContaminationSurfaceSizeCm = FVector2D::ZeroVector;
		return false;
	}

	return FMD_ScrapeSurfaceGeometry::TryCalculateSurfaceSize(
		ResolvedContaminationSurfaceNormalAxis,
		ContaminationMeshLocalBoundsMinimum,
		ContaminationMeshLocalBoundsMaximum,
		AbsoluteComponentScale,
		ResolvedContaminationSurfaceSizeCm);
}

FMD_ScrapeCellView
UMD_ScrapeSurfaceComponent::GetContaminationCellsForFace(
	const EMD_ScrapeSurfaceFace Face)
{
	int32 CellsPerFace = 0;
	int32 TotalCellCount = 0;
	if ((UsesMeshUVSurface() && Face != EMD_ScrapeSurfaceFace::Front)
		|| !TryCalculateContaminationCellCounts(
			CellsPerFace,
			TotalCellCount)
		|| GetContaminationAtlasCellCount() != TotalCellCount)
	{
		return FMD_ScrapeCellView();
	}

	const int32 FaceOffset = FMD_ScrapeSurfaceLayout::GetFaceCellOffset(
		Face,
		CellsPerFace);
	if (FaceOffset == INDEX_NONE)
	{
		return FMD_ScrapeCellView();
	}

	return GetContaminationCellView().Slice(
		FaceOffset,
		CellsPerFace);
}

FMD_ScrapeConstCellView
UMD_ScrapeSurfaceComponent::GetContaminationCellsForFace(
	const EMD_ScrapeSurfaceFace Face) const
{
	int32 CellsPerFace = 0;
	int32 TotalCellCount = 0;
	if ((UsesMeshUVSurface() && Face != EMD_ScrapeSurfaceFace::Front)
		|| !TryCalculateContaminationCellCounts(
			CellsPerFace,
			TotalCellCount)
		|| GetContaminationAtlasCellCount() != TotalCellCount)
	{
		return FMD_ScrapeConstCellView();
	}

	const int32 FaceOffset = FMD_ScrapeSurfaceLayout::GetFaceCellOffset(
		Face,
		CellsPerFace);
	if (FaceOffset == INDEX_NONE)
	{
		return FMD_ScrapeConstCellView();
	}

	return GetContaminationCellView().Slice(
		FaceOffset,
		CellsPerFace);
}

void UMD_ScrapeSurfaceComponent::InitializeContaminationSurface()
{
    if (bDeferredInitializationPending && !bRestoringContaminationGeneration) return;
    if (GetWorld() && GetWorld()->IsGameWorld())
    {
        if (!HasAuthority() && !ContaminationGeneration.IsValid()) return;
        if (HasAuthority())
            if (auto* GI=GetWorld()->GetGameInstance())
                if (auto* Persistence=GI->GetSubsystem<UMD_DayPersistenceSubsystem>())
                    if (!Persistence->IsContaminationContextReady(GetWorld())) return;
    }
    ResetContaminationBoundary();
    PreservePendingContaminationDrops();
    LocalScrapePredictions.Empty();
    LocalPredictedContaminationCells.Empty();
    LocalPredictedPagedCells.Reset();
    LocalScrapePredictionFaceScratch.Empty();
    PendingLocalScrapePredictionVisualCellIndices.Empty();
    PendingLocalScrapePredictionReconcileCells.Empty();
    ReleaseLocalScrapePredictionMemoryBudget();
    PendingBatchedContaminationVisualCellIndices.Empty();
    ContaminationVisualUpdateBatchDepth = 0;
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scrape_InitializeSurface);
	bContaminationSurfaceInitialized = false;
	bContaminationDecalsConfigured = false;
	SetContaminationDecalsVisible(false);
	bContaminationCompletionBroadcast = false;
	bDeferredContaminationCompletionTransition = false;
	CachedFrontContaminationCleanRatio = 0.0f;
	CachedBackContaminationCleanRatio = 0.0f;
	CachedFrontInitialContaminationAmount = 0.0;
	CachedBackInitialContaminationAmount = 0.0;
	CachedFrontCurrentContaminationAmount = 0.0;
	CachedBackCurrentContaminationAmount = 0.0;
	CachedMeshUVProgress = FMD_ScrapeMeshUVProgress();
	ContaminationCells.Reset();
    ContaminationPagedCells.Reset();
    LocalPredictedPagedCells.Reset();
    ContaminationCellLayout.Reset();
    SharedContaminationCellCarrierWeights.Reset();
	ContaminationGeneratedInitialAmounts.Reset();
	// 미생성 산출물은 위에서 월드 대기열로 넘겼으므로 표면의 소유분만 비운다.
	PendingContaminationItemAmounts[0] = 0.0f;
	PendingContaminationItemAmounts[1] = 0.0f;
	for (int32 FaceSlot = 0; FaceSlot < 2; ++FaceSlot)
	{
		BaseContaminationRecoveryLots[FaceSlot].Reset();
		TransferredContaminationRecoveryLots[FaceSlot].Reset();
		PendingPhysicalRecoveryLots[FaceSlot].Reset();
		SpawnedPhysicalRecoveryLotsAwaitingCarrier[FaceSlot].Reset();
		CanonicalInitialContaminationCarrierAmounts[FaceSlot] = 0.0;
	}
	ReleaseSolidContaminationVisual();
	ReleaseContaminationMask();
	if (HasAuthority())
	{
		ReplicatedContaminationState.ResetState();
	}

	if (!bContaminationEnabled)
	{
		return;
	}

	if (!ResolveContaminationSurfaceGeometry())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("MD_CuttableWall::InitializeContaminationSurface - %s의 오염 작업면 크기를 계산하지 못했습니다."),
			*GetName());
		return;
	}

	// 큰 구조물에서도 12cm 헤라 폭이 최소 4~5셀에 걸리도록 물리 크기를 기준으로
	// 오래된 64x64 인스턴스 설정을 자동 보강한다. uint16 양면 복제 상한은 유지한다.
	static constexpr int32 MinimumGridAxisForAutomaticResolution = 16;
	if (!UsesMeshUVSurface() && FMath::Min(
			ContaminationGridDimensions.X,
			ContaminationGridDimensions.Y)
		>= MinimumGridAxisForAutomaticResolution)
	{
	const int32 MinimumGridWidth = FMath::CeilToInt(
		ResolvedContaminationSurfaceSizeCm.X
			/ MaximumContaminationCellSizeForPaintCm);
	const int32 MinimumGridHeight = FMath::CeilToInt(
		ResolvedContaminationSurfaceSizeCm.Y
			/ MaximumContaminationCellSizeForPaintCm);
	ContaminationGridDimensions.X = FMath::Clamp(
		FMath::Max(ContaminationGridDimensions.X, MinimumGridWidth),
		1,
		MaximumContaminationGridAxis);
	ContaminationGridDimensions.Y = FMath::Clamp(
		FMath::Max(ContaminationGridDimensions.Y, MinimumGridHeight),
		1,
		MaximumContaminationGridAxis);

	const int32 MaximumCellsPerFace =
		FMD_ScrapeSurfaceLayout::LegacyMaximumCombinedCellCount
		/ FMD_ScrapeSurfaceLayout::FaceCount;
	const int64 RequestedCellsPerFace =
		static_cast<int64>(ContaminationGridDimensions.X)
		* static_cast<int64>(ContaminationGridDimensions.Y);
	if (RequestedCellsPerFace > MaximumCellsPerFace)
	{
		const double Scale = FMath::Sqrt(
			static_cast<double>(MaximumCellsPerFace)
				/ static_cast<double>(RequestedCellsPerFace));
		ContaminationGridDimensions.X = FMath::Max(
			1,
			FMath::FloorToInt(ContaminationGridDimensions.X * Scale));
		ContaminationGridDimensions.Y = FMath::Max(
			1,
			FMath::FloorToInt(ContaminationGridDimensions.Y * Scale));
		while (static_cast<int64>(ContaminationGridDimensions.X)
			* static_cast<int64>(ContaminationGridDimensions.Y)
			> MaximumCellsPerFace)
		{
			if (ContaminationGridDimensions.X >= ContaminationGridDimensions.Y)
			{
				--ContaminationGridDimensions.X;
			}
			else
			{
				--ContaminationGridDimensions.Y;
			}
		}
	}
	}

	int32 CellsPerFace = 0;
	int32 TotalCellCount = 0;
	if (!TryCalculateContaminationCellCounts(
			CellsPerFace,
			TotalCellCount))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("MD_CuttableWall::InitializeContaminationSurface - %s의 오염 셀 수가 허용 메모리 범위를 벗어났습니다. Grid=(%d, %d)"),
			*GetName(),
			ContaminationGridDimensions.X,
			ContaminationGridDimensions.Y);
		return;
	}

	// Reinitialization must release unused capacity from a previously larger layout.
	ContaminationCellLayout = UsesMeshUVSurface() && ContaminationMeshUVData
        ? ContaminationMeshUVData->GetSharedCellLayout() : nullptr;
    const int32 StorageCellCount = ContaminationCellLayout
        ? ContaminationCellLayout->ActiveToAtlas.Num() : TotalCellCount;
    ContaminationCells.Empty();
    ContaminationGeneratedInitialAmounts.Empty();
	if (!InitializeContaminationCellCarrierWeights())
	{
		ContaminationCells.Reset();
		UE_LOG(LogTemp, Warning, TEXT("MeshUV physical carrier weights could not be initialized: %s"), *GetName());
		return;
	}

    // Saved surfaces need their layout for validation, but build dirt only once,
    // when the next-tick restore applies the saved baseline and deltas.
    if (!bRestoringContaminationGeneration && GetWorld() && HasAuthority())
        if (auto* GI=GetWorld()->GetGameInstance())
            if (auto* Persistence=GI->GetSubsystem<UMD_DayPersistenceSubsystem>())
                if (Persistence->ShouldDeferContaminationGeneration(this)) return;
    PrepareContaminationGeneration();

	// 잘못된 초기값은 완전 오염으로 처리
	const float SafeInitialContamination =
		FMath::IsFinite(ContaminationGeneration.BaseAmount)
		? FMath::Clamp(ContaminationGeneration.BaseAmount, 0.0f, 1.0f)
		: 1.0f;

    FMD_ScrapeSurfaceCell InitialCell;
    InitialCell.InitialContamination = SafeInitialContamination;
    InitialCell.RemainingContamination = SafeInitialContamination;
    InitialCell.TransferredContamination = 0.0f;
    InitialCell.TransferredPaintPatternDensity = 0.0f;
    InitialCell.CoveredFraction = 0.0f;
    if (ContaminationCellLayout && !ContaminationGeneration.bProcedural)
    {
        // Untouched MeshUV walls share one immutable value without materializing active cells.
        ContaminationPagedCells.InitializeUniform(StorageCellCount, InitialCell);
    }
    else if (ContaminationCellLayout && ContaminationGeneration.bProcedural
        && ContaminationGeneration.Version >= 2)
    {
        // Move the completed float amounts into their immutable template directly.
        // No temporary five-channel cell array, reconstruction or second dense copy.
        if (!TryApplyContinuousInitialContamination())
        {
            ContaminationPagedCells.InitializeUniform(StorageCellCount, InitialCell);
            UE_LOG(LogTemp, Warning, TEXT("%s: procedural initialization failed; retaining uniform initial state."), *GetName());
        }
    }
    else
    {
        // Procedural generation remains atomic and uses the original world-space inputs.
        // Only the exact completed content is interned; nearby transforms are never conflated.
        ContaminationCells.Init(InitialCell, StorageCellCount);
        if (ContaminationGeneration.bProcedural && !TryApplyProceduralInitialContamination(
                CellsPerFace, SafeInitialContamination))
        {
            UE_LOG(LogTemp, Warning, TEXT("%s: procedural initial contamination failed; retaining the uniform initial state."), *GetName());
        }
        if (ContaminationCellLayout)
        {
            ContaminationPagedCells.InitializeFromCells(MoveTemp(ContaminationCells));
            ContaminationCells.Empty();
            // The immutable initial template is also the exact original save/restore baseline.
            ContaminationGeneratedInitialAmounts.Empty();
        }
        else
        {
            ContaminationGeneratedInitialAmounts.Reserve(ContaminationCells.Num());
            for (const FMD_ScrapeSurfaceCell& Cell : ContaminationCells)
            {
                ContaminationGeneratedInitialAmounts.Add(Cell.InitialContamination);
            }
        }
    }

	// 저작/절차 생성이 끝난 바로 이 시점의 제거 가능량만 제품 환산 분모로
	// 고정한다. evaluator가 첫 긁기 때 CleanTolerance 이하를 0으로 정리하므로,
	// 그 찌꺼기는 carrier·Reward1·Load100 어느 쪽에도 포함하지 않는다.
	const float SafeCleanTolerance = FMath::IsFinite(
		ScrapeSettings.CleanTolerance)
		? FMath::Max(0.0f, ScrapeSettings.CleanTolerance)
		: 0.0f;
    if (PreparedInitialState.IsSet())
    {
        CanonicalInitialContaminationCarrierAmounts[0] = PreparedInitialState->CanonicalCarrier;
        CanonicalInitialContaminationCarrierAmounts[1] = 0.0;
    }
    else
    {
	for (int32 FaceSlot = 0; FaceSlot < 2; ++FaceSlot)
	{
		const EMD_ScrapeSurfaceFace Face = FaceSlot == 0
			? EMD_ScrapeSurfaceFace::Front
			: EMD_ScrapeSurfaceFace::Back;
		double CanonicalCarrierAmount = 0.0;
		const FMD_ScrapeConstCellView FaceCells = GetContaminationCellsForFace(Face);
        // ActiveToAtlas is built in ascending atlas order. Skipping inactive zero
        // cells preserves the original double accumulation order and exact result.
        const int32 VisitCount = ContaminationCellLayout
            ? (FaceSlot == 0 ? StorageCellCount : 0) : FaceCells.Num();
        for (int32 VisitIndex = 0; VisitIndex < VisitCount; ++VisitIndex)
        {
            const int32 LocalIndex = ContaminationCellLayout
                ? ContaminationCellLayout->ActiveToAtlas[VisitIndex] : VisitIndex;
            const FMD_ScrapeSurfaceCell Cell = ContaminationCellLayout
                ? ContaminationPagedCells.Read(VisitIndex) : FaceCells[LocalIndex];
			const double SafeRemainingContamination =
				FMath::IsFinite(Cell.RemainingContamination)
				? FMath::Clamp(
					static_cast<double>(Cell.RemainingContamination),
					0.0,
					1.0)
				: 0.0;
			if (SafeRemainingContamination
				> static_cast<double>(SafeCleanTolerance))
			{
				CanonicalCarrierAmount += SafeRemainingContamination * GetContaminationCellCarrierWeight(
                    FaceSlot * CellsPerFace + LocalIndex);
			}
		}
		CanonicalInitialContaminationCarrierAmounts[FaceSlot] =
			CanonicalCarrierAmount;
	}

    }

	if ((!GetWorld() || GetWorld()->GetNetMode() != NM_DedicatedServer) && !InitializeContaminationMask())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("MD_CuttableWall::InitializeContaminationSurface - %s의 런타임 G8 오염 마스크를 만들지 못했습니다."),
			*GetName());
	}

    if (PreparedInitialState.IsSet())
    {
        CommitContaminationMeshUVProgressCache(PreparedInitialState->Progress);
        CachedBackInitialContaminationAmount = CachedBackCurrentContaminationAmount = 0.0;
        CachedBackContaminationCleanRatio = FMD_ScrapeEvaluator::CalculateCurrentCleanRatio(0.0, 0.0);
    }
    else RefreshContaminationCleanRatioCache();
	if (HasAuthority())
	{
		// 저작된 초기 오염은 오염된 face 하나당 정확히 1.0 unit을 한 번
		// 발행한다. clean face는 lot 자체가 없으므로 보상도 0이다.
		for (int32 FaceSlot = 0; FaceSlot < 2; ++FaceSlot)
		{
			const EMD_ScrapeSurfaceFace Face = FaceSlot == 0
				? EMD_ScrapeSurfaceFace::Front
				: EMD_ScrapeSurfaceFace::Back;
			const double InitialCarrierAmount =
				CanonicalInitialContaminationCarrierAmounts[FaceSlot];
			if (InitialCarrierAmount <= 0.0)
			{
				continue;
			}

			FMD_ScrapeRecoveryLot CanonicalLot;
			CanonicalLot.SourceId = BuildCanonicalContaminationSourceId(
				this,
				Face);
			CanonicalLot.RewardClass = ContaminationRewardClass;
			CanonicalLot.CarrierAmount = InitialCarrierAmount;
			CanonicalLot.RewardUnits = 1.0;
			const bool bSeeded = FMD_ScrapeRecoveryLedger::AppendLot(
				BaseContaminationRecoveryLots[FaceSlot],
				CanonicalLot);
			ensureMsgf(
				bSeeded,
				TEXT("Canonical contamination recovery lot could not be seeded."));
		}
	}
	bContaminationSurfaceInitialized = true;
	if (HasAuthority() && UsesMeshUVSurface() && GetWorld())
	{
		if (UMD_ScrapeMeshUVSyncSubsystem* Sync =
			GetWorld()->GetSubsystem<UMD_ScrapeMeshUVSyncSubsystem>())
		{
			Sync->InvalidateSurface(*this);
		}
	}

	// 초기 완료 상태의 중복 방송 방지
	bContaminationCompletionBroadcast =
		IsContaminationFullyRemoved();
	ConfigureContaminationDecals();
}


bool UMD_ScrapeSurfaceComponent::TryApplyProceduralInitialContamination(
	const int32 CellsPerFace,
	const float BaseInitialContamination)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scrape_InitialNoise);
    if (ContaminationGeneration.Version >= 2) return TryApplyContinuousInitialContamination();
    const bool Frozen = ContaminationGeneration.IsValid();
    const float NoiseScale = Frozen ? ContaminationGeneration.NoiseScaleCm : ContaminationNoiseScaleCm;
    const int32 NoiseSeed = Frozen ? ContaminationGeneration.Seed : ContaminationNoiseSeed;
    const float Coverage = Frozen ? ContaminationGeneration.Coverage : ContaminationCoverage;
    const float Minimum = Frozen ? ContaminationGeneration.MinimumAmount : ContaminationMinimumAmount;
    const FTransform GenerationTransform = Frozen ? ContaminationGeneration.SampleTransform
        : (SurfaceMesh ? SurfaceMesh->GetComponentTransform() : FTransform::Identity);
    if (UsesMeshUVSurface())
    {
        if (!ContaminationMeshUVData || !SurfaceMesh) { return false; }
        TArray<float> Amounts;
        Amounts.Init(0.0f, ContaminationCells.Num());
        for (int32 Index = 0; Index < ContaminationCells.Num(); ++Index)
        {
            const int32 AtlasId = ContaminationCellLayout ? ContaminationCellLayout->ActiveToAtlas[Index] : Index;
            const auto& Cell = ContaminationMeshUVData->Cells[AtlasId];
            if (Cell.IsActive() && !FMD_ContaminationNoise::TryCalculateInitialContamination(
                    GenerationTransform.TransformPosition(Cell.PositionLocal),
                    BaseInitialContamination, NoiseScale, NoiseSeed,
                    Coverage, Minimum, Amounts[Index]))
            { return false; }
        }
        for (int32 Index = 0; Index < Amounts.Num(); ++Index)
        {
            ContaminationCells[Index].InitialContamination = Amounts[Index];
            ContaminationCells[Index].RemainingContamination = Amounts[Index];
        }
        // Keep the all-or-nothing generation buffer as the per-wall exact save baseline.
        ContaminationGeneratedInitialAmounts = MoveTemp(Amounts);
        return true;
    }

	if (!SurfaceMesh
		|| CellsPerFace <= 0
		|| ContaminationCells.Num()
			!= CellsPerFace * FMD_ScrapeSurfaceLayout::FaceCount
		|| ContaminationGridDimensions.X <= 0
		|| ContaminationGridDimensions.Y <= 0
		|| ResolvedContaminationSurfaceSizeCm.X <= UE_SMALL_NUMBER
		|| ResolvedContaminationSurfaceSizeCm.Y <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const FVector2D CellSizeCm(
		ResolvedContaminationSurfaceSizeCm.X
			/ static_cast<double>(ContaminationGridDimensions.X),
		ResolvedContaminationSurfaceSizeCm.Y
			/ static_cast<double>(ContaminationGridDimensions.Y));
	const FVector2D SurfaceMinimumCm =
		ResolvedContaminationSurfaceSizeCm * -0.5;
	TArray<float> InitialAmounts;
	InitialAmounts.SetNumUninitialized(ContaminationCells.Num());

	for (int32 FaceIndex = 0;
		FaceIndex < FMD_ScrapeSurfaceLayout::FaceCount;
		++FaceIndex)
	{
		const EMD_ScrapeSurfaceFace Face = FaceIndex == 0
			? EMD_ScrapeSurfaceFace::Front
			: EMD_ScrapeSurfaceFace::Back;

		// MeshUV는 unwrap 하나가 앞뒤를 모두 담으므로 아틀라스 앞면 절반만 쓴다.
		// 뒷면 절반을 오염으로 채우면 아무도 닿을 수 없는 셀이 남아 청소 완료가
		// 영원히 뜨지 않는다. 주소 공간에서 빠진 셀은 처음부터 깨끗해야 한다.
		if (Face == EMD_ScrapeSurfaceFace::Back
			&& ContaminationSurfaceMapping.Mode
				== EMD_ScrapeSurfaceMappingMode::MeshUV)
		{
			for (int32 LocalCellIndex = 0;
				LocalCellIndex < CellsPerFace;
				++LocalCellIndex)
			{
				const int32 CombinedCellIndex =
					FMD_ScrapeSurfaceLayout::ToCombinedCellIndex(
						Face,
						LocalCellIndex,
						CellsPerFace);
				if (!InitialAmounts.IsValidIndex(CombinedCellIndex))
				{
					return false;
				}
				InitialAmounts[CombinedCellIndex] = 0.0f;
			}
			continue;
		}

		const int32 FaceSeed = NoiseSeed
			+ FaceIndex * 1013;
		for (int32 LocalCellIndex = 0;
			LocalCellIndex < CellsPerFace;
			++LocalCellIndex)
		{
			const int32 CellX =
				LocalCellIndex % ContaminationGridDimensions.X;
			const int32 CellY =
				LocalCellIndex / ContaminationGridDimensions.X;
			const FVector2D SurfacePositionCm = SurfaceMinimumCm
				+ FVector2D(
					(static_cast<double>(CellX) + 0.5) * CellSizeCm.X,
					(static_cast<double>(CellY) + 0.5) * CellSizeCm.Y);
			FVector WorldPosition = FVector::ZeroVector;
			float InitialAmount = 0.0f;
			if (!FMD_ScrapeSurfaceGeometry::TryCalculateSurfaceWorldPosition(
					SurfacePositionCm,
					Face,
					GenerationTransform,
					ResolvedContaminationSurfaceNormalAxis,
					ContaminationSurfaceCenterLocal,
					ContaminationMeshLocalBoundsMinimum,
					ContaminationMeshLocalBoundsMaximum,
					ContaminationSurfaceMapping,
					ResolvedContaminationSurfaceSizeCm,
					WorldPosition)
				|| !FMD_ContaminationNoise::TryCalculateInitialContamination(
					WorldPosition,
					BaseInitialContamination,
					NoiseScale,
					FaceSeed,
					Coverage,
					Minimum,
					InitialAmount))
			{
				return false;
			}

			const int32 CombinedCellIndex =
				FMD_ScrapeSurfaceLayout::ToCombinedCellIndex(
					Face,
					LocalCellIndex,
					CellsPerFace);
			if (!InitialAmounts.IsValidIndex(CombinedCellIndex))
			{
				return false;
			}
			InitialAmounts[CombinedCellIndex] = InitialAmount;
		}
	}

	for (int32 CellIndex = 0;
		CellIndex < ContaminationCells.Num();
		++CellIndex)
	{
		ContaminationCells[CellIndex].InitialContamination =
			InitialAmounts[CellIndex];
		ContaminationCells[CellIndex].RemainingContamination =
			InitialAmounts[CellIndex];
	}
	return true;
}


bool UMD_ScrapeSurfaceComponent::TryProjectWorldPointToContaminationSurface(
	const FVector& WorldPoint,
	const EMD_ScrapeSurfaceFace SurfaceFace,
	FVector2D& OutSurfacePositionCm) const
{
	OutSurfacePositionCm = FVector2D::ZeroVector;

	if (!bContaminationEnabled
		|| !bContaminationSurfaceInitialized
		|| !SurfaceMesh
		|| WorldPoint.ContainsNaN())
	{
		return false;
	}

    if (UsesMeshUVSurface())
    {
        FMD_ScrapeMeshUVSample Sample;
        return SurfaceFace == EMD_ScrapeSurfaceFace::Front && ContaminationMeshUVData
            && FMD_ScrapeMeshUVGeometry::FindNearestSource(*ContaminationMeshUVData,
                SurfaceMesh->GetComponentTransform().InverseTransformPosition(WorldPoint),
                FVector::ZeroVector, 0.25, Sample, -1.0)
            && FMD_ScrapeSurfaceGeometry::TryConvertMeshUVToSurfacePositionCm(
                Sample.CanonicalUV, ResolvedContaminationSurfaceSizeCm, OutSurfacePositionCm);
    }

	return FMD_ScrapeSurfaceGeometry::TryProjectWorldPositionToSurface(
		WorldPoint,
		SurfaceMesh->GetComponentTransform(),
		ResolvedContaminationSurfaceNormalAxis,
		SurfaceFace,
		ContaminationSurfaceCenterLocal,
		ContaminationMeshLocalBoundsMinimum,
		ContaminationMeshLocalBoundsMaximum,
		ContaminationSurfaceMapping,
		ResolvedContaminationSurfaceSizeCm,
		OutSurfacePositionCm);
}


bool UMD_ScrapeSurfaceComponent::TryGetContaminationFaceFromHit(
	const FHitResult& Hit,
	EMD_ScrapeSurfaceFace& OutSurfaceFace) const
{
	OutSurfaceFace = EMD_ScrapeSurfaceFace::Front;
	if (!bContaminationEnabled
		|| !bContaminationSurfaceInitialized
		|| !SurfaceMesh
		|| !Hit.bBlockingHit
		|| Hit.GetActor() != GetOwner()
		|| Hit.GetComponent() != SurfaceMesh.Get()
		|| Hit.ImpactPoint.ContainsNaN()
		|| Hit.ImpactNormal.ContainsNaN())
	{
		return false;
	}

	return TryResolveContaminationFace(
		Hit.ImpactPoint,
		Hit.ImpactNormal,
		OutSurfaceFace);
}


bool UMD_ScrapeSurfaceComponent::TryResolveContaminationFace(
	const FVector& WorldPosition,
	const FVector& WorldNormal,
	EMD_ScrapeSurfaceFace& OutSurfaceFace) const
{
	OutSurfaceFace = EMD_ScrapeSurfaceFace::Front;
	if (!bContaminationEnabled
		|| !bContaminationSurfaceInitialized
		|| !SurfaceMesh
		|| WorldPosition.ContainsNaN()
		|| WorldNormal.ContainsNaN())
	{
		return false;
	}

	const float MinimumNormalDot = bResolvedFullCoverageContaminationDecals
		? MinimumFullCoverageFaceNormalDot
		: MinimumContaminationFaceNormalDot;
	if (FMD_ScrapeSurfaceGeometry::TryResolveSurfaceFaceFromImpactNormal(
			WorldPosition,
			WorldNormal,
			SurfaceMesh->GetComponentTransform(),
			ResolvedContaminationSurfaceNormalAxis,
			ContaminationSurfaceMapping,
			MinimumNormalDot,
			OutSurfaceFace))
	{
		return true;
	}

	// Full coverage still needs a deterministic owner for tangent trims where
	// the receiver normal cannot distinguish the semantic front and back.
	return bResolvedFullCoverageContaminationDecals
		&& TryResolveContaminationFaceFromWorldPosition(
			WorldPosition,
			OutSurfaceFace);
}


bool UMD_ScrapeSurfaceComponent::
TryResolveContaminationFaceFromWorldPosition(
	const FVector& WorldPosition,
	EMD_ScrapeSurfaceFace& OutSurfaceFace) const
{
	OutSurfaceFace = EMD_ScrapeSurfaceFace::Front;
	if (!bContaminationEnabled
		|| !bContaminationSurfaceInitialized
		|| !SurfaceMesh
		|| WorldPosition.ContainsNaN()
		|| !FMath::IsFinite(
			ResolvedContaminationFaceSplitCoordinateLocal))
	{
		return false;
	}

	if (ContaminationSurfaceMapping.Mode
		== EMD_ScrapeSurfaceMappingMode::CurvedFrontPlanarBack)
	{
		FVector SplitOriginWorld;
		FVector FrontHalfNormalWorld;
		if (!FMD_ScrapeSurfaceGeometry::
			TryResolveCurvedFrontPlanarBackSplitPlane(
				SurfaceMesh->GetComponentTransform(),
				ContaminationSurfaceMapping,
				SplitOriginWorld,
				FrontHalfNormalWorld))
		{
			return false;
		}

		OutSurfaceFace = FVector::DotProduct(
			WorldPosition - SplitOriginWorld,
			FrontHalfNormalWorld) >= 0.0
			? EMD_ScrapeSurfaceFace::Front
			: EMD_ScrapeSurfaceFace::Back;
		return true;
	}

	if (ContaminationSurfaceMapping.Mode
		== EMD_ScrapeSurfaceMappingMode::CylindricalArc)
	{
		FMD_ScrapeCylindricalSurfaceFrame Frame;
		if (!FMD_ScrapeSurfaceGeometry::TryResolveCylindricalSurfaceFrame(
				SurfaceMesh->GetComponentTransform(),
				ContaminationSurfaceMapping,
				Frame))
		{
			return false;
		}

		const FVector CurveDelta = WorldPosition - Frame.CenterWorld;
		const double AxisDistance = FVector::DotProduct(
			CurveDelta,
			Frame.AxisWorld);
		const double RadialDistance = (
			CurveDelta - Frame.AxisWorld * AxisDistance).Size();
		if (!FMath::IsFinite(RadialDistance))
		{
			return false;
		}

		const bool bPositionIsOuter = RadialDistance >= Frame.RadiusCm;
		OutSurfaceFace = bPositionIsOuter == Frame.bFrontIsOuter
			? EMD_ScrapeSurfaceFace::Front
			: EMD_ScrapeSurfaceFace::Back;
		return true;
	}

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

	const FVector LocalPosition = SurfaceMesh->GetComponentTransform()
		.InverseTransformPosition(WorldPosition);
	const double FaceCoordinate = FVector::DotProduct(
		LocalPosition,
		FrontNormalLocal);
	if (!FMath::IsFinite(FaceCoordinate))
	{
		return false;
	}

	// The equality case belongs to Front, giving the shared split plane one
	// deterministic owner on both authority and clients.
	OutSurfaceFace = FaceCoordinate
		>= ResolvedContaminationFaceSplitCoordinateLocal
		? EMD_ScrapeSurfaceFace::Front
		: EMD_ScrapeSurfaceFace::Back;
	return true;
}
