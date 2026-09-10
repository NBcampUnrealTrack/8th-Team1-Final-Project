#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Components/PrimitiveComponent.h"
#include "MD_ScanTypes.generated.h"

class UPrimitiveComponent;

UENUM(BlueprintType)
enum class EMD_ScanMode : uint8
{
	InternalStructure UMETA(DisplayName = "내부 구조물"),
	WallType UMETA(DisplayName = "벽 종류")
};

namespace MDScanning
{
	constexpr int32 UtilityStencil = 251;
	constexpr int32 PartitionStencil = 252;
	constexpr int32 StrutStencil = 253;
	bool IsValidMode(EMD_ScanMode Mode);
	bool IsNewerRequest(uint32 Request, uint32 Previous);
	bool SphereIntersectsScanCone(const FVector& Origin, const FVector& Forward,
		const FVector& Center, double BoundRadius, double Range, double HalfAngleDegrees);
}

/** Restores only the render state still owned by this request. No strong mesh lifetime. */
struct MARINEDEMOLITION_API FMD_ScanMeshState
{
	TWeakObjectPtr<UPrimitiveComponent> Mesh;
	int32 OriginalStencil = 0;
	ERendererStencilMask OriginalWriteMask = ERendererStencilMask::ERSM_Default;
	int32 AppliedStencil = 0;
	bool bOriginalDepth = false;
	bool bApplied = false;

	void Apply(UPrimitiveComponent* Component, int32 Stencil);
	void Restore();
};

/** Mode belongs to this GAS activation, never to a separate Pawn RPC's delivery order. */
USTRUCT()
struct MARINEDEMOLITION_API FGameplayAbilityTargetData_ScanMode : public FGameplayAbilityTargetData
{
	GENERATED_BODY()

	UPROPERTY()
	EMD_ScanMode Mode = EMD_ScanMode::InternalStructure;

	UPROPERTY()
	uint32 RequestId = 0;

	virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template<> struct TStructOpsTypeTraits<FGameplayAbilityTargetData_ScanMode>
	: TStructOpsTypeTraitsBase2<FGameplayAbilityTargetData_ScanMode>
{
	enum { WithNetSerializer = true };
};
