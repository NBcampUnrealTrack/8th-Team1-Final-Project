#include "World/Contamination/MD_SurfaceStain.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Gameplay/Contamination/MD_StainStickerBaker.h"
#include "Components/DecalComponent.h"
#include "Engine/World.h"
#include "Gameplay/Contamination/MD_SurfaceStainDefinition.h"
#include "Gameplay/Contamination/MD_SurfaceStainSize.h"
#include "Gameplay/Scrape/MD_ContaminationDebrisSubsystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "World/Contamination/MD_ContaminationItem.h"

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
	SurfaceQuery->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SurfaceQuery->SetCollisionObjectType(ECC_WorldDynamic);
	SurfaceQuery->SetCollisionResponseToAllChannels(ECR_Ignore);
	SurfaceQuery->SetCollisionResponseToChannel(ECC_Visibility, ECR_Overlap);
	SurfaceQuery->SetGenerateOverlapEvents(false);
	SurfaceQuery->SetCanEverAffectNavigation(false);
	SurfaceQuery->SetHiddenInGame(true);
	Decal = CreateDefaultSubobject<UDecalComponent>(TEXT("StainDecal"));
	Decal->SetupAttachment(SurfaceQuery);
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

void AMD_SurfaceStain::ConfigurePlacement(UMD_SurfaceStainDefinition* InDefinition, int32 InSize)
{
	if (GetWorld() && GetWorld()->IsGameWorld()) return;
	Definition = InDefinition;
	InitialSize = FMD_SurfaceStainSize::ClampInitial(InSize);
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

bool AMD_SurfaceStain::HasSticker() const
{
	return StickerPatch.IsValid() && StickerMesh && StickerMesh->GetStaticMesh();
}

bool AMD_SurfaceStain::RebuildSticker()
{
#if WITH_EDITOR
	if (!GetWorld() || GetWorld()->IsGameWorld() || !Definition || !Definition->StickerMaterial)
	{ StickerStatus = TEXT("Assign a definition with a sticker material before baking."); return false; }
	FMD_StainStickerPatch Patch;
	UStaticMesh* Mesh = nullptr;
	FString Error;
	if (!FMD_StainStickerBaker::Build(*this, Definition->GetHalfSize(GetInitialSize()), GetInitialSize(),
		Definition->StickerMaterial, Patch, Mesh, Error))
	{ StickerStatus = Error; return false; }
	Modify(); StickerMesh->Modify();
	StickerPatch = MoveTemp(Patch);
	StickerMesh->SetStaticMesh(Mesh);
	StickerStatus = FString::Printf(TEXT("Surface sticker: %d triangles"), GetStickerTriangleCount());
	RemainingSize = GetInitialSize();
	RefreshSize();
	MarkPackageDirty();
	return true;
#else
	return false;
#endif
}

#if WITH_EDITOR
void AMD_SurfaceStain::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);
	if (bFinished) RebuildSticker();
}

void AMD_SurfaceStain::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);
	const FName Name = Event.GetMemberPropertyName();
	if (Name == GET_MEMBER_NAME_CHECKED(AMD_SurfaceStain, InitialSize)
		|| Name == GET_MEMBER_NAME_CHECKED(AMD_SurfaceStain, Definition)) RebuildSticker();
}

void AMD_SurfaceStain::PostEditUndo()
{
	Super::PostEditUndo();
	RemainingSize = GetInitialSize();
	RefreshSize();
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
	DOREPLIFETIME(AMD_SurfaceStain, RemainingSize);
}

void AMD_SurfaceStain::RefreshMaterial()
{
	for (auto It = ScanSources.CreateIterator(); It; ++It)
		if (!It->IsValid()) It.RemoveCurrent();
	if (HasSticker() && IsValid(Definition))
	{
		StickerMesh->SetMaterial(0, Definition->StickerMaterial);
		StickerMesh->SetCustomPrimitiveDataFloat(1, ScanSources.IsEmpty() ? 0.f : 1.f);
		ScanMaterial = nullptr;
		return;
	}
	if (ScanSources.IsEmpty() || !IsValid(Definition))
	{
		Decal->SetDecalMaterial(IsValid(Definition) ? Definition->DecalMaterial : nullptr);
		ScanMaterial = nullptr;
		return;
	}
	if (!ScanMaterial || ScanMaterial->Parent != Definition->DecalMaterial)
		ScanMaterial = UMaterialInstanceDynamic::Create(Definition->DecalMaterial, this);
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
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_SurfaceStain_RefreshSize);
	SurfaceQuery->SetAbsolute(false, false, true);
	SurfaceQuery->SetWorldScale3D(FVector::OneVector);
	const bool bVisible = RemainingSize > 0 && IsValid(Definition) && Definition->IsUsable();
	const bool bSticker = HasSticker();
	Decal->SetVisibility(bVisible && !bSticker);
	StickerMesh->SetVisibility(bVisible && bSticker);
	SurfaceQuery->SetCollisionEnabled(bVisible ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	if (!bVisible) return;
	const FVector2D HalfSize = Definition->GetHalfSize(RemainingSize);
	RefreshMaterial();
	Decal->DecalSize = FVector(Definition->ProjectionDepthCm, HalfSize.Y, HalfSize.X);
	Decal->MarkRenderStateDirty();
	if (bSticker)
	{
		StickerMesh->SetCustomPrimitiveDataFloat(0, FMD_StainStickerPatch::UVScale(RemainingSize, StickerPatch.InitialSize));
		const FVector Extent = StickerPatch.LocalBounds.Min.GetAbs().ComponentMax(StickerPatch.LocalBounds.Max.GetAbs());
		SurfaceQuery->SetBoxExtent(Extent + FVector(0.5), false);
	}
	else SurfaceQuery->SetBoxExtent(FVector(HalfSize.X, HalfSize.Y, Definition->ProjectionDepthCm), false);
}

FBoxSphereBounds AMD_SurfaceStain::GetStainBounds() const
{
	return HasSticker() ? StickerMesh->Bounds : SurfaceQuery->Bounds;
}

bool AMD_SurfaceStain::ContainsSurfacePoint(const FVector& Point, const FVector& Normal) const
{
	if (RemainingSize <= 0 || !Definition || !Definition->IsUsable()
		|| Point.ContainsNaN() || Normal.ContainsNaN()) return false;
	if (HasSticker())
	{
		FVector2D UV;
		if (!StickerPatch.FindUV(GetActorTransform().InverseTransformPositionNoScale(Point),
			GetActorTransform().InverseTransformVectorNoScale(Normal), UV)) return false;
		UV = (UV - FVector2D(0.5)) * FMD_StainStickerPatch::UVScale(RemainingSize, StickerPatch.InitialSize) + FVector2D(0.5);
		return Definition->ContainsUV(UV);
	}
	if (FVector::DotProduct(Normal, GetActorUpVector()) < Definition->MinimumSurfaceCosine) return false;
	const FVector Local = GetActorTransform().InverseTransformPositionNoScale(Point);
	const FVector2D HalfSize = Definition->GetHalfSize(RemainingSize);
	// UE fades the outer quarter of a projection volume; only target the fully covered region.
	if (FMath::Abs(Local.Z) > Definition->ProjectionDepthCm * 0.75f) return false;
	return Definition->ContainsUV(FVector2D(Local.X / (2 * HalfSize.X) + 0.5, Local.Y / (2 * HalfSize.Y) + 0.5));
}

bool AMD_SurfaceStain::ApplyScrapeStroke(int32 RemovalAmount)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_SurfaceStain_ApplyStroke);
	if (!HasAuthority() || IsActorBeingDestroyed() || bTransferringItem || RemainingSize <= 0
		|| RemovalAmount <= 0 || !IsValid(Definition) || !Definition->IsUsable()) return false;
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
	UMD_ContaminationDebrisSubsystem* Queue = GetWorld()->GetSubsystem<UMD_ContaminationDebrisSubsystem>();
	if (!Queue) return false;
	FMD_ContaminationPendingDrop Drop;
	Drop.BatchId = FGuid::NewGuid();
	Drop.SourceActorName = GetFName();
	Drop.ItemClass = Definition->RemovedItemClass;
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
