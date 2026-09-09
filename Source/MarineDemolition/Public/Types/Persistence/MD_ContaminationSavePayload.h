#pragma once
#include "CoreMinimal.h"
#include "MD_ContaminationSavePayload.generated.h"

struct FMD_ContaminationSaveStorage;

/** Immutable lossless chunks: snapshot copies share bytes, never mutable simulation state. */
USTRUCT()
struct MARINEDEMOLITION_API FMD_ContaminationSavePayload
{
    GENERATED_BODY()
    bool Encode(const TArray<int32>& Indices, const TArray<float>& Initial,
        const TArray<float>& Remaining, const TArray<float>& Transferred,
        const TArray<float>& Pattern, const TArray<float>& Covered);
    bool Decode(TArray<int32>& Indices, TArray<float>& Initial, TArray<float>& Remaining,
        TArray<float>& Transferred, TArray<float>& Pattern, TArray<float>& Covered) const;
    bool ValidateIndices(int32 MaximumCellCount) const;
    int32 Num() const;
    uint64 GetAllocatedBytes() const;
    bool IsSharedWith(const FMD_ContaminationSavePayload& Other) const;
    bool Serialize(FArchive& Ar);
private:
    TSharedPtr<const FMD_ContaminationSaveStorage, ESPMode::ThreadSafe> Storage;
};
template<> struct TStructOpsTypeTraits<FMD_ContaminationSavePayload>
    : TStructOpsTypeTraitsBase2<FMD_ContaminationSavePayload>
{
    enum { WithSerializer = true, WithCopy = true };
};
