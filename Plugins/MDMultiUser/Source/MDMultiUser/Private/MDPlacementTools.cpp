#include "MDPlacementTools.h"
#include "MDHoldSnap.h"

#include "MDWorkspacePreferences.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "IConcertClientWorkspace.h"
#include "IConcertSyncClient.h"
#include "IMultiUserClientModule.h"
#include "LevelEditor.h"
#include "LevelEditorActions.h"
#include "LevelUtils.h"
#include "MeshDescription.h"
#include "ScopedTransaction.h"
#include "Framework/Commands/UICommandList.h"

namespace MDPlacement
{
    FOptions& SessionOptions() { static FOptions Options; return Options; }

    FText Text(const TCHAR* Ko, const TCHAR* En) { return FText::FromString(FMDWorkspacePreferences::IsEnglish() ? En : Ko); }

    AActor* SelectedActor()
    {
        if (!GEditor || !GEditor->GetSelectedActors() || GEditor->GetSelectedActorCount() != 1) { return nullptr; }
        return Cast<AActor>(GEditor->GetSelectedActors()->GetSelectedObject(0));
    }

    bool CanMove(AActor* Actor, FText& Reason)
    {
        if (!GEditor || GEditor->PlayWorld || GEditor->bIsSimulatingInEditor)
        { Reason = Text(TEXT("플레이를 종료한 뒤 사용하세요."), TEXT("Stop Play / Simulate before editing.")); return false; }
        if (!IsValid(Actor) || Actor->GetWorld() != GEditor->GetEditorWorldContext().World() || !Actor->GetRootComponent())
        { Reason = Text(TEXT("레벨에서 액터 하나를 선택하세요."), TEXT("Select one actor in the editor level.")); return false; }
        if (!Actor->IsEditable() || Actor->IsLockLocation() || FLevelUtils::IsLevelLocked(Actor) || Actor->IsTemporarilyHiddenInEditor(true))
        { Reason = Text(TEXT("숨겨지거나 잠긴 액터는 이동할 수 없습니다."), TEXT("Hidden or locked actors cannot be moved.")); return false; }
        TArray<AActor*> Children;
        Actor->GetAttachedActors(Children);
        if (Actor->GetAttachParentActor() || !Children.IsEmpty())
        { Reason = Text(TEXT("부모·자식 연결이 없는 액터를 선택하세요."), TEXT("Select an actor without parent or child attachments.")); return false; }
        if (IMultiUserClientModule::IsAvailable())
        {
            const auto Sync = IMultiUserClientModule::Get().GetClient();
            if (Sync)
            {
                const auto Workspace = Sync->GetWorkspace();
                if (Workspace)
                {
                    const FName Resources[] = { FName(*Actor->GetPathName()), Actor->GetOutermost()->GetFName() };
                    for (const FName Resource : Resources)
                    {
                        const FGuid Owner = Workspace->GetResourceLockId(Resource);
                        if (Owner.IsValid() && Owner != Workspace->GetWorkspaceLockId())
                        { Reason = Text(TEXT("다른 참여자가 잠근 액터입니다."), TEXT("Another participant owns this resource lock.")); return false; }
                    }
                }
            }
        }
        return true;
    }

    UStaticMeshComponent* RootMesh(AActor* Actor)
    {
        auto* Mesh=Actor ? Cast<UStaticMeshComponent>(Actor->GetRootComponent()) : nullptr;
        return Mesh && !Mesh->IsA<UInstancedStaticMeshComponent>() ? Mesh : nullptr;
    }

    bool ExtractEdges(AActor* Actor, TArray<FEdge>& Out, FText& Reason)
    {
        Out.Reset();
        AActor* MeshActor = Actor;
        const UStaticMeshComponent* Component = RootMesh(MeshActor);
        const UStaticMesh* Mesh = Component ? Component->GetStaticMesh() : nullptr;
        if (!Mesh || MeshActor->GetRootComponent() != Component)
        { Reason = Text(TEXT("모서리 스냅은 StaticMeshComponent가 루트인 액터를 지원합니다."), TEXT("Edge snap requires an actor with a static mesh root.")); return false; }
        const FMeshDescription* Description = Mesh->GetMeshDescription(0);
        if (!Description || Description->Triangles().Num() > 50000)
        { Reason = Text(TEXT("원본 LOD0가 없거나 50,000개 삼각형 제한을 초과했습니다."), TEXT("Missing source LOD0, or more than 50,000 triangles.")); return false; }
        const FTransform Transform = Component->GetComponentTransform();
        if (Transform.GetScale3D().GetAbsMin() < KINDA_SMALL_NUMBER)
        { Reason = Text(TEXT("0에 가까운 스케일은 지원하지 않습니다."), TEXT("Near-zero scale is not supported.")); return false; }
        struct FAdjacency { FVector A; FVector B; TArray<FVector, TInlineAllocator<2>> Normals; };
        TMap<FIntVector, int32> Welded;
        TMap<uint64, FAdjacency> Adjacency;
        // Weld source positions to 0.01 cm to remove split UV/material seams before feature extraction.
        const auto VertexKey = [&Welded](const FVector& V)
        {
            const FIntVector Key(FMath::RoundToInt(V.X * 100), FMath::RoundToInt(V.Y * 100), FMath::RoundToInt(V.Z * 100));
            if (const int32* Existing = Welded.Find(Key)) { return *Existing; }
            const int32 Id = Welded.Num(); Welded.Add(Key, Id); return Id;
        };
        for (const FTriangleID Triangle : Description->Triangles().GetElementIDs())
        {
            const auto Vertices = Description->GetTriangleVertices(Triangle);
            FVector Positions[3]; int32 Ids[3];
            for (int32 I = 0; I < 3; ++I)
            {
                const FVector Local(Description->GetVertexPosition(Vertices[I]));
                if (Local.GetAbsMax() > 1.e7 || Local.ContainsNaN()) { Reason = Text(TEXT("메시 좌표 범위를 확인하세요."), TEXT("Mesh coordinates exceed the supported range.")); return false; }
                Ids[I] = VertexKey(Local);
                Positions[I] = Transform.TransformPosition(Local);
            }
            const FVector Normal = FVector::CrossProduct(Positions[1]-Positions[0], Positions[2]-Positions[0]).GetSafeNormal();
            if (Normal.IsNearlyZero()) { continue; }
            for (int32 I = 0; I < 3; ++I)
            {
                const int32 J = (I + 1) % 3;
                if (Ids[I] == Ids[J]) { continue; }
                const uint64 Key = (uint64(FMath::Min(Ids[I], Ids[J])) << 32) | uint32(FMath::Max(Ids[I], Ids[J]));
                FAdjacency& Edge = Adjacency.FindOrAdd(Key);
                Edge.A = Ids[I] < Ids[J] ? Positions[I] : Positions[J];
                Edge.B = Ids[I] < Ids[J] ? Positions[J] : Positions[I]; Edge.Normals.Add(Normal);
            }
        }
        TArray<uint64> Keys; Adjacency.GetKeys(Keys); Keys.Sort();
        TArray<uint64> FeatureKeys;
        TMap<int32, TArray<int32>> Incident;
        for (uint64 Key : Keys)
        {
            const FAdjacency& Edge = Adjacency[Key];
            // Boundaries or a >=25 degree dihedral; non-manifold edges are excluded.
            const bool bFeature = Edge.Normals.Num() == 1 || (Edge.Normals.Num() == 2 &&
                FVector::DotProduct(Edge.Normals[0], Edge.Normals[1]) <= FMath::Cos(FMath::DegreesToRadians(25.0)));
            if (bFeature && FVector::DistSquared(Edge.A, Edge.B) >= 0.01)
            {
                const int32 Index = Out.Add({Edge.A, Edge.B});
                FeatureKeys.Add(Key);
                Incident.FindOrAdd(int32(Key >> 32)).Add(Index);
                Incident.FindOrAdd(int32(uint32(Key))).Add(Index);
            }
            if (Out.Num() > 2048)
            { Out.Reset(); Reason = Text(TEXT("모서리가 너무 많습니다. 단순한 배치용 메시를 사용하세요."), TEXT("More than 2,048 feature edges. Use a simpler placement mesh.")); return false; }
        }
        // Collapse collinear degree-two chains. Tessellated straight edges should
        // expose their true endpoints and midpoint, without crossing feature junctions.
        TArray<FEdge> Merged;
        TSet<int32> Used;
        for (int32 Index = 0; Index < Out.Num(); ++Index)
        {
            if (Used.Contains(Index)) { continue; }
            Used.Add(Index);
            FEdge Edge = Out[Index];
            for (int32 Side = 0; Side < 2; ++Side)
            {
                int32 Vertex = Side == 0 ? int32(FeatureKeys[Index] >> 32) : int32(uint32(FeatureKeys[Index]));
                FVector& End = Side == 0 ? Edge.A : Edge.B;
                const FVector& Other = Side == 0 ? Edge.B : Edge.A;
                for (;;)
                {
                    const TArray<int32>& Links = Incident[Vertex];
                    if (Links.Num() != 2) { break; }
                    const int32 Next = !Used.Contains(Links[0]) ? Links[0] : Links[1];
                    if (Used.Contains(Next)) { break; }
                    const bool bAtA = int32(FeatureKeys[Next] >> 32) == Vertex;
                    const FVector NextEnd = bAtA ? Out[Next].B : Out[Next].A;
                    if (FVector::DotProduct((Other-End).GetSafeNormal(), (NextEnd-End).GetSafeNormal()) > -0.999999) { break; }
                    End = NextEnd;
                    Vertex = bAtA ? int32(uint32(FeatureKeys[Next])) : int32(FeatureKeys[Next] >> 32);
                    Used.Add(Next);
                }
            }
            Merged.Add(Edge);
        }
        Out = MoveTemp(Merged);
        if (Out.IsEmpty()) { Reason = Text(TEXT("맞출 수 있는 경계·모서리가 없습니다."), TEXT("No boundary or sharp feature edges found.")); return false; }
        return true;
    }

    FGeometryCache::FGeometryCache()
    { ChangeHandle=FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this,&FGeometryCache::Changed); }
    FGeometryCache::~FGeometryCache()
    { FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ChangeHandle); }
    void FGeometryCache::Changed(UObject* Object,FPropertyChangedEvent&)
    { if (Object && Object->IsA<UStaticMesh>()) { Reset(); } }
    bool FGeometryCache::Read(AActor* Actor,TArray<FEdge>& Out,FText& Reason)
    {
        auto* MeshActor=Actor;
        auto* Component=RootMesh(MeshActor);
        UStaticMesh* Mesh=Component ? Component->GetStaticMesh() : nullptr;
        if (!Mesh || Component!=Actor->GetRootComponent()) { return ExtractEdges(Actor,Out,Reason); }
        const FTransform Transform=Actor->GetActorTransform();
        if (const FEntry* Entry=Entries.Find(Actor))
        {
            if (Entry->Mesh==Mesh && Entry->Revision==Mesh->GetLightingGuid() && Entry->Transform.GetScale3D().Equals(Transform.GetScale3D(),1.e-6))
            {
                Out=Entry->Edges;
                for (FEdge& Edge:Out)
                {
                    Edge.A=Transform.TransformPosition(Entry->Transform.InverseTransformPosition(Edge.A));
                    Edge.B=Transform.TransformPosition(Entry->Transform.InverseTransformPosition(Edge.B));
                }
                return true;
            }
        }
        if (!ExtractEdges(Actor,Out,Reason)) { return false; }
        if (Entries.Num()>=64) { Entries.Reset(); }
        FEntry& Entry=Entries.FindOrAdd(Actor);
        Entry.Mesh=Mesh; Entry.Revision=Mesh->GetLightingGuid(); Entry.Transform=Transform; Entry.Edges=Out;
        return true;
    }

    bool MatchEdges(const FEdge& Source, const FEdge& Target, const FTransform& ActorTransform, const FOptions& Options, FMatch& Out, bool bIncludeStationary)
    {
        const FVector SD = (Source.B - Source.A).GetSafeNormal();
        FVector TD = (Target.B - Target.A).GetSafeNormal();
        if (SD.IsNearlyZero() || TD.IsNearlyZero() || Options.Distance <= 0) { return false; }
        if (FVector::DotProduct(SD, TD) < 0) { TD *= -1; }
        const double Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(SD, TD), -1.0, 1.0)));
        if (Angle > (Options.bRotate ? Options.AngleDegrees : 0.1)) { return false; }
        FVector SA = (Source.A + Source.B) * .5, TA = (Target.A + Target.B) * .5;
        if (Options.Anchor == EAnchor::Endpoint)
        {
            double Best = TNumericLimits<double>::Max();
            for (const FVector& S : {Source.A, Source.B}) for (const FVector& T : {Target.A, Target.B})
            { const double D = FVector::DistSquared(S,T); if (D < Best) { Best = D; SA = S; TA = T; } }
        }
        const double Distance = FVector::Distance(SA, TA);
        if (Distance > Options.Distance) { return false; }
        const FQuat Delta = Options.bRotate ? FQuat::FindBetweenNormals(SD, TD) : FQuat::Identity;
        const FVector NewLocation = TA - Delta.RotateVector(SA - ActorTransform.GetLocation());
        // Both the anchor and actor pivot must remain within the user-selected movement limit.
        if (FVector::Distance(NewLocation, ActorTransform.GetLocation()) > Options.Distance) { return false; }
        Out.Transform = FTransform((Delta * ActorTransform.GetRotation()).GetNormalized(), NewLocation, ActorTransform.GetScale3D());
        if (!bIncludeStationary && Out.Transform.Equals(ActorTransform, 0.001)) { return false; }
        Out.SourceEdge = Source; Out.TargetEdge = Target;
        Out.SourceAnchor = SA; Out.TargetAnchor = TA; Out.Distance = Distance; Out.Angle = Angle;
        Out.Score = Distance + Angle * .1;
        return true;
    }

    bool IsSnapshotCurrent(const FCandidate& Candidate)
    {
        return Candidate.Source.IsValid() && Candidate.Target.IsValid()
            && Candidate.Source->GetWorld() == Candidate.Target->GetWorld()
            && Candidate.Source->GetActorTransform().Equals(Candidate.OriginalSource, 0.001)
            && Candidate.Target->GetActorTransform().Equals(Candidate.OriginalTarget, 0.001);
    }

    void FindCandidates(AActor* Source, AActor* PinnedTarget, const FOptions& Options, TArray<FCandidate>& Out, FText& Result, const FTransform* SourceOverride, FGeometryCache* Cache)
    {
        Out.Reset();
        if (!CanMove(Source, Result)) { return; }
        TArray<FEdge> SourceEdges;
        if (!(Cache ? Cache->Read(Source,SourceEdges,Result) : ExtractEdges(Source, SourceEdges, Result))) { return; }
        const FTransform SourceTransform = SourceOverride ? *SourceOverride : Source->GetActorTransform();
        FBox SearchBounds(ForceInit);
        for (FEdge& Edge : SourceEdges)
        {
            if (SourceOverride)
            {
                Edge.A = SourceTransform.TransformPosition(Source->GetActorTransform().InverseTransformPosition(Edge.A));
                Edge.B = SourceTransform.TransformPosition(Source->GetActorTransform().InverseTransformPosition(Edge.B));
            }
            SearchBounds += Edge.A; SearchBounds += Edge.B;
        }
        TArray<AActor*> Targets;
        if (PinnedTarget) { Targets.Add(PinnedTarget); }
        else
        {
            const FBox Bounds = SearchBounds.ExpandBy(Options.Distance);
            for (TActorIterator<AActor> It(Source->GetWorld()); It; ++It)
            {
                if (*It != Source && Cast<UStaticMeshComponent>(It->GetRootComponent()) && !It->IsHiddenEd() && Bounds.Intersect(It->GetComponentsBoundingBox(true))) { Targets.Add(*It); }
            }
            Targets.Sort([SourceTransform](const AActor& A, const AActor& B) {
                const double DA = FVector::DistSquared(A.GetActorLocation(), SourceTransform.GetLocation());
                const double DB = FVector::DistSquared(B.GetActorLocation(), SourceTransform.GetLocation());
                return DA == DB ? A.GetPathName() < B.GetPathName() : DA < DB;
            });
        }
        int32 Skipped = FMath::Max(0, Targets.Num()-16);
        if (Targets.Num() > 16) { Targets.SetNum(16); }
        int32 Comparisons = 0;
        bool bLimited = false;
        for (AActor* Target : Targets)
        {
            if (!IsValid(Target) || Target == Source || Target->GetWorld() != Source->GetWorld() || Target->IsHiddenEd()) { ++Skipped; continue; }
            TArray<FEdge> TargetEdges; FText Error;
            if (!(Cache ? Cache->Read(Target,TargetEdges,Error) : ExtractEdges(Target, TargetEdges, Error))) { ++Skipped; if (PinnedTarget) { Result = Error; return; } continue; }
            for (const FEdge& S : SourceEdges)
            {
                for (const FEdge& T : TargetEdges)
                {
                    if (++Comparisons > 2000000) { bLimited = true; break; }
                    FMatch Match;
                    if (!MatchEdges(S, T, SourceTransform, Options, Match, SourceOverride != nullptr)) { continue; }
                    if (Options.TargetPoint.IsSet()) { Match.Score=FVector::Distance(Match.TargetAnchor,Options.TargetPoint.GetValue())*1000+Match.Distance; }
                    FCandidate Candidate;
                    static_cast<FMatch&>(Candidate) = Match;
                    Candidate.Source = Source; Candidate.Target = Target;
                    Candidate.OriginalSource = SourceTransform; Candidate.OriginalTarget = Target->GetActorTransform();
                    if (Out.ContainsByPredicate([&](const FCandidate& Existing) {
                        return Existing.Target == Target && Existing.Transform.Equals(Candidate.Transform, .01);
                    })) { continue; }
                    Out.Add(Candidate);
                    Out.Sort([](const FCandidate& A, const FCandidate& B) { return A.Score < B.Score; });
                    if (Out.Num() > 12) { Out.SetNum(12); }
                }
                if (bLimited) { break; }
            }
            if (bLimited) { break; }
        }
        Result = Out.IsEmpty()
            ? Text(TEXT("후보 없음 · 거리·회전 허용 범위를 늘리거나 기준 액터를 지정하세요."), TEXT("No match. Increase distance / angle, or pin a target actor."))
            : Text(TEXT("미리보기만 표시 중 · 적용 전에는 액터가 이동하지 않습니다."), TEXT("Preview only. Actors stay unchanged until Apply."));
        if (Skipped || bLimited)
        { Result = FText::Format(Text(TEXT("{0}\n검색 제한: 제외 {1}개 · 정밀 작업은 기준 액터를 지정하세요."), TEXT("{0}\nSearch limited: {1} skipped. Pin a target for precise search.")), Result, Skipped); }
    }

    bool Apply(const FCandidate& Candidate, FText& Result)
    {
        AActor* Source = Candidate.Source.Get();
        if (Source != SelectedActor() || !IsSnapshotCurrent(Candidate))
        { Result = Text(TEXT("선택 또는 위치가 바뀌었습니다. 다시 미리보기 하세요."), TEXT("Selection or transforms changed. Preview again.")); return false; }
        if (!CanMove(Source, Result)) { return false; }
        if (Candidate.Target->IsHiddenEd()) { Result = Text(TEXT("기준 액터가 숨겨졌습니다."), TEXT("The target is now hidden.")); return false; }
        TArray<FEdge> SourceEdges, TargetEdges;
        if (!ExtractEdges(Source, SourceEdges, Result) || !ExtractEdges(Candidate.Target.Get(), TargetEdges, Result)) { return false; }
        const auto HasEdge = [](const TArray<FEdge>& Edges, const FEdge& Edge) {
            return Edges.ContainsByPredicate([&](const FEdge& E) { return (E.A.Equals(Edge.A,.01) && E.B.Equals(Edge.B,.01)) || (E.B.Equals(Edge.A,.01) && E.A.Equals(Edge.B,.01)); });
        };
        if (!HasEdge(SourceEdges, Candidate.SourceEdge) || !HasEdge(TargetEdges, Candidate.TargetEdge))
        { Result = Text(TEXT("메시가 변경됐습니다. 다시 미리보기 하세요."), TEXT("Mesh geometry changed. Preview again.")); return false; }
        FScopedTransaction Transaction(Text(TEXT("모서리 스냅"), TEXT("Edge snap")));
        Source->Modify(); Source->GetRootComponent()->Modify();
        if (!Source->SetActorTransform(Candidate.Transform, false, nullptr, ETeleportType::TeleportPhysics))
        {
            Transaction.Cancel();
            Result = Text(TEXT("액터를 이동할 수 없습니다."), TEXT("Could not move the actor."));
            return false;
        }
        Source->PostEditMove(true); Source->MarkPackageDirty();
        GEditor->RedrawLevelEditingViewports();
        Result = Text(TEXT("적용 완료 · Ctrl+Z로 되돌릴 수 있습니다."), TEXT("Applied. Use Ctrl+Z to undo."));
        return true;
    }

    void DrawPreview(const FCandidate& C)
    {
        if (!IsSnapshotCurrent(C) || C.Source.Get() != SelectedActor()) { return; }
        UWorld* World = C.Source->GetWorld();
        const FVector A = C.Transform.TransformPosition(C.OriginalSource.InverseTransformPosition(C.SourceEdge.A));
        const FVector B = C.Transform.TransformPosition(C.OriginalSource.InverseTransformPosition(C.SourceEdge.B));
        DrawDebugLine(World, C.SourceEdge.A, C.SourceEdge.B, FColor::Orange, false, .3f, 0, 3);
        DrawDebugLine(World, C.TargetEdge.A, C.TargetEdge.B, FColor::Cyan, false, .3f, 0, 5);
        DrawDebugLine(World, A, B, FColor::Green, false, .3f, 0, 2);
        DrawDebugLine(World, C.SourceAnchor, C.TargetAnchor, FColor::White, false, .3f, 0, 1);
        DrawDebugPoint(World, C.TargetAnchor, 12, FColor::Green, false, .3f);
        GEditor->RedrawLevelEditingViewports();
    }

    bool QuickAction(EQuickAction Action, FText& Result)
    {
        AActor* Actor = SelectedActor();
        if (!CanMove(Actor, Result)) { return false; }
        if (Action == EQuickAction::Focus)
        { GEditor->MoveViewportCamerasToActor(*Actor, false); return true; }
        if (Action==EQuickAction::Floor || Action==EQuickAction::Grid)
        { return MDHoldSnap::ApplyImmediate(Actor,Action==EQuickAction::Floor ? MDHoldSnap::EMode::Floor : MDHoldSnap::EMode::Grid,Result); }
        const auto Commands = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetGlobalLevelEditorActions();
        const FLevelEditorCommands& BuiltIn = FLevelEditorCommands::Get();
        const TSharedPtr<FUICommandInfo> Command = Action == EQuickAction::Floor ? BuiltIn.SnapToFloor :
            Action == EQuickAction::Slope ? BuiltIn.AlignToFloor : BuiltIn.SnapOriginToGridPerActor;
        if (!Commands->CanExecuteAction(Command.ToSharedRef()))
        { Result = Text(TEXT("현재 선택에서는 이 동작을 실행할 수 없습니다."), TEXT("This action is unavailable for the current selection.")); return false; }
        Commands->ExecuteAction(Command.ToSharedRef());
        Result = Text(TEXT("실행 완료 · 이동이 있었다면 Ctrl+Z로 되돌릴 수 있습니다."), TEXT("Executed. Any movement can be undone with Ctrl+Z."));
        return true;
    }
}
