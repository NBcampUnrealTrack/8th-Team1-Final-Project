#include "World/Contamination/MD_SurfaceStain.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Gameplay/Contamination/MD_StainStickerBaker.h"
#include "Components/DecalComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Gameplay/Contamination/MD_SurfaceStainDefinition.h"
#include "Gameplay/Contamination/MD_SurfaceStainSize.h"
#include "Gameplay/Contamination/MD_StainRayTracingResidency.h"
#include "Gameplay/Scrape/MD_ContaminationDebrisSubsystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "World/Contamination/MD_ContaminationItem.h"
#if WITH_EDITOR
#include "Gameplay/Contamination/MD_StainSurfaceFit.h"
#endif

AMD_SurfaceStain::AMD_SurfaceStain()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(2.0f);
	SurfaceQuery = CreateDefaultSubobject<UBoxComponent>(TEXT("SurfaceQuery"));
	SetRootComponent(SurfaceQuery);
	// Furniture may move/rotate, but its non-uniform scale must not stretch the decal.
	SurfaceQuery->SetAbsolute(false, false, true);
	// A definition and its final extent must be ready before creating query physics.
	SurfaceQuery->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SurfaceQuery->SetCollisionObjectType(ECC_WorldDynamic);
	SurfaceQuery->SetCollisionResponseToAllChannels(ECR_Ignore);
	SurfaceQuery->SetCollisionResponseToChannel(ECC_Visibility, ECR_Overlap);
	SurfaceQuery->SetGenerateOverlapEvents(false);
	SurfaceQuery->SetCanEverAffectNavigation(false);
	SurfaceQuery->SetHiddenInGame(true);
	Decal = CreateDefaultSubobject<UDecalComponent>(TEXT("StainDecal"));
	Decal->SetupAttachment(SurfaceQuery);
	// RefreshSize enables the legacy decal only when its definition is usable.
	Decal->SetVisibility(false);
	Decal->SetRelativeRotation(FRotator(-90.0, 0.0, 0.0));
	Decal->SetFadeScreenSize(0.002f);
	StickerMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StickerMesh"));
	StickerMesh->SetupAttachment(SurfaceQuery);
	StickerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	StickerMesh->SetGenerateOverlapEvents(false);
	StickerMesh->SetReceivesDecals(false);
	StickerMesh->SetCastShadow(false);
	StickerMesh->SetCanEverAffectNavigation(false);
	StickerMesh->SetVisibility(false);
}

int32 AMD_SurfaceStain::GetInitialSize() const
{
	return FMD_SurfaceStainSize::ClampInitial(InitialSize == 0
		? FMD_SurfaceStainSize::FromLegacyStage(InitialStage) : InitialSize);
}

void AMD_SurfaceStain::PostLoad()
{
	Super::PostLoad();
	InitialSize = GetInitialSize();
}

void AMD_SurfaceStain::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	if (GetWorld() && !GetWorld()->IsGameWorld()) RemainingSize = GetInitialSize();
	RefreshSize();
}

void AMD_SurfaceStain::PostUnregisterAllComponents()
{
	if (StickerMesh) MD_StainRayTracingResidency::ForgetComponent(*StickerMesh);
	Super::PostUnregisterAllComponents();
}

void AMD_SurfaceStain::ConfigurePlacement(UMD_SurfaceStainDefinition* InDefinition, int32 InSize,
	UMD_SurfaceStainDefinition* InSourceDefinition)
{
	if (GetWorld() && GetWorld()->IsGameWorld()) return;
	FittedDefinition = InDefinition && InDefinition->GetOuter() == this ? InDefinition : nullptr;
	Definition = FittedDefinition ? nullptr : InDefinition;
#if WITH_EDITORONLY_DATA
	RefitSource = InSourceDefinition ? InSourceDefinition : Definition.Get();
#endif
	InitialSize = FMD_SurfaceStainSize::ClampPlacement(InSize);
	RemainingSize = InitialSize;
	RefreshSize();
}

void AMD_SurfaceStain::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (!GetWorld() || !GetWorld()->IsGameWorld())
	{
		InitialSize = GetInitialSize();
		RemainingSize = InitialSize;
	}
	RefreshSize();
}

bool AMD_SurfaceStain::UsesSharedVolume() const
{
	const UMD_SurfaceStainDefinition* ActiveDefinition = GetDefinition();
	return IsValid(ActiveDefinition) && ActiveDefinition->UsesSharedVolume();
}

int32 AMD_SurfaceStain::GetVolumeStageIndex() const
{
	const UMD_SurfaceStainDefinition* ActiveDefinition = GetDefinition();
	return IsValid(ActiveDefinition) ? ActiveDefinition->GetVolumeStageIndex(RemainingSize, GetInitialSize()) : INDEX_NONE;
}

bool AMD_SurfaceStain::HasSticker() const
{
	return !UsesSharedVolume() && StickerPatch.IsValid() && StickerMesh && StickerMesh->GetStaticMesh();
}

bool AMD_SurfaceStain::RebuildSticker()
{
#if WITH_EDITOR
	const UMD_SurfaceStainDefinition* ActiveDefinition = GetDefinition();
	if (UsesSharedVolume())
	{
		if (!GetWorld() || GetWorld()->IsGameWorld()) return false;
		RemainingSize = GetInitialSize();
		RefreshSize();
		StickerStatus = ActiveDefinition->VolumeValidationStatus;
		return ActiveDefinition->IsUsable();
	}
	if (!GetWorld() || GetWorld()->IsGameWorld() || !ActiveDefinition || !ActiveDefinition->StickerMaterial)
	{ StickerStatus = TEXT("Assign a definition with a sticker material before baking."); return false; }
	FMD_StainStickerPatch Patch;
	UStaticMesh* Mesh = nullptr;
	FString Error;
	if (!FMD_StainStickerBaker::Build(*this, ActiveDefinition->GetHalfSize(GetInitialSize()), GetInitialSize(),
		ActiveDefinition->StickerMaterial, Patch, Mesh, Error))
	{ StickerStatus = Error; return false; }
	return ApplyPreparedSticker(MoveTemp(Patch), Mesh);
#else
	return false;
#endif
}

#if WITH_EDITOR
TSoftObjectPtr<UMD_SurfaceStainDefinition> AMD_SurfaceStain::GetRefitSource() const
{
	return RefitSource.IsNull() ? TSoftObjectPtr<UMD_SurfaceStainDefinition>(Definition.Get()) : RefitSource;
}

bool AMD_SurfaceStain::ApplyPreparedSticker(FMD_StainStickerPatch Patch, UStaticMesh* Mesh)
{
	if (!GetWorld() || GetWorld()->IsGameWorld() || UsesSharedVolume() || !Mesh || !Patch.IsValid() || Patch.InitialSize != GetInitialSize()) return false;
	// A successful preview becomes the placed actor's serialized mesh, without rebaking it.
	if (Mesh->GetOuter() != this && !Mesh->Rename(nullptr, this, REN_DontCreateRedirectors | REN_NonTransactional)) return false;
	Modify(); StickerMesh->Modify();
	StickerPatch = MoveTemp(Patch);
	LegacyStickerMesh = Mesh;
	StickerMesh->SetStaticMesh(Mesh);
	StickerStatus = FString::Printf(TEXT("Surface sticker: %d triangles"), GetStickerTriangleCount());
	RemainingSize = GetInitialSize();
	RefreshSize();
	MarkPackageDirty();
	return true;
}

void AMD_SurfaceStain::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);
	if (bFinished) RebuildSticker();
}

void AMD_SurfaceStain::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);
	const FName Name = Event.GetMemberPropertyName();
	if (Name == GET_MEMBER_NAME_CHECKED(AMD_SurfaceStain, Definition))
	{
		FittedDefinition = nullptr;
		RefitSource = Definition.Get();
	}
	if (Name == GET_MEMBER_NAME_CHECKED(AMD_SurfaceStain, InitialSize))
		InitialSize = FMD_SurfaceStainSize::ClampPlacement(InitialSize);
	if (Name == GET_MEMBER_NAME_CHECKED(AMD_SurfaceStain, InitialSize)
		|| Name == GET_MEMBER_NAME_CHECKED(AMD_SurfaceStain, Definition)) RebuildSticker();
}

void AMD_SurfaceStain::PostEditUndo()
{
	Super::PostEditUndo();
	// Undoing creation or redoing deletion can clear default subobjects before this callback.
	if (!IsValid(this) || !IsValid(SurfaceQuery) || !IsValid(StickerMesh) || !IsValid(Decal)) return;
	RemainingSize = GetInitialSize();
	RefreshSize();
}

void AMD_SurfaceStain::PostEditImport()
{
	Super::PostEditImport();
	if (!FittedDefinition) return;
	// T3D copy/paste cannot carry UStaticMesh bulk data. Reuse the fitted coordinates;
	// no surface sampling or fitting is needed, including when the source map is closed.
	FString Error;
	UMD_SurfaceStainDefinition* Rebuilt = FMD_StainSurfaceFit::BuildFinal(*this, *FittedDefinition, Error);
	FittedDefinition = Rebuilt;
	RefreshSize();
	if (!Rebuilt) StickerStatus = Error;
}
#endif

void AMD_SurfaceStain::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority()) RemainingSize = GetInitialSize();
	RefreshSize();
}

void AMD_SurfaceStain::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMD_SurfaceStain, Definition);
	DOREPLIFETIME(AMD_SurfaceStain, InitialSize);
	DOREPLIFETIME(AMD_SurfaceStain, RemainingSize);
}

void AMD_SurfaceStain::RefreshMaterial()
{
	const UMD_SurfaceStainDefinition* ActiveDefinition = GetDefinition();
	for (auto It = ScanSources.CreateIterator(); It; ++It)
		if (!It->IsValid()) It.RemoveCurrent();
	if (UsesSharedVolume())
	{
		StickerMesh->SetMaterial(0, ActiveDefinition->VolumeMaterial);
		StickerMesh->SetCustomPrimitiveDataFloat(0, 1.f);
		StickerMesh->SetCustomPrimitiveDataFloat(1, ScanSources.IsEmpty() ? 0.f : 1.f);
		ScanMaterial = nullptr;
		return;
	}
	if (HasSticker() && IsValid(ActiveDefinition))
	{
		StickerMesh->SetMaterial(0, ActiveDefinition->StickerMaterial);
		StickerMesh->SetCustomPrimitiveDataFloat(1, ScanSources.IsEmpty() ? 0.f : 1.f);
		ScanMaterial = nullptr;
		return;
	}
	if (ScanSources.IsEmpty() || !IsValid(ActiveDefinition))
	{
		Decal->SetDecalMaterial(IsValid(ActiveDefinition) ? ActiveDefinition->DecalMaterial : nullptr);
		ScanMaterial = nullptr;
		return;
	}
	if (!ScanMaterial || ScanMaterial->Parent != ActiveDefinition->DecalMaterial)
		ScanMaterial = UMaterialInstanceDynamic::Create(ActiveDefinition->DecalMaterial, this);
	ScanMaterial->SetScalarParameterValue(TEXT("ScanHighlight"), 1.0f);
	Decal->SetDecalMaterial(ScanMaterial);
}

void AMD_SurfaceStain::SetScanHighlighted(UObject* Source, bool bHighlighted)
{
	if (!IsValid(Source) || GetNetMode() == NM_DedicatedServer) return;
	if (bHighlighted) ScanSources.Add(Source);
	else ScanSources.Remove(Source);
	RefreshMaterial();
}

void AMD_SurfaceStain::RefreshSize()
{
	const UMD_SurfaceStainDefinition* ActiveDefinition = GetDefinition();
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_SurfaceStain_RefreshSize);
	if (SurfaceQuery->IsUsingAbsoluteLocation() || SurfaceQuery->IsUsingAbsoluteRotation()
		|| !SurfaceQuery->IsUsingAbsoluteScale())
		SurfaceQuery->SetAbsolute(false, false, true);
	SurfaceQuery->SetWorldScale3D(FVector::OneVector);
	// The legacy bake belongs to this actor. Never rename, duplicate, or discard a shared volume asset.
	if (!LegacyStickerMesh && StickerPatch.IsValid() && StickerMesh->GetStaticMesh()
		&& StickerMesh->GetStaticMesh()->GetOuter() == this)
		LegacyStickerMesh = StickerMesh->GetStaticMesh();
	const bool bVisible = RemainingSize > 0 && IsValid(ActiveDefinition) && ActiveDefinition->IsUsable();
	if (UsesSharedVolume())
	{
		const FMD_SurfaceStainVolumeStage* Stage = ActiveDefinition->GetVolumeStage(RemainingSize, GetInitialSize());
		Decal->SetVisibility(false);
		StickerMesh->SetVisibility(bVisible && Stage);
		if (!bVisible || !Stage)
		{
            MD_StainRayTracingResidency::SetStage(*StickerMesh, GetDefinition(), nullptr);
			SurfaceQuery->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			return;
		}
		// The common contact anchor stays fixed. Scaling is for this controlled prototype, not arbitrary edge fitting.
		const FVector MeshScale = ActiveDefinition->GetVolumeScale(GetInitialSize());
		MD_StainRayTracingResidency::SetStage(*StickerMesh, GetDefinition(), Stage->Mesh);
		StickerMesh->SetRelativeTransform(FTransform(FQuat::Identity, FVector::ZeroVector, MeshScale));
		StickerMesh->SetCastShadow(true);
		const FVector Extent = Stage->QueryBounds.Min.GetAbs().ComponentMax(Stage->QueryBounds.Max.GetAbs()) * MeshScale;
		const FVector QueryExtent = Extent + FVector(0.5);
		if (SurfaceQuery->GetUnscaledBoxExtent() != QueryExtent)
			SurfaceQuery->SetBoxExtent(QueryExtent, false);
		// Create query physics with the final size when restoring an empty stain.
		SurfaceQuery->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		RefreshMaterial();
		return;
	}
	MD_StainRayTracingResidency::SetStage(*StickerMesh, nullptr, LegacyStickerMesh);
	StickerMesh->SetRelativeTransform(FTransform::Identity);
	StickerMesh->SetCastShadow(false);
	const bool bSticker = HasSticker();
	Decal->SetVisibility(bVisible && !bSticker);
	StickerMesh->SetVisibility(bVisible && bSticker);
	SurfaceQuery->SetCollisionEnabled(bVisible ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	if (!bVisible) return;
	const FVector2D HalfSize = ActiveDefinition->GetHalfSize(RemainingSize);
	RefreshMaterial();
	Decal->DecalSize = FVector(ActiveDefinition->ProjectionDepthCm, HalfSize.Y, HalfSize.X);
	Decal->MarkRenderStateDirty();
	if (bSticker)
	{
		StickerMesh->SetCustomPrimitiveDataFloat(0, FMD_StainStickerPatch::UVScale(RemainingSize, StickerPatch.InitialSize));
		const FVector Extent = StickerPatch.LocalBounds.Min.GetAbs().ComponentMax(StickerPatch.LocalBounds.Max.GetAbs());
		SurfaceQuery->SetBoxExtent(Extent + FVector(0.5), false);
	}
	else SurfaceQuery->SetBoxExtent(FVector(HalfSize.X, HalfSize.Y, ActiveDefinition->ProjectionDepthCm), false);
}

FBoxSphereBounds AMD_SurfaceStain::GetStainBounds() const
{
	return UsesSharedVolume() || HasSticker() ? StickerMesh->Bounds : SurfaceQuery->Bounds;
}

bool AMD_SurfaceStain::ContainsSurfacePoint(const FVector& Point, const FVector& Normal) const
{
	const UMD_SurfaceStainDefinition* ActiveDefinition = GetDefinition();
	if (UsesSharedVolume() || RemainingSize <= 0 || !ActiveDefinition || !ActiveDefinition->IsUsable()
		|| Point.ContainsNaN() || Normal.ContainsNaN()) return false;
	if (HasSticker())
	{
		FVector2D UV;
		if (!StickerPatch.FindUV(GetActorTransform().InverseTransformPositionNoScale(Point),
			GetActorTransform().InverseTransformVectorNoScale(Normal), UV)) return false;
		UV = (UV - FVector2D(0.5)) * FMD_StainStickerPatch::UVScale(RemainingSize, StickerPatch.InitialSize) + FVector2D(0.5);
		return ActiveDefinition->ContainsUV(UV);
	}
	if (FVector::DotProduct(Normal, GetActorUpVector()) < ActiveDefinition->MinimumSurfaceCosine) return false;
	const FVector Local = GetActorTransform().InverseTransformPositionNoScale(Point);
	const FVector2D HalfSize = ActiveDefinition->GetHalfSize(RemainingSize);
	// UE fades the outer quarter of a projection volume; only target the fully covered region.
	if (FMath::Abs(Local.Z) > ActiveDefinition->ProjectionDepthCm * 0.75f) return false;
	return ActiveDefinition->ContainsUV(FVector2D(Local.X / (2 * HalfSize.X) + 0.5, Local.Y / (2 * HalfSize.Y) + 0.5));
}

bool AMD_SurfaceStain::TraceSticker(const FVector& Origin, const FVector& Direction, double MaxDistance, double& OutDistance) const
{
	const UMD_SurfaceStainDefinition* ActiveDefinition = GetDefinition();
	if (RemainingSize <= 0 || !ActiveDefinition || !ActiveDefinition->IsUsable() || !HasSticker()) return false;
	const FTransform Frame = GetActorTransform();
	const float Scale = FMD_StainStickerPatch::UVScale(RemainingSize, StickerPatch.InitialSize);
	return StickerPatch.Raycast(Frame.InverseTransformPositionNoScale(Origin), Frame.InverseTransformVectorNoScale(Direction),
		MaxDistance, [ActiveDefinition, Scale](const FVector2D& UV)
		{ return ActiveDefinition->ContainsUV((UV - FVector2D(0.5)) * Scale + FVector2D(0.5)); }, OutDistance);
}

bool AMD_SurfaceStain::TraceVolume(const FVector& Origin, const FVector& Direction, double MaxDistance, double& OutDistance) const
{
	const UMD_SurfaceStainDefinition* ActiveDefinition = GetDefinition();
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_SurfaceStain_VolumePick);
	if (!UsesSharedVolume() || Origin.ContainsNaN() || Direction.ContainsNaN() || !Direction.IsNormalized()
		|| !FMath::IsFinite(MaxDistance) || MaxDistance <= 0) return false;
	const FMD_SurfaceStainVolumeStage* Stage = ActiveDefinition->GetVolumeStage(RemainingSize, GetInitialSize());
	if (!Stage || !StickerMesh || StickerMesh->GetStaticMesh() != Stage->Mesh || !StickerMesh->IsVisible()) return false;
	const FTransform Frame = StickerMesh->GetComponentTransform();
	const FVector LocalOrigin = Frame.InverseTransformPosition(Origin);
	const FVector LocalVector = Frame.InverseTransformVector(Direction);
	const double LocalPerWorld = LocalVector.Length();
	if (!FMath::IsFinite(LocalPerWorld) || LocalPerWorld <= UE_SMALL_NUMBER) return false;
	const FVector LocalDirection = LocalVector / LocalPerWorld;
	const double LocalReach = MaxDistance * LocalPerWorld;
	if (!FMath::IsFinite(LocalReach) || !FMath::LineBoxIntersection(Stage->QueryBounds,
		LocalOrigin, LocalOrigin + LocalDirection * LocalReach, LocalDirection * LocalReach)) return false;
	double Nearest = LocalReach;
	bool bFound = false;
	// A bounded, shared closed query also runs on dedicated servers; no render-resource copies or per-hit allocation.
	for (int32 Index = 0; Index < Stage->QueryTriangleIndices.Num(); Index += 3)
	{
		const FVector& A = Stage->QueryVerticesCm[Stage->QueryTriangleIndices[Index]];
		const FVector E0 = Stage->QueryVerticesCm[Stage->QueryTriangleIndices[Index + 1]] - A;
		const FVector E1 = Stage->QueryVerticesCm[Stage->QueryTriangleIndices[Index + 2]] - A;
		const FVector P = FVector::CrossProduct(LocalDirection, E1);
		const double Determinant = FVector::DotProduct(E0, P);
		if (FMath::Abs(Determinant) <= 1.e-10) continue;
		const FVector Delta = LocalOrigin - A;
		const double B = FVector::DotProduct(Delta, P) / Determinant;
		if (B < -1.e-7 || B > 1.0000001) continue;
		const FVector Q = FVector::CrossProduct(Delta, E0);
		const double C = FVector::DotProduct(LocalDirection, Q) / Determinant;
		if (C < -1.e-7 || B + C > 1.0000001) continue;
		const double Distance = FVector::DotProduct(E1, Q) / Determinant;
		if (!FMath::IsFinite(Distance) || Distance < 0 || Distance > Nearest) continue;
		Nearest = Distance;
		bFound = true;
	}
	if (bFound) OutDistance = Nearest / LocalPerWorld;
	return bFound;
}

bool AMD_SurfaceStain::ApplyScrapeStroke(int32 RemovalAmount)
{
	const UMD_SurfaceStainDefinition* ActiveDefinition = GetDefinition();
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_SurfaceStain_ApplyStroke);
	if (!HasAuthority() || IsActorBeingDestroyed() || bTransferringItem || RemainingSize <= 0
		|| RemovalAmount <= 0 || !IsValid(ActiveDefinition) || !ActiveDefinition->IsUsable()) return false;
	TGuardValue<bool> TransferGuard(bTransferringItem, true);
	const int32 PreviousSize = RemainingSize;
	RemainingSize = FMath::Max(0, RemainingSize - FMath::Min(RemovalAmount, FMD_SurfaceStainSize::Maximum));
	if (RemainingSize == 0 && !TransferRemovedItem())
	{
		RemainingSize = PreviousSize;
		return false;
	}
	RefreshSize();
	ForceNetUpdate();
	return true;
}

bool AMD_SurfaceStain::TransferRemovedItem()
{
	const UMD_SurfaceStainDefinition* ActiveDefinition = GetDefinition();
	UMD_ContaminationDebrisSubsystem* Queue = GetWorld()->GetSubsystem<UMD_ContaminationDebrisSubsystem>();
	if (!Queue) return false;
	FMD_ContaminationPendingDrop Drop;
	Drop.BatchId = FGuid::NewGuid();
	Drop.SourceActorName = GetFName();
	Drop.ItemClass = ActiveDefinition->RemovedItemClass;
	Drop.bSinglePhysicalItem = true;
	Drop.bSpawnConfigurationReady = true;
	Drop.SpawnTransform = FTransform(GetActorQuat(), GetActorLocation() + GetActorUpVector() * 8.0f);
	int32 SpawnedCount = 0;
	return Queue->EnqueuePendingDrop(Drop, SpawnedCount);
}

bool AMD_SurfaceStain::RestoreSize(int32 Size)
{
	if (!HasAuthority() || bTransferringItem || !FMD_SurfaceStainSize::IsValidRemaining(Size)) return false;
	RemainingSize = Size;
	RefreshSize();
	ForceNetUpdate();
	return true;
}
