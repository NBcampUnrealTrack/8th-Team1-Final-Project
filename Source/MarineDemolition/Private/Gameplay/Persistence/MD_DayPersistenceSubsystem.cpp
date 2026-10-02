#include "Gameplay/Persistence/MD_DayPersistenceSubsystem.h"
#include "Engine/World.h"
#include "Misc/Guid.h"

void UMD_DayPersistenceSubsystem::ResetContaminationContext()
{
    ContaminationContextWorld.Reset();
    bHasContaminationSessionSeed = false;
    ContaminationSessionSeed = 0;
}

bool UMD_DayPersistenceSubsystem::IsContaminationContextReady(const UWorld* World) const
{
    return World && ContaminationContextWorld.Get() == World;
}

bool UMD_DayPersistenceSubsystem::TryGetContaminationSessionSeed(const UWorld* World, int32& OutSeed) const
{
    if (!IsContaminationContextReady(World) || !bHasContaminationSessionSeed) return false;
    OutSeed = ContaminationSessionSeed;
    return true;
}

void UMD_DayPersistenceSubsystem::PrepareContaminationContext(UWorld* World)
{
    if (!World || World->GetNetMode() == NM_Client) return;
    // 같은 월드의 반복 호출은 기존 시드 유지
    if (IsContaminationContextReady(World)) return;
    ResetContaminationContext();
    ContaminationContextWorld = World;
    // 세이브가 없으므로 모든 월드 진입은 새 시드
    bHasContaminationSessionSeed = true;
    ContaminationSessionSeed = static_cast<int32>(GetTypeHash(FGuid::NewGuid()));
}

bool UMD_DayPersistenceSubsystem::TryGetSavedContaminationGeneration(const UMD_ScrapeSurfaceComponent* Surface, FMD_ContaminationGeneration& OutGeneration) const
{
    return false;
}

bool UMD_DayPersistenceSubsystem::TryGetSavedContaminationLayout(const UMD_ScrapeSurfaceComponent* Surface, FGuid& OutLayout) const
{
    return false;
}

bool UMD_DayPersistenceSubsystem::ShouldDeferContaminationGeneration(const UMD_ScrapeSurfaceComponent* Surface) const
{
    return false;
}

const FMD_WallContaminationRecord* UMD_DayPersistenceSubsystem::FindDeferredContaminationRecord(const UMD_ScrapeSurfaceComponent* Surface) const
{
    return nullptr;
}
