#pragma once

class UStaticMesh;
class UStaticMeshComponent;
class UMD_SurfaceStainDefinition;

/** Native active-stage RT residency for eligible actor-owned fitted stains.
 * Call ForgetComponent after unregister/destruction, and Shutdown
 * before the owning module unloads or the RHI shuts down. Game-thread API only.
 * Same-component/same-stage calls preserve the current render state. Unsupported
 * geometry and shared definitions retain the baseline. See cpp lifecycle limits.
 */
namespace MD_StainRayTracingResidency
{
    void SetStage(UStaticMeshComponent& Component, UMD_SurfaceStainDefinition* Definition, UStaticMesh* ActiveMesh);
    void ForgetComponent(UStaticMeshComponent& Component);
    /** Restore every managed RT resource without changing the displayed mesh. */
    void RestoreAll();
    void Shutdown();
}
