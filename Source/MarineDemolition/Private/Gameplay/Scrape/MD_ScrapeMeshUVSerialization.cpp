#include "Gameplay/Scrape/MD_ScrapeMeshUVData.h"

#include "Serialization/CustomVersion.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

const FGuid UMD_ScrapeMeshUVData::CellSerializationVersionGuid(0x5AB38641, 0xAA834C67, 0xB7E165D9, 0x6CED20B3);

namespace
{
    TAutoConsoleVariable<int32> BatchCellReads(TEXT("MD.Travel.BatchCellReads"), 1,
        TEXT("Read a version-one cell payload in one archive operation without changing its disk format."));

    FCustomVersionRegistration RegisterCellPayloadVersion(
        UMD_ScrapeMeshUVData::CellSerializationVersionGuid,
        UMD_ScrapeMeshUVData::CellSerializationVersion, TEXT("MDScrapeMeshUVCell"));
}

void UMD_ScrapeMeshUVData::Serialize(FArchive& Ar)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_ScrapeMeshUVData_Serialize);
    // Register before Super so package version gathering also sees the format.
    // UsingCustomVersion never upgrades a loading archive: old packages stay tagged.
    Ar.UsingCustomVersion(CellSerializationVersionGuid);
    Super::Serialize(Ar);
}

bool FMD_ScrapeMeshUVCell::Serialize(FArchive& Ar)
{
    // Editor transactions, object duplication and reference collection need their
    // normal reflection path. Only persistent binary payloads use this format.
    if (!Ar.IsPersistent() || Ar.IsTransacting() || Ar.IsObjectReferenceCollector()
        || Ar.IsTextFormat() || (!Ar.IsLoading() && !Ar.IsSaving())) return false;

    if (Ar.IsSaving()) Ar.UsingCustomVersion(UMD_ScrapeMeshUVData::CellSerializationVersionGuid);
    const int32 Version = Ar.CustomVer(UMD_ScrapeMeshUVData::CellSerializationVersionGuid);
    if (Version < UMD_ScrapeMeshUVData::CellSerializationVersion) return false;
    if (Version > UMD_ScrapeMeshUVData::CellSerializationVersion)
    {
        Ar.SetError();
        return true;
    }

    // The v1 wire layout is fixed, but differs from the current C++ member order.
    // Decode explicit byte offsets instead of dumping a padded/native struct.
    // Keep older float-vector archives and byte-swapped archives on the original path.
    if (Ar.IsLoading() && !Ar.IsByteSwapping()
        && Ar.UEVer() >= EUnrealEngineObjectUE5Version::LARGE_WORLD_COORDINATES
        && BatchCellReads.GetValueOnAnyThread() != 0)
    {
        static_assert(sizeof(FVector) == 24 && sizeof(double) == 8 && sizeof(int32) == 4);
        uint8 Payload[72];
        Ar.Serialize(Payload, sizeof(Payload));
        if (Ar.IsError()) return true;
        FMemory::Memcpy(&TriangleIndex, Payload, 4);
        FMemory::Memcpy(&PositionLocal, Payload + 4, 24);
        FMemory::Memcpy(&Barycentric, Payload + 28, 24);
        FMemory::Memcpy(&AreaCm2, Payload + 52, 8);
        FMemory::Memcpy(&SupportRadiusCm, Payload + 60, 8);
        FMemory::Memcpy(&VisualSourceCellIndex, Payload + 68, 4);
        PositionLocal.DiagnosticCheckNaN();
        Barycentric.DiagnosticCheckNaN();
        return true;
    }

    // Archive operators preserve double precision and platform byte swapping;
    // never dump the C++ struct, whose padding and layout are not a disk format.
    Ar << TriangleIndex;
    Ar << PositionLocal;
    Ar << Barycentric;
    Ar << AreaCm2;
    Ar << SupportRadiusCm;
    Ar << VisualSourceCellIndex;
    return true;
}
