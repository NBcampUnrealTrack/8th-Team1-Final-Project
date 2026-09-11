#include "Tools/MD_StainPlacementTool.h"

#include "BaseBehaviors/ClickDragBehavior.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "BaseBehaviors/MouseWheelBehavior.h"
#include "Components/StaticMeshComponent.h"
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
			&& Hit.GetComponent() && Hit.GetComponent()->bReceivesDecals;
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
	return Trace(Ray, Hit, WantsErase()) ? FInputRayHit(Hit.Distance) : FInputRayHit();
}

void UMD_StainPlacementTool::OnClickPress(const FInputDeviceRay& Ray)
{
	FinishStroke();
	bStrokeErase = WantsErase();
	if (!bStrokeErase && !ResolveDefinition())
	{
		GetToolManager()->DisplayMessage(LOCTEXT("MissingDefinition", "Select a valid stain definition with a sticker material and slime item class."), EToolMessageLevel::UserWarning);
		return;
	}
	bStroke = true;
	bPlaced = false;
	GetToolManager()->BeginUndoTransaction(bStrokeErase ? LOCTEXT("Erase", "Erase Surface Stains") : LOCTEXT("Place", "Place Surface Stains"));
	ApplyAt(Ray);
}

void UMD_StainPlacementTool::ApplyAt(const FInputDeviceRay& Ray)
{
	FHitResult Hit;
	if (!Trace(Ray, Hit, bStrokeErase)) return;
	if (bStrokeErase)
	{
		AMD_SurfaceStain* Stain = CastChecked<AMD_SurfaceStain>(Hit.GetActor());
		Stain->Modify();
		if (GEditor && Stain->IsSelected()) GEditor->SelectActor(Stain, false, true);
		TargetWorld->EditorDestroyActor(Stain, true);
		return;
	}
	UMD_SurfaceStainDefinition* Definition = ResolveDefinition();
	if (!Definition || !GEditor || !TargetWorld->GetCurrentLevel()) return;
	const float Spacing = FMath::Max(Properties->SpacingCm, 1.0f);
	if (bPlaced && FVector::DistSquared(LastPlacedLocation, Hit.ImpactPoint) < FMath::Square(Spacing)) return;
	AMD_SurfaceStain* Stain = Cast<AMD_SurfaceStain>(GEditor->AddActor(TargetWorld->GetCurrentLevel(),
		AMD_SurfaceStain::StaticClass(), MakeTransform(Hit), false, RF_Transactional, true));
	if (!Stain) return;
	Stain->Modify();
	Stain->ConfigurePlacement(Definition, ResolveSize());
	Hit.GetComponent()->Modify();
	Stain->AttachToComponent(Hit.GetComponent(), FAttachmentTransformRules::KeepWorldTransform);
	if (!Stain->RebuildSticker())
	{
		GetToolManager()->DisplayMessage(FText::FromString(Stain->GetStickerStatus()), EToolMessageLevel::UserWarning);
		TargetWorld->EditorDestroyActor(Stain, true);
		return;
	}
	Stain->MarkPackageDirty();
	LastPlacedLocation = Hit.ImpactPoint;
	bPlaced = true;
	++PlacementIndex;
}

void UMD_StainPlacementTool::OnClickDrag(const FInputDeviceRay& Ray) { if (bStroke) ApplyAt(Ray); OnUpdateHover(Ray); }
void UMD_StainPlacementTool::OnClickRelease(const FInputDeviceRay& Ray) { FinishStroke(); OnUpdateHover(Ray); }
void UMD_StainPlacementTool::OnTerminateDragSequence() { FinishStroke(); }
void UMD_StainPlacementTool::FinishStroke() { if (bStroke) GetToolManager()->EndUndoTransaction(); bStroke = false; }

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
		PreviewSize = 0;
	}
	else
	{
		const FTransform Transform = MakeTransform(HoverHit);
		const int32 Size = ResolveSize();
		if (Definition != PreviewDefinition || Size != PreviewSize || !Transform.Equals(PreviewTransform, 0.01))
		{
			PreviewTransform = Transform;
			PreviewDefinition = Definition;
			PreviewSize = Size;
			PreviewChangedAt = FPlatformTime::Seconds();
			bPreviewPending = true;
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

void UMD_StainPlacementTool::OnTick(float DeltaTime)
{
	Super::OnTick(DeltaTime);
	// Bake once after the cursor settles, never during every mouse-move or gameplay frame.
	if (!bPreviewPending || !bHover || WantsErase() || FPlatformTime::Seconds() - PreviewChangedAt < 0.15) return;
	bPreviewPending = false;
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_Placement_StainPreviewBake);
	UMD_SurfaceStainDefinition* Definition = PreviewDefinition.Get();
	if (!Definition || !PreviewOwner) return;
	PreviewOwner->SetActorTransform(PreviewTransform);
	FMD_StainStickerPatch Patch;
	UStaticMesh* Mesh = nullptr;
	FString Error;
	if (FMD_StainStickerBaker::Build(*PreviewOwner, Definition->GetHalfSize(PreviewSize), PreviewSize,
		Definition->StickerMaterial, Patch, Mesh, Error))
	{
		Preview->SetRelativeTransform(FTransform::Identity);
		Preview->SetStaticMesh(Mesh);
		Preview->SetVisibility(true);
		PreviewError.Reset();
	}
	else if (Error != PreviewError)
	{
		PreviewError = Error;
		GetToolManager()->DisplayMessage(FText::FromString(Error), EToolMessageLevel::UserWarning);
	}
}

void UMD_StainPlacementTool::OnEndHover()
{
	bHover = false; bPreviewPending = false; PreviewSize = 0;
	if (Preview) Preview->SetVisibility(false);
}

void UMD_StainPlacementTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	Super::Render(RenderAPI);
	if (!bHover) return;
	const FLinearColor Color = WantsErase() || !ResolveDefinition() ? FLinearColor::Red : FLinearColor::Green;
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
