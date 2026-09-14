#include "Tools/MD_StainPlacementTool.h"

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
	if (Properties->Palette.IsEmpty())
	{
		const TCHAR* Kinds[] = { TEXT("Puddle"), TEXT("Droplets"), TEXT("Splatter"), TEXT("Smear"),
			TEXT("Ring"), TEXT("DragMark"), TEXT("Handprint"), TEXT("Speckles") };
		for (const TCHAR* Kind : Kinds)
		{
			const FString Path = FString::Printf(TEXT("/Game/Contamination/SurfaceStains/DA_MD_Stain_%s.DA_MD_Stain_%s"), Kind, Kind);
			if (UMD_SurfaceStainDefinition* Definition = LoadObject<UMD_SurfaceStainDefinition>(nullptr, *Path))
				Properties->Palette.Add(Definition);
		}
	}
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
	PreviewPlane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
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
	TArray<FHitResult> Hits;
	TargetWorld->LineTraceMultiByChannel(Hits, Start, End, ECC_Visibility, Params);
	const FHitResult* Surface = Hits.FindByPredicate([](const FHitResult& Result) { return Result.bBlockingHit; });
	if (!Surface) return false;
	for (const FHitResult& Candidate : Hits)
	{
		if (Candidate.bBlockingHit) break;
		const AMD_SurfaceStain* Stain = Cast<AMD_SurfaceStain>(Candidate.GetActor());
		if (Stain && Stain->ContainsSurfacePoint(Surface->ImpactPoint, Surface->ImpactNormal))
		{ Hit = Candidate; return true; }
	}
	return false;
}

UMD_SurfaceStainDefinition* UMD_StainPlacementTool::ResolveDefinition() const
{
	if (!Properties || Properties->Palette.IsEmpty()) return nullptr;
	FRandomStream Random(HashCombine(GetTypeHash(Properties->RandomSeed), GetTypeHash(PlacementIndex)));
	const int32 Index = Properties->bRandomVariant ? Random.RandRange(0, Properties->Palette.Num() - 1) : Properties->VariantIndex;
	UMD_SurfaceStainDefinition* Definition = Properties->Palette.IsValidIndex(Index) ? Properties->Palette[Index] : nullptr;
	return IsValid(Definition) && Definition->IsUsable() && Definition->StickerMaterial ? Definition : nullptr;
}

int32 UMD_StainPlacementTool::ResolveSize() const
{
	const int32 Seed = HashCombine(GetTypeHash(Properties->RandomSeed), GetTypeHash(PlacementIndex) ^ 0x57A1u);
	return Properties->bRandomSize
		? FMD_SurfaceStainSize::RandomInitial(Properties->MinSize, Properties->MaxSize, Seed)
		: FMD_SurfaceStainSize::ClampInitial(Properties->InitialSize);
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
		GetToolManager()->DisplayMessage(LOCTEXT("MissingDefinition", "Select a valid stain definition with a sticker material and slime item class."), EToolMessageLevel::UserWarning);
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
	const FMD_StainPlacementRequest Request = PreviewRequest();
	FString Error;
	if (!ValidateRequest(Request, Error)) { ++StrokeFailed; bPreviewPending = false; SetPreviewError(Error); return; }
	if (bPreviewRejected) { ++StrokeFailed; return; }
	if (bPreviewValid)
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

void UMD_StainPlacementTool::CommitPrepared(const FMD_StainPlacementRequest& Request, FMD_StainStickerPatch Patch, UStaticMesh* Mesh)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_Placement_StainCommit);
	FString Error;
	if (!ValidateRequest(Request, Error)) { SetPreviewError(Error); return; }
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
	LastHoverRay = Ray;
	bHover = Trace(Ray, HoverHit, WantsErase());
	UMD_SurfaceStainDefinition* Definition = ResolveDefinition();
	const bool bShow = bHover && Definition && !WantsErase();
	if (!bShow)
	{
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
		if (Definition != PreviewDefinition || Size != PreviewSize || SourceMesh != PreviewSourceMesh || SourceGuid != PreviewSourceGuid || !Transform.Equals(PreviewTransform, 0.01)
			|| PreviewTarget != HoverHit.GetComponent() || !PreviewTargetTransform.Equals(HoverHit.GetComponent()->GetComponentTransform(), 0.01))
		{
			PreviewSourceMesh = SourceMesh; PreviewSourceGuid = SourceGuid;
			PreviewTarget = HoverHit.GetComponent();
			PreviewTargetTransform = HoverHit.GetComponent()->GetComponentTransform();
			PreviewTransform = Transform;
			PreviewDefinition = Definition;
			PreviewSize = Size;
			PreviewChangedAt = FPlatformTime::Seconds();
			bPreviewPending = true;
			bPreviewValid = false;
			bPreviewRejected = false;
			if (ActiveBake && !bBakeForPlacement) ActiveBake->Cancel();
			PreviewPatch = FMD_StainStickerPatch();
			ShowMovingPreview();
		}
	}
	return true;
}

void UMD_StainPlacementTool::ShowMovingPreview()
{
	// Shared quad follows the cursor immediately; expensive surface unfolding runs only after settling.
	PreviewOwner->SetActorTransform(PreviewTransform);
	const FVector2D HalfSize = PreviewDefinition->GetHalfSize(PreviewSize);
	Preview->SetStaticMesh(PreviewPlane);
	Preview->SetMaterial(0, PreviewDefinition->StickerMaterial);
	Preview->SetRelativeTransform(FTransform(FQuat::Identity, FVector(0, 0, 0.1),
		FVector(HalfSize.X / 50.0, HalfSize.Y / 50.0, 1.0)));
	Preview->SetVisibility(true);
}

void UMD_StainPlacementTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	Super::OnPropertyModified(PropertySet, Property);
	if (bHover)
	{
		PreviewSize = 0;
		OnUpdateHover(LastHoverRay);
	}
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
	return {PreviewTransform, PreviewTargetTransform, PreviewTarget, PreviewSourceMesh, PreviewSourceGuid, TargetWorld->GetCurrentLevel(), PreviewDefinition, PreviewSize};
}

bool UMD_StainPlacementTool::MatchesPreview(const FMD_StainPlacementRequest& Request) const
{
	return Request.Definition == PreviewDefinition && Request.Size == PreviewSize && Request.Surface == PreviewTarget && Request.SourceMesh == PreviewSourceMesh && Request.SourceGuid == PreviewSourceGuid
		&& Request.Transform.Equals(PreviewTransform, 0.01) && Request.SurfaceTransform.Equals(PreviewTargetTransform, 0.01);
}

bool UMD_StainPlacementTool::ValidateRequest(const FMD_StainPlacementRequest& Request, FString& Error) const
{
	UStaticMeshComponent* Surface = Cast<UStaticMeshComponent>(Request.Surface.Get());
	if (!Request.Definition.IsValid() || !Request.Definition->StickerMaterial)
	{ Error = TEXT("Select a valid stain definition and sticker material."); return false; }
	if (!Surface || !Surface->IsRegistered() || !Surface->GetStaticMesh() || !Surface->bReceivesDecals
		|| Surface->GetCollisionResponseToChannel(ECC_Visibility) != ECR_Block)
	{ Error = TEXT("Place stains on a static mesh with Visibility collision and Receives Decals enabled."); return false; }
	if (Cast<UInstancedStaticMeshComponent>(Surface))
	{ Error = TEXT("Instanced surfaces are not supported yet. Use an individual static mesh for this sticker."); return false; }
	if (!Request.Level.IsValid() || !TargetWorld->GetLevels().Contains(Request.Level.Get()) || FLevelUtils::IsLevelLocked(Request.Level.Get()))
	{ Error = TEXT("The destination level is locked or unloaded."); return false; }
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
	ActiveRequest = Request; bBakeForPlacement = bForPlacement;
	PreviewOwner->SetActorTransform(Request.Transform);
	ActiveBake = FMD_StainStickerBaker::StartAsync(*PreviewOwner, Request.Definition->GetHalfSize(Request.Size), Request.Size, SourceCache);
	PreviewOwner->SetActorTransform(PreviewTransform);
}

bool UMD_StainPlacementTool::PreparePreview()
{
	if (!bHover || WantsErase()) return false;
	if (bPreviewValid) return true;
	if (!bPreviewPending || ActiveBake || IsPlacementPending()) return false;
	bPreviewPending = false;
	StartBake(PreviewRequest(), false);
	return false;
}

void UMD_StainPlacementTool::TickBake()
{
	if (ActiveBake && ActiveBake->IsReady())
	{
		const bool bRelevant = bBakeForPlacement || (bHover && !WantsErase() && MatchesPreview(ActiveRequest));
		if (bRelevant)
		{
			FMD_StainStickerPatch Patch; UStaticMesh* Mesh = nullptr; FString Error;
			if (ValidateRequest(ActiveRequest, Error) && ActiveBake->Finish(*PreviewOwner,
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
	Super::OnTick(DeltaTime);
	if (bStroke && bDragPending) { bDragPending = false; ApplyAt(PendingDragRay); }
	TickBake();
	if (!bStroke && !ActiveBake && bPreviewPending && bHover && !WantsErase() && FPlatformTime::Seconds() - PreviewChangedAt >= 0.15)
		PreparePreview();
}

void UMD_StainPlacementTool::OnEndHover()
{
	bHover = false; bPreviewPending = false; bPreviewValid = false; PreviewSize = 0;
	if (Preview) Preview->SetVisibility(false);
	if (ActiveBake && !bBakeForPlacement) ActiveBake->Cancel();
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
