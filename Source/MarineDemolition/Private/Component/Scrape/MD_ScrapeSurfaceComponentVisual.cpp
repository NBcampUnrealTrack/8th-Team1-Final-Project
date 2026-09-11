#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"

#include "Components/DecalComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Component/Scrape/MD_ScrapeSurfaceComponentInternal.h"
#include "Gameplay/Scrape/MD_ScrapeDecalLayout.h"
#include "Gameplay/Scrape/MD_ScrapeMaskAtlas.h"
#include "Gameplay/Scrape/MD_ScrapeMeshSurfaceProfile.h"
#include "Gameplay/Scrape/MD_ScrapeMeshUVData.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceGeometry.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceLayout.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Gameplay/Scrape/MD_ScrapeRuntimeMaskTexture.h"
#include "MDScrapeInitialMaskRenderer.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "RHITypes.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

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
	int32 CellIndex) const
{
	// 합성 셀은 게임플레이 상태가 아니라 텍셀에 그대로 들어갈 불투명도 신호를
	// 담는다. 양자화가 InitialContamination으로 잘라내므로 1로 고정한다.
	FMD_ScrapeSurfaceCell MaskCell;
	MaskCell.InitialContamination = 1.0f;
	MaskCell.RemainingContamination = 0.0f;
	const FMD_ScrapeConstCellView VisualCells =
		GetVisualContaminationCells();
    if (UsesMeshUVSurface())
    {
        if (!ContaminationMeshUVData || !ContaminationMeshUVData->Cells.IsValidIndex(CellIndex))
        { return MaskCell; }
        const TConstArrayView<FMD_ScrapeMeshUVVisualSample> Samples =
            ContaminationMeshUVData->GetVisualSeamSamples(CellIndex);
        if (!Samples.IsEmpty())
        {
            double Signal = 0.0;
            for (const FMD_ScrapeMeshUVVisualSample& Sample : Samples)
            {
                if (!VisualCells.IsValidIndex(Sample.CellIndex)) continue;
                const FMD_ScrapeSurfaceCell& Cell = VisualCells[Sample.CellIndex];
                Signal += Sample.Weight * CalculateContaminationVisualSignal(Cell);
            }
            MaskCell.RemainingContamination = static_cast<float>(FMath::Clamp(Signal, 0.0, 1.0));
            return MaskCell;
        }
        CellIndex = ContaminationMeshUVData->Cells[CellIndex].VisualSourceCellIndex;
    }
	if (!VisualCells.IsValidIndex(CellIndex))
	{
		return MaskCell;
	}

	const FMD_ScrapeSurfaceCell& SourceCell = VisualCells[CellIndex];
	MaskCell.RemainingContamination =
		CalculateContaminationVisualSignal(SourceCell);
	return MaskCell;
}

bool UMD_ScrapeSurfaceComponent::TryBuildContaminationMaskPixels(
	FIntPoint& OutAtlasDimensions, TArray<uint8>& OutPixels) const
{
	OutAtlasDimensions = FIntPoint::ZeroValue;
	OutPixels.Reset();
	int32 CellsPerFace = 0;
	int32 StateCellCount = 0;
	const FMD_ScrapeConstCellView VisualCells =
		GetVisualContaminationCells();
	if (!TryCalculateContaminationCellCounts(CellsPerFace, StateCellCount)
		|| VisualCells.Num() != StateCellCount
		|| !FMD_ScrapeMaskAtlas::TryCalculateAtlasDimensions(
			ContaminationGridDimensions, OutAtlasDimensions))
	{
		return false;
	}

	// Every MeshUV face is already represented by the canonical UV atlas. The
	// material samples the full W x H mask; planar surfaces retain both face slots.
    if (UsesMeshUVSurface()) { OutAtlasDimensions = ContaminationGridDimensions; }
    if (UsesMeshUVSurface() && ContaminationMeshUVData && ContaminationCellLayout
        && ContaminationMeshUVData->Cells.Num() == StateCellCount
        && ContaminationCellLayout->AtlasToActive.Num() == StateCellCount)
    {
        // Reuse the exact float signal before G8 conversion. Quantizing before the
        // seam sum would change fractional boundary pixels. This scratch dies here.
        TArray<float> ActiveSignals;
        ActiveSignals.SetNumUninitialized(ContaminationCellLayout->ActiveToAtlas.Num());
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scrape_BuildMaskSignals);
            uint32 PreviousInputBits[3] = {};
            float PreviousSignal = 0.0f;
            bool bHasPreviousSignal = false;
            for (int32 StorageIndex = 0; StorageIndex < ActiveSignals.Num(); ++StorageIndex)
            {
                const FMD_ScrapeSurfaceCell& Cell = VisualCells[ContaminationCellLayout->ActiveToAtlas[StorageIndex]];
                // Gamma is constant for this build. Compare representations so NaN
                // payloads and signed zero retain the original function's semantics.
                uint32 InputBits[3];
                FMemory::Memcpy(&InputBits[0], &Cell.RemainingContamination, sizeof(float));
                FMemory::Memcpy(&InputBits[1], &Cell.InitialContamination, sizeof(float));
                FMemory::Memcpy(&InputBits[2], &Cell.CoveredFraction, sizeof(float));
                if (!bHasPreviousSignal || InputBits[0] != PreviousInputBits[0]
                    || InputBits[1] != PreviousInputBits[1] || InputBits[2] != PreviousInputBits[2])
                {
                    PreviousSignal = CalculateContaminationVisualSignal(Cell);
                    FMemory::Memcpy(PreviousInputBits, InputBits, sizeof(InputBits));
                    bHasPreviousSignal = true;
                }
                ActiveSignals[StorageIndex] = PreviousSignal;
            }
        }
        const FMD_ScrapeMeshUVVisualSeamView SeamView = ContaminationMeshUVData->GetVisualSeamView();
        auto SignalAtAtlas = [&](const int32 AtlasIndex) -> float
        {
            if (!ContaminationCellLayout->AtlasToActive.IsValidIndex(AtlasIndex)) return 0.0f;
            const int32 StorageIndex = ContaminationCellLayout->AtlasToActive[AtlasIndex];
            return StorageIndex != INDEX_NONE ? ActiveSignals[StorageIndex] : 0.0f;
        };
        TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scrape_RasterMask);
        OutPixels.SetNumUninitialized(StateCellCount);
        for (int32 Pixel = 0; Pixel < StateCellCount; ++Pixel)
        {
            const auto Samples = SeamView.GetSamples(Pixel);
            float Signal;
            if (!Samples.IsEmpty())
            {
                double WeightedSignal = 0.0;
                for (const FMD_ScrapeMeshUVVisualSample& Sample : Samples)
                {
                    // Retain MakeContaminationMaskCell's sample order, float product,
                    // double accumulation, clamp and final float conversion exactly.
                    if (!VisualCells.IsValidIndex(Sample.CellIndex)) continue;
                    WeightedSignal += Sample.Weight * SignalAtAtlas(Sample.CellIndex);
                }
                Signal = static_cast<float>(FMath::Clamp(WeightedSignal, 0.0, 1.0));
            }
            else
            {
                Signal = SignalAtAtlas(ContaminationMeshUVData->Cells[Pixel].VisualSourceCellIndex);
            }
            if (!FMD_ScrapeMaskAtlas::TryQuantizeRemainingContamination(Signal, 1.0f, OutPixels[Pixel]))
            {
                OutPixels.Reset(); OutAtlasDimensions = FIntPoint::ZeroValue;
                return false;
            }
        }
        return true;
    }
	OutPixels.SetNumZeroed(OutAtlasDimensions.X * OutAtlasDimensions.Y);
	for (int32 CellIndex = 0; CellIndex < StateCellCount; ++CellIndex)
	{
		const FMD_ScrapeSurfaceCell MaskCell = MakeContaminationMaskCell(CellIndex);
		if (!FMD_ScrapeMaskAtlas::TryQuantizeRemainingContamination(
			MaskCell.RemainingContamination, MaskCell.InitialContamination, OutPixels[CellIndex]))
		{
			OutPixels.Reset();
			OutAtlasDimensions = FIntPoint::ZeroValue;
			return false;
		}
	}
	return true;
}

bool UMD_ScrapeSurfaceComponent::TryBuildInitialContaminationMaskRecipe()
{
    if (!UsesMeshUVSurface() || !ContaminationMeshUVData || !ContaminationCellLayout
        || ContaminationGeneration.Version < 2 || !ContaminationGeneration.bProcedural
        || !ContaminationPagedCells.IsPristineCanonicalInitial()
        || HasLocalPredictedContaminationState() || !FMDScrapeInitialMaskRenderer::IsSupported()) return false;
    FMD_ScrapeSurfaceCell Full;
    Full.InitialContamination = Full.RemainingContamination = 1.f;
    // The pristine progress alphabet must be exact 0/1 before the seam sum.
    // Keep the generic path if the signal contract ever changes.
    const float FullSignal = CalculateContaminationVisualSignal(Full);
    uint8 FullByte = 0;
    if (!FMD_ScrapeMaskAtlas::TryQuantizeRemainingContamination(FullSignal, 1.f, FullByte) || FullByte != 255) return false;
    const auto* Data = ContaminationMeshUVData.Get();
    const auto* CellLayout = ContaminationCellLayout.Get();
    if (Data->Cells.Num() != ContaminationGridDimensions.X * ContaminationGridDimensions.Y
        || CellLayout->AtlasToActive.Num() != Data->Cells.Num()
        || ContaminationPagedCells.Num() != CellLayout->ActiveToAtlas.Num()) return false;
    struct FLayoutEntry
    {
        TWeakObjectPtr<const UMD_ScrapeMeshUVData> Data;
        uint64 Revision;
        FGuid Id;
        TWeakPtr<const FMDScrapeInitialMaskLayout, ESPMode::ThreadSafe> Layout;
    };
    static TArray<FLayoutEntry> Cache;
    Cache.RemoveAllSwap([](const auto& Entry) { return !Entry.Data.IsValid() || !Entry.Layout.IsValid(); }, EAllowShrinking::No);
    TSharedPtr<const FMDScrapeInitialMaskLayout, ESPMode::ThreadSafe> Layout;
    for (const auto& Entry : Cache)
        if (Entry.Data == Data && Entry.Revision == Data->GetRuntimeCacheRevision() && Entry.Id == Data->LayoutId)
        { Layout = Entry.Layout.Pin(); break; }
    const auto SeamView = Data->GetVisualSeamView();
    if (!Layout)
    {
        TArray<uint32> Sources;
        TArray<int32> SeamPixels;
        Sources.SetNumUninitialized(Data->Cells.Num());
        for (int32 Pixel = 0; Pixel < Sources.Num(); ++Pixel)
        {
            if (!SeamView.GetSamples(Pixel).IsEmpty())
            {
                Sources[Pixel] = 0x80000000u | uint32(SeamPixels.Num());
                SeamPixels.Add(Pixel);
            }
            else
            {
                const int32 Source = Data->Cells[Pixel].VisualSourceCellIndex;
                const int32 Active = CellLayout->AtlasToActive.IsValidIndex(Source) ? CellLayout->AtlasToActive[Source] : INDEX_NONE;
                Sources[Pixel] = Active == INDEX_NONE ? MAX_uint32 : uint32(Active);
            }
        }
        Layout = MakeShared<FMDScrapeInitialMaskLayout, ESPMode::ThreadSafe>(MoveTemp(Sources), MoveTemp(SeamPixels));
        Cache.Add({Data, Data->GetRuntimeCacheRevision(), Data->LayoutId, Layout});
    }
    TArray<uint32> Bits;
    Bits.SetNumZeroed(FMath::DivideAndRoundUp(ContaminationPagedCells.Num(), 32));
    for (int32 Active = 0; Active < ContaminationPagedCells.Num(); ++Active)
    {
        const float Amount = ContaminationPagedCells.GetInitialCell(Active).InitialContamination;
        if (!FMath::IsFinite(Amount) || Amount < 0.f || Amount > 1.f) return false;
        if (Amount > UE_SMALL_NUMBER) Bits[Active >> 5] |= 1u << (Active & 31);
    }
    TArray<uint8> SeamBytes;
    SeamBytes.SetNumUninitialized(Layout->SeamPixels.Num());
    for (int32 Seam = 0; Seam < SeamBytes.Num(); ++Seam)
    {
        // Preserve float products, double summation, sample order and G8 rounding.
        // Only seam pixels need this reduction; full atlas expansion runs on GPU.
        double Sum = 0.0;
        for (const auto& Sample : SeamView.GetSamples(Layout->SeamPixels[Seam]))
        {
            if (!CellLayout->AtlasToActive.IsValidIndex(Sample.CellIndex)) continue;
            const int32 Active = CellLayout->AtlasToActive[Sample.CellIndex];
            const float Signal = Active != INDEX_NONE && ((Bits[Active >> 5] >> (Active & 31)) & 1u) ? FullSignal : 0.f;
            Sum += Sample.Weight * Signal;
        }
        const float Signal = static_cast<float>(FMath::Clamp(Sum, 0.0, 1.0));
        if (!FMD_ScrapeMaskAtlas::TryQuantizeRemainingContamination(Signal, 1.f, SeamBytes[Seam])) return false;
    }
    ContaminationMaskAtlasPixels.InitializeBinary(MoveTemp(Layout), MoveTemp(Bits), MoveTemp(SeamBytes));
    ContaminationMaskAtlasDimensions = ContaminationGridDimensions;
    return true;
}

bool UMD_ScrapeSurfaceComponent::InitializeContaminationMask()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scrape_InitializeMask);
	ReleaseContaminationMask();
	if (!bContaminationEnabled || (GetWorld() && GetWorld()->GetNetMode() == NM_DedicatedServer))
	{
		return false;
	}

    struct FInitialMaskCacheEntry
    {
        TWeakObjectPtr<UMD_ScrapeSurfaceComponent> Owner;
        TWeakObjectPtr<UTexture2D> Texture;
        const void* StateIdentity = nullptr;
        const UMD_ScrapeMeshUVData* Data = nullptr;
        FIntPoint Dimensions;
        uint32 GammaBits = 0;
        uint64 DataRevision = 0;
        FGuid LayoutId;
    };
    static TArray<FInitialMaskCacheEntry> InitialMasks;
    uint32 GammaBits = 0;
    static_assert(sizeof(GammaBits) == sizeof(ContaminationVisualFilmOpacityGamma));
    FMemory::Memcpy(&GammaBits, &ContaminationVisualFilmOpacityGamma, sizeof(GammaBits));
    if (ContaminationGeneration.Version>=2 && ContaminationGeneration.bProcedural) GammaBits ^= 0x80000000u;
    const bool bCanShareBuild = UsesMeshUVSurface() && ContaminationPagedCells.Num() > 0
        && ContaminationPagedCells.GetOwnedPageCount() == 0 && LocalPredictedPagedCells.IsEmpty();
    UTexture2D* Cached = nullptr;
    for (int32 I = InitialMasks.Num() - 1; I >= 0; --I)
    {
        auto& Entry = InitialMasks[I];
        auto* Owner = Entry.Owner.Get();
        if (!Owner || !Entry.Texture.IsValid()
            || Owner->ContaminationPagedCells.GetSharedInitialIdentity() != Entry.StateIdentity
            || Owner->ContaminationMeshUVData != Entry.Data
            || Entry.Data->GetRuntimeCacheRevision() != Entry.DataRevision || Entry.Data->LayoutId != Entry.LayoutId)
        { InitialMasks.RemoveAtSwap(I); continue; }
        if (bCanShareBuild && Entry.StateIdentity == ContaminationPagedCells.GetSharedInitialIdentity()
            && Entry.Data == ContaminationMeshUVData && Entry.Dimensions == ContaminationGridDimensions
            && Entry.GammaBits == GammaBits) Cached = Entry.Texture.Get();
    }
    if (Cached && FMD_ScrapeRuntimeMaskTexture::GetPixels(Cached))
    {
        ContaminationMaskAtlasDimensions = ContaminationGridDimensions;
        ContaminationMaskAtlasPixels = *FMD_ScrapeRuntimeMaskTexture::GetPixels(Cached);
        ContaminationMaskTexture = Cached;
    }
    else
    {
        if (!TryBuildInitialContaminationMaskRecipe())
        {
            FIntPoint AtlasDimensions = FIntPoint::ZeroValue;
            TArray<uint8> AtlasPixels;
            if (!TryBuildContaminationMaskPixels(AtlasDimensions, AtlasPixels)) return false;
            ContaminationMaskAtlasDimensions = AtlasDimensions;
            ContaminationMaskAtlasPixels = MoveTemp(AtlasPixels);
        }
        auto* Texture = FMD_ScrapeRuntimeMaskTexture::AcquireInitial(
            ContaminationMaskAtlasDimensions, ContaminationMaskAtlasPixels, TF_Bilinear);
        if (!Texture) { ContaminationMaskAtlasPixels.Empty(); return false; }
        ContaminationMaskTexture = Texture;
        if (bCanShareBuild)
            InitialMasks.Add({this, Texture, ContaminationPagedCells.GetSharedInitialIdentity(),
                ContaminationMeshUVData, ContaminationGridDimensions, GammaBits,
                ContaminationMeshUVData->GetRuntimeCacheRevision(), ContaminationMeshUVData->LayoutId});
    }
	if (!InitializeTransferredPaintMask())
	{
		ContaminationMaskTexture = nullptr;
		ContaminationMaskAtlasPixels.Reset();
		ContaminationMaskAtlasDimensions = FIntPoint::ZeroValue;
		return false;
	}
    UpdateContaminationGenerationOverride({});
	RefreshContaminationDecalMaterialParameters();
	return true;
}


void UMD_ScrapeSurfaceComponent::ReleaseContaminationMask()
{
    ReleaseContaminationBoundaryVisual();
    ReleaseContaminationGenerationVisual();
	ReleaseTransferredPaintMask();
	if (ContaminationMaskTexture)
	{
		ContaminationMaskTexture = nullptr;
	}
    // Cleanup must run even after bContaminationEnabled becomes false.
    const auto ClearMaskBindings = [](UMaterialInstanceDynamic* MID)
    {
        if (!MID) return;
        MID->SetTextureParameterValue(ContaminationMaskTextureParameterName, nullptr);
        MID->SetTextureParameterValue(TransferredPaintMaskTextureParameterName, nullptr);
        MID->SetTextureParameterValue(TEXT("ProceduralOverrideMask"),nullptr);
    };
    ClearMaskBindings(FrontContaminationDecalMID);
    ClearMaskBindings(BackContaminationDecalMID);
    ClearMaskBindings(ContaminationMeshUVOverlayMID);
    for (UMaterialInstanceDynamic* MID : FrontContaminationDecalPatchMIDs) ClearMaskBindings(MID);
    for (UMaterialInstanceDynamic* MID : BackContaminationDecalPatchMIDs) ClearMaskBindings(MID);

	ContaminationMaskAtlasPixels.Reset();
	ContaminationMaskAtlasDimensions = FIntPoint::ZeroValue;
}


bool UMD_ScrapeSurfaceComponent::TryCalculateTransferredPaintMaskDimensions(
	FIntPoint& OutFaceDimensions,
	FIntPoint& OutAtlasDimensions) const
{
	OutFaceDimensions = FIntPoint::ZeroValue;
	OutAtlasDimensions = FIntPoint::ZeroValue;
    if (UsesMeshUVSurface())
    {
        OutFaceDimensions = ContaminationGridDimensions;
        OutAtlasDimensions = OutFaceDimensions;
        return OutFaceDimensions.X > 0 && OutFaceDimensions.Y > 0;
    }
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
	const FMD_ScrapeConstCellView VisualCells =
		GetVisualContaminationCells();
	if (!TryCalculateContaminationCellCounts(
			CellsPerFace,
			TotalCellCount)
		|| VisualCells.Num() != TotalCellCount)
	{
		return 0;
	}

    if (UsesMeshUVSurface())
    {
        if (Face != EMD_ScrapeSurfaceFace::Front || !ContaminationMeshUVData) { return 0; }
        const int32 Pixel = FacePixelCoordinate.X + FacePixelCoordinate.Y * ContaminationGridDimensions.X;
        if (!ContaminationMeshUVData->Cells.IsValidIndex(Pixel)) { return 0; }
        // Reuse dirt's physical seam footprint, including private chart gutters.
        // Blend the existing paint signal, never a binary occupied/empty classification.
        const auto Samples = ContaminationMeshUVData->GetVisualSeamSamples(Pixel);
        double Signal = 0.0;
        if (!Samples.IsEmpty())
        {
            for (const FMD_ScrapeMeshUVVisualSample& Sample : Samples)
            {
                if (!VisualCells.IsValidIndex(Sample.CellIndex)) continue;
                Signal += Sample.Weight * FMath::Clamp(
                    VisualCells[Sample.CellIndex].TransferredContamination, 0.0f, 1.0f);
            }
        }
        else
        {
            const int32 Source = ContaminationMeshUVData->Cells[Pixel].VisualSourceCellIndex;
            if (!VisualCells.IsValidIndex(Source)) return 0;
            Signal = FMath::Clamp(VisualCells[Source].TransferredContamination, 0.0f, 1.0f);
        }
        return static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Signal, 0.0, 1.0) * 255.0));
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
			if (!VisualCells.IsValidIndex(CombinedCellIndex))
			{
				continue;
			}

			const FMD_ScrapeSurfaceCell& Cell =
				VisualCells[CombinedCellIndex];
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
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scrape_InitializePaintMask);
	ReleaseTransferredPaintMask();
    if (GetWorld() && GetWorld()->GetNetMode() == NM_DedicatedServer) return false;

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

    const bool bPristineEmptyPaint = UsesMeshUVSurface()
        && ContaminationPagedCells.IsPristineCanonicalInitial()
        && !HasLocalPredictedContaminationState();
    if (bPristineEmptyPaint)
    {
        TransferredPaintMaskAtlasDimensions = AtlasDimensions;
        TransferredPaintMaskAtlasPixels.InitializeUniform(static_cast<int32>(PixelCount64), 0);
        TransferredPaintMaskTexture = FMD_ScrapeRuntimeMaskTexture::AcquireInitial(
            AtlasDimensions, TransferredPaintMaskAtlasPixels, TF_Bilinear);
        if (!TransferredPaintMaskTexture) { TransferredPaintMaskAtlasPixels.Empty(); return false; }
        return true;
    }
	TArray<uint8> AtlasPixels;
	AtlasPixels.SetNumZeroed(static_cast<int32>(PixelCount64));
	bool bHasTransferredPaint = false;
	for (const FMD_ScrapeSurfaceCell& Cell : GetVisualContaminationCells())
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

    TransferredPaintMaskAtlasDimensions = AtlasDimensions;
    TransferredPaintMaskAtlasPixels = MoveTemp(AtlasPixels);
    TransferredPaintMaskTexture = FMD_ScrapeRuntimeMaskTexture::AcquireInitial(
        AtlasDimensions, TransferredPaintMaskAtlasPixels, UsesMeshUVSurface() ? TF_Bilinear : TF_Nearest);
    if (!TransferredPaintMaskTexture) { TransferredPaintMaskAtlasPixels.Empty(); return false; }
	return true;
}


void UMD_ScrapeSurfaceComponent::ReleaseTransferredPaintMask()
{
	if (TransferredPaintMaskTexture)
	{
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
	if (UsesMeshUVSurface())
	{
		// The caller includes both physical seam and private gutter dependents.
		// Recompute only these paint texels, so sparse changes do not
		// require scanning the large atlas rectangle between unrelated UV islands.
		FIntRect ChangedRegion = FMD_ScrapeMaskAtlas::MakeEmptyAtlasRegion();
		for (const FMD_ScrapeCellChange& CellChange : CellChanges)
		{
			const int32 PixelIndex = CellChange.CellIndex;
			if (PixelIndex < 0 || PixelIndex >= CellsPerFace
				|| !TransferredPaintMaskAtlasPixels.IsValidIndex(PixelIndex))
			{
				continue;
			}
			const FIntPoint PixelCoordinate(
				PixelIndex % FaceDimensions.X, PixelIndex / FaceDimensions.X);
			const uint8 NewValue = CalculateTransferredPaintMaskPixel(
				EMD_ScrapeSurfaceFace::Front, PixelCoordinate, FaceDimensions);
			if (TransferredPaintMaskAtlasPixels[PixelIndex] == NewValue)
			{
				continue;
			}
			TransferredPaintMaskAtlasPixels.Set(PixelIndex, NewValue);
			FMD_ScrapeMaskAtlas::ExpandAtlasRegion(ChangedRegion, PixelCoordinate);
		}
		if (!FMD_ScrapeMaskAtlas::IsAtlasRegionEmpty(ChangedRegion)
			&& !UploadTransferredPaintMaskRegion(ChangedRegion))
		{
			InitializeTransferredPaintMask();
			RefreshContaminationDecalMaterialParameters();
		}
		return;
	}
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
			TransferredPaintMaskAtlasPixels.Set(PixelIndex, NewValue);
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


bool UMD_ScrapeSurfaceComponent::UploadTransferredPaintMaskRegion(const FIntRect& AtlasRegion)
{
    auto* Updated = FMD_ScrapeRuntimeMaskTexture::UpdateMask(TransferredPaintMaskTexture, TransferredPaintMaskAtlasPixels);
    if (!Updated) return false;
    if (Updated != TransferredPaintMaskTexture)
    {
        TransferredPaintMaskTexture = Updated;
        RefreshContaminationDecalMaterialParameters();
    }
    return true;
}


void UMD_ScrapeSurfaceComponent::UpdateContaminationMask(
	const TArray<FMD_ScrapeCellChange>& CellChanges)
{
	SCOPE_CYCLE_COUNTER(STAT_MD_ScrapeMaskUpdate);
	if (!bContaminationEnabled || (GetWorld() && GetWorld()->GetNetMode() == NM_DedicatedServer))
	{
		return;
	}

	if (CellChanges.IsEmpty())
	{
		return;
	}

    // Geometry uses full precision state, including paint-only changes and prediction rollback.
    // Do this before the G8 dirty-region early-outs.
    UpdateSolidContaminationVisual(CellChanges);
    UpdateContaminationBoundaryVisual(CellChanges);

    TArray<FMD_ScrapeCellChange> ExpandedChanges;
    const TArray<FMD_ScrapeCellChange>* VisualChanges = &CellChanges;
    const UMD_ScrapeMeshUVData* VisualData = GetContaminationMeshUVData();
    if (VisualData)
    {
        const TMultiMap<int32, int32>& VisualDependents = VisualData->GetVisualDependents();
        ExpandedChanges = CellChanges;
        TSet<int32> Added;
        for (const FMD_ScrapeCellChange& Change : CellChanges) { Added.Add(Change.CellIndex); }
        for (const FMD_ScrapeCellChange& Change : CellChanges)
        {
            for (const int32 Pixel : VisualData->GetVisualSeamDependents(Change.CellIndex))
            {
                if (!Added.Contains(Pixel))
                {
                    Added.Add(Pixel);
                    FMD_ScrapeCellChange Copy;
                    Copy.CellIndex = Pixel;
                    ExpandedChanges.Add(Copy);
                }
            }
            for (auto It = VisualDependents.CreateConstKeyIterator(Change.CellIndex); It; ++It)
            {
                if (!Added.Contains(It.Value()))
                {
                    Added.Add(It.Value());
                    FMD_ScrapeCellChange Copy;
                    Copy.CellIndex = It.Value();
                    ExpandedChanges.Add(Copy);
                }
            }
        }
        VisualChanges = &ExpandedChanges;
    }
    UpdateContaminationGenerationOverride(*VisualChanges);
    UpdateTransferredPaintMask(*VisualChanges);

	FIntPoint ExpectedDimensions = FIntPoint::ZeroValue;
	if (!FMD_ScrapeMaskAtlas::TryCalculateAtlasDimensions(
			ContaminationGridDimensions,
			ExpectedDimensions))
	{
		return;
	}

	if (UsesMeshUVSurface()) { ExpectedDimensions = ContaminationGridDimensions; }
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
	const FMD_ScrapeConstCellView VisualCells =
		GetVisualContaminationCells();
	for (const FMD_ScrapeCellChange& CellChange : *VisualChanges)
	{
		if (!VisualCells.IsValidIndex(CellChange.CellIndex))
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
        if (UsesMeshUVSurface())
        {
            uint8 MaskValue = 0;
            if (!FMD_ScrapeMaskAtlas::TryQuantizeRemainingContamination(
                MaskCell.RemainingContamination, MaskCell.InitialContamination, MaskValue)) { continue; }
            if (PreviousMaskValue == MaskValue) { continue; }
            ContaminationMaskAtlasPixels.Set(PixelIndex, MaskValue);
            FMD_ScrapeMaskAtlas::ExpandAtlasRegion(ChangedRegion,
                FIntPoint(PixelIndex % ExpectedDimensions.X, PixelIndex / ExpectedDimensions.X));
            continue;
        }
        uint8 MaskValue = 0;
        if (!FMD_ScrapeMaskAtlas::TryQuantizeRemainingContamination(
            MaskCell.RemainingContamination, MaskCell.InitialContamination, MaskValue)
            || !ContaminationMaskAtlasPixels.Set(PixelIndex, MaskValue)) continue;
        PixelCoordinate = FIntPoint(PixelIndex % ExpectedDimensions.X, PixelIndex / ExpectedDimensions.X);

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

void UMD_ScrapeSurfaceComponent::BeginContaminationVisualUpdateBatch()
{
	if (ContaminationVisualUpdateBatchDepth == 0)
	{
		PendingBatchedContaminationVisualCellIndices.Empty();
	}
	++ContaminationVisualUpdateBatchDepth;
}

void UMD_ScrapeSurfaceComponent::EndContaminationVisualUpdateBatch()
{
	if (ContaminationVisualUpdateBatchDepth <= 0)
	{
		return;
	}
	--ContaminationVisualUpdateBatchDepth;
	if (ContaminationVisualUpdateBatchDepth > 0)
	{
		return;
	}

	PendingBatchedContaminationVisualCellIndices.Sort();
	TArray<FMD_ScrapeCellChange> CellChanges;
	CellChanges.Reserve(PendingBatchedContaminationVisualCellIndices.Num());
	int32 PreviousCellIndex = INDEX_NONE;
	for (const int32 CellIndex : PendingBatchedContaminationVisualCellIndices)
	{
		if (CellIndex == PreviousCellIndex)
		{
			continue;
		}
		FMD_ScrapeCellChange& CellChange = CellChanges.AddDefaulted_GetRef();
		CellChange.CellIndex = CellIndex;
		PreviousCellIndex = CellIndex;
	}
	PendingBatchedContaminationVisualCellIndices.Empty();
	UpdateContaminationMask(CellChanges);
}


bool UMD_ScrapeSurfaceComponent::UploadContaminationMaskRegion(const FIntRect& AtlasRegion)
{
    auto* Updated = FMD_ScrapeRuntimeMaskTexture::UpdateMask(ContaminationMaskTexture, ContaminationMaskAtlasPixels);
    if (!Updated) return false;
    if (Updated != ContaminationMaskTexture)
    {
        ContaminationMaskTexture = Updated;
        RefreshContaminationDecalMaterialParameters();
    }
    return true;
}


bool UMD_ScrapeSurfaceComponent::ConfigureContaminationDecals()
{
	bContaminationDecalsConfigured = false;
	bResolvedFullCoverageContaminationDecals = false;
	SetContaminationDecalsVisible(false);
	DestroyAdditionalContaminationDecalPatches();
	if (!bContaminationEnabled || (GetWorld() && GetWorld()->GetNetMode() == NM_DedicatedServer))
	{
		ReleaseContaminationMeshUVOverlay();
		return false;
	}

	// MeshUV는 투영 자체를 쓰지 않는다. 프로젝터 배치·깊이 해결·면 소유권
	// 계산을 전부 건너뛰고 메시에 오버레이 하나만 붙인다.
	if (ContaminationSurfaceMapping.Mode
		== EMD_ScrapeSurfaceMappingMode::MeshUV)
	{
		return ConfigureContaminationMeshUVOverlay();
	}
	ReleaseContaminationMeshUVOverlay();

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


bool UMD_ScrapeSurfaceComponent::ConfigureContaminationMeshUVOverlay()
{
	if (!bShowContaminationDecals
		|| !bContaminationSurfaceInitialized
		|| !ContaminationMaskTexture
		|| !SurfaceMesh)
	{
		ReleaseContaminationMeshUVOverlay();
		return false;
	}

    if (!ContaminationMeshUVData || SurfaceMesh->GetStaticMesh() != ContaminationMeshUVData->BakedRenderMesh)
    {
        ReleaseContaminationMeshUVOverlay();
        UE_LOG(LogTemp, Warning, TEXT("MeshUV overlay requires the profile's matching baked render mesh."));
        return false;
    }
	if (!ContaminationMeshUVMaterial)
	{
		ReleaseContaminationMeshUVOverlay();
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("MD_CuttableWall::ConfigureContaminationMeshUVOverlay - %s는 MeshUV 모드지만 ContaminationMeshUVMaterial이 비어 있습니다."),
			*GetName());
		return false;
	}

	// Mesh overlays are ordered above the base surface in the translucent pass.
	// Masked overlays share opaque depth/sort rules and can be overwritten by the wall.
	// Use the opacity mask as translucent coverage instead.
	const UMaterial* const BaseMaterial =
		ContaminationMeshUVMaterial->GetMaterial();
	if (!BaseMaterial || BaseMaterial->MaterialDomain != MD_Surface
        || ContaminationMeshUVMaterial->GetBlendMode() != BLEND_Translucent)
	{
		ReleaseContaminationMeshUVOverlay();
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("MD_CuttableWall::ConfigureContaminationMeshUVOverlay - %s의 ContaminationMeshUVMaterial은 Surface Domain / Translucent여야 합니다. 마스크는 재질의 피복량(Opacity 또는 Substrate Coverage Weight)에 연결하세요."),
			*GetName());
		return false;
	}

	if (!ContaminationMeshUVOverlayMID
		|| ContaminationMeshUVOverlayMID->Parent
			!= ContaminationMeshUVMaterial)
	{
		ContaminationMeshUVOverlayMID = UMaterialInstanceDynamic::Create(
			ContaminationMeshUVMaterial,
			this);
	}
	if (!ContaminationMeshUVOverlayMID)
	{
		return false;
	}

	ApplyContaminationMeshUVOverlayParameters(*ContaminationMeshUVOverlayMID);
	SurfaceMesh->SetOverlayMaterial(ContaminationMeshUVOverlayMID);
    ConfigureSolidContaminationVisual();

	bContaminationDecalsConfigured = true;
	// 모든 셀이 하나의 unwrap 주소 공간에 있으므로 부분 피복이라는 개념이 없다.
	bResolvedFullCoverageContaminationDecals = true;
	return true;
}


void UMD_ScrapeSurfaceComponent::ReleaseContaminationMeshUVOverlay()
{
	if (SurfaceMesh && ContaminationMeshUVOverlayMID)
	{
		SurfaceMesh->SetOverlayMaterial(nullptr);
	}
	ContaminationMeshUVOverlayMID = nullptr;
}


bool UMD_ScrapeSurfaceComponent::ConfigureContaminationScanMaterial(UMaterialInstanceDynamic* Material) const
{
    if (!Material || !bContaminationEnabled || !bContaminationSurfaceInitialized
        || !UsesMeshUVSurface() || !SurfaceMesh || !ContaminationMaskTexture) return false;
    ApplyContaminationMeshUVOverlayParameters(*Material);
    return true;
}

void UMD_ScrapeSurfaceComponent::ApplyContaminationMeshUVOverlayParameters(
	UMaterialInstanceDynamic& OverlayMID) const
{
    ApplyContaminationBoundaryParameters(OverlayMID);
    OverlayMID.SetScalarParameterValue(TEXT("ContaminationGridWidth"), static_cast<float>(ContaminationGridDimensions.X));
    OverlayMID.SetScalarParameterValue(TEXT("ContaminationGridHeight"), static_cast<float>(ContaminationGridDimensions.Y));
	OverlayMID.SetTextureParameterValue(
		ContaminationMaskTextureParameterName,
		ContaminationMaskTexture);
	OverlayMID.SetTextureParameterValue(
		TransferredPaintMaskTextureParameterName,
		TransferredPaintMaskTexture);
	ApplyContaminationAppearanceParameters(
		OverlayMID,
		ContaminationMeshUVMaterial);

	const float DetailTileWorldSizeCm = FMath::IsFinite(
		ContaminationDetailTileWorldSizeCm)
		? FMath::Max(ContaminationDetailTileWorldSizeCm, 1.0f)
		: 150.0f;
	OverlayMID.SetScalarParameterValue(
		ContaminationDetailTileWorldSizeParameterName,
		DetailTileWorldSizeCm);

	// MeshUV contains all physical faces in a single W x H canonical atlas.
	OverlayMID.SetScalarParameterValue(
		ContaminationMaskVScaleParameterName,
		1.0f);
	OverlayMID.SetScalarParameterValue(
		ContaminationMaskVOffsetParameterName,
		0.0f);
	OverlayMID.SetScalarParameterValue(
		ContaminationMaskUFlipParameterName,
		0.0f);

	// inset이 없으므로 데칼용 보정이 아니라 순수 반 텍셀만 넣는다.
	FVector2D HalfTexelUV = FVector2D::ZeroVector;
	if (FMD_ScrapeMaskAtlas::TryCalculateHalfTexelUV(
			ContaminationMaskAtlasDimensions,
			HalfTexelUV))
	{
		OverlayMID.SetScalarParameterValue(
			ContaminationMaskHalfTexelUParameterName,
			static_cast<float>(HalfTexelUV.X));
		OverlayMID.SetScalarParameterValue(
			ContaminationMaskHalfTexelVParameterName,
			static_cast<float>(HalfTexelUV.Y));
	}

	OverlayMID.SetScalarParameterValue(
		ContaminationSurfaceWidthParameterName,
		static_cast<float>(ResolvedContaminationSurfaceSizeCm.X));
	OverlayMID.SetScalarParameterValue(
		ContaminationSurfaceHeightParameterName,
		static_cast<float>(ResolvedContaminationSurfaceSizeCm.Y));
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
	ApplyContaminationDetailWorldFrame(*InOutMID, Face);
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
	if (bResolvedFullCoverageContaminationDecals
		&& SurfaceMesh
		&& ContaminationSurfaceMapping.Mode
			== EMD_ScrapeSurfaceMappingMode::CurvedFrontPlanarBack)
	{
		bEnableOwnership = FMD_ScrapeSurfaceGeometry::
			TryResolveCurvedFrontPlanarBackSplitPlane(
				SurfaceMesh->GetComponentTransform(),
				ContaminationSurfaceMapping,
				SplitOriginWS,
				SplitNormalWS);
	}

	FVector HorizontalAxisLocal;
	FVector FrontNormalLocal;
	FVector VerticalAxisLocal;
	if (!bEnableOwnership
		&& bResolvedFullCoverageContaminationDecals
		&& ContaminationSurfaceMapping.Mode
			!= EMD_ScrapeSurfaceMappingMode::CylindricalArc
		&& ContaminationSurfaceMapping.Mode
			!= EMD_ScrapeSurfaceMappingMode::CurvedFrontPlanarBack
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
	UMaterialInstanceDynamic& DecalMID,
	const EMD_ScrapeSurfaceFace Face) const
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
	FVector VerticalAxisWorld =
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

	FMD_ScrapeCylindricalSurfaceFrame CurveFrame;
	const bool bHasCurveFrame =
		(ContaminationSurfaceMapping.Mode
				== EMD_ScrapeSurfaceMappingMode::CylindricalArc
			|| ContaminationSurfaceMapping.Mode
				== EMD_ScrapeSurfaceMappingMode::CurvedFrontPlanarBack)
		&& FMD_ScrapeSurfaceGeometry::TryResolveCylindricalSurfaceFrame(
			MeshTransform,
			ContaminationSurfaceMapping,
			CurveFrame);
	const bool bUseCylindricalCanonicalU = bHasCurveFrame
		&& (ContaminationSurfaceMapping.Mode
				== EMD_ScrapeSurfaceMappingMode::CylindricalArc
			|| Face == EMD_ScrapeSurfaceFace::Front);
	if (bHasCurveFrame)
	{
		VerticalAxisWorld = CurveFrame.AxisWorld;
	}

	SetVectorParameter(
		ContaminationSurfaceOriginParameterName,
		SurfaceOriginWorld);
	SetVectorParameter(
		ContaminationSurfaceTangentUParameterName,
		HorizontalAxisWorld);
	SetVectorParameter(
		ContaminationSurfaceTangentVParameterName,
		VerticalAxisWorld);
	FVector2D PlanarSurfaceSizeCm = FVector2D::ZeroVector;
	const bool bHasPlanarSurfaceSize =
		FMD_ScrapeSurfaceGeometry::TryCalculateSurfaceSize(
			ResolvedContaminationSurfaceNormalAxis,
			ContaminationMeshLocalBoundsMinimum,
			ContaminationMeshLocalBoundsMaximum,
			MeshTransform.GetScale3D().GetAbs(),
			PlanarSurfaceSizeCm);
	DecalMID.SetScalarParameterValue(
		ContaminationPlanarSurfaceWidthParameterName,
		bHasPlanarSurfaceSize
			? static_cast<float>(PlanarSurfaceSizeCm.X)
			: static_cast<float>(ResolvedContaminationSurfaceSizeCm.X));
	DecalMID.SetScalarParameterValue(
		ContaminationUseCylindricalCanonicalUParameterName,
		bUseCylindricalCanonicalU ? 1.0f : 0.0f);
	if (bHasCurveFrame)
	{
		// 이 네 값은 M_Contamination_CanonicalUV의 atan2 입력과 정확히 같은
		// 월드 기준계다. CPU 셀 투영도 동일한 CurveFrame을 사용한다.
		SetVectorParameter(
			ContaminationCurveCenterParameterName,
			CurveFrame.CenterWorld);
		SetVectorParameter(
			ContaminationCurveAxisParameterName,
			CurveFrame.AxisWorld);
		SetVectorParameter(
			ContaminationCurveStartDirectionParameterName,
			CurveFrame.StartDirectionWorld);
		DecalMID.SetScalarParameterValue(
			ContaminationCurveSweepRadiansParameterName,
			static_cast<float>(CurveFrame.SweepRadians));
		DecalMID.SetScalarParameterValue(
			ContaminationCurveRadiusParameterName,
			static_cast<float>(CurveFrame.RadiusCm));
		DecalMID.SetScalarParameterValue(
			ContaminationCurveFrontIsOuterParameterName,
			CurveFrame.bFrontIsOuter ? 1.0f : 0.0f);
	}

	const FVector LocalBoundsSize = ContaminationMeshLocalBoundsMaximum
		- ContaminationMeshLocalBoundsMinimum;
	const double LocalThicknessCm = FMath::Abs(FVector::DotProduct(
		LocalBoundsSize,
		FrontNormalLocal));
	const double NormalWorldScale = MeshTransform
		.TransformVector(FrontNormalLocal)
		.Size();
	const double SurfaceThicknessCm = bUseCylindricalCanonicalU
		? CurveFrame.ThicknessCm
		: LocalThicknessCm * NormalWorldScale;
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
    ApplyContaminationGenerationParameters(DecalMID);
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
    if (ContaminationSolidVisual)
    {
        ContaminationSolidVisual->SetVisualsVisible(bShouldBeVisible);
    }
	if (ContaminationSurfaceMapping.Mode
		== EMD_ScrapeSurfaceMappingMode::MeshUV)
	{
		if (SurfaceMesh)
		{
			SurfaceMesh->SetOverlayMaterial(
				bShouldBeVisible
					? ContaminationMeshUVOverlayMID.Get()
					: nullptr);
		}
		return;
	}

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
	if (!bContaminationEnabled || (GetWorld() && GetWorld()->GetNetMode() == NM_DedicatedServer))
	{
		return;
	}

	if (ContaminationSurfaceMapping.Mode
		== EMD_ScrapeSurfaceMappingMode::MeshUV)
	{
		if (ContaminationMeshUVOverlayMID)
		{
			ApplyContaminationMeshUVOverlayParameters(
				*ContaminationMeshUVOverlayMID);
		}
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
		ApplyContaminationDetailWorldFrame(*DecalMID, Face);
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
