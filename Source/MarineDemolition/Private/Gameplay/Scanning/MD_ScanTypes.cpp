#include "Gameplay/Scanning/MD_ScanTypes.h"
#include "Components/PrimitiveComponent.h"

bool MDScanning::IsValidMode(EMD_ScanMode Mode)
{
	return Mode == EMD_ScanMode::InternalStructure || Mode == EMD_ScanMode::Cleanup;
}

bool MDScanning::IsNewerRequest(uint32 Request, uint32 Previous)
{
	return Request != 0 && (Previous == 0 || static_cast<int32>(Request - Previous) > 0);
}

bool MDScanning::SphereIntersectsScanCone(const FVector& Origin, const FVector& Forward,
	const FVector& Center, double BoundRadius, double Range, double HalfAngleDegrees)
{
	if (Origin.ContainsNaN() || Forward.ContainsNaN() || Center.ContainsNaN()
		|| Forward.IsNearlyZero() || !FMath::IsFinite(BoundRadius) || BoundRadius < 0
		|| !FMath::IsFinite(Range) || Range <= 0 || !FMath::IsFinite(HalfAngleDegrees))
	{
		return false;
	}
	const FVector Delta = Center - Origin;
	const double Distance = Delta.Size();
	if (Distance - BoundRadius > Range) return false;
	if (Distance <= BoundRadius + UE_DOUBLE_SMALL_NUMBER) return true;
	// Conservative candidate test; the GPU clips actual pixels to the exact cone/range.
	const double AngularRadius = FMath::Asin(FMath::Clamp(BoundRadius / Distance, 0.0, 1.0));
	const double Limit = FMath::Min(UE_PI,
		FMath::DegreesToRadians(FMath::Clamp(HalfAngleDegrees, 0.0, 180.0)) + AngularRadius);
	return FVector::DotProduct(Forward.GetSafeNormal(), Delta / Distance)
		+ UE_DOUBLE_KINDA_SMALL_NUMBER >= FMath::Cos(Limit);
}

void FMD_ScanMeshState::Apply(UPrimitiveComponent* Component, int32 Stencil)
{
	if (!IsValid(Component)) return;
	if (!bApplied || Mesh.Get() != Component)
	{
		Restore();
		Mesh = Component;
		OriginalStencil = Component->CustomDepthStencilValue;
		OriginalWriteMask = Component->CustomDepthStencilWriteMask;
		bOriginalDepth = Component->bRenderCustomDepth;
	}
	AppliedStencil = Stencil;
	bApplied = true;
	Component->SetCustomDepthStencilWriteMask(ERendererStencilMask::ERSM_Default);
	Component->SetCustomDepthStencilValue(Stencil);
	Component->SetRenderCustomDepth(true);
}

void FMD_ScanMeshState::Restore()
{
	if (UPrimitiveComponent* Component = Mesh.Get(); bApplied && Component
		&& Component->bRenderCustomDepth
		&& Component->CustomDepthStencilValue == AppliedStencil
		&& Component->CustomDepthStencilWriteMask == ERendererStencilMask::ERSM_Default)
	{
		Component->SetCustomDepthStencilValue(OriginalStencil);
		Component->SetCustomDepthStencilWriteMask(OriginalWriteMask);
		Component->SetRenderCustomDepth(bOriginalDepth);
	}
	Mesh.Reset();
	bApplied = false;
}

bool FGameplayAbilityTargetData_ScanMode::NetSerialize(FArchive& Ar, UPackageMap*, bool& bOutSuccess)
{
	uint8 WireMode = static_cast<uint8>(Mode);
	Ar << WireMode;
	Ar.SerializeIntPacked(RequestId);
	if (Ar.IsLoading()) Mode = static_cast<EMD_ScanMode>(WireMode);
	bOutSuccess = !Ar.IsError() && MDScanning::IsValidMode(Mode) && RequestId != 0;
	return true;
}
