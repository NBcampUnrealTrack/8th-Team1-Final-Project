#pragma once

#include "CoreMinimal.h"
#include "Components/MeshComponent.h"
#include "MDSolidMeshComponent.generated.h"

/** Immutable triangle corner. Heights are logical IDs, never expanded per instance. */
struct MDSOLIDRENDERING_API FMDSolidRenderVertex
{
    FVector3f BasePosition = FVector3f::ZeroVector;
    FVector3f OffsetPerCm = FVector3f::ZeroVector;
    FVector2f UV = FVector2f::ZeroVector;
    uint32 HeightIndex = 0;
    uint32 NormalIndex = 0;
    // Top = 1, fixed skirt bottom = 0. BasePosition already includes the wall inset.
    float HeightScale = 1.0f;
};

/** Shared by every wall with identical geometry. Three consecutive corners per triangle. */
struct MDSOLIDRENDERING_API FMDSolidRenderTemplate
{
    TArray<FMDSolidRenderVertex> Vertices;
    TArray<uint32> Indices;
    // Optional second index buffer: only bit-identical attributes reuse an existing corner.
    // Keep Vertices and identity Indices unchanged for triangle-local UV tangent evaluation.
    TArray<uint32> ReusedIndices;
    int32 ReusedUniqueVertexCount = 0;
    FBox LocalBounds = FBox(ForceInit);
    int32 HeightCount = 0;
    int32 NormalCount = 0;

    void BuildReusedIndices();
    bool UsesReusedIndices(bool bUseUVTangents) const { return !bUseUVTangents && !ReusedIndices.IsEmpty(); }
    TConstArrayView<uint32> GetDrawIndices(bool bUseUVTangents) const
    {
        return UsesReusedIndices(bUseUVTangents) ? MakeArrayView(ReusedIndices) : MakeArrayView(Indices);
    }
    bool Validate(FString& OutError) const;
    int64 GetAllocatedBytes() const;
    int64 GetGPUBytes() const;
};

/** Game-thread authority retained once, shared by the gameplay owner and component. */
struct MDSOLIDRENDERING_API FMDSolidDynamicData
{
    TArray<float> Heights;
    TArray<FVector3f> Normals;
    int64 GetAllocatedBytes() const { return Heights.GetAllocatedSize() + Normals.GetAllocatedSize(); }
    int64 GetGPUBytes() const { return int64(Heights.Num()) * sizeof(float) + int64(Normals.Num()) * sizeof(FVector3f); }
};

struct MDSOLIDRENDERING_API FMDSolidRenderMemoryStats
{
    int64 SharedCPUBytes = 0;
    int64 SharedGPUBytes = 0;
    int64 SharedReusedIndexCPUBytes = 0;
    int64 SharedReusedIndexGPUBytes = 0;
    int32 TemplateVertexCount = 0;
    int32 ReusedUniqueVertexCount = 0;
    // Actual initialized render proxy, not a prediction from the game-thread material.
    // -1 = no initialized proxy, 0 = identity triangle corners, 1 = exact-attribute reuse.
    int32 ProxyIndexMode = -1;
    int32 ProxyReferencedVertexCount = 0;
    int64 DynamicCPUBytes = 0;
    int64 DynamicGPUBytes = 0;
    int64 PendingUploadBytes = 0;
    int64 PeakPendingUploadBytes = 0;
    int64 TotalUploadBytes = 0;
    int64 UploadCount = 0;
    // Requested sparse bytes are separate from actual queued GPU upload accounting.
    int64 RequestedUploadBytes = 0;
    int64 RequestedUploadCount = 0;
    int64 LastRequestedUploadBytes = 0;
};

struct FMDSolidUploadAccounting;

/** One primitive per wall, GPU displacement and sparse compute scatter updates. */
UCLASS(ClassGroup=(Rendering), Transient)
class MDSOLIDRENDERING_API UMDSolidMeshComponent : public UMeshComponent
{
    GENERATED_BODY()
public:
    UMDSolidMeshComponent();

    bool Configure(TSharedPtr<const FMDSolidRenderTemplate, ESPMode::ThreadSafe> InTemplate,
        TSharedPtr<FMDSolidDynamicData, ESPMode::ThreadSafe> InDynamicData, UMaterialInterface* Material);
    // Unique IDs are required; unchanged data remains resident. No full-position upload.
    void SubmitUpdates(TConstArrayView<int32> HeightIds, TConstArrayView<int32> NormalIds);
    void ClearSolid();
    FMDSolidRenderMemoryStats GetSolidMemoryStats() const;

    virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
    virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
    virtual int32 GetNumMaterials() const override { return 1; }

    const TSharedPtr<const FMDSolidRenderTemplate, ESPMode::ThreadSafe>& GetSolidTemplate() const { return SharedTemplate; }
    const TSharedPtr<FMDSolidDynamicData, ESPMode::ThreadSafe>& GetSolidDynamicData() const { return DynamicData; }
    const TSharedPtr<FMDSolidUploadAccounting, ESPMode::ThreadSafe>& GetUploadAccounting() const { return UploadAccounting; }

private:
    TSharedPtr<const FMDSolidRenderTemplate, ESPMode::ThreadSafe> SharedTemplate;
    TSharedPtr<FMDSolidDynamicData, ESPMode::ThreadSafe> DynamicData;
    TSharedPtr<FMDSolidUploadAccounting, ESPMode::ThreadSafe> UploadAccounting;
};
