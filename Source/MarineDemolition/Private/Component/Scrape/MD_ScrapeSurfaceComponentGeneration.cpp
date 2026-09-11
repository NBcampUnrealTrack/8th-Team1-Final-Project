#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Gameplay/Scrape/MD_ContaminationNoise.h"
#include "Gameplay/Scrape/MD_ScrapeMeshUVData.h"
#include "Gameplay/Scrape/MD_ScrapeMeshUVSyncSubsystem.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceGeometry.h"
#include "Gameplay/Scrape/MD_ScrapeMaskAtlas.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Gameplay/Scrape/MD_ScrapeRuntimeMaskTexture.h"
#include "Async/ParallelFor.h"
#include "GameFramework/Actor.h"
#include "Math/RandomStream.h"
#include "Misc/Crc.h"
#include "Engine/GameInstance.h"
#include "Gameplay/Persistence/MD_DayPersistenceSubsystem.h"

float UMD_ScrapeSurfaceComponent::ResolveContaminationCoverage(const int32 ResolvedSeed) const
{
    const float A = FMath::Clamp(FMath::IsFinite(ContaminationCoverageMin) ? ContaminationCoverageMin : 0.55f, 0.f, 1.f);
    const float B = FMath::Clamp(FMath::IsFinite(ContaminationCoverageMax) ? ContaminationCoverageMax : 1.f, 0.f, 1.f);
    const float Low = FMath::Min(A, B), High = FMath::Max(A, B);
    if (Low == High) return Low;

    // Actor/component names omit PIE package prefixes and are reproducible across worlds.
    // Never hash FName indices, pointers, world position or the process-global random state.
    const AActor* Owner = GetOwner();
    const FString Identity = Owner ? Owner->GetName() + TEXT("/") + GetName() : GetName();
    FRandomStream Random(static_cast<int32>(FCrc::StrCrc32(*Identity, static_cast<uint32>(ResolvedSeed))));
    return FMath::Lerp(Low, High, Random.GetFraction());
}

FMD_ContaminationGeneration UMD_ScrapeSurfaceComponent::MakeContaminationGeneration(const bool bLegacy) const
{
    FMD_ContaminationGeneration G;
    G.Version=bLegacy?1:3; G.bProcedural=bLegacy?bUseProceduralContamination:!bForceFullContamination;
    G.BaseAmount=FMath::IsFinite(InitialContaminationPerCell)?FMath::Clamp(InitialContaminationPerCell,0.f,1.f):1.f;
    G.NoiseScaleCm=ContaminationNoiseScaleCm;G.Seed=ContaminationNoiseSeed;
    if (!bLegacy && !bUseFixedContaminationSeed && GetWorld() && GetWorld()->IsGameWorld())
        if (const auto* GI=GetWorld()->GetGameInstance())
            if (const auto* Persistence=GI->GetSubsystem<UMD_DayPersistenceSubsystem>())
                Persistence->TryGetContaminationSessionSeed(GetWorld(),G.Seed);
    G.Coverage=bLegacy?ContaminationCoverage:(G.bProcedural?ResolveContaminationCoverage(G.Seed):1.f);
    G.MinimumAmount=bLegacy?ContaminationMinimumAmount:ContaminationDensityMinimumAmount;
    if (SurfaceMesh) G.SampleTransform=SurfaceMesh->GetComponentTransform();
    return G;
}
void UMD_ScrapeSurfaceComponent::PrepareContaminationGeneration()
{
    if (GenerationTransformSource.Get()!=SurfaceMesh.Get())
    {
        if (auto* Previous=GenerationTransformSource.Get()) Previous->TransformUpdated.Remove(GenerationTransformHandle);
        GenerationTransformSource=SurfaceMesh;
        if (SurfaceMesh) GenerationTransformHandle=SurfaceMesh->TransformUpdated.AddWeakLambda(this,
            [this](USceneComponent*,EUpdateTransformFlags,ETeleportType) { RefreshContaminationDecalMaterialParameters(); });
    }
    if (bRestoringContaminationGeneration) { bRestoringContaminationGeneration=false; AppliedContaminationGeneration=ContaminationGeneration; return; }
    const bool bRuntime=GetWorld() && GetWorld()->IsGameWorld();
    if ((!HasAuthority() || bRuntime) && ContaminationGeneration.IsValid())
    {
        AppliedContaminationGeneration=ContaminationGeneration;
        return;
    }
    FMD_ContaminationGeneration G;
    bool bLegacy=false;
    const auto* GI=bRuntime?GetWorld()->GetGameInstance():nullptr;
    const auto* Persistence=GI?GI->GetSubsystem<UMD_DayPersistenceSubsystem>():nullptr;
    if (!Persistence || !Persistence->TryGetSavedContaminationGeneration(this,G,bLegacy))
        G=MakeContaminationGeneration(bLegacy);
    if (!G.IsValid())
    {
        UE_LOG(LogTemp,Warning,TEXT("%s: invalid contamination generation settings; using full coverage."),*GetPathName());
        G=FMD_ContaminationGeneration();G.Version=1;
    }
    if (G!=ContaminationGeneration && GetWorld())
        if (auto* Sync=GetWorld()->GetSubsystem<UMD_ScrapeMeshUVSyncSubsystem>()) Sync->InvalidateSurface(*this);
    ContaminationGeneration=G;AppliedContaminationGeneration=G;
}
bool UMD_ScrapeSurfaceComponent::RestoreContaminationGeneration(const FMD_ContaminationGeneration& G, const bool bResetState)
{
    if (!G.IsValid()) return false;
    if (!bResetState && G==AppliedContaminationGeneration && bContaminationSurfaceInitialized) return true;
    if (HasAuthority() && GetWorld())
        if (auto* Sync=GetWorld()->GetSubsystem<UMD_ScrapeMeshUVSyncSubsystem>()) Sync->InvalidateSurface(*this);
    ContaminationGeneration=G;bRestoringContaminationGeneration=true;
    InitializeSurface();
    return !bContaminationEnabled || bContaminationSurfaceInitialized;
}
void UMD_ScrapeSurfaceComponent::OnRep_ContaminationGeneration()
{
    if (!ContaminationGeneration.IsValid() || (bContaminationSurfaceInitialized && ContaminationGeneration==AppliedContaminationGeneration)) return;
    bRestoringContaminationGeneration=true;
    InitializeRuntimeState();
}
bool UMD_ScrapeSurfaceComponent::TryApplyContinuousInitialContamination()
{
    const auto G=ContaminationGeneration;
    if (!G.IsValid() || !SurfaceMesh) return false;
    const int32 Count = UsesMeshUVSurface() && ContaminationCellLayout
        ? ContaminationCellLayout->ActiveToAtlas.Num() : ContaminationCells.Num();
    TArray<float> Amounts;Amounts.SetNumZeroed(Count);
    if (UsesMeshUVSurface())
    {
        if (!ContaminationMeshUVData || !ContaminationCellLayout) return false;
        const auto* Data=ContaminationMeshUVData.Get();
        const auto* Layout=ContaminationCellLayout.Get();
        const double MaximumAxisScale = G.SampleTransform.GetMaximumAxisScale();
        // Immutable geometry and descriptor are read by workers. No UObject calls or GPU readback.
        ParallelFor(Amounts.Num(),[&](int32 I)
        {
            const int32 Address=Layout->ActiveToAtlas[I];const auto& Cell=Data->Cells[Address];
            const FVector WorldCenter=G.SampleTransform.TransformPosition(Cell.PositionLocal);
            const float N=FMD_ContaminationNoise::FractalNoise3D(WorldCenter,G.NoiseScaleCm,G.Seed);
            const double Bound=4.5*Cell.SupportRadiusCm*MaximumAxisScale/G.NoiseScaleCm;
            if (G.Coverage<=UE_SMALL_NUMBER || N+Bound<=1.f-G.Coverage) return;
            if (N-Bound>1.f-G.Coverage) { Amounts[I]=G.AmountFromNoise(N);return; }
            TArray<FVector> Polygon;
            if (!FMD_ScrapeMeshUVGeometry::GetCellPolygonLocal(*Data,Address,Polygon)) return;
            double AreaSum=0,Sum=0;
            for (int32 J=1;J+1<Polygon.Num();++J)
            {
                const double Area=FVector::CrossProduct(Polygon[J]-Polygon[0],Polygon[J+1]-Polygon[0]).Length();
                AreaSum+=Area;Sum+=Area*G.IntegrateTriangle(Polygon[0],Polygon[J],Polygon[J+1]);
            }
            Amounts[I]=AreaSum>0?float(Sum/AreaSum):0;
        });
    }
    else
    {
        const int32 PerFace=ContaminationGridDimensions.X*ContaminationGridDimensions.Y;
        const FVector2D CellSize(ResolvedContaminationSurfaceSizeCm.X/ContaminationGridDimensions.X,
            ResolvedContaminationSurfaceSizeCm.Y/ContaminationGridDimensions.Y);
        TAtomic<bool> Valid(true);
        ParallelFor(Amounts.Num(),[&](int32 I)
        {
            const auto Face=I>=PerFace?EMD_ScrapeSurfaceFace::Back:EMD_ScrapeSurfaceFace::Front;
            const int32 Local=I%PerFace;
            const FVector2D Min=ResolvedContaminationSurfaceSizeCm*-.5
                +FVector2D(Local%ContaminationGridDimensions.X,Local/ContaminationGridDimensions.X)*CellSize;
            FVector P[4];
            for (int32 J=0;J<4;++J)
            {
                const FVector2D Offset((J==1||J==2)?CellSize.X:0,(J>=2)?CellSize.Y:0);
                FVector World;
                if (!FMD_ScrapeSurfaceGeometry::TryCalculateSurfaceWorldPosition(Min+Offset,Face,
                    G.SampleTransform,ResolvedContaminationSurfaceNormalAxis,ContaminationSurfaceCenterLocal,
                    ContaminationMeshLocalBoundsMinimum,ContaminationMeshLocalBoundsMaximum,
                    ContaminationSurfaceMapping,ResolvedContaminationSurfaceSizeCm,World)) { Valid.Store(false);return; }
                P[J]=G.SampleTransform.InverseTransformPosition(World);
            }
            const FVector Center=(P[0]+P[1]+P[2]+P[3])*.25;
            double Radius=0;
            for (const FVector& Point : P) Radius=FMath::Max(Radius,G.SampleTransform.TransformVector(Point-Center).Length());
            const float N=FMD_ContaminationNoise::FractalNoise3D(G.SampleTransform.TransformPosition(Center),G.NoiseScaleCm,G.Seed);
            const double Bound=4.5*Radius/G.NoiseScaleCm;
            if (G.Coverage<=UE_SMALL_NUMBER || N+Bound<=1.f-G.Coverage) return;
            if (N-Bound>1.f-G.Coverage) { Amounts[I]=G.AmountFromNoise(N);return; }
            Amounts[I]=.5f*(G.IntegrateTriangle(P[0],P[1],P[2])+G.IntegrateTriangle(P[0],P[2],P[3]));
        });
        if (!Valid.Load()) return false;
    }
    if (UsesMeshUVSurface() && ContaminationCellLayout)
    {
        ContaminationPagedCells.InitializeFromAmounts(MoveTemp(Amounts));
        ContaminationCells.Empty();
        ContaminationGeneratedInitialAmounts.Empty();
        return true;
    }
    for (int32 I=0;I<Amounts.Num();++I)
    {
        ContaminationCells[I].InitialContamination=Amounts[I];
        ContaminationCells[I].RemainingContamination=Amounts[I];
    }
    ContaminationGeneratedInitialAmounts=MoveTemp(Amounts);
    return true;
}
float UMD_ScrapeSurfaceComponent::CalculateContaminationVisualSignal(const FMD_ScrapeSurfaceCell& Cell) const
{
    if (ContaminationGeneration.Version<2 || !ContaminationGeneration.bProcedural)
        return FMD_ScrapeMaskAtlas::CalculateVisualMaskValue(Cell.RemainingContamination,
            Cell.InitialContamination,Cell.CoveredFraction,ContaminationVisualFilmOpacityGamma);
    if (Cell.InitialContamination<=UE_SMALL_NUMBER) return 0;
    // Initial shape is reconstructed independently on GPU; the cell mask carries scrape progress.
    return FMD_ScrapeMaskAtlas::CalculateVisualMaskValue(Cell.RemainingContamination/Cell.InitialContamination,
        1.f,Cell.CoveredFraction,ContaminationVisualFilmOpacityGamma);
}
void UMD_ScrapeSurfaceComponent::ApplyContaminationGenerationParameters(UMaterialInstanceDynamic& MID) const
{
    const auto& G=ContaminationGeneration;
    // Shader mode: 0 bypass, 1 legacy contour, 2 continuous absolute density.
    MID.SetScalarParameterValue(TEXT("ProceduralContourEnabled"),(G.Version>=2 && G.bProcedural)?(G.Version>=3?2.f:1.f):0.f);
    MID.SetVectorParameterValue(TEXT("ProceduralNoiseSettings"),FLinearColor(G.NoiseScaleCm,G.Coverage,G.MinimumAmount,G.BaseAmount));
    MID.SetVectorParameterValue(TEXT("ProceduralNoiseSeed"),FLinearColor(uint32(G.Seed)&65535,uint32(G.Seed)>>16,0,0));
    // The pattern is anchored at generation time; moving a wall does not regenerate its dirt.
    const FMatrix M=(SurfaceMesh ? SurfaceMesh->GetComponentTransform().ToInverseMatrixWithScale() : FMatrix::Identity)
        *G.SampleTransform.ToMatrixWithScale();
    MID.SetTextureParameterValue(TEXT("ProceduralOverrideMask"),ProceduralOverrideTexture);
    for (int32 I=0;I<3;++I)
        MID.SetVectorParameterValue(FName(*FString::Printf(TEXT("ProceduralTransform%d"),I)),
            FLinearColor(M.M[0][I],M.M[1][I],M.M[2][I],M.M[3][I]));
}

void UMD_ScrapeSurfaceComponent::ReleaseContaminationGenerationVisual()
{
    ProceduralOverrideTexture=nullptr;ProceduralOverridePixels.Empty();
}
void UMD_ScrapeSurfaceComponent::UpdateContaminationGenerationOverride(const TArray<FMD_ScrapeCellChange>& Changes)
{
    if (ContaminationGeneration.Version<2 || !ContaminationGeneration.bProcedural || !ContaminationMaskTexture) return;
    // MeshUV's immutable initial template is the generated baseline. With no
    // modified/predicted pages every override is provably zero, including save reinit.
    if (!ProceduralOverrideTexture && UsesMeshUVSurface()
        && ContaminationPagedCells.IsPristineCanonicalInitial()
        && !HasLocalPredictedContaminationState()) return;
    const auto Cells=GetVisualContaminationCells();
    auto OverrideAt=[&](int32 Address)->uint8
    {
        if (UsesMeshUVSurface())
        {
            if (!ContaminationMeshUVData || !ContaminationMeshUVData->Cells.IsValidIndex(Address)) return 0;
            if (!ContaminationMeshUVData->IsActiveCell(Address)) Address=ContaminationMeshUVData->Cells[Address].VisualSourceCellIndex;
        }
        if (!Cells.IsValidIndex(Address)) return 0;
        const int32 Storage=GetContaminationStorageIndex(Address);
        if (Storage==INDEX_NONE || !HasGeneratedInitialContamination(Storage)) return 0;
        // Network initial amounts are G8. A half quantization step is not an overnight deposit.
        const float Initial=Cells[Address].InitialContamination;
        if (Initial<=GetGeneratedInitialContamination(Storage)+.5f/255.f+1.e-6f) return 0;
        // Version 3 deposits retain their absolute density, including outside the original patch.
        return ContaminationGeneration.Version>=3
            ?uint8(FMath::Clamp(FMath::RoundToInt(Initial*255.f),0,255)):255;
    };
    bool NeedsMask=ProceduralOverrideTexture!=nullptr;
    if (!NeedsMask)
    {
        if (Changes.IsEmpty())
        {
            for (int32 I=0;I<GetContaminationAtlasCellCount();++I)
                if (OverrideAt(I)) { NeedsMask=true;break; }
        }
        else for (const auto& C : Changes) if (OverrideAt(C.CellIndex)) { NeedsMask=true;break; }
    }
    if (!NeedsMask) return;
    const bool Initial=!ProceduralOverrideTexture || ProceduralOverridePixels.Num()!=ContaminationMaskAtlasPixels.Num();
    if (Initial || Changes.IsEmpty())
    {
        TArray<uint8> Pixels;Pixels.SetNumZeroed(ContaminationMaskAtlasPixels.Num());
        for (int32 I=0;I<Pixels.Num();++I) Pixels[I]=OverrideAt(I);
        ProceduralOverridePixels=MoveTemp(Pixels);
    }
    else for (const auto& C : Changes)
        if (C.CellIndex>=0 && C.CellIndex<ProceduralOverridePixels.Num()) ProceduralOverridePixels.Set(C.CellIndex,OverrideAt(C.CellIndex));
    ProceduralOverrideTexture=Initial
        ?FMD_ScrapeRuntimeMaskTexture::AcquireInitial(ContaminationMaskAtlasDimensions,ProceduralOverridePixels,TF_Bilinear)
        :FMD_ScrapeRuntimeMaskTexture::UpdateMask(ProceduralOverrideTexture,ProceduralOverridePixels);
    RefreshContaminationDecalMaterialParameters();
}
