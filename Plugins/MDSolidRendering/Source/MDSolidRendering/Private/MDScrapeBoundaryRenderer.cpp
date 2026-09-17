#include "MDScrapeBoundaryRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "Containers/Ticker.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include <atomic>

namespace
{
std::atomic<uint64> Pending{0};
struct FPendingBoundaryFence { FGPUFenceRHIRef Fence; uint64 Bytes; };
TArray<FPendingBoundaryFence> Fences;
FTSTicker::FDelegateHandle FenceTicker;
std::atomic<bool> PollQueued{false};
void Reap(FRHICommandListImmediate& Cmd)
{
    for (int32 I=Fences.Num()-1;I>=0;--I)
    {
        if (!Fences[I].Fence->Poll()) continue;
        Pending.fetch_sub(Fences[I].Bytes); Fences.RemoveAtSwap(I);
    }
}
void EnsureTicker()
{
    if (FenceTicker.IsValid()) return;
    FenceTicker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
    {
        if (Pending.load()==0)
        { FenceTicker.Reset(); return false; }
        if (!PollQueued.exchange(true))
        {
            ENQUEUE_RENDER_COMMAND(MDScrapeBoundaryReap)([](FRHICommandListImmediate& Cmd)
            { Reap(Cmd); PollQueued.store(false); });
        }
        return true;
    }));
}
class FMDScrapeBoundaryCS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FMDScrapeBoundaryCS);
    SHADER_USE_PARAMETER_STRUCT(FMDScrapeBoundaryCS,FGlobalShader);
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
        SHADER_PARAMETER(FIntPoint,LogicalOrigin)
        SHADER_PARAMETER(FIntPoint,PhysicalOrigin)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,Edges)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,Cells)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,Domains)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>,OutAtlas)
    END_SHADER_PARAMETER_STRUCT()
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P)
    { return IsFeatureLevelSupported(P.Platform,ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FMDScrapeBoundaryCS,"/Plugin/MDSolidRendering/Private/MDScrapeBoundary.usf","MainCS",SF_Compute);
class FMDScrapeBoundaryFilmCS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FMDScrapeBoundaryFilmCS);
    SHADER_USE_PARAMETER_STRUCT(FMDScrapeBoundaryFilmCS,FGlobalShader);
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
        SHADER_PARAMETER(FIntPoint,PhysicalOrigin)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,Domains)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>,RawTile)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>,OutAtlas)
    END_SHADER_PARAMETER_STRUCT()
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P)
    { return IsFeatureLevelSupported(P.Platform,ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FMDScrapeBoundaryFilmCS,"/Plugin/MDSolidRendering/Private/MDScrapeBoundary.usf","FilterFilmCS",SF_Compute);
}

bool FMDScrapeBoundaryRenderer::Submit(UTextureRenderTarget2D* Atlas,TArray<FMDScrapeBoundaryTileUpload>&& Tiles)
{
    check(IsInGameThread());
    if (!Atlas || Tiles.IsEmpty()) return false;
    uint64 Bytes=0;
    for (const auto& T : Tiles)
    {
        if (T.Cells.Num()!=100 || T.Domains.Num()!=300 || T.Edges.Num()>51200
            || T.PhysicalOrigin.X<0 || T.PhysicalOrigin.Y<0
            || T.PhysicalOrigin.X+TileSize>AtlasSize || T.PhysicalOrigin.Y+TileSize>AtlasSize) return false;
        Bytes+=Align(uint64(T.Edges.Num()+400)*sizeof(FVector4f),uint64(512))+4096;
    }
    const uint64 Before=Pending.fetch_add(Bytes);
    if (Bytes>MaximumPendingBytes || Before>MaximumPendingBytes-Bytes)
    { Pending.fetch_sub(Bytes); return false; }
    auto* Resource=Atlas->GameThread_GetRenderTargetResource();
    if (!Resource) { Pending.fetch_sub(Bytes); return false; }
    EnsureTicker();
    ENQUEUE_RENDER_COMMAND(MDScrapeBoundaryDirtyTiles)(
        [Resource,Tiles=MoveTemp(Tiles),Bytes](FRHICommandListImmediate& Cmd)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scrape_BoundaryGPUUpload);
        Reap(Cmd);
        FRDGBuilder Graph(Cmd);
        auto Texture=Graph.RegisterExternalTexture(CreateRenderTarget(Resource->GetRenderTargetTexture(),TEXT("MD_ScrapeBoundaryAtlas")));
        auto UAV=Graph.CreateUAV(Texture);
        TShaderMapRef<FMDScrapeBoundaryCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        TShaderMapRef<FMDScrapeBoundaryFilmCS> FilmShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        auto Raw=Graph.CreateTexture(FRDGTextureDesc::Create2D(FIntPoint(TileSize,TileSize),PF_FloatRGBA,
            FClearValueBinding::None,TexCreate_ShaderResource|TexCreate_UAV),TEXT("MD_BoundaryRawTile"));
        auto RawUAV=Graph.CreateUAV(Raw);
        for (const auto& T : Tiles)
        {
            auto* P=Graph.AllocParameters<FMDScrapeBoundaryCS::FParameters>();
            P->LogicalOrigin=T.LogicalOrigin; P->PhysicalOrigin=FIntPoint::ZeroValue;
            const FVector4f Empty(0,0,0,0);
            P->Edges=Graph.CreateSRV(CreateStructuredBuffer(Graph,TEXT("MD_BoundaryEdges"),
                sizeof(FVector4f),FMath::Max(1,T.Edges.Num()),T.Edges.IsEmpty()?&Empty:T.Edges.GetData(),
                sizeof(FVector4f)*FMath::Max(1,T.Edges.Num())));
            P->Cells=Graph.CreateSRV(CreateStructuredBuffer(Graph,TEXT("MD_BoundaryCells"),T.Cells));
            P->Domains=Graph.CreateSRV(CreateStructuredBuffer(Graph,TEXT("MD_BoundaryDomains"),T.Domains));
            P->OutAtlas=RawUAV;
            {
                RDG_EVENT_SCOPE(Graph,"MD_ScrapeBoundary_Distance");
                FComputeShaderUtils::AddPass(Graph,RDG_EVENT_NAME("MD_ScrapeBoundaryDirtyTile"),Shader,P,FIntVector(10,10,1));
            }
            auto* F=Graph.AllocParameters<FMDScrapeBoundaryFilmCS::FParameters>();
            F->PhysicalOrigin=T.PhysicalOrigin; F->Domains=P->Domains;
            F->RawTile=Raw; F->OutAtlas=UAV;
            {
                RDG_EVENT_SCOPE(Graph,"MD_ScrapeBoundary_Film");
                FComputeShaderUtils::AddPass(Graph,RDG_EVENT_NAME("MD_ScrapeBoundaryFilm"),FilmShader,F,FIntVector(10,10,1));
            }
        }
        Graph.SetTextureAccessFinal(Texture,ERHIAccess::SRVMask);
        Graph.Execute();
        auto Fence=RHICreateGPUFence(TEXT("MD_ScrapeBoundaryUpload"));
        Cmd.WriteGPUFence(Fence); Fences.Add({Fence,Bytes});
    });
    return true;
}
