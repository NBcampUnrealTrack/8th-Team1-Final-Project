#include "Tools/MD_StainPlacementTool.h"
#include "Gameplay/Contamination/MD_StainSurfaceFit.h"
#include "Tools/MD_StainEdgePlacement.h"
#include "MD_TrashPlacementSettings.h"
#include "Gameplay/Contamination/MD_StainTargeting.h"

#include "BaseBehaviors/ClickDragBehavior.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "BaseBehaviors/MouseWheelBehavior.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "LevelUtils.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Gameplay/Contamination/MD_StainStickerBaker.h"
#include "Gameplay/Contamination/MD_StainStickerPatch.h"
#include "Components/PrimitiveComponent.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Engine/World.h"
#include "Gameplay/Contamination/MD_SurfaceStainDefinition.h"
#include "Gameplay/Contamination/MD_SurfaceStainSize.h"
#include "InteractiveToolManager.h"
#include "Math/RandomStream.h"
#include "PrimitiveDrawingUtils.h"
#include "ToolContextInterfaces.h"
#include "World/Contamination/MD_SurfaceStain.h"

#define LOCTEXT_NAMESPACE "MDStainPlacement"

bool UMD_StainPlacementToolBuilder::CanBuildTool(const FToolBuilderState& State) const
{
	return State.World && State.World->WorldType == EWorldType::Editor;
}

UInteractiveTool* UMD_StainPlacementToolBuilder::BuildTool(const FToolBuilderState& State) const
{
	UMD_StainPlacementTool* Tool = NewObject<UMD_StainPlacementTool>(State.ToolManager);
	Tool->SetWorld(State.World);
	return Tool;
}

void UMD_StainPlacementTool::Setup()
{
	Super::Setup();
	SourceCache = FMD_StainStickerBaker::CreateSourceCache();
	Properties = NewObject<UMD_StainPlacementProperties>(this);
	Properties->RestoreProperties(this);
	const UMD_TrashPlacementSettings* Settings = GetDefault<UMD_TrashPlacementSettings>();
	if (Properties->Palette.IsEmpty())
		for (const TSoftObjectPtr<UMD_SurfaceStainDefinition>& Reference : Settings->StainPalette)
			Properties->Palette.Add(Reference.LoadSynchronous());
	StickerPreviewMesh = Settings->StickerPreviewMesh.LoadSynchronous();
	AddToolPropertySource(Properties);
	UClickDragInputBehavior* Drag = NewObject<UClickDragInputBehavior>(this);
	Drag->Initialize(this);
	Drag->Modifiers.RegisterModifier(1, FInputDeviceState::IsShiftKeyDown);
	Drag->bUpdateModifiersDuringDrag = false;
	AddInputBehavior(Drag);
	UMouseHoverBehavior* Hover = NewObject<UMouseHoverBehavior>(this);
	Hover->Initialize(this);
	Hover->Modifiers.RegisterModifier(1, FInputDeviceState::IsShiftKeyDown);
	AddInputBehavior(Hover);
	UMouseWheelInputBehavior* Wheel = NewObject<UMouseWheelInputBehavior>(this);
	Wheel->Initialize(this);
	// UE 5.8 camera zoom has priority 95; spacing must win while this tool is active.
	Wheel->SetDefaultPriority(FInputCapturePriority(FInputCapturePriority::DEFAULT_TOOL_PRIORITY).MakeHigher(10));
	Wheel->ModifierCheckFunc = [](const FInputDeviceState& Input)
	{
		return !Input.Mouse.Left.bDown && !Input.Mouse.Right.bDown && !Input.Mouse.Middle.bDown
			&& !Input.bAltKeyDown && !Input.bCtrlKeyDown && !Input.bCmdKeyDown;
	};
	AddInputBehavior(Wheel);
	FActorSpawnParameters PreviewParams;
	PreviewParams.ObjectFlags = RF_Transient;
	PreviewParams.bHideFromSceneOutliner = true;
	PreviewOwner = TargetWorld->SpawnActor<AActor>(PreviewParams);
	USceneComponent* Frame = NewObject<USceneComponent>(PreviewOwner, NAME_None, RF_Transient);
	Frame->SetMobility(EComponentMobility::Movable);
	PreviewOwner->SetRootComponent(Frame);
	Frame->RegisterComponentWithWorld(TargetWorld);
	Preview = NewObject<UStaticMeshComponent>(PreviewOwner, NAME_None, RF_Transient);
	Preview->SetupAttachment(Frame);
	Preview->SetMobility(EComponentMobility::Movable);
	Preview->bSelectable = false;
	Preview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Preview->SetReceivesDecals(false);
	Preview->SetCastShadow(false);
	Preview->SetCanEverAffectNavigation(false);
	Preview->SetVisibility(false);
	Preview->RegisterComponentWithWorld(TargetWorld);
	Preview->SetCustomPrimitiveDataFloat(0, 1.0f);
	Preview->SetCustomPrimitiveDataFloat(1, 0.0f);
	GetToolManager()->DisplayMessage(LOCTEXT("Help", "Choose stain definitions in Palette. Click/drag to place; Shift erases stains; wheel changes spacing."), EToolMessageLevel::UserNotification);
}

void UMD_StainPlacementTool::Shutdown(EToolShutdownType Type)
{
	if (ActiveBake) ActiveBake->Cancel();
	ActiveBake.Reset(); PendingPlacements.Reset(); SourceCache.Reset();
	FinishStroke();
	Properties->SaveProperties(this);
	if (PreviewOwner) TargetWorld->DestroyActor(PreviewOwner);
	Preview = nullptr;
	PreviewOwner = nullptr;
	Super::Shutdown(Type);
}

bool UMD_StainPlacementTool::ValidateVolumePlacement(UWorld* World, UMD_SurfaceStainDefinition* Definition,
	UPrimitiveComponent* Surface, const FTransform& ContactTransform, int32 InitialSize, FString& Error)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_Placement_VolumeContact);
	Error.Reset();
	if (!IsValid(World) || World->WorldType != EWorldType::Editor || !IsValid(Definition))
	{ Error = TEXT("Volume placement requires an editor world and a valid definition."); return false; }
	const bool bEdge = Definition->VolumePlacementPolicy == EMD_SurfaceStainVolumePlacement::ConvexRightAngleSupport;
	if (!(bEdge ? Definition->CanPlaceEdgeVolume(InitialSize, Error) : Definition->CanPlacePlanarVolume(InitialSize, Error))) return false;
	const UStaticMeshComponent* MeshSurface = Cast<UStaticMeshComponent>(Surface);
	if (!IsValid(MeshSurface) || !MeshSurface->IsRegistered() || MeshSurface->GetWorld() != World
		|| !MeshSurface->GetStaticMesh() || Cast<UInstancedStaticMeshComponent>(MeshSurface)
		|| !MeshSurface->IsQueryCollisionEnabled() || MeshSurface->GetCollisionResponseToChannel(ECC_Visibility) != ECR_Block)
	{ Error = TEXT("Volume placement requires an individual static mesh with Visibility query collision."); return false; }
	if (!Surface->GetOwner() || FLevelUtils::IsLevelLocked(Surface->GetOwner()->GetLevel()))
	{ Error = TEXT("The receiving surface belongs to a locked level."); return false; }
	if (ContactTransform.ContainsNaN() || !ContactTransform.GetRotation().IsNormalized()
		|| !ContactTransform.GetScale3D().Equals(FVector::OneVector, 1.e-6))
	{ Error = TEXT("Use an unscaled contact anchor; initial size controls the shared volume's XY scale."); return false; }
	if (bEdge) return FMD_StainEdgePlacement::Validate(*World, *Definition, *Surface, ContactTransform, InitialSize, Error);
	const FVector Normal = ContactTransform.GetUnitAxis(EAxis::Z);
	const double Scale = FMD_SurfaceStainSize::LinearScale(InitialSize);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MDVolumePlacementSupport), true);
	Params.bFindInitialOverlaps = true;
	const double TraceAbove = FMath::Max(10.0, Definition->VolumeStages[0].QueryBounds.Max.Z + 2.0);
	for (const FVector2D& Local : Definition->GetPlanarPlacementSamples())
	{
		const FVector Contact = ContactTransform.TransformPosition(FVector(Local.X * Scale, Local.Y * Scale, 0.0));
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Contact + Normal * TraceAbove, Contact - Normal * 2.0, ECC_Visibility, Params)
			|| Hit.bStartPenetrating || Hit.GetComponent() != Surface)
		{ Error = TEXT("The full footprint needs one receiving surface. A gap, edge, or another object blocks this placement."); return false; }
		if (FMath::Abs(FVector::DotProduct(Hit.ImpactPoint - Contact, Normal)) > 0.1
			|| FVector::DotProduct(Hit.ImpactNormal, Normal) < 0.98)
		{ Error = TEXT("This shared shape requires planar support within 0.1 cm. Move it away from bends and steps."); return false; }
	}
	return true;
}

bool UMD_StainPlacementTool::ResolveVolumePlacement(UWorld* World, UMD_SurfaceStainDefinition* Definition,
	UPrimitiveComponent* Surface, const FTransform& Hint, int32 InitialSize, FTransform& Contact, FString& Error)
{
	Error.Reset();
	Contact = Hint;
	if (!IsValid(World) || World->WorldType != EWorldType::Editor || !IsValid(Definition)
		|| !IsValid(Surface) || Surface->GetWorld() != World || Hint.ContainsNaN()
		|| !Hint.GetRotation().IsNormalized() || !Hint.GetScale3D().Equals(FVector::OneVector, 1.e-6))
	{ Error = TEXT("Edge fitting needs a valid editor surface and an unscaled main-face hit."); return false; }
	if (Definition->VolumePlacementPolicy == EMD_SurfaceStainVolumePlacement::ConvexRightAngleSupport
		&& !FMD_StainEdgePlacement::Resolve(*World, *Definition, *Surface, Hint, InitialSize, Contact, Error)) return false;
	return ValidateVolumePlacement(World, Definition, Surface, Contact, InitialSize, Error);
}

AMD_SurfaceStain* UMD_StainPlacementTool::PlaceVolume(UWorld* World, UMD_SurfaceStainDefinition* Definition,
	UPrimitiveComponent* Surface, const FTransform& ContactTransform, int32 InitialSize, FString& Error)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_Placement_VolumeCommit);
	if (!ValidateVolumePlacement(World, Definition, Surface, ContactTransform, InitialSize, Error)) return nullptr;
	ULevel* Level = World->GetCurrentLevel();
	if (!GEditor || !Level || FLevelUtils::IsLevelLocked(Level))
	{ Error = TEXT("The destination level is locked or unloaded."); return nullptr; }
	const FScopedTransaction Transaction(LOCTEXT("PlaceVolume", "Place Surface Stain"));
	AMD_SurfaceStain* Stain = Cast<AMD_SurfaceStain>(GEditor->AddActor(Level,
		AMD_SurfaceStain::StaticClass(), ContactTransform, true, RF_Transactional, false));
	if (!Stain) { Error = TEXT("The editor could not create the surface stain."); return nullptr; }
	Stain->Modify();
	Stain->GetRootComponent()->Modify();
	Stain->ConfigurePlacement(Definition, InitialSize);
	Surface->Modify();
	Stain->AttachToComponent(Surface, FAttachmentTransformRules::KeepWorldTransform);
	Stain->MarkPackageDirty();
	return Stain;
}

bool UMD_StainPlacementTool::WantsErase() const
{
	return bStroke ? bStrokeErase : bShift || Properties->bErase;
}

void UMD_StainPlacementTool::OnUpdateModifierState(int ModifierId, bool bOn)
{
	if (ModifierId == 1)
	{
		bShift = bOn;
		if (bHover) OnUpdateHover(LastHoverRay);
	}
}

AMD_SurfaceStain* UMD_StainPlacementTool::PlaceFittedVolume(UWorld* World, UMD_SurfaceStainDefinition* Definition,
	UPrimitiveComponent* Surface, const FTransform& ContactTransform, int32 InitialSize, FString& Error)
{
	UStaticMeshComponent* Receiver = Cast<UStaticMeshComponent>(Surface);
	if (!World || World->IsGameWorld() || !GEditor || !World->GetCurrentLevel()
		|| FLevelUtils::IsLevelLocked(World->GetCurrentLevel()) || !Definition || !Definition->UsesSharedVolume()
		|| !Definition->IsUsable() || InitialSize < FMD_SurfaceStainSize::Minimum || InitialSize > FMD_SurfaceStainSize::MaximumPlacement || !Receiver
		|| Cast<UInstancedStaticMeshComponent>(Receiver) || !Receiver->GetStaticMesh() || !Receiver->IsRegistered()
		|| Receiver->GetWorld() != World || !Receiver->IsQueryCollisionEnabled()
		|| Receiver->GetCollisionResponseToChannel(ECC_Visibility) != ECR_Block || !Receiver->GetOwner()
		|| FLevelUtils::IsLevelLocked(Receiver->GetOwner()->GetLevel()) || ContactTransform.ContainsNaN()
		|| !ContactTransform.GetScale3D().Equals(FVector::OneVector,0.0001))
	{ Error = TEXT("A valid editor surface, volume profile, and size 100-5000 are required."); return nullptr; }
	FActorSpawnParameters Params; Params.ObjectFlags = RF_Transient; Params.bHideFromSceneOutliner = true;
	AActor* Helper = World->SpawnActor<AActor>(AActor::StaticClass(), ContactTransform, Params);
	USceneComponent* Root = NewObject<USceneComponent>(Helper); Helper->SetRootComponent(Root); Root->RegisterComponent(); Helper->SetActorTransform(ContactTransform);
	FMD_StainStickerPatch Patch; UStaticMesh* Unused = nullptr;
	UMD_SurfaceStainDefinition* Fit = nullptr;
	if (FMD_StainStickerBaker::Build(*Helper, FMD_StainSurfaceFit::HalfSize(*Definition, InitialSize), InitialSize,
		Definition->VolumeMaterial, Patch, Unused, Error, true))
		Fit = FMD_StainSurfaceFit::Build(*Helper, *Definition, Patch, InitialSize, Error);
	UMD_SurfaceStainDefinition* Saved = Fit ? FMD_StainSurfaceFit::Save(*Fit, Error) : nullptr;
	World->DestroyActor(Helper);
	if (!Saved) return nullptr;
	FScopedTransaction Transaction(LOCTEXT("PlaceFittedVolume", "Place Surface-Fitted Slime"));
	AMD_SurfaceStain* Stain = Cast<AMD_SurfaceStain>(GEditor->AddActor(World->GetCurrentLevel(), AMD_SurfaceStain::StaticClass(), ContactTransform, true, RF_Transactional, false));
	if (!Stain) return nullptr;
	Stain->Modify(); Stain->ConfigurePlacement(Saved,InitialSize); Receiver->Modify();
	Stain->AttachToComponent(Receiver,FAttachmentTransformRules::KeepWorldTransform); Stain->MarkPackageDirty(); return Stain;
}

bool UMD_StainPlacementTool::Trace(const FInputDeviceRay& Ray, FHitResult& Hit, const bool bErase) const
{
	if (!TargetWorld || TargetWorld->WorldType != EWorldType::Editor) return false;
	constexpr double TraceDistanceCm = 1000000.0;
	const FVector Start = Ray.WorldRay.Origin;
	const FVector End = Start + Ray.WorldRay.Direction * TraceDistanceCm;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MDStainPlacement), true);
	if (!bErase)
	{
		return TargetWorld->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params)
			&& Hit.GetComponent();
	}
	return FMD_StainTargeting::Trace(*TargetWorld, Start, Ray.WorldRay.Direction, TraceDistanceCm, Hit);
}

UMD_SurfaceStainDefinition* UMD_StainPlacementTool::ResolveDefinition() const
{
	if (!Properties || Properties->Palette.IsEmpty()) return nullptr;
	FRandomStream Random(HashCombine(GetTypeHash(Properties->RandomSeed), GetTypeHash(PlacementIndex)));
	const int32 Index = Properties->bRandomVariant ? Random.RandRange(0, Properties->Palette.Num() - 1) : Properties->VariantIndex;
	UMD_SurfaceStainDefinition* Definition = Properties->Palette.IsValidIndex(Index) ? Properties->Palette[Index] : nullptr;
	return IsValid(Definition) && Definition->IsUsable() && (Definition->UsesSharedVolume() || Definition->StickerMaterial) ? Definition : nullptr;
}

int32 UMD_StainPlacementTool::ResolveSize() const
{
	const int32 Seed = HashCombine(GetTypeHash(Properties->RandomSeed), GetTypeHash(PlacementIndex) ^ 0x57A1u);
	return Properties->bRandomSize
		? FMD_SurfaceStainSize::RandomInitial(Properties->MinSize, Properties->MaxSize, Seed)
		: FMD_SurfaceStainSize::ClampPlacement(Properties->InitialSize);
}

FTransform UMD_StainPlacementTool::MakeTransform(const FHitResult& Hit) const
{
	FRandomStream Random(HashCombine(GetTypeHash(Properties->RandomSeed), GetTypeHash(PlacementIndex)));
	const double Roll = Properties->RotationDegrees + (Properties->bRandomRotation ? Random.FRandRange(-180.0f, 180.0f) : 0.0f);
	const FQuat SurfaceRotation = FRotationMatrix::MakeFromZ(Hit.ImpactNormal).ToQuat();
	const FQuat Rotation = SurfaceRotation * FQuat(FVector::UpVector, FMath::DegreesToRadians(Roll));
	return FTransform(Rotation, Hit.ImpactPoint, FVector::OneVector);
}

FInputRayHit UMD_StainPlacementTool::CanBeginClickDragSequence(const FInputDeviceRay& Ray)
{
	FHitResult Hit;
	if (Trace(Ray, Hit, WantsErase())) return FInputRayHit(Hit.Distance);
	// A paint stroke may start over empty space and subsequently enter a surface.
	return !WantsErase() && ResolveDefinition() ? FInputRayHit(0.0) : FInputRayHit();
}

void UMD_StainPlacementTool::OnClickPress(const FInputDeviceRay& Ray)
{
	if (IsPlacementPending())
	{
		GetToolManager()->DisplayMessage(LOCTEXT("StillPlacing", "Finishing the previous stroke. Wait for placement to complete, or switch tools to cancel the remaining stamps."), EToolMessageLevel::UserNotification);
		return;
	}
	FinishStroke();
	bStrokeErase = WantsErase();
	if (!bStrokeErase && !ResolveDefinition())
	{
		GetToolManager()->DisplayMessage(LOCTEXT("MissingDefinition", "Select a valid stain definition with its presentation material and slime item class."), EToolMessageLevel::UserWarning);
		return;
	}
	bStroke = true;
	bAttempted = false;
	StrokePlaced = 0; StrokeFailed = 0;
	// A new click can retry a repaired collision/material/surface at the same point.
	if (bPreviewRejected) PreviewSize = 0;
	ApplyAt(Ray);
}

void UMD_StainPlacementTool::BeginStrokeTransaction()
{
	if (bTransaction) return;
	GetToolManager()->BeginUndoTransaction(bStrokeErase ? LOCTEXT("Erase", "Erase Surface Stains") : LOCTEXT("Place", "Place Surface Stains"));
	bTransaction = true;
}

void UMD_StainPlacementTool::ApplyAt(const FInputDeviceRay& Ray)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_Placement_StainApply);
	FHitResult Hit;
	if (!Trace(Ray, Hit, bStrokeErase)) return;
	if (bStrokeErase)
	{
		AMD_SurfaceStain* Stain = CastChecked<AMD_SurfaceStain>(Hit.GetActor());
		if (FLevelUtils::IsLevelLocked(Stain->GetLevel())) return;
		BeginStrokeTransaction();
		Stain->Modify();
		if (GEditor && Stain->IsSelected()) GEditor->SelectActor(Stain, false, true);
		TargetWorld->EditorDestroyActor(Stain, true);
		return;
	}
	UMD_SurfaceStainDefinition* Definition = ResolveDefinition();
	if (!Definition || !GEditor || !TargetWorld->GetCurrentLevel()) return;
	const float Spacing = FMath::Max(Properties->SpacingCm, 1.0f);
	// Failed attempts consume spacing too; a stationary failed hit must not rebake every mouse event.
	if (bAttempted && FVector::DistSquared(LastAttemptLocation, Hit.ImpactPoint) < FMath::Square(Spacing)) return;
	LastAttemptLocation = Hit.ImpactPoint;
	bAttempted = true;
	OnUpdateHover(Ray);
	if (!Properties->bFollowSurface && Definition->VolumePlacementPolicy == EMD_SurfaceStainVolumePlacement::ConvexRightAngleSupport && !bPreviewValid)
	{
		bPreviewPending = true;
		if (!PreparePreview()) { ++StrokeFailed; return; }
	}
	const FMD_StainPlacementRequest Request = PreviewRequest();
	FString Error;
	if (!ValidateRequest(Request, Error)) { ++StrokeFailed; bPreviewPending = false; SetPreviewError(Error); return; }
	if (Definition->UsesSharedVolume() && !Request.bFitSurface)
	{
		// Recheck every contact at commit even when the stationary preview was accepted earlier.
		CommitPrepared(Request, FMD_StainStickerPatch(), nullptr);
		if (!bPreviewRejected) ++PlacementIndex;
		PreviewSize = 0; bPreviewValid = false;
		return;
	}
	if (bPreviewRejected) { ++StrokeFailed; return; }
	if (bPreviewValid && Request.bFitSurface)
	{
		if (!PreparedSurface || !PreparedSurface->SourcesAreCurrent())
		{ ++StrokeFailed; SetPreviewError(TEXT("A receiving surface changed. Move the cursor to prepare it again.")); return; }
		CommitFit(Request, PreparedFit);
	}
	else if (bPreviewValid)
	{
		UStaticMesh* Mesh = Preview->GetStaticMesh();
		if (Mesh && Mesh->GetOuter() != PreviewOwner)
			Mesh = FMD_StainStickerBaker::CreateMesh(*PreviewOwner, PreviewPatch, Request.Definition->StickerMaterial);
		Preview->SetStaticMesh(nullptr);
		CommitPrepared(Request, MoveTemp(PreviewPatch), Mesh);
	}
	else if (ActiveBake && !ActiveBake->IsCancelled() && !bBakeForPlacement && MatchesPreview(ActiveRequest))
	{
		bBakeForPlacement = true;
	}
	else
	{
		constexpr int32 MaximumQueuedPlacements = 256;
		if (PendingPlacements.Num() >= MaximumQueuedPlacements)
		{
			GetToolManager()->DisplayMessage(LOCTEXT("QueueFull", "Placement queue is full. Release the mouse and let the current stroke finish."), EToolMessageLevel::UserWarning);
			return;
		}
		PendingPlacements.Add(Request);
		if (ActiveBake && !bBakeForPlacement) ActiveBake->Cancel();
	}
	PreviewSize = 0; bPreviewValid = false;
	++PlacementIndex;
}

void UMD_StainPlacementTool::CommitFit(const FMD_StainPlacementRequest& Request, UMD_SurfaceStainDefinition* Fit)
{
	FString Error;
	if (!Fit || !ValidateRequest(Request, Error)) { ++StrokeFailed; SetPreviewError(Error); return; }
	UMD_SurfaceStainDefinition* Saved = FMD_StainSurfaceFit::Save(*Fit, Error);
	if (!Saved) { ++StrokeFailed; SetPreviewError(Error); return; }
	BeginStrokeTransaction();
	AMD_SurfaceStain* Stain = Cast<AMD_SurfaceStain>(GEditor->AddActor(Request.Level.Get(),
		AMD_SurfaceStain::StaticClass(), Request.Transform, true, RF_Transactional, false));
	if (!Stain) { ++StrokeFailed; return; }
	Stain->Modify(); Stain->ConfigurePlacement(Saved, Request.Size);
	Request.Surface->Modify(); Stain->AttachToComponent(Request.Surface.Get(), FAttachmentTransformRules::KeepWorldTransform);
	Stain->MarkPackageDirty(); ++StrokePlaced; bPreviewRejected = false;
}

void UMD_StainPlacementTool::CommitPrepared(const FMD_StainPlacementRequest& Request, FMD_StainStickerPatch Patch, UStaticMesh* Mesh)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_Placement_StainCommit);
	FString Error;
	if (!ValidateRequest(Request, Error)) { ++StrokeFailed; SetPreviewError(Error); return; }
	if (Request.Definition->UsesSharedVolume())
	{
		// PlaceVolume opens the nested transaction; the stroke owns the outer Undo grouping.
		BeginStrokeTransaction();
		if (PlaceVolume(TargetWorld, Request.Definition.Get(), Request.Surface.Get(), Request.Transform, Request.Size, Error))
		{
			++StrokePlaced;
			bPreviewRejected = false;
		}
		else { ++StrokeFailed; SetPreviewError(Error); }
		return;
	}
	BeginStrokeTransaction();
	AMD_SurfaceStain* Stain = Cast<AMD_SurfaceStain>(GEditor->AddActor(Request.Level.Get(),
		AMD_SurfaceStain::StaticClass(), Request.Transform, true, RF_Transactional, false));
	if (!Stain) return;
	Stain->Modify();
	Stain->ConfigurePlacement(Request.Definition.Get(), Request.Size);
	Request.Surface->Modify();
	Stain->AttachToComponent(Request.Surface.Get(), FAttachmentTransformRules::KeepWorldTransform);
	if (MatchesPreview(Request)) PreviewPatch = Patch;
	if (!Stain->ApplyPreparedSticker(MoveTemp(Patch), Mesh))
	{
		SetPreviewError(TEXT("Could not transfer the prepared sticker to the level."));
		TargetWorld->EditorDestroyActor(Stain, true);
		++StrokeFailed;
	}
	else
	{
		++StrokePlaced;
		if (MatchesPreview(Request))
		{
			Preview->SetRelativeTransform(FTransform::Identity);
			Preview->SetStaticMesh(Mesh); Preview->SetVisibility(true);
			bPreviewPending = false; bPreviewValid = true; bPreviewRejected = false;
		}
	}
}

void UMD_StainPlacementTool::OnClickDrag(const FInputDeviceRay& Ray)
{
	// Coalesce high-frequency pointer events; author at most one position per editor frame.
	if (bStroke) { PendingDragRay = Ray; bDragPending = true; }
	OnUpdateHover(Ray);
}
void UMD_StainPlacementTool::OnClickRelease(const FInputDeviceRay& Ray)
{
	if (bStroke && bDragPending) { bDragPending = false; ApplyAt(Ray); }
	FinishStroke(); OnUpdateHover(Ray);
}
void UMD_StainPlacementTool::OnTerminateDragSequence() { FinishStroke(); }
void UMD_StainPlacementTool::FinishStroke()
{
	if (bTransaction && !IsPlacementPending())
	{
		GetToolManager()->EndUndoTransaction(); bTransaction = false;
	}
	bStroke = false; bDragPending = false;
}

FInputRayHit UMD_StainPlacementTool::BeginHoverSequenceHitTest(const FInputDeviceRay& Ray) { return CanBeginClickDragSequence(Ray); }
void UMD_StainPlacementTool::OnBeginHover(const FInputDeviceRay& Ray) { OnUpdateHover(Ray); }

bool UMD_StainPlacementTool::OnUpdateHover(const FInputDeviceRay& Ray)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_Placement_StainHover);
	const bool bRestorePreview = bHoverEndPending && bPreviewWasVisible;
	bHoverEndPending = false;
	LastHoverRay = Ray;
	bHover = Trace(Ray, HoverHit, WantsErase());
	UMD_SurfaceStainDefinition* Definition = ResolveDefinition();
	const bool bShow = bHover && Definition && !WantsErase();
	if (!bShow)
	{
		if (ActiveBake && !bBakeForPlacement) ActiveBake->Cancel();
		DraftPreview = nullptr;
		PreviewDefinition = Definition;
		PreviewDefinitionRevision = Definition ? Definition->GetAuthoringRevision() : 0;
		Preview->SetVisibility(false);
		bPreviewPending = false;
		bPreviewValid = false;
		PreviewSize = 0;
	}
	else
	{
		const FTransform Transform = MakeTransform(HoverHit);
		const int32 Size = ResolveSize();
		const auto* Surface = Cast<UStaticMeshComponent>(HoverHit.GetComponent());
		UStaticMesh* SourceMesh = Surface ? Surface->GetStaticMesh() : nullptr;
		const FGuid SourceGuid = SourceMesh ? SourceMesh->GetLightingGuid() : FGuid();
		if (Definition != PreviewDefinition || Definition->GetAuthoringRevision() != PreviewDefinitionRevision || Size != PreviewSize || SourceMesh != PreviewSourceMesh || SourceGuid != PreviewSourceGuid || !Transform.Equals(PreviewInputTransform, 0.01)
			|| PreviewTarget != HoverHit.GetComponent() || !PreviewTargetTransform.Equals(HoverHit.GetComponent()->GetComponentTransform(), 0.01))
		{
			PreviewSourceMesh = SourceMesh; PreviewSourceGuid = SourceGuid;
			PreviewTarget = HoverHit.GetComponent();
			PreviewTargetTransform = HoverHit.GetComponent()->GetComponentTransform();
			PreviewInputTransform = Transform;
			PreviewTransform = Transform;
			PreviewDefinition = Definition;
			PreviewDefinitionRevision = Definition->GetAuthoringRevision();
			PreviewSize = Size;
			PreviewChangedAt = FPlatformTime::Seconds();
			bPreviewPending = true;
			bPreviewValid = false;
			bPreviewRejected = false;
			if (ActiveBake && !bBakeForPlacement) ActiveBake->Cancel();
			PreviewPatch = FMD_StainStickerPatch();
			PreparedFit = nullptr; PreparedSurface.Reset(); DraftPreview = nullptr;
			ShowMovingPreview();
		}
		else if (bRestorePreview)
		{
			// Resume the existing component, including a prepared legacy sticker mesh.
			Preview->SetVisibility(true);
		}
	}
	return true;
}

void UMD_StainPlacementTool::ShowMovingPreview()
{
	PreviewOwner->SetActorTransform(PreviewTransform);
	if (PreviewDefinition->UsesSharedVolume())
	{
		if (Properties->bFollowSurface)
		{
			const auto* Stage = PreparedFit ? PreparedFit->GetVolumeStage(PreviewSize, PreviewSize) : nullptr;
			Preview->SetStaticMesh(Stage ? Stage->Mesh.Get() : DraftPreview.Get());
			Preview->SetMaterial(0, PreviewDefinition->VolumeMaterial);
			Preview->SetRelativeTransform(FTransform(FQuat::Identity,FVector::ZeroVector,
				PreparedFit ? PreparedFit->GetVolumeScale(PreviewSize) : FVector::OneVector));
			Preview->SetCastShadow(true); Preview->SetVisibility((Stage && bPreviewValid) || DraftPreview != nullptr);
			return;
		}
		FString Error;
		const bool bEdge = PreviewDefinition->VolumePlacementPolicy == EMD_SurfaceStainVolumePlacement::ConvexRightAngleSupport;
		if (!(bEdge ? PreviewDefinition->CanPlaceEdgeVolume(PreviewSize, Error) : PreviewDefinition->CanPlacePlanarVolume(PreviewSize, Error)))
		{ bPreviewPending = false; SetPreviewError(Error); return; }
		const FMD_SurfaceStainVolumeStage* Stage = PreviewDefinition->GetVolumeStage(PreviewSize, PreviewSize);
		const FVector MeshScale = PreviewDefinition->GetVolumeScale(PreviewSize);
		Preview->SetStaticMesh(Stage ? Stage->Mesh.Get() : nullptr);
		Preview->SetMaterial(0, PreviewDefinition->VolumeMaterial);
		Preview->SetRelativeTransform(FTransform(FQuat::Identity, FVector::ZeroVector, MeshScale));
		Preview->SetCastShadow(true);
		Preview->SetVisibility(Stage != nullptr && (!bEdge || bPreviewValid));
		return;
	}
	// Optional editor-assigned quad; absence keeps the status marker until the real sticker bake finishes.
	const FVector2D HalfSize = PreviewDefinition->GetHalfSize(PreviewSize);
	Preview->SetStaticMesh(StickerPreviewMesh);
	Preview->SetMaterial(0, PreviewDefinition->StickerMaterial);
	Preview->SetRelativeTransform(FTransform(FQuat::Identity, FVector(0, 0, 0.1),
		FVector(HalfSize.X / 50.0, HalfSize.Y / 50.0, 1.0)));
	Preview->SetCastShadow(false);
	Preview->SetVisibility(StickerPreviewMesh != nullptr);
}

void UMD_StainPlacementTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	Super::OnPropertyModified(PropertySet, Property);
	if (bHover || bHoverEndPending) PreviewSize = 0;
	if (bHover) OnUpdateHover(LastHoverRay);
}

void UMD_StainPlacementTool::SetPreviewError(const FString& Error)
{
	bPreviewValid = false;
	bPreviewRejected = true;
	Preview->SetVisibility(false);
	if (Error == PreviewError) return;
	PreviewError = Error;
	GetToolManager()->DisplayMessage(FText::FromString(Error), EToolMessageLevel::UserWarning);
}

FMD_StainPlacementRequest UMD_StainPlacementTool::PreviewRequest() const
{
	return {PreviewTransform, PreviewTargetTransform, PreviewTarget, PreviewSourceMesh, PreviewSourceGuid, TargetWorld->GetCurrentLevel(), PreviewDefinition, PreviewSize, PreviewDefinitionRevision, Properties->bFollowSurface && PreviewDefinition.IsValid() && PreviewDefinition->UsesSharedVolume()};
}

bool UMD_StainPlacementTool::MatchesPreview(const FMD_StainPlacementRequest& Request) const
{
	return Request.bFitSurface == (Properties->bFollowSurface && PreviewDefinition.IsValid() && PreviewDefinition->UsesSharedVolume()) && Request.Definition == PreviewDefinition && Request.DefinitionRevision == PreviewDefinitionRevision && Request.Size == PreviewSize && Request.Surface == PreviewTarget && Request.SourceMesh == PreviewSourceMesh && Request.SourceGuid == PreviewSourceGuid
		&& Request.Transform.Equals(PreviewTransform, 0.01) && Request.SurfaceTransform.Equals(PreviewTargetTransform, 0.01);
}

bool UMD_StainPlacementTool::ValidateRequest(const FMD_StainPlacementRequest& Request, FString& Error) const
{
	UStaticMeshComponent* Surface = Cast<UStaticMeshComponent>(Request.Surface.Get());
	if (!Request.Definition.IsValid() || !Request.Definition->IsUsable()
		|| (!Request.Definition->UsesSharedVolume() && !Request.Definition->StickerMaterial))
	{ Error = TEXT("Select a valid stain definition and its presentation material."); return false; }
	if (Request.DefinitionRevision != Request.Definition->GetAuthoringRevision())
	{ Error = TEXT("The definition changed while this placement was prepared. Place the stamp again."); return false; }
	if (!Surface || !Surface->IsRegistered() || !Surface->GetStaticMesh()
		|| (!Request.Definition->UsesSharedVolume() && !Surface->bReceivesDecals)
		|| Surface->GetCollisionResponseToChannel(ECC_Visibility) != ECR_Block)
	{ Error = TEXT("Place stains on a static mesh with Visibility collision and Receives Decals enabled."); return false; }
	if (Cast<UInstancedStaticMeshComponent>(Surface))
	{ Error = TEXT("Instanced surfaces are not supported yet. Use an individual static mesh for this sticker."); return false; }
	if (!Request.Level.IsValid() || TargetWorld->GetCurrentLevel() != Request.Level.Get() || !TargetWorld->GetLevels().Contains(Request.Level.Get()) || FLevelUtils::IsLevelLocked(Request.Level.Get()))
	{ Error = TEXT("The destination level is locked or unloaded."); return false; }
	if (!Surface->GetOwner() || FLevelUtils::IsLevelLocked(Surface->GetOwner()->GetLevel()))
	{ Error = TEXT("The receiving surface belongs to a locked level."); return false; }
	if (Request.SourceMesh.Get() != Surface->GetStaticMesh() || Request.SourceGuid != Surface->GetStaticMesh()->GetLightingGuid()
		|| !Request.SurfaceTransform.Equals(Surface->GetComponentTransform(), 0.01))
	{ Error = TEXT("The receiving surface moved before placement completed. Place this stamp again."); return false; }
	return true;
}

void UMD_StainPlacementTool::StartBake(const FMD_StainPlacementRequest& Request, bool bForPlacement)
{
	FString Error;
	if (!ValidateRequest(Request, Error))
	{
		if (bForPlacement) ++StrokeFailed;
		if (MatchesPreview(Request)) SetPreviewError(Error);
		else GetToolManager()->DisplayMessage(FText::FromString(Error), EToolMessageLevel::UserWarning);
		return;
	}
	ActiveRequest = Request; bBakeForPlacement = bForPlacement; bDraftAttempted = false;
	PreviewOwner->SetActorTransform(Request.Transform);
	ActiveBake = FMD_StainStickerBaker::StartAsync(*PreviewOwner, (Request.bFitSurface ? FMD_StainSurfaceFit::HalfSize(*Request.Definition, Request.Size) : Request.Definition->GetHalfSize(Request.Size)), Request.Size, SourceCache, Request.bFitSurface ? Request.Definition.Get() : nullptr, !bForPlacement);
	PreviewOwner->SetActorTransform(PreviewTransform);
}

bool UMD_StainPlacementTool::PreparePreview()
{
	if (!bHover || WantsErase()) return false;
	if (bPreviewValid) return true;
	if (!bPreviewPending || ActiveBake || IsPlacementPending()) return false;
	bPreviewPending = false;
	if (PreviewDefinition.IsValid() && PreviewDefinition->UsesSharedVolume() && !Properties->bFollowSurface)
	{
		FString Error;
		const FMD_StainPlacementRequest Request = PreviewRequest();
		FTransform Resolved;
		if (!ValidateRequest(Request, Error) || !ResolveVolumePlacement(TargetWorld, PreviewDefinition.Get(),
			PreviewTarget.Get(), PreviewInputTransform, PreviewSize, Resolved, Error)) { SetPreviewError(Error); return false; }
		PreviewTransform = Resolved;
		bPreviewValid = true; bPreviewRejected = false; PreviewError.Reset();
		ShowMovingPreview();
		GetToolManager()->DisplayMessage(FText::GetEmpty(), EToolMessageLevel::UserWarning);
		return true;
	}
	StartBake(PreviewRequest(), false);
	return false;
}

void UMD_StainPlacementTool::TickBake()
{
	if (ActiveBake && !bDraftAttempted && ActiveRequest.bFitSurface && !bBakeForPlacement
		&& !ActiveBake->IsReady() && !ActiveBake->IsCancelled() && ActiveBake->IsPreviewReady()
		&& bHover && !WantsErase() && MatchesPreview(ActiveRequest))
	{
		bDraftAttempted = true;
		FString Error;
		if (ValidateRequest(ActiveRequest, Error))
			DraftPreview = ActiveBake->FinishPreview(*PreviewOwner, ActiveRequest.Definition->VolumeMaterial);
		if (DraftPreview)
		{
			ShowMovingPreview();
			GetToolManager()->DisplayMessage(LOCTEXT("FitEstimate", "Preview shown; refining the surface. Click to place after refinement."), EToolMessageLevel::UserNotification);
		}
	}
	if (ActiveBake && ActiveBake->IsReady())
	{
		const bool bRelevant = bBakeForPlacement || (bHover && !WantsErase() && MatchesPreview(ActiveRequest));
		if (bRelevant && !ActiveBake->IsCancelled())
		{
			FMD_StainStickerPatch Patch; UStaticMesh* Mesh = nullptr; FString Error;
			if (ActiveRequest.bFitSurface)
			{
				UMD_SurfaceStainDefinition* Fit = nullptr;
				if (ValidateRequest(ActiveRequest, Error))
					Fit = ActiveBake->FinishVolume(*PreviewOwner, *ActiveRequest.Definition, Error);
				if (Fit)
				{
					if (bBakeForPlacement) CommitFit(ActiveRequest, Fit);
					else
					{
						PreparedFit = Fit; PreparedSurface = ActiveBake; DraftPreview = nullptr;
						bPreviewValid = true; bPreviewRejected = false; PreviewError.Reset(); ShowMovingPreview();
						GetToolManager()->DisplayMessage(FText::GetEmpty(), EToolMessageLevel::UserWarning);
					}
				}
				else { if (bBakeForPlacement) ++StrokeFailed; SetPreviewError(Error); }
			}
			else if (ValidateRequest(ActiveRequest, Error) && ActiveBake->Finish(*PreviewOwner,
				ActiveRequest.Definition->StickerMaterial, Patch, Mesh, Error))
			{
				if (bBakeForPlacement) CommitPrepared(ActiveRequest, MoveTemp(Patch), Mesh);
				else
				{
					PreviewPatch = MoveTemp(Patch);
					PreviewOwner->SetActorTransform(PreviewTransform);
					Preview->SetRelativeTransform(FTransform::Identity);
					Preview->SetStaticMesh(Mesh); Preview->SetVisibility(true);
					bPreviewValid = true; bPreviewRejected = false;
					PreviewError.Reset();
					GetToolManager()->DisplayMessage(FText::GetEmpty(), EToolMessageLevel::UserWarning);
				}
			}
			else if (!Error.IsEmpty())
			{
				if (bBakeForPlacement) ++StrokeFailed;
				if (MatchesPreview(ActiveRequest)) SetPreviewError(Error);
				else GetToolManager()->DisplayMessage(FText::FromString(Error), EToolMessageLevel::UserWarning);
			}
		}
		ActiveBake.Reset();
	}
	if (!ActiveBake && !PendingPlacements.IsEmpty())
	{
		const FMD_StainPlacementRequest Request = PendingPlacements[0];
		PendingPlacements.RemoveAt(0, 1, EAllowShrinking::No);
		StartBake(Request, true);
	}
	const int32 Pending = PendingPlacements.Num() + (ActiveBake && bBakeForPlacement ? 1 : 0);
	if (Pending != LastPendingCount)
	{
		LastPendingCount = Pending;
		GetToolManager()->DisplayMessage(Pending > 0
			? FText::Format(LOCTEXT("PlacingProgress", "Placing stains: {0} remaining. Switch tools to cancel the remaining stamps."), FText::AsNumber(Pending))
			: FText::Format(LOCTEXT("PlacementDone", "Placed {0} stains; {1} positions could not be placed. Yellow checks the surface; green is ready; red is unavailable."), FText::AsNumber(StrokePlaced), FText::AsNumber(StrokeFailed)), EToolMessageLevel::UserNotification);
	}
	if (!bStroke && !IsPlacementPending() && bTransaction) FinishStroke();
}

void UMD_StainPlacementTool::OnTick(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_Placement_StainTick);
	Super::OnTick(DeltaTime);
	if (bCompletingBake) return;
	TGuardValue<bool> CompletionGuard(bCompletingBake, true);
	if (bHoverEndPending)
	{
		// No same-event click/hover claimed the result: this was a genuine hover exit.
		bHoverEndPending = false; bPreviewWasVisible = false;
		bPreviewPending = false; bPreviewValid = false; PreviewSize = 0;
		DraftPreview = nullptr; PreparedFit = nullptr; PreparedSurface.Reset();
		PreviewPatch = FMD_StainStickerPatch();
		if (Preview) Preview->SetStaticMesh(nullptr);
		if (ActiveBake && !bBakeForPlacement) ActiveBake->Cancel();
	}
	// Definition edits/Undo and target movement invalidate settled previews without rebuilding geometry per frame.
	if (bHover && !WantsErase())
	{
		UMD_SurfaceStainDefinition* CurrentDefinition = ResolveDefinition();
		const UStaticMeshComponent* CurrentSurface = Cast<UStaticMeshComponent>(PreviewTarget.Get());
		const bool bDefinitionChanged = CurrentDefinition != PreviewDefinition
			|| (CurrentDefinition && CurrentDefinition->GetAuthoringRevision() != PreviewDefinitionRevision);
		const bool bSurfaceChanged = CurrentDefinition && (!CurrentSurface
			|| CurrentSurface->GetStaticMesh() != PreviewSourceMesh
			|| !PreviewTargetTransform.Equals(CurrentSurface->GetComponentTransform(), 0.01)
			|| (PreviewSourceMesh.IsValid() && PreviewSourceGuid != PreviewSourceMesh->GetLightingGuid()));
		if (bDefinitionChanged || bSurfaceChanged || (PreparedSurface && !PreparedSurface->SourcesAreCurrent())
			|| (DraftPreview && ActiveBake && !ActiveBake->SourcesAreCurrent()))
		{ PreviewSize = 0; OnUpdateHover(LastHoverRay); }
	}
	if (bStroke && bDragPending) { bDragPending = false; ApplyAt(PendingDragRay); }
	TickBake();
	// Shared shapes only need a bounded contact check; keep their settled preview within 100 ms.
	const double PreviewDelay = Properties->bFollowSurface ? 0.06 : 0.05;
	if (!bStroke && !ActiveBake && bPreviewPending && bHover && !WantsErase() && FPlatformTime::Seconds() - PreviewChangedAt >= PreviewDelay)
		PreparePreview();
}

void UMD_StainPlacementTool::OnEndHover()
{
	// InputRouter ends hover immediately before delivering a click. Keep the result
	// until the next tick so that click can validate and consume it without rebaking.
	if (!bHoverEndPending) bPreviewWasVisible = Preview && Preview->IsVisible();
	bHover = false; bHoverEndPending = true;
	if (Preview) Preview->SetVisibility(false);
}

void UMD_StainPlacementTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	Super::Render(RenderAPI);
	if (!bHover) return;
	const FLinearColor Color = WantsErase() || !ResolveDefinition() || bPreviewRejected ? FLinearColor::Red
		: bPreviewValid ? FLinearColor::Green : FLinearColor::Yellow;
	RenderAPI->GetPrimitiveDrawInterface()->DrawPoint(HoverHit.ImpactPoint + HoverHit.ImpactNormal, Color, 10.0f, SDPG_Foreground);
}

FInputRayHit UMD_StainPlacementTool::ShouldRespondToMouseWheel(const FInputDeviceRay& Ray) { return bStroke ? FInputRayHit() : FInputRayHit(0.0); }
void UMD_StainPlacementTool::OnMouseWheelScrollUp(const FInputDeviceRay& Ray) { ChangeSpacing(5.0f); }
void UMD_StainPlacementTool::OnMouseWheelScrollDown(const FInputDeviceRay& Ray) { ChangeSpacing(-5.0f); }
void UMD_StainPlacementTool::ChangeSpacing(const float Delta)
{
	Properties->SpacingCm = FMath::Max(1.0f, Properties->SpacingCm + Delta);
	NotifyOfPropertyChangeByTool(Properties);
}

#undef LOCTEXT_NAMESPACE
