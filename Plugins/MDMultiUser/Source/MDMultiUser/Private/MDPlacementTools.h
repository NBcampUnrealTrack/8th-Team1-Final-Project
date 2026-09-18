#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

class UStaticMesh;
class UStaticMeshComponent;
struct FPropertyChangedEvent;

namespace MDPlacement
{
    struct FEdge { FVector A; FVector B; };
    // Per-drag cache. Mesh edits invalidate it; actor translation/rotation only rebase edges.
    class FGeometryCache
    {
    public:
        FGeometryCache();
        ~FGeometryCache();
        FGeometryCache(const FGeometryCache&) = delete;
        FGeometryCache& operator=(const FGeometryCache&) = delete;
        bool Read(AActor* Actor, TArray<FEdge>& Out, FText& Reason);
        void Reset() { Entries.Reset(); }
    private:
        struct FEntry { TWeakObjectPtr<UStaticMesh> Mesh; FTransform Transform; FGuid Revision; TArray<FEdge> Edges; };
        TMap<TWeakObjectPtr<AActor>,FEntry> Entries;
        FDelegateHandle ChangeHandle;
        void Changed(UObject* Object,FPropertyChangedEvent& Event);
    };
    enum class EAnchor : uint8 { Endpoint, Midpoint };
    struct FOptions
    {
        TOptional<FVector> TargetPoint;
        double Distance = 100.0;
        double AngleDegrees = 15.0;
        bool bRotate = false;
        EAnchor Anchor = EAnchor::Endpoint;
    };
    FOptions& SessionOptions();

    struct FMatch
    {
        FTransform Transform;
        FEdge SourceEdge;
        FEdge TargetEdge;
        FVector SourceAnchor;
        FVector TargetAnchor;
        double Distance = 0;
        double Angle = 0;
        double Score = 0;
    };
    struct FCandidate : FMatch
    {
        TWeakObjectPtr<AActor> Source;
        TWeakObjectPtr<AActor> Target;
        FTransform OriginalSource;
        FTransform OriginalTarget;
    };

    FText Text(const TCHAR* Ko, const TCHAR* En);
    AActor* SelectedActor();
    UStaticMeshComponent* RootMesh(AActor* Actor);
    bool CanMove(AActor* Actor, FText& Reason);
    bool ExtractEdges(AActor* Actor, TArray<FEdge>& Out, FText& Reason);
    bool MatchEdges(const FEdge& Source, const FEdge& Target, const FTransform& ActorTransform,
        const FOptions& Options, FMatch& Out, bool bIncludeStationary = false);
    bool IsSnapshotCurrent(const FCandidate& Candidate);
    void FindCandidates(AActor* Source, AActor* PinnedTarget, const FOptions& Options,
        TArray<FCandidate>& Out, FText& Result, const FTransform* SourceOverride = nullptr, FGeometryCache* Cache = nullptr);
    bool Apply(const FCandidate& Candidate, FText& Result);
    void DrawPreview(const FCandidate& Candidate);
    enum class EQuickAction : uint8 { Floor, Slope, Grid, Focus };
    bool QuickAction(EQuickAction Action, FText& Result);
}
