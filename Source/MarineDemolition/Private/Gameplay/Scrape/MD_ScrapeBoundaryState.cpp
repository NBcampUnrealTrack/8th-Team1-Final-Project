#include "Gameplay/Scrape/MD_ScrapeBoundaryState.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

using namespace UE::Geometry;
namespace
{
FGeneralPolygon2d MakeBox(const FVector2d Min, const FVector2d Max)
{
    FPolygon2d P;
    P.AppendVertex(Min);
    P.AppendVertex(FVector2d(Max.X, Min.Y));
    P.AppendVertex(Max);
    P.AppendVertex(FVector2d(Min.X, Max.Y));
    return FGeneralPolygon2d(P);
}

struct FBoundaryEdge
{
    int64 AX,AY,BX,BY;
    bool operator==(const FBoundaryEdge& E) const { return AX==E.AX && AY==E.AY && BX==E.BX && BY==E.BY; }
    bool operator<(const FBoundaryEdge& E) const
    { if (AX!=E.AX) return AX<E.AX; if (AY!=E.AY) return AY<E.AY; if (BX!=E.BX) return BX<E.BX; return BY<E.BY; }
};
TArray<FBoundaryEdge> Edges(const FMD_ScrapeBoundaryPatch& Patch)
{
    TArray<FBoundaryEdge> Result;
    auto Add=[&](const FPolygon2d& Ring)
    {
        for (int32 I=0;I<Ring.VertexCount();++I)
        {
            const auto A=Ring[I],B=Ring[(I+1)%Ring.VertexCount()];
            Result.Add({FMath::RoundToInt(A.X*4096),FMath::RoundToInt(A.Y*4096),
                FMath::RoundToInt(B.X*4096),FMath::RoundToInt(B.Y*4096)});
        }
    };
    for (const auto& P : Patch.Polygons) { Add(P.GetOuter()); for (const auto& H : P.GetHoles()) Add(H); }
    Result.Sort(); return Result;
}
double Cross(const FVector2d A,const FVector2d B,const FVector2d C)
{ return (B.X-A.X)*(C.Y-A.Y)-(B.Y-A.Y)*(C.X-A.X); }
bool SegmentsTouch(const FVector2d A,const FVector2d B,const FVector2d C,const FVector2d D)
{
    if (FMath::Max(A.X,B.X)<FMath::Min(C.X,D.X) || FMath::Max(C.X,D.X)<FMath::Min(A.X,B.X)
        || FMath::Max(A.Y,B.Y)<FMath::Min(C.Y,D.Y) || FMath::Max(C.Y,D.Y)<FMath::Min(A.Y,B.Y)) return false;
    return Cross(A,B,C)*Cross(A,B,D)<=0 && Cross(C,D,A)*Cross(C,D,B)<=0;
}
bool ValidRing(const FPolygon2d& R)
{
    const int32 N=R.VertexCount();
    if (N<3 || FMath::Abs(R.SignedArea())<0.5/(4096.0*4096.0)) return false;
    for (int32 I=0;I<N;++I)
    {
        const int32 J=(I+1)%N;
        if (R[I]==R[J]) return false;
        for (int32 K=I+1;K<N;++K)
        {
            const int32 L=(K+1)%N;
            if (J==K || L==I) continue;
            if (SegmentsTouch(R[I],R[J],R[K],R[L])) return false;
        }
    }
    return true;
}
bool RingsTouch(const FPolygon2d& A,const FPolygon2d& B)
{
    for (int32 I=0;I<A.VertexCount();++I) for (int32 J=0;J<B.VertexCount();++J)
        if (SegmentsTouch(A[I],A[(I+1)%A.VertexCount()],B[J],B[(J+1)%B.VertexCount()])) return true;
    return false;
}

void QuantizeRing(FPolygon2d& Ring)
{
    for (int32 I = 0; I < Ring.VertexCount(); ++I)
    {
        const FVector2d P = Ring[I];
        Ring[I] = FVector2d(FMath::RoundToDouble(P.X * 4096.0) / 4096.0,
            FMath::RoundToDouble(P.Y * 4096.0) / 4096.0);
    }
    // Clipping before quantization can leave zero-length or collinear edges.
    // Normalize them before equality, upload and persistence use the contours.
    Ring.Simplify(1.e-8,1.e-8);
}
}
int32 FMD_ScrapeBoundaryPatch::VertexCount() const
{
    int32 N = 0;
    for (const auto& P : Polygons)
    {
        N += P.GetOuter().VertexCount();
        for (const auto& H : P.GetHoles()) N += H.VertexCount();
    }
    return N;
}
uint64 FMD_ScrapeBoundaryPatch::GetAllocatedBytes() const
{
    uint64 Bytes=sizeof(*this)+Polygons.GetAllocatedSize()+32;
    for (const auto& P : Polygons) Bytes+=P.GetAllocatedSize();
    return Bytes;
}
void FMD_ScrapeBoundaryState::Reset(FGuid Layout, FIntPoint Size)
{
    Patches.Empty(); DirtyTiles.Empty(); LayoutId = Layout; Dimensions = Size;
    Generation = 0; bDisabled = false;
}
const FMD_ScrapeBoundaryPatch* FMD_ScrapeBoundaryState::Find(int32 Tile, int32 Triangle) const
{
    const auto* P = Patches.Find(Key(Tile, Triangle));
    return P ? P->Get() : nullptr;
}
uint64 FMD_ScrapeBoundaryState::GetAllocatedBytes() const
{
    uint64 N = sizeof(*this) + Patches.GetAllocatedSize() + DirtyTiles.GetAllocatedSize();
    for (const auto& P : Patches) N += P.Value->GetAllocatedBytes();
    return N;
}
void FMD_ScrapeBoundaryState::Apply(TConstArrayView<FMD_ScrapeBoundaryFootprint> Footprints)
{
    if (bDisabled || Dimensions.X <= 0 || Dimensions.Y <= 0) return;
    const int32 Columns = TileColumns();
    const int32 Rows = FMath::DivideAndRoundUp(Dimensions.Y, TileCells);
    TMap<uint64, TArray<FGeneralPolygon2d>> Additions;
    int32 WorkingVertices=0;
    if (Footprints.Num()>512) { bDisabled=true; ++Generation; Patches.Empty(); return; }
    for (const auto& Footprint : Footprints)
    {
        if (Footprint.TriangleIndex < 0 || Footprint.Points.Num() < 3 || Footprint.Points.Num()>192) continue;
        FPolygon2d Ring;
        FVector2d Min(DBL_MAX, DBL_MAX), Max(-DBL_MAX, -DBL_MAX);
        for (const auto& P : Footprint.Points)
        {
            if (!FMath::IsFinite(P.X) || !FMath::IsFinite(P.Y) || FMath::Abs(P.X)>1.e6 || FMath::Abs(P.Y)>1.e6) { Ring = FPolygon2d(); break; }
            Ring.AppendVertex(P); Min = FVector2d(FMath::Min(Min.X,P.X),FMath::Min(Min.Y,P.Y));
            Max = FVector2d(FMath::Max(Max.X,P.X),FMath::Max(Max.Y,P.Y));
        }
        if (Ring.VertexCount() < 3) continue;
        if (Ring.IsClockwise()) Ring.Reverse();
        const FGeneralPolygon2d Shape(Ring);
        const int32 X0 = FMath::Clamp(FMath::FloorToInt(FMath::Clamp((Min.X-1)/TileCells,0.0,double(Columns-1))),0,Columns-1);
        const int32 X1 = FMath::Clamp(FMath::FloorToInt(FMath::Clamp((Max.X+1)/TileCells,0.0,double(Columns-1))),0,Columns-1);
        const int32 Y0 = FMath::Clamp(FMath::FloorToInt(FMath::Clamp((Min.Y-1)/TileCells,0.0,double(Rows-1))),0,Rows-1);
        const int32 Y1 = FMath::Clamp(FMath::FloorToInt(FMath::Clamp((Max.Y+1)/TileCells,0.0,double(Rows-1))),0,Rows-1);
        // A pathological blade cannot allocate atlas-sized scratch.
        if (Footprint.Tiles.IsEmpty() && int64(X1-X0+1)*(Y1-Y0+1) > MaximumPatches)
        { bDisabled = true; ++Generation; Patches.Empty(); return; }
        TArray<int32> Tiles=Footprint.Tiles;
        if (Tiles.IsEmpty())
            for (int32 Y=Y0;Y<=Y1;++Y) for (int32 X=X0;X<=X1;++X) Tiles.Add(X+Y*Columns);
        if (Tiles.Num()>MaximumPatches) { bDisabled=true; ++Generation; Patches.Empty(); return; }
        for (const int32 Tile : Tiles)
        {
            if (Tile<0 || Tile>=Columns*Rows) continue;
            const int32 X=Tile%Columns,Y=Tile/Columns;
            const FGeneralPolygon2d Box = MakeBox(FVector2d(X*TileCells-1,Y*TileCells-1),
                FVector2d((X+1)*TileCells+1,(Y+1)*TileCells+1));
            TArray<FGeneralPolygon2d> Clipped;
            if (!PolygonsIntersection(MakeArrayView(&Shape,1),MakeArrayView(&Box,1),Clipped)) continue;
            for (const auto& P : Clipped)
            {
                WorkingVertices+=P.GetOuter().VertexCount();
                for (const auto& H : P.GetHoles()) WorkingVertices+=H.VertexCount();
            }
            if (WorkingVertices>65536) { bDisabled=true; ++Generation; Patches.Empty(); return; }
            if (!Clipped.IsEmpty()) Additions.FindOrAdd(Key(Tile,Footprint.TriangleIndex)).Append(MoveTemp(Clipped));
            if (Additions.Num() > MaximumPatches)
            { bDisabled = true; ++Generation; Patches.Empty(); return; }
        }
    }
    for (auto& Addition : Additions)
    {
        const int32 Tile = int32(Addition.Key >> 32);
        const auto* Previous = Patches.Find(Addition.Key);
        if (Previous && (*Previous)->bFallback) continue;
        if (!Previous && Patches.Num() >= MaximumPatches)
        { bDisabled = true; ++Generation; Patches.Empty(); return; }
        if (Previous) Addition.Value.Append((*Previous)->Polygons);
        auto Next = MakeShared<FMD_ScrapeBoundaryPatch>();
        if (!PolygonsUnion(Addition.Value, Next->Polygons, false)
            || Next->VertexCount() > MaximumPatchVertices)
        {
            Next->Polygons.Empty(); Next->bFallback = true;
        }
        // Coordinates have a fixed, sub-millimetre canonical precision. The same
        // quantized current contours are used by local rendering, saves and peers.
        for (auto& P : Next->Polygons)
        {
            FPolygon2d Outer=P.GetOuter(); QuantizeRing(Outer); P.SetOuter(Outer);
            TArray<FPolygon2d> Holes=P.GetHoles(); P.ClearHoles();
            for (auto& H : Holes) { QuantizeRing(H); P.AddHole(H,false,false); }
        }
        if (Previous && (*Previous)->bFallback==Next->bFallback && Edges(**Previous)==Edges(*Next)) continue;
        DirtyTiles.Add(Tile); ++Generation;
        Patches.Add(Addition.Key, MoveTemp(Next));
    }
    if (GetAllocatedBytes() > MaximumPayloadBytes)
    { bDisabled = true; ++Generation; Patches.Empty(); }
}
bool FMD_ScrapeBoundaryState::Serialize(TArray<uint8>& Out) const
{
    Out.Reset();
    FMemoryWriter Ar(Out);
    int32 V=Version; FGuid L=LayoutId; FIntPoint D=Dimensions;
    uint8 Disabled=bDisabled?1:0;
    Ar << V << L << D << Disabled;
    TArray<uint64> Keys; Patches.GetKeys(Keys); Keys.Sort();
    int32 Count=Keys.Num(); Ar << Count;
    for (uint64 K : Keys)
    {
        Ar << K;
        const auto& Patch = *Patches[K];
        uint8 F=Patch.bFallback?1:0; Ar << F;
        int32 N=Patch.Polygons.Num(); Ar << N;
        auto WriteRing=[&](const FPolygon2d& Ring)
        {
            int32 R=Ring.VertexCount(); Ar << R;
            for (const auto& P : Ring.GetVertices())
            {
                int32 X=FMath::RoundToInt(P.X*4096.0),Y=FMath::RoundToInt(P.Y*4096.0);
                Ar << X << Y;
            }
        };
        for (const auto& P : Patch.Polygons)
        {
            WriteRing(P.GetOuter());
            int32 H=P.GetHoles().Num(); Ar << H;
            for (const auto& Hole : P.GetHoles()) WriteRing(Hole);
        }
        if (Out.Num() > MaximumPayloadBytes) { Out.Reset(); return false; }
    }
    return !Ar.IsError();
}
bool FMD_ScrapeBoundaryState::Deserialize(TConstArrayView<uint8> Bytes, FGuid ExpectedLayout, FIntPoint ExpectedSize)
{
    if (Bytes.IsEmpty()) { Reset(ExpectedLayout,ExpectedSize); return true; }
    if (Bytes.Num() > MaximumPayloadBytes || ExpectedSize.X<=0 || ExpectedSize.Y<=0 || ExpectedSize.X>65536 || ExpectedSize.Y>65536) return false;
    TArray<uint8> Copy(Bytes);
    FMemoryReader Ar(Copy);
    int32 V=0; FGuid L; FIntPoint D; uint8 Disabled=0; int32 Count=0;
    Ar << V << L << D << Disabled << Count;
    if (Ar.IsError() || V!=Version || L!=ExpectedLayout || D!=ExpectedSize || Disabled>1
        || Count<0 || Count>MaximumPatches || (Disabled && Count!=0)) return false;
    FMD_ScrapeBoundaryState Next; Next.Reset(L,D); Next.bDisabled=Disabled!=0;
    const int32 TileCount=Next.TileColumns()*FMath::DivideAndRoundUp(D.Y,TileCells);
    for (int32 I=0;I<Count;++I)
    {
        uint64 K=0; uint8 F=0; int32 N=0; Ar << K << F << N;
        const int32 Tile=int32(K>>32), Triangle=int32(uint32(K));
        if (Ar.IsError() || F>1 || Tile<0 || Tile>=TileCount || Triangle<0
            || N<0 || N>MaximumPatchVertices/3 || (F && N!=0) || (!F && N==0) || Next.Patches.Contains(K)) return false;
        auto P=MakeShared<FMD_ScrapeBoundaryPatch>(); P->bFallback=F!=0;
        int32 Vertices=0;
        auto ReadRing=[&](FPolygon2d& Ring)->bool
        {
            int32 R=0; Ar << R;
            if (Ar.IsError() || R<3 || R>MaximumPatchVertices-Vertices || int64(R)*8>Ar.TotalSize()-Ar.Tell()) return false;
            Vertices+=R;
            for (int32 J=0;J<R;++J)
            {
                int32 X=0,Y=0; Ar << X << Y;
                const FVector2d Point(double(X)/4096.0,double(Y)/4096.0);
                const FVector2d Min((Tile%Next.TileColumns())*TileCells-1,(Tile/Next.TileColumns())*TileCells-1);
                if (Point.X<Min.X || Point.Y<Min.Y || Point.X>Min.X+TileCells+2 || Point.Y>Min.Y+TileCells+2) return false;
                Ring.AppendVertex(Point);
            }
            return !Ar.IsError() && ValidRing(Ring);
        };
        for (int32 J=0;J<N;++J)
        {
            FPolygon2d Outer; if (!ReadRing(Outer) || Outer.IsClockwise()) return false;
            FGeneralPolygon2d Poly(Outer);
            int32 H=0; Ar << H;
            if (Ar.IsError() || H<0 || H>MaximumPatchVertices/3) return false;
            for (int32 Q=0;Q<H;++Q)
            {
                FPolygon2d Hole;
                if (!ReadRing(Hole) || !Hole.IsClockwise() || !Outer.Contains(Hole[0]) || RingsTouch(Outer,Hole)) return false;
                for (const auto& Other : Poly.GetHoles())
                    if (RingsTouch(Other,Hole) || Other.Contains(Hole[0]) || Hole.Contains(Other[0])) return false;
                Poly.AddHole(Hole,false,false);
            }
            P->Polygons.Add(MoveTemp(Poly));
        }
        Next.Patches.Add(K,MoveTemp(P)); Next.DirtyTiles.Add(Tile);
        if (Next.GetAllocatedBytes()>MaximumPayloadBytes) return false;
    }
    if (Ar.IsError() || Ar.Tell()!=Ar.TotalSize()) return false;
    *this=MoveTemp(Next); return true;
}
