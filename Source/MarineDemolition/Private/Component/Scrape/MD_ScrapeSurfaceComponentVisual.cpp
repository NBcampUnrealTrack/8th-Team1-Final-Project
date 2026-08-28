#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"

#include "Components/DecalComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Component/Scrape/MD_ScrapeSurfaceComponentInternal.h"
#include "Gameplay/Scrape/MD_ScrapeDecalLayout.h"
#include "Gameplay/Scrape/MD_ScrapeMaskAtlas.h"
#include "Gameplay/Scrape/MD_ScrapeMeshSurfaceProfile.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceGeometry.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceLayout.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "RHITypes.h"

// 파일 분할 기준은 MD_ScrapeSurfaceComponentInternal.h 표 참고.
// 책임: G8 마스크 · 앞뒤 데칼 · 셀 오버레이

using namespace MD_ScrapeSurfaceComponentInternal;

namespace
{
	uint32 MixTransferredPaintHash(uint32 Value)
	{
		Value ^= Value >> 16;
		Value *= 0x7FEB352Du;
		Value ^= Value >> 15;
		Value *= 0x846CA68Bu;
		Value ^= Value >> 16;
		return Value;
	}

	double CalculateTransferredPaintGrainSample(
		const int32 GrainX,
		const int32 GrainY,
		const EMD_ScrapeSurfaceFace Face)
	{
		uint32 Hash = HashCombineFast(
			GetTypeHash(GrainX),
			GetTypeHash(GrainY));
		Hash = HashCombineFast(Hash, GetTypeHash(static_cast<uint8>(Face)));
		Hash = MixTransferredPaintHash(Hash ^ 0x51ED270Bu);
		return static_cast<double>(Hash & 0x00FFFFFFu) / 16777216.0;
	}
}

FMD_ScrapeSurfaceCell
UMD_ScrapeSurfaceComponent::MakeContaminationMaskCell(
	const int32 CellIndex) const
{
	// 합성 셀은 게임플레이 상태가 아니라 텍셀에 그대로 들어갈 불투명도 신호를
	// 담는다. 양자화가 InitialContamination으로 잘라내므로 1로 고정한다.
	FMD_ScrapeSurfaceCell MaskCell;
	MaskCell.InitialContamination = 1.0f;
	MaskCell.RemainingContamination = 0.0f;
	if (!ContaminationCells.IsValidIndex(CellIndex))
	{
		return MaskCell;
	}

	const FMD_ScrapeSurfaceCell& SourceCell = ContaminationCells[CellIndex];
	MaskCell.RemainingContamination =
		FMD_ScrapeMaskAtlas::CalculateVisualMaskValue(
			SourceCell.RemainingContamination,
			SourceCell.InitialContamination,
			SourceCell.CoveredFraction,
			ContaminationVisualFilmOpacityGamma);
	return MaskCell;
}

bool UMD_ScrapeSurfaceComponent::InitializeContaminationMask()
{
	ReleaseContaminationMask();
	if (!bContaminationEnabled)
	{
		return false;
	}

	FIntPoint AtlasDimensions = FIntPoint::ZeroValue;
	TArray<uint8> AtlasPixels;
	TArray<FMD_ScrapeSurfaceCell> MaskCells;
	MaskCells.Reserve(ContaminationCells.Num());
	for (int32 CellIndex = 0;
		CellIndex < ContaminationCells.Num();
		++CellIndex)
	{
		MaskCells.Add(MakeContaminationMaskCell(CellIndex));
	}

	if (!FMD_ScrapeMaskAtlas::TryCalculateAtlasDimensions(
			ContaminationGridDimensions,
			AtlasDimensions)
		|| !FMD_ScrapeMaskAtlas::TryBuildAtlasBuffer(
			ContaminationGridDimensions,
			MakeArrayView(MaskCells),
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
	if (!InitializeTransferredPaintMask())
	{
		ContaminationMaskTexture->ReleaseResource();
		ContaminationMaskTexture = nullptr;
		ContaminationMaskAtlasPixels.Reset();
		ContaminationMaskAtlasDimensions = FIntPoint::ZeroValue;
		return false;
	}
	RefreshContaminationDecalMaterialParameters();
	return true;
}


void UMD_ScrapeSurfaceComponent::ReleaseContaminationMask()
{
	ReleaseTransferredPaintMask();
	if (ContaminationMaskTexture)
	{
		ContaminationMaskTexture->ReleaseResource();
		ContaminationMaskTexture = nullptr;
	}
	RefreshContaminationDecalMaterialParameters();

	ContaminationMaskAtlasPixels.Reset();
	ContaminationMaskAtlasDimensions = FIntPoint::ZeroValue;
}


bool UMD_ScrapeSurfaceComponent::TryCalculateTransferredPaintMaskDimensions(
	FIntPoint& OutFaceDimensions,
	FIntPoint& OutAtlasDimensions) const
{
	OutFaceDimensions = FIntPoint::ZeroValue;
	OutAtlasDimensions = FIntPoint::ZeroValue;
	if (!FMath::IsFinite(TransferredPaintVisualTexelSizeCm)
		|| TransferredPaintVisualTexelSizeCm <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(ResolvedContaminationSurfaceSizeCm.X)
		|| !FMath::IsFinite(ResolvedContaminationSurfaceSizeCm.Y)
		|| ResolvedContaminationSurfaceSizeCm.X <= UE_SMALL_NUMBER
		|| ResolvedContaminationSurfaceSizeCm.Y <= UE_SMALL_NUMBER)
	{
		return false;
	}

	OutFaceDimensions.X = FMath::Clamp(
		FMath::Max(
			ContaminationGridDimensions.X,
			FMath::CeilToInt(
				ResolvedContaminationSurfaceSizeCm.X
					/ TransferredPaintVisualTexelSizeCm)),
		1,
		MaximumTransferredPaintMaskAxis);
	OutFaceDimensions.Y = FMath::Clamp(
		FMath::Max(
			ContaminationGridDimensions.Y,
			FMath::CeilToInt(
				ResolvedContaminationSurfaceSizeCm.Y
					/ TransferredPaintVisualTexelSizeCm)),
		1,
		MaximumTransferredPaintMaskAxis);
	OutAtlasDimensions = FIntPoint(
		OutFaceDimensions.X,
		OutFaceDimensions.Y * FMD_ScrapeSurfaceLayout::FaceCount);
	return OutAtlasDimensions.X > 0 && OutAtlasDimensions.Y > 0;
}


uint8 UMD_ScrapeSurfaceComponent::CalculateTransferredPaintMaskPixel(
	const EMD_ScrapeSurfaceFace Face,
	const FIntPoint& FacePixelCoordinate,
	const FIntPoint& FaceMaskDimensions) const
{
	if (FacePixelCoordinate.X < 0
		|| FacePixelCoordinate.Y < 0
		|| FacePixelCoordinate.X >= FaceMaskDimensions.X
		|| FacePixelCoordinate.Y >= FaceMaskDimensions.Y
		|| ContaminationGridDimensions.X <= 0
		|| ContaminationGridDimensions.Y <= 0)
	{
		return 0;
	}

	int32 CellsPerFace = 0;
	int32 TotalCellCount = 0;
	if (!FMD_ScrapeSurfaceLayout::TryCalculateCellCounts(
			ContaminationGridDimensions,
			CellsPerFace,
			TotalCellCount)
		|| ContaminationCells.Num() != TotalCellCount)
	{
		return 0;
	}

	const double U = (static_cast<double>(FacePixelCoordinate.X) + 0.5)
		/ static_cast<double>(FaceMaskDimensions.X);
	const double V = (static_cast<double>(FacePixelCoordinate.Y) + 0.5)
		/ static_cast<double>(FaceMaskDimensions.Y);
	const double GridX = U * ContaminationGridDimensions.X - 0.5;
	const double GridY = V * ContaminationGridDimensions.Y - 0.5;
	const int32 GridX0 = FMath::FloorToInt(GridX);
	const int32 GridY0 = FMath::FloorToInt(GridY);
	const double FractionX = GridX - GridX0;
	const double FractionY = GridY - GridY0;

	double PaintAmount = 0.0;
	double WeightedPatternDensity = 0.0;
	for (int32 OffsetY = 0; OffsetY <= 1; ++OffsetY)
	{
		const int32 CellY = FMath::Clamp(
			GridY0 + OffsetY,
			0,
			ContaminationGridDimensions.Y - 1);
		const double WeightY = OffsetY == 0
			? 1.0 - FractionY
			: FractionY;
		for (int32 OffsetX = 0; OffsetX <= 1; ++OffsetX)
		{
			const int32 CellX = FMath::Clamp(
				GridX0 + OffsetX,
				0,
				ContaminationGridDimensions.X - 1);
			const double WeightX = OffsetX == 0
				? 1.0 - FractionX
				: FractionX;
			const double Weight = WeightX * WeightY;
			const int32 CombinedCellIndex =
				FMD_ScrapeSurfaceLayout::ToCombinedCellIndex(
					Face,
					CellX + CellY * ContaminationGridDimensions.X,
					CellsPerFace);
			if (!ContaminationCells.IsValidIndex(CombinedCellIndex))
			{
				continue;
			}

			const FMD_ScrapeSurfaceCell& Cell =
				ContaminationCells[CombinedCellIndex];
			const double CellPaintAmount = FMath::Clamp(
				static_cast<double>(Cell.TransferredContamination),
				0.0,
				1.0);
			PaintAmount += CellPaintAmount * Weight;
			WeightedPatternDensity += CellPaintAmount
				* FMath::Clamp(
					static_cast<double>(
						Cell.TransferredPaintPatternDensity),
					0.0,
					1.0)
				* Weight;
		}
	}

	if (PaintAmount <= ScrapeSettings.CleanTolerance)
	{
		return 0;
	}

	const double PatternDensity = FMath::Clamp(
		WeightedPatternDensity / PaintAmount,
		0.0,
		1.0);
	static constexpr double PaintGrainSizeCm = 0.85;
	const int32 GrainX = FMath::FloorToInt(
		(U * ResolvedContaminationSurfaceSizeCm.X) / PaintGrainSizeCm);
	const int32 GrainY = FMath::FloorToInt(
		(V * ResolvedContaminationSurfaceSizeCm.Y) / PaintGrainSizeCm);
	if (CalculateTransferredPaintGrainSample(GrainX, GrainY, Face)
		>= PatternDensity)
	{
		return 0;
	}

	return static_cast<uint8>(FMath::Clamp(
		FMath::RoundToInt(PaintAmount * static_cast<double>(MAX_uint8)),
		0,
		static_cast<int32>(MAX_uint8)));
}


bool UMD_ScrapeSurfaceComponent::InitializeTransferredPaintMask()
{
	ReleaseTransferredPaintMask();

	FIntPoint FaceDimensions = FIntPoint::ZeroValue;
	FIntPoint AtlasDimensions = FIntPoint::ZeroValue;
	if (!TryCalculateTransferredPaintMaskDimensions(
			FaceDimensions,
			AtlasDimensions))
	{
		return false;
	}

	const int64 PixelCount64 = static_cast<int64>(AtlasDimensions.X)
		* static_cast<int64>(AtlasDimensions.Y);
	if (PixelCount64 <= 0 || PixelCount64 > MAX_int32)
	{
		return false;
	}

	TArray<uint8> AtlasPixels;
	AtlasPixels.SetNumZeroed(static_cast<int32>(PixelCount64));
	bool bHasTransferredPaint = false;
	for (const FMD_ScrapeSurfaceCell& Cell : ContaminationCells)
	{
		if (Cell.TransferredContamination > ScrapeSettings.CleanTolerance)
		{
			bHasTransferredPaint = true;
			break;
		}
	}
	if (bHasTransferredPaint)
	{
		for (int32 AtlasY = 0; AtlasY < AtlasDimensions.Y; ++AtlasY)
		{
			const EMD_ScrapeSurfaceFace Face = AtlasY < FaceDimensions.Y
				? EMD_ScrapeSurfaceFace::Front
				: EMD_ScrapeSurfaceFace::Back;
			const int32 FaceY = AtlasY % FaceDimensions.Y;
			for (int32 PixelX = 0; PixelX < FaceDimensions.X; ++PixelX)
			{
				AtlasPixels[PixelX + AtlasY * AtlasDimensions.X] =
					CalculateTransferredPaintMaskPixel(
						Face,
						FIntPoint(PixelX, FaceY),
						FaceDimensions);
			}
		}
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
	// Dry-brush holes must remain discrete; bilinear filtering turns the
	// stipple into a faint continuous decal at this sub-centimeter resolution.
	NewMaskTexture->Filter = TF_Nearest;
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

	TransferredPaintMaskAtlasDimensions = AtlasDimensions;
	TransferredPaintMaskAtlasPixels = MoveTemp(AtlasPixels);
	TransferredPaintMaskTexture = NewMaskTexture;
	TransferredPaintMaskTexture->UpdateResource();
	return true;
}


void UMD_ScrapeSurfaceComponent::ReleaseTransferredPaintMask()
{
	if (TransferredPaintMaskTexture)
	{
		TransferredPaintMaskTexture->ReleaseResource();
		TransferredPaintMaskTexture = nullptr;
	}
	TransferredPaintMaskAtlasPixels.Reset();
	TransferredPaintMaskAtlasDimensions = FIntPoint::ZeroValue;
}


void UMD_ScrapeSurfaceComponent::UpdateTransferredPaintMask(
	const TArray<FMD_ScrapeCellChange>& CellChanges)
{
	FIntPoint FaceDimensions = FIntPoint::ZeroValue;
	FIntPoint ExpectedAtlasDimensions = FIntPoint::ZeroValue;
	if (!TryCalculateTransferredPaintMaskDimensions(
			FaceDimensions,
			ExpectedAtlasDimensions))
	{
		return;
	}

	const int32 ExpectedPixelCount =
		ExpectedAtlasDimensions.X * ExpectedAtlasDimensions.Y;
	if (!TransferredPaintMaskTexture
		|| !TransferredPaintMaskTexture->GetResource()
		|| TransferredPaintMaskAtlasDimensions != ExpectedAtlasDimensions
		|| TransferredPaintMaskAtlasPixels.Num() != ExpectedPixelCount)
	{
		InitializeTransferredPaintMask();
		RefreshContaminationDecalMaterialParameters();
		return;
	}

	const int32 CellsPerFace = ContaminationGridDimensions.X
		* ContaminationGridDimensions.Y;
	FIntRect CandidateRegion = FMD_ScrapeMaskAtlas::MakeEmptyAtlasRegion();
	for (const FMD_ScrapeCellChange& CellChange : CellChanges)
	{
		EMD_ScrapeSurfaceFace Face = EMD_ScrapeSurfaceFace::Front;
		int32 LocalCellIndex = INDEX_NONE;
		if (!FMD_ScrapeSurfaceLayout::TryDecodeCombinedCellIndex(
				CellChange.CellIndex,
				CellsPerFace,
				Face,
				LocalCellIndex))
		{
			continue;
		}

		const int32 CellX = LocalCellIndex % ContaminationGridDimensions.X;
		const int32 CellY = LocalCellIndex / ContaminationGridDimensions.X;
		const int32 MinimumCellX = FMath::Max(0, CellX - 1);
		const int32 MaximumCellX = FMath::Min(
			ContaminationGridDimensions.X,
			CellX + 2);
		const int32 MinimumCellY = FMath::Max(0, CellY - 1);
		const int32 MaximumCellY = FMath::Min(
			ContaminationGridDimensions.Y,
			CellY + 2);
		const int32 MinimumPixelX = FMath::Clamp(
			FMath::FloorToInt(
				static_cast<double>(MinimumCellX)
					/ ContaminationGridDimensions.X * FaceDimensions.X),
			0,
			FaceDimensions.X - 1);
		const int32 MaximumPixelX = FMath::Clamp(
			FMath::CeilToInt(
				static_cast<double>(MaximumCellX)
					/ ContaminationGridDimensions.X * FaceDimensions.X) - 1,
			0,
			FaceDimensions.X - 1);
		const int32 FacePixelOffsetY =
			Face == EMD_ScrapeSurfaceFace::Back ? FaceDimensions.Y : 0;
		const int32 MinimumPixelY = FacePixelOffsetY + FMath::Clamp(
			FMath::FloorToInt(
				static_cast<double>(MinimumCellY)
					/ ContaminationGridDimensions.Y * FaceDimensions.Y),
			0,
			FaceDimensions.Y - 1);
		const int32 MaximumPixelY = FacePixelOffsetY + FMath::Clamp(
			FMath::CeilToInt(
				static_cast<double>(MaximumCellY)
					/ ContaminationGridDimensions.Y * FaceDimensions.Y) - 1,
			0,
			FaceDimensions.Y - 1);
		FMD_ScrapeMaskAtlas::ExpandAtlasRegion(
			CandidateRegion,
			FIntPoint(MinimumPixelX, MinimumPixelY));
		FMD_ScrapeMaskAtlas::ExpandAtlasRegion(
			CandidateRegion,
			FIntPoint(MaximumPixelX, MaximumPixelY));
	}

	if (FMD_ScrapeMaskAtlas::IsAtlasRegionEmpty(CandidateRegion))
	{
		return;
	}

	FIntRect ChangedRegion = FMD_ScrapeMaskAtlas::MakeEmptyAtlasRegion();
	for (int32 AtlasY = CandidateRegion.Min.Y;
		AtlasY < CandidateRegion.Max.Y;
		++AtlasY)
	{
		const EMD_ScrapeSurfaceFace Face = AtlasY < FaceDimensions.Y
			? EMD_ScrapeSurfaceFace::Front
			: EMD_ScrapeSurfaceFace::Back;
		const int32 FaceY = AtlasY % FaceDimensions.Y;
		for (int32 PixelX = CandidateRegion.Min.X;
			PixelX < CandidateRegion.Max.X;
			++PixelX)
		{
			const int32 PixelIndex = PixelX
				+ AtlasY * TransferredPaintMaskAtlasDimensions.X;
			if (!TransferredPaintMaskAtlasPixels.IsValidIndex(PixelIndex))
			{
				continue;
			}
			const uint8 NewValue = CalculateTransferredPaintMaskPixel(
				Face,
				FIntPoint(PixelX, FaceY),
				FaceDimensions);
			if (TransferredPaintMaskAtlasPixels[PixelIndex] == NewValue)
			{
				continue;
			}
			TransferredPaintMaskAtlasPixels[PixelIndex] = NewValue;
			FMD_ScrapeMaskAtlas::ExpandAtlasRegion(
				ChangedRegion,
				FIntPoint(PixelX, AtlasY));
		}
	}

	if (!FMD_ScrapeMaskAtlas::IsAtlasRegionEmpty(ChangedRegion)
		&& !UploadTransferredPaintMaskRegion(ChangedRegion))
	{
		InitializeTransferredPaintMask();
		RefreshContaminationDecalMaterialParameters();
	}
}


bool UMD_ScrapeSurfaceComponent::UploadTransferredPaintMaskRegion(
	const FIntRect& AtlasRegion)
{
	const int32 RegionWidth = AtlasRegion.Width();
	const int32 RegionHeight = AtlasRegion.Height();
	if (!TransferredPaintMaskTexture
		|| !TransferredPaintMaskTexture->GetResource()
		|| RegionWidth <= 0
		|| RegionHeight <= 0)
	{
		return false;
	}

	uint8* UploadValues = new uint8[RegionWidth * RegionHeight];
	if (!FMD_ScrapeMaskAtlas::TryPackAtlasRegion(
			TransferredPaintMaskAtlasDimensions,
			MakeArrayView(TransferredPaintMaskAtlasPixels),
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
	TransferredPaintMaskTexture->UpdateTextureRegions(
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


void UMD_ScrapeSurfaceComponent::UpdateContaminationMask(
	const TArray<FMD_ScrapeCellChange>& CellChanges)
{
	SCOPE_CYCLE_COUNTER(STAT_MD_ScrapeMaskUpdate);
	if (!bContaminationEnabled)
	{
		return;
	}

	if (CellChanges.IsEmpty())
	{
		return;
	}

	UpdateTransferredPaintMask(CellChanges);

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
		const FMD_ScrapeSurfaceCell MaskCell =
			MakeContaminationMaskCell(CellChange.CellIndex);
		FIntPoint PixelCoordinate = FIntPoint::NoneValue;
		if (!FMD_ScrapeMaskAtlas::TryUpdateAtlasPixel(
				ContaminationGridDimensions,
				CellChange.CellIndex,
				MaskCell,
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
	bResolvedFullCoverageContaminationDecals = false;
	SetContaminationDecalsVisible(false);
	DestroyAdditionalContaminationDecalPatches();
	if (!bContaminationEnabled)
	{
		return false;
	}
	if (!bShowContaminationDecals
		|| !bContaminationSurfaceInitialized
		|| !ContaminationMaskTexture
		|| !TransferredPaintMaskTexture
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

	const bool bConfiguredFront = ConfigureContaminationDecalPatchesForFace(
		FrontContaminationDecal,
		EMD_ScrapeSurfaceFace::Front,
		FrontContaminationDecalPatches,
		FrontContaminationDecalPatchMIDs);
	const bool bConfiguredBack = ConfigureContaminationDecalPatchesForFace(
		BackContaminationDecal,
		EMD_ScrapeSurfaceFace::Back,
		BackContaminationDecalPatches,
		BackContaminationDecalPatchMIDs);
	if (!bConfiguredFront || !bConfiguredBack)
	{
		DestroyAdditionalContaminationDecalPatches();
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("MD_CuttableWall::ConfigureContaminationDecals - %s의 앞/뒤 Decal 배치에 필요한 표면 크기 또는 벽 두께가 올바르지 않습니다."),
			*GetName());
		return false;
	}

	FrontContaminationDecalMID = FrontContaminationDecalPatchMIDs[0];
	BackContaminationDecalMID = BackContaminationDecalPatchMIDs[0];

	bContaminationDecalsConfigured = true;
	SetContaminationDecalsVisible(true);
	return true;
}


void UMD_ScrapeSurfaceComponent::ResolveContaminationDecalDepths()
{
	bResolvedFullCoverageContaminationDecals = false;
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
	FVector HorizontalAxisLocal = FVector::ZeroVector;
	FVector FrontNormalLocal = FVector::ZeroVector;
	FVector VerticalAxisLocal = FVector::ZeroVector;
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
	if (bAutoFitContaminationDecalDepth
		&& bUseFullCoverageContaminationDecals)
	{
		// Facing-normal profiles do not include tangent trims. The logical front
		// and back halves therefore meet at one split plane. Their render volumes
		// overlap that plane below, while the material applies an exclusive
		// world-position ownership gate. This avoids both a missing tangent strip
		// and double blending from the two atlas halves.
		constexpr double FaceSeparationGapCm = 0.0;
		bResolvedFullCoverageContaminationDecals =
			FMD_ScrapeMeshSurfaceProfile::TryPartitionFaceDepths(
				WallThicknessCm,
				FaceSeparationGapCm,
				FrontDepthCm,
				BackDepthCm);
		if (!bResolvedFullCoverageContaminationDecals)
		{
			const double RequiredFrontDepthCm = FrontDepthCm;
			const double RequiredBackDepthCm = BackDepthCm;
			const double RequiredCombinedDepthCm =
				RequiredFrontDepthCm + RequiredBackDepthCm;
			const double TargetCombinedDepthCm = FMath::IsFinite(
				WallThicknessCm)
				? FMath::Max(
					WallThicknessCm - FaceSeparationGapCm,
					0.0)
				: 0.0;
			const bool bHasValidSpatialPartitionInputs =
				FMath::IsFinite(RequiredFrontDepthCm)
				&& RequiredFrontDepthCm >= 0.0
				&& FMath::IsFinite(RequiredBackDepthCm)
				&& RequiredBackDepthCm >= 0.0
				&& FMath::IsFinite(RequiredCombinedDepthCm)
				&& FMath::IsFinite(TargetCombinedDepthCm)
				&& TargetCombinedDepthCm
					> static_cast<double>(UE_SMALL_NUMBER)
				&& RequiredCombinedDepthCm > TargetCombinedDepthCm;

			// Corner meshes can require both normal-facing profiles deep into the
			// same bounds interval. Coverage-first mode owns receivers by the spatial
			// division rather than the original facing profile, so compress both
			// requirements proportionally into the complete wall thickness.
			if (bHasValidSpatialPartitionInputs)
			{
				FMD_ScrapeMeshSurfaceProfile::BalanceFaceDepths(
					TargetCombinedDepthCm,
					FrontDepthCm,
					BackDepthCm);
			}

			const double BalancedCombinedDepthCm =
				FrontDepthCm + BackDepthCm;
			bResolvedFullCoverageContaminationDecals =
				bHasValidSpatialPartitionInputs
				&& FMath::IsFinite(FrontDepthCm)
				&& FrontDepthCm >= 0.0
				&& FMath::IsFinite(BackDepthCm)
				&& BackDepthCm >= 0.0
				&& FMath::IsNearlyEqual(
					BalancedCombinedDepthCm,
					TargetCombinedDepthCm,
					0.01);
			if (bResolvedFullCoverageContaminationDecals)
			{
				UE_LOG(
					LogTemp,
					Verbose,
					TEXT("MD_ScrapeSurfaceComponent: %s compressed full-coverage depths %.3f/%.3f cm to the gapless logical partition %.3f/%.3f cm."),
					*GetName(),
					RequiredFrontDepthCm,
					RequiredBackDepthCm,
					FrontDepthCm,
					BackDepthCm);
			}
			else
			{
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("MD_ScrapeSurfaceComponent: full-coverage decal partition was rejected for %s (wall %.3f cm, required %.3f/%.3f cm, target %.3f cm); using directional decal placement."),
					*GetName(),
					WallThicknessCm,
					RequiredFrontDepthCm,
					RequiredBackDepthCm,
					TargetCombinedDepthCm);
			}
		}
	}
	else
	{
		FMD_ScrapeMeshSurfaceProfile::BalanceFaceDepths(
			WallThicknessCm,
			FrontDepthCm,
			BackDepthCm);
	}

	// Keep the logical partition for the near-tangent ownership fallback. The
	// normal-first classifier can assign a curved receiver to either face at
	// any depth in the mesh bounds, so both physical decal volumes must span
	// the complete thickness. The material's complementary hard gate still
	// guarantees that exactly one atlas half contributes at every pixel.
	const double FaceSplitFrontDepthCm = FrontDepthCm;
	const double FaceSplitBackDepthCm = BackDepthCm;
	if (bResolvedFullCoverageContaminationDecals)
	{
		FrontDepthCm = WallThicknessCm;
		BackDepthCm = WallThicknessCm;
	}

	ResolvedFrontDecalDepthCm = static_cast<float>(FrontDepthCm);
	ResolvedBackDecalDepthCm = static_cast<float>(BackDepthCm);
	ResolvedContaminationFaceSplitCoordinateLocal = 0.0;
	if (SurfaceMesh
		&& !FrontNormalLocal.IsNearlyZero())
	{
		const double NormalScale = FMath::Abs(FVector::DotProduct(
			SurfaceMesh->GetComponentScale().GetAbs(),
			FrontNormalLocal));
		if (FMath::IsFinite(NormalScale)
			&& NormalScale > static_cast<double>(UE_SMALL_NUMBER))
		{
			const double FrontOuterCoordinate = FVector::DotProduct(
				ContaminationMeshLocalBoundsMaximum,
				FrontNormalLocal);
			const double BackOuterCoordinate = FVector::DotProduct(
				ContaminationMeshLocalBoundsMinimum,
				FrontNormalLocal);
			const double FrontInnerCoordinate = FrontOuterCoordinate
				- FaceSplitFrontDepthCm / NormalScale;
			const double BackInnerCoordinate = BackOuterCoordinate
				+ FaceSplitBackDepthCm / NormalScale;
			ResolvedContaminationFaceSplitCoordinateLocal =
				(FrontInnerCoordinate + BackInnerCoordinate) * 0.5;
		}
	}
}


bool UMD_ScrapeSurfaceComponent::ConfigureContaminationDecalPatch(
	UDecalComponent* DecalComponent,
	const EMD_ScrapeSurfaceFace Face,
	const FMD_ScrapeDecalFaceLayout& Layout,
	TObjectPtr<UMaterialInstanceDynamic>& InOutMID)
{
	if (!DecalComponent || !ContaminationDecalMaterial)
	{
		return false;
	}

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
			ResolvedContaminationDecalEdgeInsetCm,
			MaskHalfTexelUV))
	{
		return false;
	}

	DecalComponent->SetDecalMaterial(InOutMID);
	InOutMID->SetTextureParameterValue(
		ContaminationMaskTextureParameterName,
		ContaminationMaskTexture);
	InOutMID->SetTextureParameterValue(
		TransferredPaintMaskTextureParameterName,
		TransferredPaintMaskTexture);
	ApplyContaminationAppearanceParameters(
		*InOutMID,
		ContaminationDecalMaterial);
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
	InOutMID->SetScalarParameterValue(
		ContaminationPatchUVScaleUParameterName,
		Layout.PatchUVScaleU);
	InOutMID->SetScalarParameterValue(
		ContaminationPatchUVOffsetUParameterName,
		Layout.PatchUVOffsetU);
	InOutMID->SetScalarParameterValue(
		ContaminationPatchUVScaleVParameterName,
		Layout.PatchUVScaleV);
	InOutMID->SetScalarParameterValue(
		ContaminationPatchUVOffsetVParameterName,
		Layout.PatchUVOffsetV);
	InOutMID->SetScalarParameterValue(
		ContaminationSurfaceWidthParameterName,
		static_cast<float>(ResolvedContaminationSurfaceSizeCm.X));
	InOutMID->SetScalarParameterValue(
		ContaminationSurfaceHeightParameterName,
		static_cast<float>(ResolvedContaminationSurfaceSizeCm.Y));
	const float DetailTileWorldSizeCm = FMath::IsFinite(
		ContaminationDetailTileWorldSizeCm)
		? FMath::Max(ContaminationDetailTileWorldSizeCm, 1.0f)
		: 150.0f;
	InOutMID->SetScalarParameterValue(
		ContaminationDetailTileWorldSizeParameterName,
		DetailTileWorldSizeCm);
	const FVector ProjectionDirectionWS = DecalComponent
		->GetComponentTransform()
		.TransformVectorNoScale(FVector::ForwardVector)
		.GetSafeNormal();
	InOutMID->SetVectorParameterValue(
		ContaminationDecalProjectionDirectionParameterName,
		FLinearColor(
			static_cast<float>(ProjectionDirectionWS.X),
			static_cast<float>(ProjectionDirectionWS.Y),
			static_cast<float>(ProjectionDirectionWS.Z),
			0.0f));
	if (bResolvedFullCoverageContaminationDecals)
	{
		// The material assigns the overlapping volumes to complementary spatial
		// halves, so a receiver-normal fade would only remove valid tangent trims.
		// SmoothStep(-1, 0, saturate(...)) is identically one.
		InOutMID->SetScalarParameterValue(
			ContaminationAngleFadeStartParameterName,
			-1.0f);
		InOutMID->SetScalarParameterValue(
			ContaminationAngleFadeEndParameterName,
			0.0f);
	}
	ApplyContaminationFaceOwnershipParameters(*InOutMID, Face);
	ApplyContaminationDetailWorldFrame(*InOutMID);
	DecalComponent->MarkRenderStateDirty();
	return true;
}


void UMD_ScrapeSurfaceComponent::ApplyContaminationFaceOwnershipParameters(
	UMaterialInstanceDynamic& DecalMID,
	const EMD_ScrapeSurfaceFace Face) const
{
	FVector SplitOriginWS = FVector::ZeroVector;
	FVector SplitNormalWS = FVector::ForwardVector;
	bool bEnableOwnership = false;
	FVector HorizontalAxisLocal;
	FVector FrontNormalLocal;
	FVector VerticalAxisLocal;
	if (bResolvedFullCoverageContaminationDecals
		&& SurfaceMesh
		&& FMath::IsFinite(ResolvedContaminationFaceSplitCoordinateLocal)
		&& FMD_ScrapeSurfaceGeometry::TryGetSurfaceLocalAxes(
			ResolvedContaminationSurfaceNormalAxis,
			HorizontalAxisLocal,
			FrontNormalLocal,
			VerticalAxisLocal))
	{
		const FTransform& MeshTransform = SurfaceMesh->GetComponentTransform();
		SplitOriginWS = MeshTransform.TransformPosition(
			FrontNormalLocal
				* ResolvedContaminationFaceSplitCoordinateLocal);
		// Keep the render-side ownership axis identical to the gameplay face
		// resolver. TransformVector preserves a mirrored component scale's sign,
		// while TransformVectorNoScale would make CPU and material ownership
		// disagree for mirrored wall instances.
		SplitNormalWS = MeshTransform.TransformVector(
			FrontNormalLocal).GetSafeNormal();
		bEnableOwnership = !SplitOriginWS.ContainsNaN()
			&& !SplitNormalWS.ContainsNaN()
			&& !SplitNormalWS.IsNearlyZero();
	}

	DecalMID.SetVectorParameterValue(
		ContaminationFaceSplitOriginParameterName,
		FLinearColor(
			static_cast<float>(SplitOriginWS.X),
			static_cast<float>(SplitOriginWS.Y),
			static_cast<float>(SplitOriginWS.Z),
			0.0f));
	DecalMID.SetVectorParameterValue(
		ContaminationFaceSplitNormalParameterName,
		FLinearColor(
			static_cast<float>(SplitNormalWS.X),
			static_cast<float>(SplitNormalWS.Y),
			static_cast<float>(SplitNormalWS.Z),
			0.0f));
	DecalMID.SetScalarParameterValue(
		ContaminationFaceIsFrontParameterName,
		Face == EMD_ScrapeSurfaceFace::Front ? 1.0f : 0.0f);
	DecalMID.SetScalarParameterValue(
		ContaminationFaceOwnershipEnabledParameterName,
		bEnableOwnership ? 1.0f : 0.0f);
}


void UMD_ScrapeSurfaceComponent::ApplyContaminationDetailWorldFrame(
	UMaterialInstanceDynamic& DecalMID) const
{
	FVector HorizontalAxisLocal;
	FVector FrontNormalLocal;
	FVector VerticalAxisLocal;
	if (!SurfaceMesh
		|| !FMD_ScrapeSurfaceGeometry::TryGetSurfaceLocalAxes(
			ResolvedContaminationSurfaceNormalAxis,
			HorizontalAxisLocal,
			FrontNormalLocal,
			VerticalAxisLocal))
	{
		return;
	}

	const FTransform MeshTransform = SurfaceMesh->GetComponentTransform();
	const FVector HorizontalAxisWorld =
		MeshTransform.TransformVector(HorizontalAxisLocal).GetSafeNormal();
	const FVector VerticalAxisWorld =
		MeshTransform.TransformVector(VerticalAxisLocal).GetSafeNormal();
	if (HorizontalAxisWorld.IsNearlyZero()
		|| VerticalAxisWorld.IsNearlyZero())
	{
		return;
	}

	// 무늬 좌표의 원점은 표면 중심이다. 벽 하나 안에서만 일관되면 되고, 앞/뒤 패치가
	// 전부 같은 값을 받으므로 어느 패치가 칠하든 같은 월드 지점은 같은 무늬를 얻는다.
	const FVector SurfaceOriginWorld =
		MeshTransform.TransformPosition(ContaminationSurfaceCenterLocal);
	auto SetVectorParameter = [&DecalMID](
		const FName ParameterName,
		const FVector& Value)
	{
		DecalMID.SetVectorParameterValue(
			ParameterName,
			FLinearColor(
				static_cast<float>(Value.X),
				static_cast<float>(Value.Y),
				static_cast<float>(Value.Z),
				0.0f));
	};

	SetVectorParameter(
		ContaminationSurfaceOriginParameterName,
		SurfaceOriginWorld);
	SetVectorParameter(
		ContaminationSurfaceTangentUParameterName,
		HorizontalAxisWorld);
	SetVectorParameter(
		ContaminationSurfaceTangentVParameterName,
		VerticalAxisWorld);

	const FVector LocalBoundsSize = ContaminationMeshLocalBoundsMaximum
		- ContaminationMeshLocalBoundsMinimum;
	const double LocalThicknessCm = FMath::Abs(FVector::DotProduct(
		LocalBoundsSize,
		FrontNormalLocal));
	const double NormalWorldScale = MeshTransform
		.TransformVector(FrontNormalLocal)
		.Size();
	const double SurfaceThicknessCm = LocalThicknessCm * NormalWorldScale;
	if (!FMath::IsFinite(SurfaceThicknessCm)
		|| SurfaceThicknessCm <= static_cast<double>(UE_SMALL_NUMBER))
	{
		// Keep the shared coordinate frame current while failing open for the
		// optional spill gate. This avoids retaining a stale MID value when a
		// temporarily degenerate mesh is assigned.
		DecalMID.SetScalarParameterValue(
			ContaminationSurfaceThicknessParameterName,
			1000000.0f);
		return;
	}
	DecalMID.SetScalarParameterValue(
		ContaminationSurfaceThicknessParameterName,
		static_cast<float>(SurfaceThicknessCm));
}


void UMD_ScrapeSurfaceComponent::ApplyContaminationAppearanceParameters(
	UMaterialInstanceDynamic& DecalMID,
	UMaterialInterface* const ParentMaterial) const
{
	auto ResolveTexture = [ParentMaterial](
		UTexture2D* const TextureOverride,
		const FName ParameterName) -> UTexture*
	{
		if (TextureOverride)
		{
			return TextureOverride;
		}

		UTexture* InheritedTexture = nullptr;
		if (ParentMaterial)
		{
			ParentMaterial->GetTextureParameterValue(
				FHashedMaterialParameterInfo(ParameterName),
				InheritedTexture);
		}
		return InheritedTexture;
	};

	if (UTexture* const BaseColorTexture = ResolveTexture(
			ContaminationBaseColorTexture,
			ContaminationBaseColorTextureParameterName))
	{
		DecalMID.SetTextureParameterValue(
			ContaminationBaseColorTextureParameterName,
			BaseColorTexture);
	}
	if (UTexture* const ShapeMaskTexture = ResolveTexture(
			ContaminationShapeMaskTexture,
			ContaminationShapeMaskTextureParameterName))
	{
		DecalMID.SetTextureParameterValue(
			ContaminationShapeMaskTextureParameterName,
			ShapeMaskTexture);
	}

	const float BaseCoverage = FMath::IsFinite(ContaminationBaseCoverage)
		? FMath::Clamp(ContaminationBaseCoverage, 0.0f, 1.0f)
		: 0.85f;
	const float OverallOpacity = FMath::IsFinite(ContaminationOpacity)
		? FMath::Clamp(ContaminationOpacity, 0.0f, 1.0f)
		: 0.90f;
	DecalMID.SetScalarParameterValue(
		ContaminationBaseCoverageParameterName,
		BaseCoverage);
	DecalMID.SetScalarParameterValue(
		ContaminationOpacityParameterName,
		OverallOpacity);
	ApplyContaminationVisualMaskCurveParameters(DecalMID);
}


void UMD_ScrapeSurfaceComponent::ApplyContaminationVisualMaskCurveParameters(
	UMaterialInstanceDynamic& DecalMID) const
{
	// 마스크 텍셀은 FMD_ScrapeMaskAtlas가 만든 0~1 불투명도 신호다. 자르는
	// 구간을 0.5 기준으로 대칭으로 두면 보이는 경계가 부분 피복 50% 지점, 즉
	// 실제 헤라 날 경계에 놓인다. 비대칭으로 두면 자국 폭이 날 폭과 달라진다.
	const float EdgeSoftness = FMath::IsFinite(ContaminationVisualEdgeSoftness)
		? FMath::Clamp(ContaminationVisualEdgeSoftness, 0.02f, 1.0f)
		: 0.12f;
	DecalMID.SetScalarParameterValue(
		ContaminationEdgeMinParameterName,
		0.5f - EdgeSoftness * 0.5f);
	DecalMID.SetScalarParameterValue(
		ContaminationEdgeMaxParameterName,
		0.5f + EdgeSoftness * 0.5f);

	// 노이즈는 구간 폭에 비례해서 넣는다. 절대값으로 두면 구간을 좁힐수록
	// 노이즈가 경계를 한 칸씩 밀어내 톱니가 된다.
	const float EdgeNoiseAmount =
		FMath::IsFinite(ContaminationVisualEdgeNoiseAmount)
		? FMath::Clamp(ContaminationVisualEdgeNoiseAmount, 0.0f, 1.0f)
		: 0.0f;
	DecalMID.SetScalarParameterValue(
		ContaminationEdgeNoiseStrengthParameterName,
		EdgeNoiseAmount * EdgeSoftness);

	// 마스크 값은 CPU가 이미 불투명도 공간으로 만들어 두었다. 여기서 한 번 더
	// 휘면 경계가 날 위치에서 밀리므로 중립값으로 고정한다.
	DecalMID.SetScalarParameterValue(
		ContaminationVisualMaskGammaParameterName,
		1.0f);
}


void UMD_ScrapeSurfaceComponent::RefreshContaminationAppearance()
{
	// FilmOpacityGamma는 텍셀 값 자체를 바꾸므로 MID만 갱신하면 반영되지 않는다.
	// 마스크가 이미 있을 때만 다시 만든다. 아직 없다면 표면 초기화가 만든다.
	if (ContaminationMaskTexture)
	{
		InitializeContaminationMask();
		return;
	}

	RefreshContaminationDecalMaterialParameters();
}


void UMD_ScrapeSurfaceComponent::SetContaminationDecalsVisible(
	const bool bVisible)
{
	const bool bShouldBeVisible = bVisible && bContaminationEnabled;
	auto SetFaceVisible = [bShouldBeVisible](
		const TArray<TObjectPtr<UDecalComponent>>& PatchComponents,
		UDecalComponent* BaseDecalComponent)
	{
		if (PatchComponents.IsEmpty())
		{
			if (BaseDecalComponent)
			{
				BaseDecalComponent->SetVisibility(bShouldBeVisible);
				BaseDecalComponent->SetHiddenInGame(!bShouldBeVisible);
			}
			return;
		}

		for (UDecalComponent* const PatchComponent : PatchComponents)
		{
			if (PatchComponent)
			{
				PatchComponent->SetVisibility(bShouldBeVisible);
				PatchComponent->SetHiddenInGame(!bShouldBeVisible);
			}
		}
	};

	SetFaceVisible(
		FrontContaminationDecalPatches,
		FrontContaminationDecal);
	SetFaceVisible(
		BackContaminationDecalPatches,
		BackContaminationDecal);
}


void UMD_ScrapeSurfaceComponent::RefreshContaminationDecalMaterialParameters()
{
	if (!bContaminationEnabled)
	{
		return;
	}

	// 배치 때와 같은 inset 보정을 써야 마스크를 다시 만들 때 무늬가 밀리지 않는다.
	FVector2D MaskHalfTexelUV = FVector2D::ZeroVector;
	if (!FMD_ScrapeDecalLayout::TryCalculateMaskHalfTexelUV(
			ContaminationMaskAtlasDimensions,
			ResolvedContaminationSurfaceSizeCm,
			ResolvedContaminationDecalEdgeInsetCm,
			MaskHalfTexelUV))
	{
		return;
	}

	auto RefreshMID = [this, MaskHalfTexelUV](
		UMaterialInstanceDynamic* DecalMID,
		const EMD_ScrapeSurfaceFace Face,
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
		DecalMID->SetTextureParameterValue(
			TransferredPaintMaskTextureParameterName,
			TransferredPaintMaskTexture);
		ApplyContaminationAppearanceParameters(
			*DecalMID,
			DecalMID->Parent.Get());
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
		ApplyContaminationFaceOwnershipParameters(*DecalMID, Face);
		ApplyContaminationDetailWorldFrame(*DecalMID);
	};

	auto RefreshFaceMIDs = [&RefreshMID](
		const TArray<TObjectPtr<UMaterialInstanceDynamic>>& PatchMIDs,
		UMaterialInstanceDynamic* BaseMID,
		const EMD_ScrapeSurfaceFace Face,
		const float VOffset,
		const float UFlip)
	{
		if (PatchMIDs.IsEmpty())
		{
			RefreshMID(BaseMID, Face, VOffset, UFlip);
			return;
		}

		for (UMaterialInstanceDynamic* const PatchMID : PatchMIDs)
		{
			RefreshMID(PatchMID, Face, VOffset, UFlip);
		}
	};

	RefreshFaceMIDs(
		FrontContaminationDecalPatchMIDs,
		FrontContaminationDecalMID,
		EMD_ScrapeSurfaceFace::Front,
		0.0f,
		1.0f);
	RefreshFaceMIDs(
		BackContaminationDecalPatchMIDs,
		BackContaminationDecalMID,
		EMD_ScrapeSurfaceFace::Back,
		0.5f,
		0.0f);
}
