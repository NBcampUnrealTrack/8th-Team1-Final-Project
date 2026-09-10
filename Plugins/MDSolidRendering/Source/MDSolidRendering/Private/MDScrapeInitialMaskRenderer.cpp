#include "MDScrapeInitialMaskRenderer.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "RHICommandList.h"

namespace
{
    class FMDScrapeInitialMaskCS : public FGlobalShader
    {
        DECLARE_GLOBAL_SHADER(FMDScrapeInitialMaskCS);
        SHADER_USE_PARAMETER_STRUCT(FMDScrapeInitialMaskCS, FGlobalShader);
        BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
            SHADER_PARAMETER(FIntPoint, Size)
            SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, Sources)
            SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ActiveBits)
            SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, SeamBytes)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutMask)
        END_SHADER_PARAMETER_STRUCT()
        static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P)
        { return IsFeatureLevelSupported(P.Platform, ERHIFeatureLevel::SM5); }
    };
    IMPLEMENT_GLOBAL_SHADER(FMDScrapeInitialMaskCS,
        "/Plugin/MDSolidRendering/Private/MDScrapeInitialMask.usf", "MainCS", SF_Compute);
}
struct FMDScrapeInitialMaskLayout::FGPUData
{
    TRefCountPtr<FRDGPooledBuffer> Sources;
};
FMDScrapeInitialMaskLayout::FMDScrapeInitialMaskLayout(TArray<uint32>&& InSources, TArray<int32>&& InSeamPixels)
    : Sources(MoveTemp(InSources)), SeamPixels(MoveTemp(InSeamPixels)), GPU(MakeUnique<FGPUData>()) {}
FMDScrapeInitialMaskLayout::~FMDScrapeInitialMaskLayout() = default;
uint64 FMDScrapeInitialMaskLayout::GetCPUBytes() const
{ return sizeof(*this) + Sources.GetAllocatedSize() + SeamPixels.GetAllocatedSize() + sizeof(FGPUData); }
bool FMDScrapeInitialMaskRenderer::IsSupported()
{
    return GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5
        && EnumHasAllFlags(GPixelFormats[PF_G8].Capabilities, EPixelFormatCapabilities::UAV | EPixelFormatCapabilities::TypedUAVStore);
}
bool FMDScrapeInitialMaskRenderer::Generate(FRHICommandListImmediate& Cmd, FRHITexture* Texture,
    FIntPoint Size, const FMDScrapeInitialMaskLayout& Layout, TConstArrayView<uint32> Bits,
    TConstArrayView<uint8> Seam)
{
    check(IsInRenderingThread());
    if (!IsSupported() || !Texture || Layout.Sources.Num() != Size.X * Size.Y || Bits.IsEmpty()) return false;
    FRDGBuilder Graph(Cmd);
    FRDGBufferRef Sources;
    if (Layout.GPU->Sources) Sources = Graph.RegisterExternalBuffer(Layout.GPU->Sources);
    else
    {
        Sources = CreateStructuredBuffer(Graph, TEXT("MD_InitialMaskLayout"), Layout.Sources);
        Graph.QueueBufferExtraction(Sources, &Layout.GPU->Sources);
    }
    TArray<uint32> PackedSeam;
    PackedSeam.SetNumZeroed(FMath::Max(1, FMath::DivideAndRoundUp(Seam.Num(), 4)));
    if (!Seam.IsEmpty()) FMemory::Memcpy(PackedSeam.GetData(), Seam.GetData(), Seam.Num());
    auto* P = Graph.AllocParameters<FMDScrapeInitialMaskCS::FParameters>();
    P->Size = Size;
    P->Sources = Graph.CreateSRV(Sources);
    P->ActiveBits = Graph.CreateSRV(CreateStructuredBuffer(Graph, TEXT("MD_InitialMaskBits"),
        sizeof(uint32), Bits.Num(), Bits.GetData(), Bits.Num() * sizeof(uint32)));
    P->SeamBytes = Graph.CreateSRV(CreateStructuredBuffer(Graph, TEXT("MD_InitialMaskSeam"), PackedSeam));
    auto Target = Graph.RegisterExternalTexture(CreateRenderTarget(Texture, TEXT("MD_InitialMask")));
    P->OutMask = Graph.CreateUAV(Target);
    TShaderMapRef<FMDScrapeInitialMaskCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
    FComputeShaderUtils::AddPass(Graph, RDG_EVENT_NAME("MD_ScrapeInitialMask"), Shader, P,
        FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1));
    Graph.SetTextureAccessFinal(Target, ERHIAccess::SRVMask);
    Graph.Execute();
    return true;
}
