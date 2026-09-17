#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"
#include "Gameplay/Scrape/MD_ScrapeMeshUVData.h"
#include "Gameplay/Scrape/MD_ScrapeMaskAtlas.h"
#include "Gameplay/Scrape/MD_ScrapeRuntimeMaskTexture.h"
#include "MDScrapeBoundaryRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/StrongObjectPtr.h"
#include "HAL/IConsoleManager.h"
#include "Containers/Ticker.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
TAutoConsoleVariable<int32> CVarBoundary(TEXT("md.Scrape.BoundarySDF"),1,
    TEXT("Footprint-derived boundary reconstruction. 0 displays the existing mask for same-state comparison."));
TMap<TWeakObjectPtr<UMD_ScrapeSurfaceComponent>,uint64> BoundaryAllocations;
uint64 PageMapBytes=0;
FTSTicker::FDelegateHandle BoundaryMaintenance;
constexpr uint64 MaximumPageMapBytes=8ull*1024*1024;
struct FBoundaryPool
{
    TStrongObjectPtr<UTextureRenderTarget2D> Atlas;
    TBitArray<> Used;
    FBoundaryPool()
    {
        auto* T=NewObject<UTextureRenderTarget2D>(GetTransientPackage(),NAME_None,RF_Transient);
        T->bCanCreateUAV=true; T->bAutoGenerateMips=false;
        T->ClearColor=FLinearColor(8,0,0,0); T->Filter=TF_Bilinear;
        T->AddressX=TA_Clamp; T->AddressY=TA_Clamp;
        T->InitCustomFormat(FMDScrapeBoundaryRenderer::AtlasSize,FMDScrapeBoundaryRenderer::AtlasSize,PF_FloatRGBA,true);
        T->UpdateResourceImmediate(true); Atlas.Reset(T);
        Used.Init(false,FMDScrapeBoundaryRenderer::TileCapacity);
    }
    int32 Acquire()
    {
        for (int32 I=0;I<Used.Num();++I) if (!Used[I]) { Used[I]=true; return I; }
        return INDEX_NONE;
    }
};
// Keep one idle pool until the last registered surface leaves the world.
// Repeated visibility changes must not create orphaned 32 MiB RTs awaiting GC.
TSharedPtr<FBoundaryPool> SharedPool;
}
struct FMD_ScrapeBoundaryVisual
{
    TSharedPtr<FBoundaryPool> Pool;
    TStrongObjectPtr<UTexture2D> Low,High;
    FMD_ScrapeMaskPixels LowPixels,HighPixels;
    FIntPoint MapSize=FIntPoint::ZeroValue;
    TMap<int32,int32> Slots;
    TSet<int32> Dirty;
    uint64 ReservedMapBytes=0;
    ~FMD_ScrapeBoundaryVisual()
    {
        if (Pool) for (const auto& Pair : Slots) Pool->Used[Pair.Value]=false;
        PageMapBytes-=ReservedMapBytes;
    }
};

void UMD_ScrapeSurfaceComponent::ResetContaminationBoundary()
{
    ReleaseContaminationBoundaryVisual();
    ContaminationBoundary.Reset(FGuid(),FIntPoint::ZeroValue);
    PredictedContaminationBoundary.Reset(FGuid(),FIntPoint::ZeroValue);
    bHasPredictedContaminationBoundary=false;
    BoundaryAllocations.Remove(this);
    if (BoundaryAllocations.IsEmpty()) SharedPool.Reset();
    LastBoundaryVisualRevision=0; LastBoundaryEpoch.Invalidate();
}
void UMD_ScrapeSurfaceComponent::ResetPredictedContaminationBoundary()
{
    if (ContaminationBoundaryVisual)
        for (const auto& P : ContaminationBoundaryVisual->Slots) ContaminationBoundaryVisual->Dirty.Add(P.Key);
    PredictedContaminationBoundary.Reset(FGuid(),FIntPoint::ZeroValue);
    bHasPredictedContaminationBoundary=false;
}
bool UMD_ScrapeSurfaceComponent::CheckContaminationBoundaryBudget()
{
    const uint64 Bytes=ContaminationBoundary.GetAllocatedBytes()
        +(bHasPredictedContaminationBoundary?PredictedContaminationBoundary.GetAllocatedBytes():0);
    BoundaryAllocations.FindOrAdd(this)=Bytes;
    if (!BoundaryMaintenance.IsValid() && GetWorld() && GetWorld()->GetNetMode()!=NM_DedicatedServer)
    {
        // One weak registry services retries and releases invisible caches. Current
        // geometry stays on the surface and reconstructs when it is drawn again.
        BoundaryMaintenance=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
        {
            for (auto It=BoundaryAllocations.CreateIterator();It;++It)
            {
                auto* S=It.Key().Get();
                if (!S) { It.RemoveCurrent(); continue; }
                const auto* Owner=S->GetOwner();
                if (!Owner || !Owner->WasRecentlyRendered(0.5f))
                { if (S->ContaminationBoundaryVisual) S->ReleaseContaminationBoundaryVisual(); continue; }
                S->UpdateContaminationBoundaryVisual({});
            }
            if (BoundaryAllocations.IsEmpty()) { BoundaryMaintenance.Reset(); return false; }
            return true;
        }),0.1f);
    }
    uint64 Total=0;
    for (auto It=BoundaryAllocations.CreateIterator();It;++It)
    {
        if (!It.Key().IsValid()) { It.RemoveCurrent(); continue; }
        Total+=It.Value();
    }
    if (Total<=FMD_ScrapeBoundaryState::MaximumWorldBytes) return true;
    auto& State=bHasPredictedContaminationBoundary?PredictedContaminationBoundary:ContaminationBoundary;
    State.Patches.Empty(); State.bDisabled=true;
    BoundaryAllocations.FindOrAdd(this)=ContaminationBoundary.GetAllocatedBytes()
        +(bHasPredictedContaminationBoundary?PredictedContaminationBoundary.GetAllocatedBytes():0);
    return false;
}
bool UMD_ScrapeSurfaceComponent::CommitContaminationBoundary(const FMD_ScrapeEvaluationScratch& Scratch,bool bPrediction)
{
    if (!UsesMeshUVSurface() || !ContaminationMeshUVData) return false;
    if (ContaminationBoundary.LayoutId!=ContaminationMeshUVData->LayoutId
        || ContaminationBoundary.Dimensions!=ContaminationGridDimensions)
        ContaminationBoundary.Reset(ContaminationMeshUVData->LayoutId,ContaminationGridDimensions);
    if (bPrediction && !bHasPredictedContaminationBoundary)
    { PredictedContaminationBoundary=ContaminationBoundary; bHasPredictedContaminationBoundary=true; }
    auto& State=bPrediction?PredictedContaminationBoundary:ContaminationBoundary;
    const uint64 Before=State.Generation; const bool WasDisabled=State.bDisabled;
    if (Scratch.bBoundaryOverflow && !State.bDisabled)
    { State.Patches.Empty(); State.bDisabled=true; ++State.Generation; }
    else State.Apply(Scratch.BoundaryFootprints);
    CheckContaminationBoundaryBudget();
    return Before!=State.Generation || WasDisabled!=State.bDisabled;
}
bool UMD_ScrapeSurfaceComponent::ExportContaminationBoundary(TArray<uint8>& Out) const
{
    Out.Reset();
    if (!UsesMeshUVSurface() || ContaminationBoundary.LayoutId!=GetContaminationLayoutId()) return true;
    return ContaminationBoundary.Serialize(Out);
}
bool UMD_ScrapeSurfaceComponent::ValidateContaminationBoundary(TConstArrayView<uint8> Bytes) const
{
    if (!UsesMeshUVSurface() || !ContaminationMeshUVData) return Bytes.IsEmpty();
    FMD_ScrapeBoundaryState Next;
    if (!Next.Deserialize(Bytes,GetContaminationLayoutId(),ContaminationGridDimensions)) return false;
    for (const auto& P : Next.Patches)
        if (!ContaminationMeshUVData->Triangles.IsValidIndex(int32(uint32(P.Key)))) return false;
    return true;
}
bool UMD_ScrapeSurfaceComponent::RestoreContaminationBoundary(TConstArrayView<uint8> Bytes, uint32 VisualRevision, FGuid Epoch)
{
    if (Epoch.IsValid() && LastBoundaryEpoch==Epoch && LastBoundaryVisualRevision!=0
        && IsVisualRevisionNewer(LastBoundaryVisualRevision,VisualRevision)) return true;
    if (!UsesMeshUVSurface() || !ContaminationMeshUVData) return Bytes.IsEmpty();
    FMD_ScrapeBoundaryState Next;
    if (!Next.Deserialize(Bytes,GetContaminationLayoutId(),ContaminationGridDimensions)) return false;
    for (const auto& P : Next.Patches)
        if (!ContaminationMeshUVData->Triangles.IsValidIndex(int32(uint32(P.Key)))) return false;
    ContaminationBoundary=MoveTemp(Next);
    LastBoundaryVisualRevision=VisualRevision; LastBoundaryEpoch=Epoch;
    CheckContaminationBoundaryBudget();
    RebuildLocalScrapePredictionShadow();
    UpdateContaminationBoundaryVisual({});
    return true;
}
void UMD_ScrapeSurfaceComponent::ReleaseContaminationBoundaryVisual()
{
    ContaminationBoundaryVisual.Reset();
    if (ContaminationMeshUVOverlayMID)
    {
        ContaminationMeshUVOverlayMID->SetScalarParameterValue(TEXT("BoundarySDFEnabled"),0);
        ContaminationMeshUVOverlayMID->SetTextureParameterValue(TEXT("BoundarySDFAtlas"),nullptr);
        ContaminationMeshUVOverlayMID->SetTextureParameterValue(TEXT("BoundaryPageLow"),nullptr);
        ContaminationMeshUVOverlayMID->SetTextureParameterValue(TEXT("BoundaryPageHigh"),nullptr);
    }
    for (const auto& P : ContaminationBoundary.Patches) ContaminationBoundary.DirtyTiles.Add(int32(P.Key>>32));
    for (const auto& P : PredictedContaminationBoundary.Patches) PredictedContaminationBoundary.DirtyTiles.Add(int32(P.Key>>32));
}
void UMD_ScrapeSurfaceComponent::ApplyContaminationBoundaryParameters(UMaterialInstanceDynamic& MID) const
{
    const auto& State=bHasPredictedContaminationBoundary?PredictedContaminationBoundary:ContaminationBoundary;
    const auto& V=ContaminationBoundaryVisual;
    const bool Enabled=CVarBoundary.GetValueOnGameThread()!=0 && V && V->Pool && V->Low.IsValid() && V->High.IsValid() && !State.bDisabled;
    MID.SetScalarParameterValue(TEXT("BoundarySDFEnabled"),Enabled?1.0f:0.0f);
    if (!Enabled) return;
    MID.SetTextureParameterValue(TEXT("BoundarySDFAtlas"),V->Pool->Atlas.Get());
    MID.SetTextureParameterValue(TEXT("BoundaryPageLow"),V->Low.Get());
    MID.SetTextureParameterValue(TEXT("BoundaryPageHigh"),V->High.Get());
}
void UMD_ScrapeSurfaceComponent::UpdateContaminationBoundaryVisual(const TArray<FMD_ScrapeCellChange>& Changes)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scrape_BoundaryDirtyTiles);
    if (!UsesMeshUVSurface() || !ContaminationMeshUVData || !GetWorld()
        || GetWorld()->GetNetMode()==NM_DedicatedServer) return;
    auto& State=bHasPredictedContaminationBoundary?PredictedContaminationBoundary:ContaminationBoundary;
    if (State.bDisabled || State.Patches.IsEmpty())
    {
        if (ContaminationBoundaryVisual) ReleaseContaminationBoundaryVisual();
        return;
    }
    const int32 Columns=State.TileColumns();
    const int32 Rows=FMath::DivideAndRoundUp(State.Dimensions.Y,8);
    if (!ContaminationBoundaryVisual)
    {
        // Two G8 pages, including conservative CPU/RHI ownership headroom.
        const uint64 Reservation=uint64(Columns)*Rows*8;
        if (PageMapBytes+Reservation>MaximumPageMapBytes) return;
        auto V=MakeShared<FMD_ScrapeBoundaryVisual>();
        V->MapSize=FIntPoint(Columns,Rows); V->ReservedMapBytes=Reservation;
        PageMapBytes+=Reservation;
        V->Pool=SharedPool;
        if (!V->Pool) { V->Pool=MakeShared<FBoundaryPool>(); SharedPool=V->Pool; }
        TArray<uint8> Zero; Zero.SetNumZeroed(Columns*Rows);
        V->LowPixels=TArray<uint8>(Zero); V->HighPixels=MoveTemp(Zero);
        V->Low.Reset(FMD_ScrapeRuntimeMaskTexture::AcquireInitial(V->MapSize,V->LowPixels,TF_Nearest));
        V->High.Reset(FMD_ScrapeRuntimeMaskTexture::AcquireInitial(V->MapSize,V->HighPixels,TF_Nearest));
        if (!V->Low.IsValid() || !V->High.IsValid()) return;
        ContaminationBoundaryVisual=V;
        for (const auto& P : State.Patches) V->Dirty.Add(int32(P.Key>>32));

    }
    auto& V=*ContaminationBoundaryVisual;
    V.Dirty.Append(State.DirtyTiles); State.DirtyTiles.Empty();
    for (const auto& Change : Changes)
    {
        const int32 X=Change.CellIndex%State.Dimensions.X,Y=Change.CellIndex/State.Dimensions.X;
        for (int32 TY=FMath::Max(0,(Y-1)/8);TY<=FMath::Min(Rows-1,(Y+1)/8);++TY)
            for (int32 TX=FMath::Max(0,(X-1)/8);TX<=FMath::Min(Columns-1,(X+1)/8);++TX)
            {
                const int32 Tile=TX+TY*Columns;
                if (V.Slots.Contains(Tile)) V.Dirty.Add(Tile);
            }
    }
    if (V.Dirty.IsEmpty()) { if (ContaminationMeshUVOverlayMID) ApplyContaminationBoundaryParameters(*ContaminationMeshUVOverlayMID); return; }
    const auto Cells=GetVisualContaminationCells();
    TArray<int32> Dirty=V.Dirty.Array();
    Dirty.Sort([&V](int32 A,int32 B)
    {
        // Full admission must not starve updates to tiles that already reside.
        const bool HasA=V.Slots.Contains(A),HasB=V.Slots.Contains(B);
        return HasA!=HasB?HasA:A<B;
    });
    // Work is capped per submission; remaining dirty tiles are coalesced in place.
    if (Dirty.Num()>8) Dirty.SetNum(8);
    TArray<FMDScrapeBoundaryTileUpload> Uploads;
    TArray<int32> Ready;
    for (const int32 Tile : Dirty)
    {
        int32* Existing=V.Slots.Find(Tile);
        int32 Slot=Existing?*Existing:V.Pool->Acquire();
        if (Slot==INDEX_NONE) { continue; }
        if (!Existing) V.Slots.Add(Tile,Slot);
        FMDScrapeBoundaryTileUpload& U=Uploads.AddDefaulted_GetRef();
        U.LogicalOrigin=FIntPoint(Tile%Columns,Tile/Columns)*8;
        U.PhysicalOrigin=FIntPoint(Slot%FMDScrapeBoundaryRenderer::TileColumns,Slot/FMDScrapeBoundaryRenderer::TileColumns)*80;
        U.Cells.SetNumZeroed(100); U.Domains.SetNumZeroed(300);
        TMap<int32,FIntPoint> Ranges;
        for (int32 Y=0;Y<10;++Y) for (int32 X=0;X<10;++X)
        {
            const int32 Local=X+Y*10;
            const int32 AX=FMath::Clamp(U.LogicalOrigin.X+X-1,0,State.Dimensions.X-1);
            const int32 AY=FMath::Clamp(U.LogicalOrigin.Y+Y-1,0,State.Dimensions.Y-1);
            const int32 Address=AX+AY*State.Dimensions.X;
            int32 Source=ContaminationMeshUVData->Cells[Address].VisualSourceCellIndex;
            if (ContaminationMeshUVData->IsActiveCell(Address)) Source=Address;
            if (!Cells.IsValidIndex(Source)) continue;
            const int32 Triangle=ContaminationMeshUVData->Cells[Source].TriangleIndex;
            if (!ContaminationMeshUVData->Triangles.IsValidIndex(Triangle)) continue;
            const float Target=MakeContaminationMaskCell(Address).RemainingContamination;
            const auto& Cell=Cells[Source];
            FMD_ScrapeSurfaceCell InitialCell=Cell;
            InitialCell.RemainingContamination=Cell.InitialContamination;InitialCell.CoveredFraction=0.f;
            const float Original=CalculateContaminationVisualSignal(InitialCell);
            U.Cells[Local]=FVector4f(0,0,Target,Original);
            U.Domains[Local*3].W=float(Triangle+1);
            const auto* Patch=State.Find(Tile,Triangle);
            if (!Patch || Patch->bFallback) continue;
            FIntPoint* Range=Ranges.Find(Triangle);
            if (!Range)
            {
                const int32 Start=U.Edges.Num();
                auto AddRing=[&](const UE::Geometry::FPolygon2d& Ring)
                {
                    for (int32 I=0;I<Ring.VertexCount();++I)
                    {
                        const auto A=Ring[I],B=Ring[(I+1)%Ring.VertexCount()];
                        U.Edges.Add(FVector4f(A.X,A.Y,B.X,B.Y));
                    }
                };
                for (const auto& P : Patch->Polygons)
                { AddRing(P.GetOuter()); for (const auto& H : P.GetHoles()) AddRing(H); }
                Range=&Ranges.Add(Triangle,FIntPoint(Start,U.Edges.Num()-Start));
            }
            U.Cells[Local]=FVector4f(Range->X,Range->Y,Target,Original);
            if (Source==Address)
            {
                const auto& T=ContaminationMeshUVData->Triangles[Triangle];
                const FVector2D Scale(State.Dimensions);
                const FVector2D Points[]={T.UVA*Scale,T.UVB*Scale,T.UVC*Scale};
                const FVector2D B=Points[1]-Points[0],C=Points[2]-Points[0];
                const double Sign=B.X*C.Y-B.Y*C.X>=0?1.0:-1.0;
                for (int32 J=0;J<3;++J)
                {
                    const auto A=Points[J],E=Points[(J+1)%3]-A;
                    U.Domains[Local*3+J]=FVector4f(-E.Y*Sign,E.X*Sign,(E.Y*A.X-E.X*A.Y)*Sign,float(Triangle+1));
                }
            }
        }
        if (U.Edges.IsEmpty())
        {
            Uploads.Pop();
            V.Pool->Used[Slot]=false; V.Slots.Remove(Tile); V.Dirty.Remove(Tile);
            V.LowPixels.Set(Tile,0); V.HighPixels.Set(Tile,0);
            continue;
        }
        Ready.Add(Tile);
    }
    if (!Uploads.IsEmpty() && FMDScrapeBoundaryRenderer::Submit(V.Pool->Atlas.Get(),MoveTemp(Uploads)))
    {
        for (int32 Tile : Ready)
        {
            const int32 Encoded=V.Slots[Tile]+1;
            V.LowPixels.Set(Tile,uint8(Encoded&255)); V.HighPixels.Set(Tile,uint8(Encoded>>8));
            V.Dirty.Remove(Tile);
        }
    }
    V.Low.Reset(FMD_ScrapeRuntimeMaskTexture::UpdateMask(V.Low.Get(),V.LowPixels));
    V.High.Reset(FMD_ScrapeRuntimeMaskTexture::UpdateMask(V.High.Get(),V.HighPixels));
    if (ContaminationMeshUVOverlayMID) ApplyContaminationBoundaryParameters(*ContaminationMeshUVOverlayMID);
}
