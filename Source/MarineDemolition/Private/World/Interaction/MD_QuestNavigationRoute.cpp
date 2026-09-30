#include "World/Interaction/MD_QuestNavigationRoute.h"

#include "Components/SplineComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Component/Merchant/MD_MerchantComponent.h"
#include "Game/MD_GameStateBase.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"
#include "World/Interaction/MD_DataDumpTerminal.h"
#include "World/Wall/MD_WallUtility.h"

AMD_QuestNavigationRoute::AMD_QuestNavigationRoute()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.2f;
    RouteSpline = CreateDefaultSubobject<USplineComponent>(TEXT("RouteSpline"));
    SetRootComponent(RouteSpline);
    RouteGeometry = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("RouteGeometry"));
    RouteGeometry->SetupAttachment(RouteSpline);
    RouteGeometry->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    RouteGeometry->SetGenerateOverlapEvents(false);
    RouteGeometry->SetCanEverAffectNavigation(false);
    RouteGeometry->SetCastShadow(false);
    RouteGeometry->SetHiddenInGame(true);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
    RouteGeometry->SetStaticMesh(Cube.Object);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/QuestNavigation/M_QuestRoute"));
    RouteMaterial = Material.Object;
    QuestDefinitionId = FPrimaryAssetId(TEXT("Quest"), TEXT("X7-M02"));
}

void AMD_QuestNavigationRoute::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    RebuildRoute();
    RouteGeometry->SetVisibility(bPreviewInEditor);
}

void AMD_QuestNavigationRoute::BeginPlay()
{
    Super::BeginPlay();
    if (GetNetMode() == NM_DedicatedServer)
    {
        RouteGeometry->ClearInstances();
        SetActorTickEnabled(false);
        return;
    }
    RebuildRoute();
    RouteGeometry->SetVisibility(true);
    const bool bActive = ShouldDisplayRoute();
    RouteGeometry->SetHiddenInGame(!bActive);
    UpdateTargetHighlight(IsQuestActive());
}

void AMD_QuestNavigationRoute::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const bool bActive = ShouldDisplayRoute();
    RouteGeometry->SetHiddenInGame(!bActive);
    UpdateTargetHighlight(IsQuestActive());
}

void AMD_QuestNavigationRoute::UpdateTargetHighlight(bool bActive)
{
    TSet<TWeakObjectPtr<AActor>> Targets;
    if (bActive && !bGuideToQuestLocation)
    {
        // All outstanding repairs belong to the objective, regardless of the selected path.
        auto AddRepair = [&Targets](AMD_WallUtility* Utility)
        {
            if (IsValid(Utility) && Utility->GetUtilityState() == EMD_UtilityState::Broken) Targets.Add(Utility);
        };
        AddRepair(RepairTarget);
        for (AMD_WallUtility* Utility : RequiredRepairs) AddRepair(Utility);
        if (bGuideToTerminal && ShouldDisplayRoute()) Targets.Add(Terminal.Get());
    }
    auto SetHighlight = [this](AActor* Actor, bool bHighlighted)
    {
        if (AMD_WallUtility* Utility = Cast<AMD_WallUtility>(Actor)) Utility->SetQuestHighlighted(this, bHighlighted);
        if (AMD_DataDumpTerminal* Console = Cast<AMD_DataDumpTerminal>(Actor)) Console->SetQuestHighlighted(this, bHighlighted);
    };
    for (const auto& Target : HighlightedTargets)
        if (!Targets.Contains(Target)) SetHighlight(Target.Get(), false);
    for (const auto& Target : Targets)
        if (!HighlightedTargets.Contains(Target)) SetHighlight(Target.Get(), true);
    HighlightedTargets = MoveTemp(Targets);
}

void AMD_QuestNavigationRoute::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    UpdateTargetHighlight(false);
    Super::EndPlay(EndPlayReason);
}

bool AMD_QuestNavigationRoute::IsQuestActive() const
{
    if (!QuestDefinitionId.IsValid()) return false;
    if (!bGuideToQuestLocation && (!IsValid(Terminal) || Terminal->IsDumped())) return false;
    const AMD_GameStateBase* State = GetWorld() ? GetWorld()->GetGameState<AMD_GameStateBase>() : nullptr;
    const UMD_MerchantComponent* Merchant = State ? State->GetMerchantComponent() : nullptr;
    if (!Merchant || (RequiredDay > 0 && Merchant->GetCurrentDay() != RequiredDay)) return false;
    const TArray<FMD_MainQuestState> Quests = Merchant->GetMainQuests();
    const FMD_MainQuestState* Quest = Quests.FindByPredicate([this](const FMD_MainQuestState& Q)
    {
        return Q.DefinitionId == QuestDefinitionId && Q.IsActive() && Q.CurrentPhaseIndex == PhaseIndex;
    });
    return Quest != nullptr;
}

bool AMD_QuestNavigationRoute::ShouldDisplayRoute() const
{
    if (!IsQuestActive()) return false;
    if (bGuideToQuestLocation) return true;
    for (const AMD_WallUtility* Utility : RequiredRepairs)
    {
        // Missing references must not prematurely advance the guide.
        if (!IsValid(Utility) || Utility->GetUtilityState() == EMD_UtilityState::Broken) return false;
    }
    if (bGuideToTerminal) return !Terminal->GetPersistentLocked();
    return IsValid(RepairTarget) && RepairTarget->GetUtilityState() == EMD_UtilityState::Broken;
}

void AMD_QuestNavigationRoute::RebuildRoute()
{
    RouteGeometry->ClearInstances();
    RouteGeometry->SetMaterial(0, RouteMaterial);
    const float Length = RouteSpline->GetSplineLength();
    if (Length <= KINDA_SMALL_NUMBER) return;
    const float Width = FMath::Clamp(LineWidth, 1.f, 20.f);
    auto AddBar = [this, Width](const FVector& A, const FVector& B, float BarWidth)
    {
        const FVector Delta = B - A;
        if (Delta.IsNearlyZero()) return;
        RouteGeometry->AddInstance(FTransform(Delta.Rotation(), (A + B) * 0.5f,
            FVector(Delta.Size() / 100.f, BarWidth / 100.f, Width / 200.f)));
    };
    // Sample the editable spline into short bars; instancing keeps draw-call cost bounded.
    const int32 Count = FMath::Clamp(FMath::CeilToInt(Length / 40.f), 1, 4096);
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const float D = Length * Index / Count;
        const float End = Length * (Index + 1) / Count;
        AddBar(RouteSpline->GetLocationAtDistanceAlongSpline(D, ESplineCoordinateSpace::Local),
            RouteSpline->GetLocationAtDistanceAlongSpline(End, ESplineCoordinateSpace::Local), Width);
    }
    // Chevrons show travel direction even when the player joins the path halfway through.
    for (float D = 120.f; D < Length; D += 200.f)
    {
        const FVector Tip = RouteSpline->GetLocationAtDistanceAlongSpline(D, ESplineCoordinateSpace::Local);
        const FVector Forward = RouteSpline->GetDirectionAtDistanceAlongSpline(D, ESplineCoordinateSpace::Local);
        const FVector Side = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();
        AddBar(Tip - Forward * 28.f + Side * 20.f, Tip, Width);
        AddBar(Tip - Forward * 28.f - Side * 20.f, Tip, Width);
    }
}
