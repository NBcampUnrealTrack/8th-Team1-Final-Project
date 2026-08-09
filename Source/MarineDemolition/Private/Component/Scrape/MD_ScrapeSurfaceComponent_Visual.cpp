#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"

#include "TimerManager.h"
#include "Components/DecalComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Gameplay/Scrape/MD_ContaminationNoise.h"
#include "Gameplay/Scrape/MD_ScrapeDecalLayout.h"
#include "Gameplay/Scrape/MD_ScrapeEvaluator.h"
#include "Gameplay/Scrape/MD_ScrapeFaceProgress.h"
#include "Gameplay/Scrape/MD_ScrapeMaskAtlas.h"
#include "Gameplay/Scrape/MD_ScrapeMeshSurfaceProfile.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceGeometry.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceLayout.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "Net/UnrealNetwork.h"
#include "RHITypes.h"
#include "Stats/Stats.h"
#include "World/Contamination/MD_Barnacle.h"
#include "World/Contamination/MD_ContaminationItem.h"
#include "World/Sector/MD_Sector.h"
#include "World/Wall/MD_WallPiece.h"
#include "Component/Scrape/MD_ScrapeSurfaceComponentInternal.h"

// 파일 분할 기준은 MD_ScrapeSurfaceComponentInternal.h 표 참고.
// 이 파일: G8 마스크 · 앞뒤 데칼 · 셀 오버레이

using namespace MD_ScrapeSurfaceComponentInternal;

bool UMD_ScrapeSurfaceComponent::InitializeContaminationMask()
{
	ReleaseContaminationMask();

	FIntPoint AtlasDimensions = FIntPoint::ZeroValue;
	TArray<uint8> AtlasPixels;
	if (!FMD_ScrapeMaskAtlas::TryCalculateAtlasDimensions(
			ContaminationGridDimensions,
			AtlasDimensions)
		|| !FMD_ScrapeMaskAtlas::TryBuildAtlasBuffer(
			ContaminationGridDimensions,
			MakeArrayView(ContaminationCells),
			AtlasPixels))
	{
		return false;
	}

	UTexture2D* NewMaskTexture = UTexture2D::CreateTransient(
		AtlasDimensions.X,
		AtlasDimensions.Y,
		PF_G8);
	if (!NewMaskTexture)
	{
		return false;
	}

	NewMaskTexture->SRGB = false;
	NewMaskTexture->NeverStream = true;
	NewMaskTexture->Filter = TF_Bilinear;
	NewMaskTexture->AddressX = TA_Clamp;
	NewMaskTexture->AddressY = TA_Clamp;

	FTexturePlatformData* PlatformData = NewMaskTexture->GetPlatformData();
	if (!PlatformData || PlatformData->Mips.IsEmpty())
	{
		NewMaskTexture->ReleaseResource();
		return false;
	}

	FTexture2DMipMap& Mip = PlatformData->Mips[0];
	Mip.BulkData.Lock(LOCK_READ_WRITE);
	void* DestinationData = Mip.BulkData.Realloc(AtlasPixels.Num());
	if (DestinationData)
	{
		FMemory::Memcpy(
			DestinationData,
			AtlasPixels.GetData(),
			AtlasPixels.Num());
	}
	Mip.BulkData.Unlock();

	if (!DestinationData)
	{
		NewMaskTexture->ReleaseResource();
		return false;
	}

	ContaminationMaskAtlasDimensions = AtlasDimensions;
	ContaminationMaskAtlasPixels = MoveTemp(AtlasPixels);
	ContaminationMaskTexture = NewMaskTexture;
	ContaminationMaskTexture->UpdateResource();
	RefreshContaminationDecalMaterialParameters();
	return true;
}


void UMD_ScrapeSurfaceComponent::ReleaseContaminationMask()
{
	if (ContaminationMaskTexture)
	{
		ContaminationMaskTexture->ReleaseResource();
		ContaminationMaskTexture = nullptr;
	}
	RefreshContaminationDecalMaterialParameters();

	ContaminationMaskAtlasPixels.Reset();
	ContaminationMaskAtlasDimensions = FIntPoint::ZeroValue;
}


void UMD_ScrapeSurfaceComponent::UpdateContaminationMask(
	const TArray<FMD_ScrapeCellChange>& CellChanges)
{
	SCOPE_CYCLE_COUNTER(STAT_MD_ScrapeMaskUpdate);

	if (CellChanges.IsEmpty())
	{
		return;
	}

	FIntPoint ExpectedDimensions = FIntPoint::ZeroValue;
	if (!FMD_ScrapeMaskAtlas::TryCalculateAtlasDimensions(
			ContaminationGridDimensions,
			ExpectedDimensions))
	{
		return;
	}

	const int32 ExpectedPixelCount =
		ExpectedDimensions.X * ExpectedDimensions.Y;
	if (!ContaminationMaskTexture
		|| ContaminationMaskAtlasDimensions != ExpectedDimensions
		|| ContaminationMaskAtlasPixels.Num() != ExpectedPixelCount
		|| !ContaminationMaskTexture->GetResource())
	{
		InitializeContaminationMask();
		return;
	}

	// 값이 실제로 달라진 픽셀만 감싸는 최소 사각형을 모은다.
	FIntRect ChangedRegion = FMD_ScrapeMaskAtlas::MakeEmptyAtlasRegion();
	for (const FMD_ScrapeCellChange& CellChange : CellChanges)
	{
		if (!ContaminationCells.IsValidIndex(CellChange.CellIndex))
		{
			continue;
		}

		int32 PixelIndex = INDEX_NONE;
		if (!FMD_ScrapeMaskAtlas::TryGetAtlasPixelIndexFromCombinedCellIndex(
				ContaminationGridDimensions,
				CellChange.CellIndex,
				PixelIndex)
			|| !ContaminationMaskAtlasPixels.IsValidIndex(PixelIndex))
		{
			continue;
		}

		const uint8 PreviousMaskValue =
			ContaminationMaskAtlasPixels[PixelIndex];
		FIntPoint PixelCoordinate = FIntPoint::NoneValue;
		if (!FMD_ScrapeMaskAtlas::TryUpdateAtlasPixel(
				ContaminationGridDimensions,
				CellChange.CellIndex,
				ContaminationCells[CellChange.CellIndex],
				ContaminationMaskAtlasPixels,
				PixelCoordinate)
			|| PreviousMaskValue
				== ContaminationMaskAtlasPixels[PixelIndex])
		{
			continue;
		}

		FMD_ScrapeMaskAtlas::ExpandAtlasRegion(
			ChangedRegion,
			PixelCoordinate);
	}

	if (FMD_ScrapeMaskAtlas::IsAtlasRegionEmpty(ChangedRegion))
	{
		// 스트로크가 이미 깨끗한 곳만 지나면 올릴 픽셀이 없다.
		return;
	}

	if (!UploadContaminationMaskRegion(ChangedRegion))
	{
		// 렌더 리소스가 잠시 유효하지 않다면 현재 CPU 전체 상태로 다시 만든다.
		InitializeContaminationMask();
	}
}


bool UMD_ScrapeSurfaceComponent::UploadContaminationMaskRegion(
	const FIntRect& AtlasRegion)
{
	const int32 RegionWidth = AtlasRegion.Width();
	const int32 RegionHeight = AtlasRegion.Height();
	if (!ContaminationMaskTexture
		|| !ContaminationMaskTexture->GetResource()
		|| RegionWidth <= 0
		|| RegionHeight <= 0)
	{
		return false;
	}

	// 사각형에 걸친 행만 빈틈없이 모아 RHI 업로드 한 번으로 끝낸다.
	uint8* UploadValues = new uint8[RegionWidth * RegionHeight];
	if (!FMD_ScrapeMaskAtlas::TryPackAtlasRegion(
			ContaminationMaskAtlasDimensions,
			MakeArrayView(ContaminationMaskAtlasPixels),
			AtlasRegion,
			MakeArrayView(UploadValues, RegionWidth * RegionHeight)))
	{
		delete[] UploadValues;
		return false;
	}

	FUpdateTextureRegion2D* UpdateRegions = new FUpdateTextureRegion2D[1];
	UpdateRegions[0] = FUpdateTextureRegion2D(
		static_cast<uint32>(AtlasRegion.Min.X),
		static_cast<uint32>(AtlasRegion.Min.Y),
		0,
		0,
		static_cast<uint32>(RegionWidth),
		static_cast<uint32>(RegionHeight));

	ContaminationMaskTexture->UpdateTextureRegions(
		0,
		1,
		UpdateRegions,
		static_cast<uint32>(RegionWidth),
		1,
		UploadValues,
		[](uint8* SourceData, const FUpdateTextureRegion2D* Regions)
		{
			delete[] SourceData;
			delete[] Regions;
		});
	return true;
}


bool UMD_ScrapeSurfaceComponent::ConfigureContaminationDecals()
{
	bContaminationDecalsConfigured = false;
	SetContaminationDecalsVisible(false);

	if (!bShowContaminationDecals
		|| !bContaminationSurfaceInitialized
		|| !ContaminationMaskTexture
		|| !FrontContaminationDecal
		|| !BackContaminationDecal
		|| !ContaminationDecalMaterial)
	{
		return false;
	}

	const UMaterial* BaseMaterial = ContaminationDecalMaterial->GetMaterial();
	if (!BaseMaterial || BaseMaterial->MaterialDomain != MD_DeferredDecal)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("MD_CuttableWall::ConfigureContaminationDecals - %s의 ContaminationDecalMaterial은 Deferred Decal Domain이어야 합니다."),
			*GetName());
		return false;
	}

	// 두 면의 투영 깊이는 서로 영향을 주므로 배치 전에 함께 계산한다.
	ResolveContaminationDecalDepths();

	const bool bConfiguredFront = ConfigureContaminationDecalForFace(
		FrontContaminationDecal,
		EMD_ScrapeSurfaceFace::Front,
		FrontContaminationDecalMID);
	const bool bConfiguredBack = ConfigureContaminationDecalForFace(
		BackContaminationDecal,
		EMD_ScrapeSurfaceFace::Back,
		BackContaminationDecalMID);
	if (!bConfiguredFront || !bConfiguredBack)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("MD_CuttableWall::ConfigureContaminationDecals - %s의 앞/뒤 Decal 배치에 필요한 표면 크기 또는 벽 두께가 올바르지 않습니다."),
			*GetName());
		return false;
	}

	bContaminationDecalsConfigured = true;
	SetContaminationDecalsVisible(true);
	if (ContaminationOverlay)
	{
		ContaminationOverlay->SetVisibility(false);
	}
	return true;
}


void UMD_ScrapeSurfaceComponent::ResolveContaminationDecalDepths()
{
	const double RequestedDepthCm = FMath::IsFinite(
		ContaminationDecalProjectionDepthCm)
		? FMath::Max(
			static_cast<double>(ContaminationDecalProjectionDepthCm),
			0.0)
		: 0.0;
	const double FitMarginCm = FMath::IsFinite(
		ContaminationDecalDepthFitMarginCm)
		? FMath::Max(
			static_cast<double>(ContaminationDecalDepthFitMarginCm),
			0.0)
		: 0.0;

	double FrontDepthCm = RequestedDepthCm;
	double BackDepthCm = RequestedDepthCm;

	// 앞/뒤 축 방향의 실제 벽 두께. 축을 못 구하면 0으로 두고 두께 배분을 건너뛴다.
	double WallThicknessCm = 0.0;
	FVector HorizontalAxisLocal;
	FVector FrontNormalLocal;
	FVector VerticalAxisLocal;
	if (SurfaceMesh
		&& FMD_ScrapeSurfaceGeometry::TryGetSurfaceLocalAxes(
			ResolvedContaminationSurfaceNormalAxis,
			HorizontalAxisLocal,
			FrontNormalLocal,
			VerticalAxisLocal))
	{
		const FVector LocalBoundsSize = ContaminationMeshLocalBoundsMaximum
			- ContaminationMeshLocalBoundsMinimum;
		WallThicknessCm =
			FMath::Abs(FVector::DotProduct(LocalBoundsSize, FrontNormalLocal))
			* FMath::Abs(FVector::DotProduct(
				SurfaceMesh->GetComponentScale().GetAbs(),
				FrontNormalLocal));
		if (!FMath::IsFinite(WallThicknessCm))
		{
			WallThicknessCm = 0.0;
		}
	}

	// 메시 형상에서 각 면이 실제로 필요로 하는 깊이를 구한다.
	if (bAutoFitContaminationDecalDepth && SurfaceMesh)
	{
		const UStaticMesh* MeshAsset = SurfaceMesh->GetStaticMesh();
		const FVector AbsoluteScale = SurfaceMesh->GetComponentScale().GetAbs();
		auto FitFaceDepth = [&](const EMD_ScrapeSurfaceFace Face)
		{
			FMD_ScrapeFaceSurfaceProfile Profile;
			if (FMD_ScrapeMeshSurfaceProfile::TryMeasureFaceProfile(
					MeshAsset,
					Face,
					ResolvedContaminationSurfaceNormalAxis,
					AbsoluteScale,
					ContaminationGridDimensions,
					ResolvedContaminationSurfaceSizeCm,
					MinimumContaminationFaceNormalDot,
					Profile))
			{
				return FMD_ScrapeMeshSurfaceProfile::
					CalculateFittedProjectionDepthCm(
						Profile,
						RequestedDepthCm,
						FitMarginCm);
			}

			// CPU 정점 데이터를 읽을 수 없는 메시는 형상을 알 수 없다.
			// 이때는 벽 두께의 절반을 덮어 어떤 형상이든 자기 쪽 절반을 놓치지 않게 한다.
			return WallThicknessCm > 0.0
				? FMath::Max(RequestedDepthCm, WallThicknessCm * 0.5)
				: RequestedDepthCm;
		};

		FrontDepthCm = FitFaceDepth(EMD_ScrapeSurfaceFace::Front);
		BackDepthCm = FitFaceDepth(EMD_ScrapeSurfaceFace::Back);
	}

	// 앞/뒤 상자가 겹쳐 같은 표면을 두 번 칠하지 않도록 벽 두께를 나눈다.
	FMD_ScrapeMeshSurfaceProfile::BalanceFaceDepths(
		WallThicknessCm,
		FrontDepthCm,
		BackDepthCm);

	ResolvedFrontDecalDepthCm = static_cast<float>(FrontDepthCm);
	ResolvedBackDecalDepthCm = static_cast<float>(BackDepthCm);
}


bool UMD_ScrapeSurfaceComponent::ConfigureContaminationDecalForFace(
	UDecalComponent* DecalComponent,
	const EMD_ScrapeSurfaceFace Face,
	TObjectPtr<UMaterialInstanceDynamic>& InOutMID)
{
	if (!DecalComponent || !SurfaceMesh || !ContaminationDecalMaterial)
	{
		return false;
	}

	float& ResolvedDepthCm = Face == EMD_ScrapeSurfaceFace::Front
		? ResolvedFrontDecalDepthCm
		: ResolvedBackDecalDepthCm;
	FMD_ScrapeDecalFaceLayout Layout;
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
			Layout))
	{
		return false;
	}

	// 두께·표면 크기 제한까지 적용된 최종값을 에디터 Details에서 확인할 수 있게 남긴다.
	ResolvedDepthCm = static_cast<float>(Layout.AppliedInwardDepthCm);
	ResolvedContaminationDecalEdgeInsetCm =
		static_cast<float>(Layout.AppliedEdgeInsetCm);

	DecalComponent->SetAbsolute(false, false, true);
	DecalComponent->SetRelativeScale3D(FVector::OneVector);
	DecalComponent->SetRelativeLocationAndRotation(
		Layout.RelativeLocation,
		Layout.RelativeRotation);
	DecalComponent->DecalSize = Layout.DecalSize;
	DecalComponent->SetFadeScreenSize(0.0f);
	DecalComponent->SetSortOrder(ContaminationDecalSortOrder);

	if (!InOutMID || InOutMID->Parent != ContaminationDecalMaterial)
	{
		DecalComponent->SetDecalMaterial(ContaminationDecalMaterial);
		InOutMID = DecalComponent->CreateDynamicMaterialInstance();
	}
	if (!InOutMID)
	{
		return false;
	}

	FVector2D MaskHalfTexelUV = FVector2D::ZeroVector;
	if (!FMD_ScrapeDecalLayout::TryCalculateMaskHalfTexelUV(
			ContaminationMaskAtlasDimensions,
			ResolvedContaminationSurfaceSizeCm,
			ContaminationDecalEdgeInsetCm,
			MaskHalfTexelUV))
	{
		return false;
	}

	DecalComponent->SetDecalMaterial(InOutMID);
	InOutMID->SetTextureParameterValue(
		ContaminationMaskTextureParameterName,
		ContaminationMaskTexture);
	InOutMID->SetScalarParameterValue(
		ContaminationMaskVScaleParameterName,
		Layout.MaskVScale);
	InOutMID->SetScalarParameterValue(
		ContaminationMaskVOffsetParameterName,
		Layout.MaskVOffset);
	InOutMID->SetScalarParameterValue(
		ContaminationMaskUFlipParameterName,
		Layout.MaskUFlip);
	InOutMID->SetScalarParameterValue(
		ContaminationMaskHalfTexelUParameterName,
		MaskHalfTexelUV.X);
	InOutMID->SetScalarParameterValue(
		ContaminationMaskHalfTexelVParameterName,
		MaskHalfTexelUV.Y);
	DecalComponent->MarkRenderStateDirty();
	return true;
}


void UMD_ScrapeSurfaceComponent::SetContaminationDecalsVisible(
	const bool bVisible)
{
	if (FrontContaminationDecal)
	{
		FrontContaminationDecal->SetVisibility(bVisible);
		FrontContaminationDecal->SetHiddenInGame(!bVisible);
	}
	if (BackContaminationDecal)
	{
		BackContaminationDecal->SetVisibility(bVisible);
		BackContaminationDecal->SetHiddenInGame(!bVisible);
	}
}


void UMD_ScrapeSurfaceComponent::RefreshContaminationDecalMaterialParameters()
{
	// 배치 때와 같은 inset 보정을 써야 마스크를 다시 만들 때 무늬가 밀리지 않는다.
	FVector2D MaskHalfTexelUV = FVector2D::ZeroVector;
	if (!FMD_ScrapeDecalLayout::TryCalculateMaskHalfTexelUV(
			ContaminationMaskAtlasDimensions,
			ResolvedContaminationSurfaceSizeCm,
			ContaminationDecalEdgeInsetCm,
			MaskHalfTexelUV))
	{
		return;
	}

	auto RefreshMID = [this, MaskHalfTexelUV](
		UMaterialInstanceDynamic* DecalMID,
		const float VOffset,
		const float UFlip)
	{
		if (!DecalMID)
		{
			return;
		}

		DecalMID->SetTextureParameterValue(
			ContaminationMaskTextureParameterName,
			ContaminationMaskTexture);
		DecalMID->SetScalarParameterValue(
			ContaminationMaskVScaleParameterName,
			0.5f);
		DecalMID->SetScalarParameterValue(
			ContaminationMaskVOffsetParameterName,
			VOffset);
		DecalMID->SetScalarParameterValue(
			ContaminationMaskUFlipParameterName,
			UFlip);
		DecalMID->SetScalarParameterValue(
			ContaminationMaskHalfTexelUParameterName,
			MaskHalfTexelUV.X);
		DecalMID->SetScalarParameterValue(
			ContaminationMaskHalfTexelVParameterName,
			MaskHalfTexelUV.Y);
	};

	RefreshMID(FrontContaminationDecalMID, 0.0f, 1.0f);
	RefreshMID(BackContaminationDecalMID, 0.5f, 0.0f);
}


void UMD_ScrapeSurfaceComponent::InitializeContaminationVisualization()
{
	if (!ContaminationOverlay)
	{
		return;
	}

	ContaminationOverlay->ClearInstances();
	ContaminationCellBaseTransforms.Reset();
	ContaminationOverlay->SetVisibility(false);

	if (!bShowContaminationOverlay
		|| bContaminationDecalsConfigured
		|| !bContaminationSurfaceInitialized
		|| ContaminationCells.IsEmpty())
	{
		return;
	}

	UStaticMesh* OverlayMesh = ContaminationOverlay->GetStaticMesh();
	if (!OverlayMesh)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("MD_CuttableWall::InitializeContaminationVisualization - %s의 ContaminationOverlay 컴포넌트에 Static SurfaceMesh가 지정되지 않았습니다."),
			*GetName());
		return;
	}

	// SurfaceMesh와 Material은 Blueprint에서 지정
	FVector HorizontalAxisLocal;
	FVector FrontNormalLocal;
	FVector VerticalAxisLocal;
	if (!FMD_ScrapeSurfaceGeometry::TryGetSurfaceLocalAxes(
			ResolvedContaminationSurfaceNormalAxis,
			HorizontalAxisLocal,
			FrontNormalLocal,
			VerticalAxisLocal))
	{
		return;
	}

	const FVector AbsoluteScale = SurfaceMesh->GetComponentScale().GetAbs();
	const double SafeHorizontalScale = FMath::Max(
		FMath::Abs(FVector::DotProduct(
			AbsoluteScale,
			HorizontalAxisLocal)),
		static_cast<double>(UE_SMALL_NUMBER));
	const double SafeNormalScale = FMath::Max(
		FMath::Abs(FVector::DotProduct(
			AbsoluteScale,
			FrontNormalLocal)),
		static_cast<double>(UE_SMALL_NUMBER));
	const double SafeVerticalScale = FMath::Max(
		FMath::Abs(FVector::DotProduct(
			AbsoluteScale,
			VerticalAxisLocal)),
		static_cast<double>(UE_SMALL_NUMBER));

	// 월드 크기를 Overlay 로컬 단위로 변환
	const double SurfaceWidthLocal =
		static_cast<double>(ResolvedContaminationSurfaceSizeCm.X)
		/ SafeHorizontalScale;
	const double SurfaceHeightLocal =
		static_cast<double>(ResolvedContaminationSurfaceSizeCm.Y)
		/ SafeVerticalScale;
	const double CellWidthLocal =
		SurfaceWidthLocal
		/ static_cast<double>(ContaminationGridDimensions.X);
	const double CellHeightLocal =
		SurfaceHeightLocal
		/ static_cast<double>(ContaminationGridDimensions.Y);

	const FVector OverlayMeshSize =
		OverlayMesh->GetBounds().BoxExtent * 2.0f;
	if (OverlayMeshSize.X <= UE_SMALL_NUMBER
		|| OverlayMeshSize.Y <= UE_SMALL_NUMBER
		|| OverlayMeshSize.Z <= UE_SMALL_NUMBER)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("MD_CuttableWall::InitializeContaminationVisualization - %s의 오염막 단위 메시 Bounds가 올바르지 않습니다."),
			*GetName());
		return;
	}

	const double ThicknessLocal =
		static_cast<double>(ContaminationOverlayThicknessCm)
		/ SafeNormalScale;
	const double OffsetLocal =
		static_cast<double>(ContaminationOverlayOffsetCm)
		/ SafeNormalScale;
	const double FrontSurfaceCoordinate = FVector::DotProduct(
		ContaminationMeshLocalBoundsMaximum,
		FrontNormalLocal);
	const double CenterNormalCoordinate = FVector::DotProduct(
		ContaminationSurfaceCenterLocal,
		FrontNormalLocal);
	const FQuat CellRotation = FRotationMatrix::MakeFromXY(
		HorizontalAxisLocal,
		FrontNormalLocal).ToQuat();

	const int32 FrontCellCount =
		GetContaminationCellsForFace(EMD_ScrapeSurfaceFace::Front).Num();
	ContaminationCellBaseTransforms.Reserve(FrontCellCount);
	ContaminationOverlay->PreAllocateInstancesMemory(FrontCellCount);

	for (int32 CellY = 0; CellY < ContaminationGridDimensions.Y; ++CellY)
	{
		for (int32 CellX = 0; CellX < ContaminationGridDimensions.X; ++CellX)
		{
			const double HorizontalOffset = -SurfaceWidthLocal * 0.5
				+ (static_cast<double>(CellX) + 0.5) * CellWidthLocal;
			const double VerticalOffset = -SurfaceHeightLocal * 0.5
				+ (static_cast<double>(CellY) + 0.5) * CellHeightLocal;
			const double NormalOffset = FrontSurfaceCoordinate
				- CenterNormalCoordinate
				+ OffsetLocal
				+ ThicknessLocal * 0.5;
			const FVector CellLocation = ContaminationSurfaceCenterLocal
				+ HorizontalAxisLocal * HorizontalOffset
				+ FrontNormalLocal * NormalOffset
				+ VerticalAxisLocal * VerticalOffset;
			const FVector FullContaminationScale(
				CellWidthLocal / static_cast<double>(OverlayMeshSize.X),
				ThicknessLocal / static_cast<double>(OverlayMeshSize.Y),
				CellHeightLocal / static_cast<double>(OverlayMeshSize.Z));

			ContaminationCellBaseTransforms.Emplace(
				CellRotation,
				CellLocation,
				FullContaminationScale);

			const int32 CellIndex =
				CellX + (CellY * ContaminationGridDimensions.X);
			ContaminationOverlay->AddInstance(
				MakeContaminationCellVisualTransform(CellIndex));
		}
	}

	ContaminationOverlay->SetVisibility(true);
}


void UMD_ScrapeSurfaceComponent::UpdateContaminationVisualization(
	const TArray<FMD_ScrapeCellChange>& CellChanges)
{
	SCOPE_CYCLE_COUNTER(STAT_MD_ScrapeOverlayUpdate);

	// 데칼 구성에 성공하면 ISM 오버레이는 숨겨져 인스턴스도 비어 있다.
	// 매 틱 헛도는 루프를 아예 들어가지 않는다.
	if (!ContaminationOverlay
		|| bContaminationDecalsConfigured
		|| CellChanges.IsEmpty())
	{
		return;
	}

	bool bUpdatedAnyInstance = false;
	for (const FMD_ScrapeCellChange& CellChange : CellChanges)
	{
		// 기존 ISM 프로토타입은 앞면 N개만 표시한다. 뒷면은 데칼 Task에서 시각화한다.
		if (!ContaminationCellBaseTransforms.IsValidIndex(
				CellChange.CellIndex)
			|| !ContaminationCells.IsValidIndex(CellChange.CellIndex))
		{
			continue;
		}

		bUpdatedAnyInstance |= ContaminationOverlay->UpdateInstanceTransform(
			CellChange.CellIndex,
			MakeContaminationCellVisualTransform(CellChange.CellIndex),
			false,
			false,
			true);
	}

	if (bUpdatedAnyInstance)
	{
		ContaminationOverlay->MarkRenderStateDirty();
	}
}


FTransform UMD_ScrapeSurfaceComponent::MakeContaminationCellVisualTransform(
	const int32 CellIndex) const
{
	if (!ContaminationCellBaseTransforms.IsValidIndex(CellIndex)
		|| !ContaminationCells.IsValidIndex(CellIndex))
	{
		return FTransform::Identity;
	}

	FTransform VisualTransform = ContaminationCellBaseTransforms[CellIndex];
	const FMD_ScrapeSurfaceCell& Cell = ContaminationCells[CellIndex];

	const float RemainingRatio =
		Cell.InitialContamination > UE_SMALL_NUMBER
		? FMath::Clamp(
			Cell.RemainingContamination / Cell.InitialContamination,
			0.0f,
			1.0f)
		: 0.0f;

	if (RemainingRatio <= ScrapeSettings.CleanTolerance)
	{
		VisualTransform.SetScale3D(FVector::ZeroVector);
		return VisualTransform;
	}

	// 면적 비율 유지를 위해 X/Z에 제곱근 적용
	const float LinearScale = FMath::Sqrt(RemainingRatio);
	FVector VisualScale = VisualTransform.GetScale3D();
	VisualScale.X *= LinearScale;
	VisualScale.Z *= LinearScale;
	VisualTransform.SetScale3D(VisualScale);

	return VisualTransform;
}

