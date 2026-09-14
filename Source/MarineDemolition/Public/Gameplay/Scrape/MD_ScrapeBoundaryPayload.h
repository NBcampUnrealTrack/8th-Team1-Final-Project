#pragma once
#include "CoreMinimal.h"
#include "MD_ScrapeBoundaryPayload.generated.h"

/** Immutable bounded bytes shared by day/slot snapshots. The length is checked
 * before allocation, including for malformed archives; old records default empty. */
USTRUCT()
struct MARINEDEMOLITION_API FMD_ScrapeBoundaryPayload
{
    GENERATED_BODY()
    static constexpr int32 MaximumBytes=2*1024*1024;
    bool Set(TArray<uint8>&& In)
    {
        if (In.Num()>MaximumBytes) return false;
        if (In.IsEmpty()) Data.Reset();
        else Data=MakeShared<const TArray<uint8>>(MoveTemp(In));
        return true;
    }
    TConstArrayView<uint8> Get() const
    { return Data ? MakeArrayView(*Data) : TConstArrayView<uint8>(); }
    int32 Num() const { return Data ? Data->Num() : 0; }
    bool Serialize(FArchive& Ar)
    {
        int32 Count=Num(); Ar << Count;
        if (Ar.IsError() || Count<0 || Count>MaximumBytes)
        { Ar.SetError(); return false; }
        if (Ar.IsLoading())
        {
            if (Ar.TotalSize()>=0 && (Ar.Tell()>Ar.TotalSize() || Count>Ar.TotalSize()-Ar.Tell()))
            { Ar.SetError(); return false; }
            TArray<uint8> Bytes; Bytes.SetNumUninitialized(Count);
            if (Count) Ar.Serialize(Bytes.GetData(),Count);
            if (Ar.IsError()) return false;
            return Set(MoveTemp(Bytes));
        }
        if (Count) Ar.Serialize(const_cast<uint8*>(Data->GetData()),Count);
        return !Ar.IsError();
    }
private:
    TSharedPtr<const TArray<uint8>> Data;
};
template<> struct TStructOpsTypeTraits<FMD_ScrapeBoundaryPayload> : TStructOpsTypeTraitsBase2<FMD_ScrapeBoundaryPayload>
{ enum { WithSerializer=true, WithCopy=true }; };
