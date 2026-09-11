#include "MDSolidMeshComponent.h"

#include "Containers/ResourceArray.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalRenderResources.h"
#include "GlobalShader.h"
#include "HAL/IConsoleManager.h"
#include "LocalVertexFactory.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Materials/MaterialRenderProxy.h"
#include "MeshBatch.h"
#include "MeshDrawShaderBindings.h"
#include "MeshMaterialShader.h"
#include "Misc/Crc.h"
#include "Misc/ScopeLock.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveUniformShaderParametersBuilder.h"
#include "PrimitiveViewRelevance.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/RealtimeGPUProfiler.h"
#include "RenderGraphUtils.h"
#include "RenderUtils.h"
#include "SceneInterface.h"
#include "SceneManagement.h"
#include "SceneView.h"
#include "ShaderParameterUtils.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MDSolidMeshComponent)

TRACE_DECLARE_INT_COUNTER(MDSolidRenderSharedGPU, TEXT("MD/SolidRenderer/SharedGPUBytes"));
TRACE_DECLARE_INT_COUNTER(MDSolidRenderInstanceGPU, TEXT("MD/SolidRenderer/InstanceGPUBytes"));
TRACE_DECLARE_INT_COUNTER(MDSolidRenderInstances, TEXT("MD/SolidRenderer/Instances"));
TRACE_DECLARE_INT_COUNTER(MDSolidRenderUploadBytes, TEXT("MD/SolidRenderer/LastUploadBytes"));
TRACE_DECLARE_INT_COUNTER(MDSolidRenderReusedInstances, TEXT("MD/SolidRenderer/ReusedIndexInstances"));
TRACE_DECLARE_INT_COUNTER(MDSolidRenderReferencedVertices, TEXT("MD/SolidRenderer/ReferencedVertices"));

static_assert(sizeof(FMDSolidRenderVertex) == 44, "Keep the shader structured-buffer layout identical.");

struct FMDSolidUploadAccounting
{
    TAtomic<int64> Pending{0};
    TAtomic<int64> Peak{0};
    TAtomic<int64> Total{0};
    TAtomic<int64> Count{0};
    TAtomic<int64> RequestedBytes{0};
    TAtomic<int64> RequestedCount{0};
    TAtomic<int64> LastRequestedBytes{0};
    TAtomic<int32> ProxyIndexMode{-1};
    TAtomic<int32> ProxyReferencedVertexCount{0};

    void Add(int64 Bytes)
    {
        const int64 Current = Pending.AddExchange(Bytes) + Bytes;
        // Add is game-thread-only; render thread only decrements Pending.
        Peak.Store(FMath::Max(Peak.Load(), Current));
        Total.AddExchange(Bytes);
        Count.AddExchange(1);
    }
};

namespace MDSolidRenderingPrivate
{
    int64 SharedGPUBytes = 0;
    int64 InstanceGPUBytes = 0;
    int64 InstanceCount = 0;
    int64 ReusedIndexInstanceCount = 0;
    int64 ReferencedVertexCount = 0;
    FCriticalSection IndexModeAccountingMutex;
    TAutoConsoleVariable<int32> CVarReuseVertexIndices(
        TEXT("MD.Solid.ReuseVertexIndices"), 0,
        TEXT("0: original triangle-corner indices; 1: exact-attribute reuse for non-UV-tangent materials. ")
        TEXT("Switches drawing immediately without rebuilding proxies or changing shared allocations."),
        ECVF_RenderThreadSafe);

    struct FExactVertexKey
    {
        uint32 Words[sizeof(FMDSolidRenderVertex) / sizeof(uint32)];
        explicit FExactVertexKey(const FMDSolidRenderVertex& Vertex) { FMemory::Memcpy(Words, &Vertex, sizeof(Words)); }
        bool operator==(const FExactVertexKey& Other) const { return FMemory::Memcmp(Words, Other.Words, sizeof(Words)) == 0; }
        friend uint32 GetTypeHash(const FExactVertexKey& Key) { return FCrc::MemCrc32(Key.Words, sizeof(Key.Words)); }
    };

    struct FScatterEntry
    {
        uint32 Index;
        float Value;
    };

    struct FUploadPacket
    {
        TArray<FScatterEntry> Entries;
        TSharedPtr<FMDSolidUploadAccounting, ESPMode::ThreadSafe> Accounting;
        int64 Bytes = 0;
        ~FUploadPacket() { if (Accounting) Accounting->Pending.SubExchange(Bytes); }
        void Account(const TSharedPtr<FMDSolidUploadAccounting, ESPMode::ThreadSafe>& InAccounting)
        {
            Accounting = InAccounting;
            Bytes = Entries.GetAllocatedSize();
            Accounting->Add(Bytes);
        }
    };

    class FSharedIndexBuffer final : public FIndexBuffer
    {
    public:
        void Initialize(FRHICommandListBase& RHICmdList, const TArray<uint32>& Indices,
            const TCHAR* DebugName = TEXT("MDSolid.SharedIndices"))
        {
            ResourceName = DebugName;
            InitialIndices = Indices;
            InitResource(RHICmdList);
            InitialIndices = {};
        }
        virtual void InitRHI(FRHICommandListBase& RHICmdList) override
        {
            FResourceArrayUploadArrayView Upload(InitialIndices);
            IndexBufferRHI = RHICmdList.CreateBuffer(FRHIBufferCreateDesc::CreateIndex<uint32>(
                ResourceName, InitialIndices.Num()).AddUsage(BUF_Static)
                .SetInitActionResourceArray(&Upload).DetermineInitialState());
        }
    private:
        TConstArrayView<uint32> InitialIndices;
        const TCHAR* ResourceName = TEXT("MDSolid.SharedIndices");
    };

    struct FSharedResources
    {
        FBufferRHIRef Vertices;
        FShaderResourceViewRHIRef VerticesSRV;
        FSharedIndexBuffer Indices;
        FSharedIndexBuffer ReusedIndices;
        int64 Bytes = 0;

        FSharedResources(FRHICommandListBase& RHICmdList, const FMDSolidRenderTemplate& Template)
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(MD_SolidRenderer_CreateSharedGPU);
            FResourceArrayUploadArrayView Upload(Template.Vertices);
            Vertices = RHICmdList.CreateBuffer(FRHIBufferCreateDesc::CreateVertex(
                TEXT("MDSolid.SharedVertices"), Template.Vertices.Num() * sizeof(FMDSolidRenderVertex))
                .AddUsage(BUF_Static | BUF_ShaderResource).SetInitActionResourceArray(&Upload).DetermineInitialState());
            VerticesSRV = RHICmdList.CreateShaderResourceView(Vertices,
                FRHIViewDesc::CreateBufferSRV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(PF_R32_UINT));
            Indices.Initialize(RHICmdList, Template.Indices);
            if (!Template.ReusedIndices.IsEmpty()) ReusedIndices.Initialize(RHICmdList, Template.ReusedIndices, TEXT("MDSolid.SharedReusedIndices"));
            Bytes = Template.GetGPUBytes();
            SharedGPUBytes += Bytes;
            TRACE_COUNTER_SET(MDSolidRenderSharedGPU, SharedGPUBytes);
        }
        ~FSharedResources()
        {
            check(IsInRenderingThread());
            Indices.ReleaseResource();
            ReusedIndices.ReleaseResource();

            VerticesSRV.SafeRelease();
            Vertices.SafeRelease();
            SharedGPUBytes -= Bytes;
            TRACE_COUNTER_SET(MDSolidRenderSharedGPU, SharedGPUBytes);
        }
    };

    TMap<const FMDSolidRenderTemplate*, TWeakPtr<FSharedResources, ESPMode::ThreadSafe>> ResourceCache;

    TSharedPtr<FSharedResources, ESPMode::ThreadSafe> FindResources(FRHICommandListBase& RHICmdList,
        const FMDSolidRenderTemplate& Template)
    {
        check(IsInRenderingThread());
        for (auto It = ResourceCache.CreateIterator(); It; ++It)
            if (!It.Value().IsValid()) It.RemoveCurrent();
        if (auto* Weak = ResourceCache.Find(&Template))
            if (auto Existing = Weak->Pin()) return Existing;
        auto Resources = MakeShared<FSharedResources, ESPMode::ThreadSafe>(RHICmdList, Template);
        ResourceCache.Add(&Template, Resources);
        return Resources;
    }
}

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FMDSolidVFParameters, )
    SHADER_PARAMETER_SRV(Buffer<uint>, TemplateVertices)
    SHADER_PARAMETER_SRV(Buffer<float>, State)
    SHADER_PARAMETER(uint32, NormalOffset)
    SHADER_PARAMETER(uint32, UseUVTangents)
END_GLOBAL_SHADER_PARAMETER_STRUCT()
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FMDSolidVFParameters, "MDSolidVF");

// D3D requires a declaration for every LocalVF signature semantic, including unused fields.
// Attributes are replaced in the shader; all declaration entries share these 16 constant bytes.
class FMDSolidNullAttributeBuffer : public FVertexBuffer
{
    virtual void InitRHI(FRHICommandListBase& RHICmdList) override
    {
        VertexBufferRHI = RHICmdList.CreateBuffer(FRHIBufferCreateDesc::CreateVertex<FVector4f>(
            TEXT("MDSolid.NullAttributes"), 1).AddUsage(BUF_Static)
            .SetInitActionZeroData().SetInitialState(ERHIAccess::VertexOrIndexBuffer));
    }
};
TGlobalResource<FMDSolidNullAttributeBuffer> GMDSolidNullAttributes;

class FMDSolidVertexFactory final : public FVertexFactory
{
    DECLARE_VERTEX_FACTORY_TYPE(FMDSolidVertexFactory);
public:
    explicit FMDSolidVertexFactory(ERHIFeatureLevel::Type FeatureLevel) : FVertexFactory(FeatureLevel) {}
    TUniformBufferRef<FMDSolidVFParameters> SolidUniform;
    TUniformBufferRef<FLocalVertexFactoryUniformShaderParameters> LocalUniform;

    static bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) &&
            Parameters.MaterialParameters.MaterialDomain == MD_Surface &&
            (Parameters.MaterialParameters.bIsUsedWithStaticMesh || Parameters.MaterialParameters.bIsSpecialEngineMaterial);
    }
    static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters,
        FShaderCompilerEnvironment& Environment)
    {
        FLocalVertexFactory::ModifyCompilationEnvironment(Parameters, Environment);
        // The wrapper builds the complete input from shared IDs, not an expanded local VF stream.
        Environment.SetDefine(TEXT("MANUAL_VERTEX_FETCH"), 0);
        Environment.SetDefine(TEXT("VF_SUPPORTS_SPEEDTREE_WIND"), 0);
        Environment.SetDefine(TEXT("SUPPORT_GPUSKIN_PASSTHROUGH"), 0);
    }
    static void ValidateCompiledResult(const FVertexFactoryType* Type, EShaderPlatform Platform,
        const FShaderParameterMap& Map, TArray<FString>& Errors)
    {
        FLocalVertexFactory::ValidateCompiledResult(Type, Platform, Map, Errors);
    }
    virtual void InitRHI(FRHICommandListBase& RHICmdList) override
    {
        FVertexDeclarationElementList Elements;
        Elements.Add(AccessStreamComponent(FVertexStreamComponent(&GMDSolidNullAttributes, 0, 0, VET_Float4), 0));
        Elements.Add(AccessStreamComponent(FVertexStreamComponent(&GMDSolidNullAttributes, 0, 0, VET_Float3), 1));
        Elements.Add(AccessStreamComponent(FVertexStreamComponent(&GMDSolidNullAttributes, 0, 0, VET_Float4), 2));
        Elements.Add(AccessStreamComponent(FVertexStreamComponent(&GMDSolidNullAttributes, 0, 0, VET_Float4), 3));
        for (uint8 Attribute = 4; Attribute <= 7; ++Attribute)
            Elements.Add(AccessStreamComponent(FVertexStreamComponent(&GMDSolidNullAttributes, 0, 0, VET_Float4), Attribute));
        Elements.Add(AccessStreamComponent(FVertexStreamComponent(&GMDSolidNullAttributes, 0, 0, VET_Float2), 15));
        AddPrimitiveIdStreamElement(EVertexInputStreamType::Default, Elements, 13, 13);
        InitDeclaration(Elements);
        FLocalVertexFactoryUniformShaderParameters Local;
        Local.VertexFetch_Parameters = FIntVector4(0, 1, 0, 0);
        Local.InstanceDitherParameters = MakeNullLocalVertexFactoryInstanceDitherParameters();
        Local.PreSkinBaseVertexIndex = 0;
        Local.LODLightmapDataIndex = 0;
        Local.bIsGPUSkinPassThrough = 0;
        Local.VertexFetch_TexCoordBuffer = GNullColorVertexBuffer.VertexBufferSRV;
        Local.VertexFetch_PositionBuffer = GNullColorVertexBuffer.VertexBufferSRV;
        Local.VertexFetch_PreSkinPositionBuffer = GNullColorVertexBuffer.VertexBufferSRV;
        Local.VertexFetch_PackedTangentsBuffer = GNullColorVertexBuffer.VertexBufferSRV;
        Local.VertexFetch_ColorComponentsBuffer = GNullColorVertexBuffer.VertexBufferSRV;
        LocalUniform = TUniformBufferRef<FLocalVertexFactoryUniformShaderParameters>::CreateUniformBufferImmediate(
            Local, UniformBuffer_MultiFrame);
    }
    virtual void ReleaseRHI() override
    {
        SolidUniform.SafeRelease();
        LocalUniform.SafeRelease();
        FVertexFactory::ReleaseRHI();
    }
};

class FMDSolidVertexFactoryShaderParameters : public FVertexFactoryShaderParameters
{
    DECLARE_TYPE_LAYOUT(FMDSolidVertexFactoryShaderParameters, NonVirtual);
public:
    void Bind(const FShaderParameterMap&) {}
    void GetElementShaderBindings(const FSceneInterface*, const FSceneView*, const FMeshMaterialShader* Shader,
        EVertexInputStreamType, ERHIFeatureLevel::Type, const FVertexFactory* Factory,
        const FMeshBatchElement&, FMeshDrawSingleShaderBindings& Bindings, FVertexInputStreamArray&) const
    {
        const auto* SolidFactory = static_cast<const FMDSolidVertexFactory*>(Factory);
        Bindings.Add(Shader->GetUniformBufferParameter<FMDSolidVFParameters>(), SolidFactory->SolidUniform);
        Bindings.Add(Shader->GetUniformBufferParameter<FLocalVertexFactoryUniformShaderParameters>(), SolidFactory->LocalUniform);
    }
};
IMPLEMENT_TYPE_LAYOUT(FMDSolidVertexFactoryShaderParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FMDSolidVertexFactory, SF_Vertex, FMDSolidVertexFactoryShaderParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FMDSolidVertexFactory, SF_Pixel, FMDSolidVertexFactoryShaderParameters);
IMPLEMENT_VERTEX_FACTORY_TYPE(FMDSolidVertexFactory, "/Plugin/MDSolidRendering/Private/MDSolidVertexFactory.ush",
    EVertexFactoryFlags::UsedWithMaterials | EVertexFactoryFlags::SupportsDynamicLighting |
    EVertexFactoryFlags::SupportsPrimitiveIdStream | EVertexFactoryFlags::SupportsCachingMeshDrawCommands);

class FMDSolidScatterCS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FMDSolidScatterCS);
    SHADER_USE_PARAMETER_STRUCT(FMDSolidScatterCS, FGlobalShader);
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_SRV(Buffer<uint2>, Updates)
        SHADER_PARAMETER_UAV(RWBuffer<float>, OutputState)
        SHADER_PARAMETER(uint32, UpdateCount)
    END_SHADER_PARAMETER_STRUCT()
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
    }
};
IMPLEMENT_GLOBAL_SHADER(FMDSolidScatterCS, "/Plugin/MDSolidRendering/Private/MDSolidScatter.usf", "MainCS", SF_Compute);

class FMDSolidSceneProxy final : public FPrimitiveSceneProxy
{
public:
    explicit FMDSolidSceneProxy(const UMDSolidMeshComponent* Component)
        : FPrimitiveSceneProxy(Component)
        , Template(Component->GetSolidTemplate())
        , Accounting(Component->GetUploadAccounting())
        , VertexFactory(GetScene().GetFeatureLevel())
        , Material(Component->GetMaterial(0) ? Component->GetMaterial(0) : UMaterial::GetDefaultMaterial(MD_Surface))
        , MaterialRelevance(Material->GetRelevance_Concurrent(GetScene().GetShaderPlatform()))
    {
        const FMDSolidDynamicData& Data = *Component->GetSolidDynamicData();
        InitialState.Reserve(Data.Heights.Num() + Data.Normals.Num() * 3);
        InitialState.Append(Data.Heights);
        InitialState.Append(reinterpret_cast<const float*>(Data.Normals.GetData()), Data.Normals.Num() * 3);
        InitialUploadBytes = InitialState.GetAllocatedSize();
        Accounting->Add(InitialUploadBytes);
        StateBytes = int64(InitialState.Num()) * sizeof(float);
        bWillEverBeLit = true;
        const UMaterial* Base = Material->GetMaterial();
        bUseUVTangents = (Base && Base->bTangentSpaceNormal) || MaterialRelevance.bUsesAnisotropy;
        bCanReuseIndices = Template->UsesReusedIndices(bUseUVTangents);
    }
    virtual ~FMDSolidSceneProxy()
    {
        if (InitialUploadBytes) Accounting->Pending.SubExchange(InitialUploadBytes);
    }
    virtual SIZE_T GetTypeHash() const override
    {
        static size_t UniquePointer;
        return reinterpret_cast<SIZE_T>(&UniquePointer);
    }
    virtual void CreateRenderThreadResources(FRHICommandListBase& RHICmdList) override
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(MD_SolidRenderer_CreateInstanceGPU);
        SharedResources = MDSolidRenderingPrivate::FindResources(RHICmdList, *Template);
        FResourceArrayUploadArrayView Upload(InitialState);
        StateBuffer = RHICmdList.CreateBuffer(FRHIBufferCreateDesc::CreateVertex(
            TEXT("MDSolid.InstanceState"), StateBytes).AddUsage(BUF_Static | BUF_ShaderResource | BUF_UnorderedAccess)
            .SetInitActionResourceArray(&Upload).SetInitialState(ERHIAccess::SRVMask));
        StateSRV = RHICmdList.CreateShaderResourceView(StateBuffer,
            FRHIViewDesc::CreateBufferSRV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(PF_R32_FLOAT));
        StateUAV = RHICmdList.CreateUnorderedAccessView(StateBuffer,
            FRHIViewDesc::CreateBufferUAV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(PF_R32_FLOAT));
        VertexFactory.InitResource(RHICmdList);
        FMDSolidVFParameters Parameters;
        Parameters.TemplateVertices = SharedResources->VerticesSRV;
        Parameters.State = StateSRV;
        Parameters.NormalOffset = Template->HeightCount;
        Parameters.UseUVTangents = bUseUVTangents ? 1 : 0;
        VertexFactory.SolidUniform = TUniformBufferRef<FMDSolidVFParameters>::CreateUniformBufferImmediate(
            Parameters, UniformBuffer_MultiFrame);
        InitialState.Empty();
        Accounting->Pending.SubExchange(InitialUploadBytes);
        InitialUploadBytes = 0;
        MDSolidRenderingPrivate::InstanceGPUBytes += StateBytes;
        ++MDSolidRenderingPrivate::InstanceCount;
        TRACE_COUNTER_SET(MDSolidRenderInstanceGPU, MDSolidRenderingPrivate::InstanceGPUBytes);
        TRACE_COUNTER_SET(MDSolidRenderInstances, MDSolidRenderingPrivate::InstanceCount);
        UpdateIndexMode(SelectIndexMode());
        bResourcesCreated = true;
    }
    virtual void DestroyRenderThreadResources() override
    {
        VertexFactory.ReleaseResource();
        StateUAV.SafeRelease();
        StateSRV.SafeRelease();
        StateBuffer.SafeRelease();
        SharedResources.Reset();
        if (bResourcesCreated)
        {
            MDSolidRenderingPrivate::InstanceGPUBytes -= StateBytes;
            --MDSolidRenderingPrivate::InstanceCount;
            TRACE_COUNTER_SET(MDSolidRenderInstanceGPU, MDSolidRenderingPrivate::InstanceGPUBytes);
            TRACE_COUNTER_SET(MDSolidRenderInstances, MDSolidRenderingPrivate::InstanceCount);
            UpdateIndexMode(-1);
            bResourcesCreated = false;
        }
    }
    void Update_RenderThread(FRHICommandListImmediate& RHICmdList, const MDSolidRenderingPrivate::FUploadPacket& Packet)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(MD_SolidRenderer_SparseGPUUpload);
        if (Packet.Entries.IsEmpty()) return;
        if (!bResourcesCreated)
        {
            // Scene insertion may defer resource creation until the next scene update.
            // Preserve same-frame cleaning in the pending initial snapshot instead of dropping it.
            for (const auto& Entry : Packet.Entries)
                if (InitialState.IsValidIndex(Entry.Index)) InitialState[Entry.Index] = Entry.Value;
            return;
        }
        SCOPED_DRAW_EVENT(RHICmdList, MDSolidSparseScatter);
        FResourceArrayUploadArrayView Upload(Packet.Entries);
        FBufferRHIRef UpdateBuffer = RHICmdList.CreateBuffer(
            FRHIBufferCreateDesc::CreateVertex(
                TEXT("MDSolid.SparseUpload"), Packet.Entries.Num() * sizeof(MDSolidRenderingPrivate::FScatterEntry)).AddUsage(BUF_Static | BUF_ShaderResource)
                .SetInitActionResourceArray(&Upload).SetInitialState(ERHIAccess::SRVCompute));
        FShaderResourceViewRHIRef UpdateSRV = RHICmdList.CreateShaderResourceView(UpdateBuffer,
            FRHIViewDesc::CreateBufferSRV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(PF_R32G32_UINT));
        RHICmdList.Transition(FRHITransitionInfo(StateBuffer, ERHIAccess::SRVMask, ERHIAccess::UAVCompute));
        TShaderMapRef<FMDSolidScatterCS> Shader(GetGlobalShaderMap(GetScene().GetFeatureLevel()));
        FMDSolidScatterCS::FParameters Parameters;
        Parameters.Updates = UpdateSRV;
        Parameters.OutputState = StateUAV;
        Parameters.UpdateCount = Packet.Entries.Num();
        FComputeShaderUtils::Dispatch(RHICmdList, Shader, Parameters,
            FIntVector(FMath::DivideAndRoundUp(Packet.Entries.Num(), 64), 1, 1));
        RHICmdList.Transition(FRHITransitionInfo(StateBuffer, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
        TRACE_COUNTER_SET(MDSolidRenderUploadBytes, Packet.Entries.Num() * sizeof(MDSolidRenderingPrivate::FScatterEntry));
    }
    virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily&,
        uint32 VisibilityMap, FMeshElementCollector& Collector) const override
    {
        if (!bResourcesCreated) return;
        const int32 IndexMode = SelectIndexMode();
        UpdateIndexMode(IndexMode);
        for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
        {
            if (!(VisibilityMap & (1u << ViewIndex))) continue;
            FMeshBatch& Mesh = Collector.AllocateMesh();
            Mesh.VertexFactory = &VertexFactory;
            Mesh.MaterialRenderProxy = Material->GetRenderProxy();
            Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
            Mesh.Type = PT_TriangleList;
            Mesh.DepthPriorityGroup = SDPG_World;
            Mesh.bCanApplyViewModeOverrides = true;
            Mesh.CastShadow = true;
            FMeshBatchElement& Element = Mesh.Elements[0];
            Element.IndexBuffer = IndexMode == 1 ? &SharedResources->ReusedIndices : &SharedResources->Indices;
            Element.FirstIndex = 0;
            Element.NumPrimitives = Template->Indices.Num() / 3;
            Element.MinVertexIndex = 0;
            Element.MaxVertexIndex = Template->Vertices.Num() - 1;
            auto& Uniform = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
            FPrimitiveUniformShaderParametersBuilder Builder;
            BuildUniformShaderParameters(Builder);
            Uniform.Set(Collector.GetRHICommandList(), Builder);
            Element.PrimitiveUniformBufferResource = &Uniform.UniformBuffer;
            Collector.AddMesh(ViewIndex, Mesh);
        }
    }
    virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
    {
        FPrimitiveViewRelevance Result;
        Result.bDrawRelevance = IsShown(View);
        Result.bShadowRelevance = IsShadowCast(View);
        Result.bDynamicRelevance = true;
        Result.bRenderInMainPass = ShouldRenderInMainPass();
        Result.bRenderCustomDepth = ShouldRenderCustomDepth();
        Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
        Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;
        MaterialRelevance.SetPrimitiveViewRelevance(Result);
        Result.bVelocityRelevance = DrawsVelocity() && Result.bOpaque && Result.bRenderInMainPass;
        return Result;
    }
    virtual bool CanBeOccluded() const override { return !MaterialRelevance.bDisableDepthTest; }
    virtual uint32 GetMemoryFootprint() const override { return sizeof(*this) + GetAllocatedSize() + InitialState.GetAllocatedSize(); }
private:
    int32 SelectIndexMode() const
    {
        return bCanReuseIndices && MDSolidRenderingPrivate::CVarReuseVertexIndices.GetValueOnRenderThread() != 0 ? 1 : 0;
    }
    void UpdateIndexMode(const int32 NewMode) const
    {
        if (CurrentIndexMode.Load() == NewMode) return;
        // UE5.8 gathers dynamic meshes in parallel. Only a mode transition takes this
        // lock; steady drawing does not serialize, and counter events remain ordered.
        FScopeLock Lock(&MDSolidRenderingPrivate::IndexModeAccountingMutex);
        const int32 OldMode = CurrentIndexMode.Exchange(NewMode);
        if (OldMode == NewMode) return;
        const int64 OldVertices = OldMode < 0 ? 0 : OldMode == 1 ? Template->ReusedUniqueVertexCount : Template->Vertices.Num();
        const int64 NewVertices = NewMode < 0 ? 0 : NewMode == 1 ? Template->ReusedUniqueVertexCount : Template->Vertices.Num();
        MDSolidRenderingPrivate::ReusedIndexInstanceCount += int64(NewMode == 1) - int64(OldMode == 1);
        MDSolidRenderingPrivate::ReferencedVertexCount += NewVertices - OldVertices;
        Accounting->ProxyReferencedVertexCount.Store(int32(NewVertices));
        Accounting->ProxyIndexMode.Store(NewMode);
        TRACE_COUNTER_SET(MDSolidRenderReusedInstances, MDSolidRenderingPrivate::ReusedIndexInstanceCount);
        TRACE_COUNTER_SET(MDSolidRenderReferencedVertices, MDSolidRenderingPrivate::ReferencedVertexCount);
    }
    TSharedPtr<const FMDSolidRenderTemplate, ESPMode::ThreadSafe> Template;
    TSharedPtr<FMDSolidUploadAccounting, ESPMode::ThreadSafe> Accounting;
    TSharedPtr<MDSolidRenderingPrivate::FSharedResources, ESPMode::ThreadSafe> SharedResources;
    FMDSolidVertexFactory VertexFactory;
    UMaterialInterface* Material;
    FMaterialRelevance MaterialRelevance;
    TArray<float> InitialState;
    FBufferRHIRef StateBuffer;
    FShaderResourceViewRHIRef StateSRV;
    FUnorderedAccessViewRHIRef StateUAV;
    int64 InitialUploadBytes = 0;
    int64 StateBytes = 0;
    bool bResourcesCreated = false;
    bool bUseUVTangents = false;
    bool bCanReuseIndices = false;
    mutable TAtomic<int32> CurrentIndexMode{-1};
};

void FMDSolidRenderTemplate::BuildReusedIndices()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Solid_BuildReusedIndices);
    ReusedIndices.SetNumUninitialized(Indices.Num());
    ReusedUniqueVertexCount = 0;
    TMap<MDSolidRenderingPrivate::FExactVertexKey, uint32> Representatives;
    Representatives.Reserve(FMath::Min(Vertices.Num(), FMath::Max(HeightCount, 0)));
    for (int32 Corner = 0; Corner < Indices.Num(); ++Corner)
    {
        check(Indices[Corner] == uint32(Corner) && Vertices.IsValidIndex(Corner));
        const MDSolidRenderingPrivate::FExactVertexKey Key(Vertices[Corner]);
        if (const uint32* Existing = Representatives.Find(Key)) ReusedIndices[Corner] = *Existing;
        else
        {
            Representatives.Add(Key, Corner);
            ReusedIndices[Corner] = Corner;
            ++ReusedUniqueVertexCount;
        }
    }
}

bool FMDSolidRenderTemplate::Validate(FString& OutError) const
{
    if (Vertices.IsEmpty() || Vertices.Num() % 3 || Indices.Num() != Vertices.Num() ||
        HeightCount <= 0 || NormalCount <= 0 || !LocalBounds.IsValid)
    {
        OutError = TEXT("Solid template needs triangle corners, matching sequential indices, bounds and state counts.");
        return false;
    }
    for (int32 I = 0; I < Vertices.Num(); ++I)
    {
        const auto& Vertex = Vertices[I];
        if (Indices[I] != uint32(I) || Vertex.HeightIndex >= uint32(HeightCount) ||
            Vertex.NormalIndex >= uint32(NormalCount) || Vertex.BasePosition.ContainsNaN() ||
            Vertex.OffsetPerCm.ContainsNaN() || Vertex.UV.ContainsNaN() ||
            !FMath::IsFinite(Vertex.HeightScale))
        {
            OutError = FString::Printf(TEXT("Invalid solid template corner %d."), I);
            return false;
        }
    }
    if (!ReusedIndices.IsEmpty())
    {
        if (ReusedIndices.Num() != Indices.Num())
        {
            OutError = TEXT("Reused indices must preserve every original triangle corner.");
            return false;
        }
        int32 UniqueCount = 0;
        for (int32 Corner = 0; Corner < ReusedIndices.Num(); ++Corner)
        {
            const uint32 Representative = ReusedIndices[Corner];
            if (Representative > uint32(Corner) || ReusedIndices[Representative] != Representative ||
                FMemory::Memcmp(&Vertices[Corner], &Vertices[Representative], sizeof(FMDSolidRenderVertex)) != 0)
            {
                OutError = FString::Printf(TEXT("Reused corner %d changes vertex attributes."), Corner);
                return false;
            }
            UniqueCount += Representative == uint32(Corner) ? 1 : 0;
        }
        if (UniqueCount != ReusedUniqueVertexCount)
        {
            OutError = TEXT("Reused index unique vertex count is inconsistent.");
            return false;
        }
    }
    else if (ReusedUniqueVertexCount != 0)
    {
        OutError = TEXT("A missing reused index buffer cannot report reused vertices.");
        return false;
    }
    OutError.Reset();
    return true;
}

int64 FMDSolidRenderTemplate::GetAllocatedBytes() const
{
    return sizeof(*this) + Vertices.GetAllocatedSize() + Indices.GetAllocatedSize() + ReusedIndices.GetAllocatedSize();
}
int64 FMDSolidRenderTemplate::GetGPUBytes() const
{
    return int64(Vertices.Num()) * sizeof(FMDSolidRenderVertex) + (int64(Indices.Num()) + ReusedIndices.Num()) * sizeof(uint32);
}

UMDSolidMeshComponent::UMDSolidMeshComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetMobility(EComponentMobility::Movable);
    SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetGenerateOverlapEvents(false);
    SetCanEverAffectNavigation(false);
    bVisibleInRayTracing = false;
    SetCastShadow(true);
    UploadAccounting = MakeShared<FMDSolidUploadAccounting, ESPMode::ThreadSafe>();
}

bool UMDSolidMeshComponent::Configure(TSharedPtr<const FMDSolidRenderTemplate, ESPMode::ThreadSafe> InTemplate,
    TSharedPtr<FMDSolidDynamicData, ESPMode::ThreadSafe> InDynamicData, UMaterialInterface* Material)
{
    if (!InTemplate || !InDynamicData || !Material ||
        InTemplate->HeightCount != InDynamicData->Heights.Num() ||
        InTemplate->NormalCount != InDynamicData->Normals.Num() || InTemplate->Vertices.IsEmpty()) return false;
    SharedTemplate = MoveTemp(InTemplate);
    DynamicData = MoveTemp(InDynamicData);
    SetMaterial(0, Material);
    UpdateBounds();
    MarkRenderStateDirty();
    return true;
}

void UMDSolidMeshComponent::SubmitUpdates(TConstArrayView<int32> HeightIds, TConstArrayView<int32> NormalIds)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_SolidRenderer_PackSparseUpload);
    if (!DynamicData || (HeightIds.IsEmpty() && NormalIds.IsEmpty())) return;
    const int64 RequestedBytes = (int64(HeightIds.Num()) + int64(NormalIds.Num()) * 3) * sizeof(MDSolidRenderingPrivate::FScatterEntry);
    UploadAccounting->RequestedBytes.AddExchange(RequestedBytes);
    UploadAccounting->RequestedCount.AddExchange(1);
    UploadAccounting->LastRequestedBytes.Store(RequestedBytes);
    if (!SceneProxy) return; // A subsequently created proxy takes the latest complete state.
    auto Packet = MakeShared<MDSolidRenderingPrivate::FUploadPacket, ESPMode::ThreadSafe>();
    Packet->Entries.Reserve(HeightIds.Num() + NormalIds.Num() * 3);
    for (const int32 Id : HeightIds)
        if (DynamicData->Heights.IsValidIndex(Id)) Packet->Entries.Add({uint32(Id), DynamicData->Heights[Id]});
    const uint32 NormalOffset = DynamicData->Heights.Num();
    for (const int32 Id : NormalIds)
    {
        if (!DynamicData->Normals.IsValidIndex(Id)) continue;
        const FVector3f N = DynamicData->Normals[Id];
        const uint32 Offset = NormalOffset + uint32(Id) * 3;
        Packet->Entries.Add({Offset, N.X});
        Packet->Entries.Add({Offset + 1, N.Y});
        Packet->Entries.Add({Offset + 2, N.Z});
    }
    if (Packet->Entries.IsEmpty()) return;
    // Notify the scene when geometry changes so cached shadows are invalidated, as in UE dynamic meshes.
    if (!HeightIds.IsEmpty()) MarkRenderTransformDirty();
    Packet->Account(UploadAccounting);
    auto* Proxy = static_cast<FMDSolidSceneProxy*>(SceneProxy);
    ENQUEUE_RENDER_COMMAND(MDSolidSparseUpdate)([Proxy, Packet](FRHICommandListImmediate& RHICmdList)
    {
        Proxy->Update_RenderThread(RHICmdList, *Packet);
    });
}

void UMDSolidMeshComponent::ClearSolid()
{
    SharedTemplate.Reset();
    DynamicData.Reset();
    MarkRenderStateDirty();
    UpdateBounds();
}

FMDSolidRenderMemoryStats UMDSolidMeshComponent::GetSolidMemoryStats() const
{
    FMDSolidRenderMemoryStats Result;
    if (SharedTemplate)
    {
        Result.SharedCPUBytes = SharedTemplate->GetAllocatedBytes();
        Result.SharedGPUBytes = SharedTemplate->GetGPUBytes();
        Result.SharedReusedIndexCPUBytes = SharedTemplate->ReusedIndices.GetAllocatedSize();
        Result.SharedReusedIndexGPUBytes = int64(SharedTemplate->ReusedIndices.Num()) * sizeof(uint32);
        Result.TemplateVertexCount = SharedTemplate->Vertices.Num();
        Result.ReusedUniqueVertexCount = SharedTemplate->ReusedUniqueVertexCount;
    }
    if (DynamicData)
    {
        Result.DynamicCPUBytes = DynamicData->GetAllocatedBytes() + sizeof(FMDSolidSceneProxy) + sizeof(FMDSolidUploadAccounting);
        Result.DynamicGPUBytes = DynamicData->GetGPUBytes();
    }
    Result.ProxyIndexMode = UploadAccounting->ProxyIndexMode.Load();
    Result.ProxyReferencedVertexCount = UploadAccounting->ProxyReferencedVertexCount.Load();
    Result.PendingUploadBytes = UploadAccounting->Pending.Load();
    Result.PeakPendingUploadBytes = UploadAccounting->Peak.Load();
    Result.TotalUploadBytes = UploadAccounting->Total.Load();
    Result.UploadCount = UploadAccounting->Count.Load();
    Result.RequestedUploadBytes = UploadAccounting->RequestedBytes.Load();
    Result.RequestedUploadCount = UploadAccounting->RequestedCount.Load();
    Result.LastRequestedUploadBytes = UploadAccounting->LastRequestedBytes.Load();
    return Result;
}

FPrimitiveSceneProxy* UMDSolidMeshComponent::CreateSceneProxy()
{
    return SharedTemplate && DynamicData && !SharedTemplate->Vertices.IsEmpty() ? new FMDSolidSceneProxy(this) : nullptr;
}

FBoxSphereBounds UMDSolidMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
    if (!SharedTemplate || !SharedTemplate->LocalBounds.IsValid) return Super::CalcBounds(LocalToWorld);
    FBoxSphereBounds Result(SharedTemplate->LocalBounds.TransformBy(LocalToWorld));
    Result.BoxExtent *= BoundsScale;
    Result.SphereRadius *= BoundsScale;
    return Result;
}
