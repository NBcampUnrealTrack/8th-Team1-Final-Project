#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Types/Scrape/MD_ScrapeTypes.h"
#include "Gameplay/Scrape/MD_ScrapeCellView.h"
#include "MD_ContaminationSolidVisualComponent.generated.h"

class UMDSolidMeshComponent;
struct FMDSolidRenderTemplate;
struct FMDSolidDynamicData;
class UMaterialInterface;
class UStaticMeshComponent;
class UMD_ScrapeMeshUVData;
struct FMD_ContaminationSolidGeometry;

/** Centimetres are measured after the wall's scale, including nonuniform scale. */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_ContaminationSolidSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Solid", meta=(ClampMin="2", ClampMax="12", Units="cm"))
    float SampleSpacingCm = 4.0f;
    /** Coarsen only when the triangle cap prevents reaching the requested sample spacing. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Solid|Budget")
    bool bAllowAdaptiveSpacing = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Solid|Budget", meta=(ClampMin="2", ClampMax="12", Units="cm", EditCondition="bAllowAdaptiveSpacing"))
    float MaximumAdaptiveSpacingCm = 12.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Solid", meta=(ClampMin="0.1", ClampMax="20", Units="cm"))
    float MinimumThicknessCm = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Solid", meta=(ClampMin="0.1", ClampMax="20", Units="cm"))
    float MaximumThicknessCm = 5.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Solid", meta=(ClampMin="4", ClampMax="200", Units="cm"))
    float NoiseScaleCm = 24.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Solid")
    int32 Seed = 7319;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Solid|Budget", meta=(ClampMin="1000", ClampMax="300000"))
    int32 MaximumSurfaceTriangles = 180000;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Solid|Budget", meta=(ClampMin="256", ClampMax="8192"))
    int32 TrianglesPerChunk = 2048;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Solid|Budget", meta=(ClampMin="1", ClampMax="256"))
    int32 MaximumChunks = 192;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Solid|Budget", meta=(ClampMin="0", Units="cm"))
    float CullDistanceCm = 6000.0f;
};

/** Derived visual shell only: no replicated state, tick, collision or navigation. */
UCLASS(ClassGroup=(MD))
class MARINEDEMOLITION_API UMD_ContaminationSolidVisualComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UMD_ContaminationSolidVisualComponent();

    bool Configure(UStaticMeshComponent* Mesh, UMD_ScrapeMeshUVData* Data,
        UMaterialInterface* Material, FMD_ScrapeConstCellView States,
        const FMD_ContaminationSolidSettings& Settings);
    void UpdateCells(TConstArrayView<int32> ChangedCells,
        FMD_ScrapeConstCellView States);
    void RefreshAll(FMD_ScrapeConstCellView States);
    void ResetVisuals();
    void SetVisualsVisible(bool bVisible);

    /** Current shell height in world cm; atlas/source-triangle lookup avoids full-mesh scans. */
    float SampleVisualOffset(const FVector& LocalPosition, const FVector2D& CanonicalUV,
        const FVector& NormalLocal) const;
    int32 GetSurfaceTriangleCount() const;
    int32 GetChunkCount() const { return SolidMesh ? 1 : 0; }
    int64 GetAllocatedBytes() const;
    FString GetSummary() const;

protected:
    virtual void OnUnregister() override;

private:
    FVector3d RaisedPosition(int32 VertexIndex) const;
    void RebuildNormals();
    void BeginHeightUpdate();
    void UpdateNormals();
    void UpdateSkirtNormals(int32 ChunkIndex, int32 EdgeIndex, bool bTrackChanges);
    void SubmitUpdates();
    float EvaluateVertexHeight(int32 VertexIndex,
        FMD_ScrapeConstCellView States) const;

    // Payloads deliberately have no Details exposure and are never serialized.
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> SurfaceMesh;
    UPROPERTY(Transient) TObjectPtr<UMD_ScrapeMeshUVData> Profile;
    UPROPERTY(Transient) TObjectPtr<UMaterialInterface> SolidMaterial;
    UPROPERTY(Transient) TObjectPtr<UMDSolidMeshComponent> SolidMesh;
    TSharedPtr<const FMD_ContaminationSolidGeometry> Geometry;
    TSharedPtr<const FMDSolidRenderTemplate, ESPMode::ThreadSafe> RenderTemplate;
    TSharedPtr<FMDSolidDynamicData, ESPMode::ThreadSafe> DynamicData;
    TArray<int32> ChunkBoundaryOffsets;
    TArray<uint32> VertexVisitStamps;
    TArray<uint32> NormalVisitStamps;
    TArray<int32> ChangedVertices;
    TArray<int32> ChangedNormals;
    TArray<int32> NormalVertices;
    uint32 UpdateStamp = 0;
    int32 LastUpdatedChunkCount = 0;
    float ResolvedCullDistanceCm = 0.0f;
    FMD_ContaminationSolidSettings ActiveSettings;
    FVector ActiveScale = FVector::OneVector;
    FGuid ActiveLayoutId;
};
