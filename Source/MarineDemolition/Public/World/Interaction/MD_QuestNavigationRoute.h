#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/PrimaryAssetId.h"
#include "MD_QuestNavigationRoute.generated.h"

class USplineComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class AMD_WallUtility;
class AMD_DataDumpTerminal;

/** Authored, collision-free world guidance. Reads replicated quest/utility state without changing it. */
UCLASS(Blueprintable)
class MARINEDEMOLITION_API AMD_QuestNavigationRoute : public AActor
{
    GENERATED_BODY()
public:
    AMD_QuestNavigationRoute();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Route")
    TObjectPtr<USplineComponent> RouteSpline;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Route|Quest")
    FPrimaryAssetId QuestDefinitionId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Route|Quest", meta=(ClampMin="0"))
    int32 PhaseIndex = 0;

    /** Zero allows this quest on any day. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Route|Quest", meta=(ClampMin="0"))
    int32 RequiredDay = 1;

    /** Route is available after all these utilities cease to be broken. */
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Route|Quest")
    TArray<TObjectPtr<AMD_WallUtility>> RequiredRepairs;

    /** When GuideToTerminal is false, show this route only while this target is broken. */
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Route|Quest")
    TObjectPtr<AMD_WallUtility> RepairTarget;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Route|Quest")
    bool bGuideToTerminal = false;

    /** Required for all stages; collecting its cartridge ends every associated route. */
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Route|Quest")
    TObjectPtr<AMD_DataDumpTerminal> Terminal;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Route|Appearance")
    TObjectPtr<UMaterialInterface> RouteMaterial;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Route|Appearance", meta=(ClampMin="1", ClampMax="20"))
    float LineWidth = 4.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Route|Appearance")
    bool bPreviewInEditor = true;

    UFUNCTION(BlueprintPure, Category="Route")
    bool ShouldDisplayRoute() const;

    UFUNCTION(BlueprintCallable, CallInEditor, Category="Route")
    void RebuildRoute();

private:
    TSet<TWeakObjectPtr<AActor>> HighlightedTargets;
    bool IsQuestActive() const;
    void UpdateTargetHighlight(bool bActive);
    UPROPERTY(VisibleAnywhere, Category="Route")
    TObjectPtr<UInstancedStaticMeshComponent> RouteGeometry;
};
