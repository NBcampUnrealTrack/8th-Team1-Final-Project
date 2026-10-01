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
#include "ProfilingDebugging/CpuProfilerTrace.h"
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

FMD_ContaminationGeneration UMD_ScrapeSurfaceComponent::MakeContaminationGeneration() const
{
    // 현재 표면의 생성 설정을 보관할 기록 구성
    FMD_ContaminationGeneration Generation;
    // 현재 사용하는 연속 밀도 생성 알고리즘 식별자 설정
    Generation.Version = 3;
    // 전체 오염 강제 옵션을 반영한 절차적 생성 사용 여부 설정
    Generation.bProcedural = bForceFullContamination == false;
    // 유효한 범위의 셀 기준 오염량 반영
    Generation.BaseAmount = FMath::IsFinite(InitialContaminationPerCell) == true ? FMath::Clamp(InitialContaminationPerCell, 0.f, 1.f) : 1.f;
    // 현재 노이즈 크기 설정 반영
    Generation.NoiseScaleCm = ContaminationNoiseScaleCm;
    // 고정 시드 또는 세션 시드 조회 실패 시 사용할 작성 시드 반영
    Generation.Seed = ContaminationNoiseSeed;
    // 게임 실행 중 공통 세션 시드를 사용할 월드 조회
    UWorld* World = GetWorld();
    // 고정 시드가 아닌 실행 월드에서 공통 세션 시드 사용
    if (bUseFixedContaminationSeed == false && IsValid(World) == true && World->IsGameWorld() == true)
    {
        // 공통 세션 시드를 보관하는 게임 인스턴스 조회
        UGameInstance* GameInstance = World->GetGameInstance();
        // 현재 월드의 영속 상태 관리자 조회
        UMD_DayPersistenceSubsystem* Persistence = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_DayPersistenceSubsystem>() : nullptr;
        // 유효한 관리자에서 현재 세션의 오염 생성 시드 조회
        if (IsValid(Persistence) == true)
        {
            // 고정 시드 설정을 제외한 공통 시드 반영
            Persistence->TryGetContaminationSessionSeed(World, Generation.Seed);
        }
    }
    // 현재 시드와 생성 설정에 따른 표면 오염 면적 설정
    Generation.Coverage = Generation.bProcedural == true ? ResolveContaminationCoverage(Generation.Seed) : 1.f;
    // 현재 연속 밀도 설정의 최소 오염량 사용
    Generation.MinimumAmount = ContaminationDensityMinimumAmount;
    // 샘플 좌표 기준이 될 유효한 표면 메시 확인
    if (IsValid(SurfaceMesh) == true)
    {
        // 현재 표면 메시 좌표계 기록
        Generation.SampleTransform = SurfaceMesh->GetComponentTransform();
    }
    // 현재 생성 형식의 완성된 기록 반환
    return Generation;
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
    // 실행 월드에 대응하는 현재 저장 생성 정보 조회 준비
    UGameInstance* GI = bRuntime == true ? GetWorld()->GetGameInstance() : nullptr;
    // 저장된 생성 정보를 제공하는 영속 상태 관리자 조회
    UMD_DayPersistenceSubsystem* Persistence = IsValid(GI) == true ? GI->GetSubsystem<UMD_DayPersistenceSubsystem>() : nullptr;
    // 현재 표면의 저장 기록이 없으면 현재 설정으로 새 생성 정보 구성
    if (IsValid(Persistence) == false || Persistence->TryGetSavedContaminationGeneration(this, G) == false)
    {
        // 과거 생성 방식 대체 없이 현재 생성 정책 적용
        G = MakeContaminationGeneration();
    }
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
    ++DeferredInitializationEpoch;
    bDeferredInitializationPending=false;PreparedInitialState.Reset();
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
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scrape_InitialAreaIntegration);
    const auto G=ContaminationGeneration;
    if (!G.IsValid() || !SurfaceMesh) return false;
    if (PreparedInitialState.IsSet())
    {
        ContaminationPagedCells.InitializeFromAmounts(MoveTemp(PreparedInitialState->Amounts));
        ContaminationCells.Empty();
        ContaminationGeneratedInitialAmounts.Empty();
        return true;
    }
    const int32 Count = UsesMeshUVSurface() && ContaminationCellLayout
        ? ContaminationCellLayout->ActiveToAtlas.Num() : ContaminationCells.Num();
    TArray<float> Amounts;Amounts.SetNumZeroed(Count);
    if (UsesMeshUVSurface())
    {
        if (!ContaminationMeshUVData || !ContaminationCellLayout) return false;
        const auto* Data=ContaminationMeshUVData.Get();
        const auto* Layout=ContaminationCellLayout.Get();
        Amounts = MD_ScrapeInitialState::Generate(*Data, *Layout, G);
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
