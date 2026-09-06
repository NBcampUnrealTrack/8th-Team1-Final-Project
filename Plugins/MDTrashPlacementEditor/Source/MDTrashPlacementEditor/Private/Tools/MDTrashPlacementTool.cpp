#include "Tools/MDTrashPlacementTool.h"

#include "BaseBehaviors/ClickDragBehavior.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "BaseBehaviors/MouseWheelBehavior.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/HitResult.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Gameplay/Trash/MD_TrashLibrary.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "InteractiveToolManager.h"
#include "Math/RandomStream.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "PrimitiveDrawingUtils.h"
#include "Templates/TypeHash.h"
#include "ToolContextInterfaces.h"
#include "UObject/ConstructorHelpers.h"
#include "World/Trash/MD_TrashItem.h"

#define LOCTEXT_NAMESPACE "MDTrashPlacementTool"

namespace
{
	constexpr double PlacementTraceDistance = 10000000.0;
	constexpr float HoverNormalLength = 25.0f;
	constexpr float HoverPointSize = 12.0f;
	constexpr TCHAR DefaultTrashItemClassPath[] = TEXT("/Game/Blueprint/Resource/Trash/TrashItem/BP_TrashItemBase");
	constexpr TCHAR GhostMaterialPath[] = TEXT("/Engine/EditorLandscapeResources/FoliageBrushSphereMaterial.FoliageBrushSphereMaterial");
	const FName GhostColorParameterName(TEXT("HighlightColor"));
	const FName GhostOpacityParameterName(TEXT("OpacityAmount"));
	const FLinearColor ValidGhostColor(0.05f, 1.0f, 0.1f, 1.0f);
	const FLinearColor InvalidGhostColor(1.0f, 0.05f, 0.05f, 1.0f);
	constexpr uint32 TrashKindSeedSalt = 0x6D445450u;
	constexpr int32 PlacementSpacingSolveIterations = 8;
	constexpr double PlacementSpacingSolveTolerance = 0.1;

	FVector GetSafeSurfaceNormal(const FHitResult& HitResult)
	{
		const FVector SurfaceNormal = HitResult.ImpactNormal.GetSafeNormal();
		return SurfaceNormal.IsNearlyZero() ? FVector::UpVector : SurfaceNormal;
	}
}

bool UMDTrashPlacementToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return SceneState.World != nullptr;
}

UInteractiveTool* UMDTrashPlacementToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UMDTrashPlacementTool* NewTool = NewObject<UMDTrashPlacementTool>(SceneState.ToolManager);
	NewTool->SetTargetWorld(SceneState.World);
	return NewTool;
}

UMDTrashPlacementToolProperties::UMDTrashPlacementToolProperties()
{
	static ConstructorHelpers::FClassFinder<AMD_TrashItem> TrashItemClassFinder(DefaultTrashItemClassPath);
	TrashItemClass = TrashItemClassFinder.Class;
	RandomCategoryTags.Add(MDGameplayTags::Score_Sorting);
}

TArray<FName> UMDTrashPlacementToolProperties::GetTrashKindIdOptions() const
{
	return UMD_TrashLibrary::GetTrashKindIds();
}

void UMDTrashPlacementTool::SetTargetWorld(UWorld* InTargetWorld)
{
	TargetWorld = InTargetWorld;
}

void UMDTrashPlacementTool::Setup()
{
	UInteractiveTool::Setup();

	Properties = NewObject<UMDTrashPlacementToolProperties>(this);
	Properties->RestoreProperties(this);
	AddToolPropertySource(Properties);
	SetupGhostPreview();

	UClickDragInputBehavior* ClickDragBehavior = NewObject<UClickDragInputBehavior>(this);
	ClickDragBehavior->Initialize(this);
	ClickDragBehavior->Modifiers.RegisterModifier(ShiftEraseModifierId, FInputDeviceState::IsShiftKeyDown);
	ClickDragBehavior->bUpdateModifiersDuringDrag = false;
	AddInputBehavior(ClickDragBehavior);

	UMouseHoverBehavior* HoverBehavior = NewObject<UMouseHoverBehavior>(this);
	HoverBehavior->Initialize(this);
	HoverBehavior->Modifiers.RegisterModifier(ShiftEraseModifierId, FInputDeviceState::IsShiftKeyDown);
	AddInputBehavior(HoverBehavior);

	UMouseWheelInputBehavior* MouseWheelBehavior = NewObject<UMouseWheelInputBehavior>(this);
	MouseWheelBehavior->Initialize(this);
	AddInputBehavior(MouseWheelBehavior);

	GetToolManager()->DisplayMessage(
		LOCTEXT(
			"TaskTwelveInstructions",
			"Use the mouse wheel to adjust Placement Spacing. Hold Shift before clicking or dragging for a temporary Erase stroke."),
		EToolMessageLevel::UserNotification);
}

void UMDTrashPlacementTool::OnUpdateModifierState(int ModifierID, bool bIsOn)
{
	if (ModifierID != ShiftEraseModifierId)
	{
		return;
	}

	bShiftEraseActive = bIsOn;
	if (IsEraseMode())
	{
		HideGhostPreview();
	}
}

FInputRayHit UMDTrashPlacementTool::ShouldRespondToMouseWheel(const FInputDeviceRay& CurrentPos)
{
	return IsValid(Properties) && !bIsStrokeActive ? FInputRayHit(0.0) : FInputRayHit();
}

void UMDTrashPlacementTool::OnMouseWheelScrollUp(const FInputDeviceRay& CurrentPos)
{
	AdjustPlacementSpacing(1);
}

void UMDTrashPlacementTool::OnMouseWheelScrollDown(const FInputDeviceRay& CurrentPos)
{
	AdjustPlacementSpacing(-1);
}

void UMDTrashPlacementTool::Shutdown(EToolShutdownType ShutdownType)
{
	if (IsValid(Properties))
	{
		Properties->SaveProperties(this);
	}

	if (bIsStrokeActive || bHasActiveUndoTransaction)
	{
		FinishStroke();
	}
	DestroyGhostPreview();

	UInteractiveTool::Shutdown(ShutdownType);
}

void UMDTrashPlacementTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	UInteractiveTool::Render(RenderAPI);

	if (!bHasHoverHit)
	{
		return;
	}

	FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
	const FVector DrawLocation = HoverLocation + HoverNormal;
	const FLinearColor HoverColor = IsEraseMode() || !bIsHoverPlacementValid
		? FLinearColor::Red
		: FLinearColor::Green;
	PDI->DrawPoint(DrawLocation, HoverColor, HoverPointSize, SDPG_Foreground);
	PDI->DrawLine(
		DrawLocation,
		DrawLocation + HoverNormal * HoverNormalLength,
		HoverColor,
		SDPG_Foreground,
		2.0f);
}

FInputRayHit UMDTrashPlacementTool::CanBeginClickDragSequence(const FInputDeviceRay& PressPos)
{
	FHitResult HitResult;
	if (IsEraseMode())
	{
		AMD_TrashItem* TrashItem = nullptr;
		return TraceTrash(PressPos, TrashItem, HitResult)
			? FInputRayHit(HitResult.Distance)
			: FInputRayHit();
	}

	return TraceWorld(PressPos, HitResult)
		? FInputRayHit(HitResult.Distance)
		: FInputRayHit();
}

void UMDTrashPlacementTool::OnClickPress(const FInputDeviceRay& PressPos)
{
	if (bIsStrokeActive || bHasActiveUndoTransaction)
	{
		FinishStroke();
	}

	bIsStrokeActive = false;
	bHasPreviousSurfaceHit = false;
	StrokePlacedTrashItems.Reset();
	LastPlacementMesh = nullptr;
	LastPlacementTransform = FTransform::Identity;
	LastPlacementSurfaceLocation = FVector::ZeroVector;
	DistanceSinceLastPlacement = 0.0;
	StrokePlacementCount = 0;
	StrokeEraseCount = 0;

	if (!IsValid(TargetWorld) || !IsValid(Properties))
	{
		DisplayWarning(LOCTEXT("InvalidToolState", "Trash Placement is not connected to a valid editor world."));
		return;
	}

	ActiveStrokeMode = ResolveRequestedMode(bShiftEraseActive);
	if (ActiveStrokeMode == EMDTrashPlacementToolMode::Erase)
	{
		AMD_TrashItem* TrashItem = nullptr;
		FHitResult TrashHit;
		if (!TraceTrash(PressPos, TrashItem, TrashHit))
		{
			return;
		}

		BeginStrokeTransaction();
		bIsStrokeActive = true;
		UpdateHoverFromHit(TrashHit);
		if (!EraseTrashItem(TrashItem))
		{
			CancelStroke();
			return;
		}

		bHasHoverHit = false;
		return;
	}

	if (!ValidatePlacementSettings())
	{
		return;
	}

	FHitResult HitResult;
	if (!TraceWorld(PressPos, HitResult))
	{
		DisplayWarning(LOCTEXT("NoPlacementSurface", "No placement surface was found under the cursor."));
		return;
	}

	const FName PlacementTrashKindId = ResolveTrashKindId(HitResult);
	UpdateHoverFromHit(HitResult);
	if (!IsPlacementAllowedAtHit(HitResult, PlacementTrashKindId))
	{
		DisplayWarning(
			LOCTEXT(
				"UnsafePlacement",
				"Placement is blocked because the preview mesh is missing or overlaps another object."));
		return;
	}

	BeginStrokeTransaction();
	bIsStrokeActive = true;
	if (!PlaceTrashAtHit(HitResult, PlacementTrashKindId))
	{
		CancelStroke();
		return;
	}

	PreviousSurfaceLocation = HitResult.ImpactPoint;
	PreviousSurfaceNormal = GetSafeSurfaceNormal(HitResult);
	DistanceSinceLastPlacement = 0.0;
	bHasPreviousSurfaceHit = true;
}

void UMDTrashPlacementTool::OnClickDrag(const FInputDeviceRay& DragPos)
{
	if (!bIsStrokeActive || !IsValid(Properties))
	{
		return;
	}

	if (ActiveStrokeMode == EMDTrashPlacementToolMode::Erase)
	{
		AMD_TrashItem* TrashItem = nullptr;
		FHitResult TrashHit;
		bHasHoverHit = TraceTrash(DragPos, TrashItem, TrashHit);
		if (bHasHoverHit)
		{
			UpdateHoverFromHit(TrashHit);
			if (EraseTrashItem(TrashItem))
			{
				bHasHoverHit = false;
			}
		}
		return;
	}

	const double PlacementSpacing = FMath::Max(Properties->PlacementSpacing, 1.0);
	FHitResult CurrentHit;
	if (!TraceWorld(DragPos, CurrentHit))
	{
		bHasHoverHit = false;
		HideGhostPreview();
		bHasPreviousSurfaceHit = false;
		DistanceSinceLastPlacement = 0.0;
		return;
	}

	UpdateHoverFromHit(CurrentHit);
	const FVector CurrentSurfaceLocation = CurrentHit.ImpactPoint;
	const FVector CurrentSurfaceNormal = GetSafeSurfaceNormal(CurrentHit);

	if (!bHasPreviousSurfaceHit)
	{
		PreviousSurfaceLocation = CurrentSurfaceLocation;
		PreviousSurfaceNormal = CurrentSurfaceNormal;
		DistanceSinceLastPlacement = 0.0;
		bHasPreviousSurfaceHit = true;
		return;
	}

	const FVector SurfaceSegment = CurrentSurfaceLocation - PreviousSurfaceLocation;
	const double SegmentLength = SurfaceSegment.Length();
	if (SegmentLength > UE_KINDA_SMALL_NUMBER && IsValid(LastPlacementMesh))
	{
		const FVector SegmentDirection = SurfaceSegment / SegmentLength;
		auto BuildSurfaceHit = [&](const double DistanceAlongSegment)
		{
			const double SegmentAlpha = FMath::Clamp(DistanceAlongSegment / SegmentLength, 0.0, 1.0);
			FHitResult PlacementHit = CurrentHit;
			PlacementHit.ImpactPoint = FMath::Lerp(PreviousSurfaceLocation, CurrentSurfaceLocation, SegmentAlpha);
			PlacementHit.Location = PlacementHit.ImpactPoint;
			PlacementHit.ImpactNormal = FMath::Lerp(PreviousSurfaceNormal, CurrentSurfaceNormal, SegmentAlpha).GetSafeNormal();
			if (PlacementHit.ImpactNormal.IsNearlyZero())
			{
				PlacementHit.ImpactNormal = CurrentSurfaceNormal;
			}
			PlacementHit.Normal = PlacementHit.ImpactNormal;
			return PlacementHit;
		};

		double CursorDistance = 0.0;
		double AccumulatedDistance = DistanceSinceLastPlacement;
		while (bIsStrokeActive && CursorDistance <= SegmentLength + UE_KINDA_SMALL_NUMBER)
		{
			const double PreviousForwardExtent = CalculatePlacementExtent(
				LastPlacementMesh,
				LastPlacementTransform,
				LastPlacementSurfaceLocation,
				SegmentDirection);
			double CandidateOffset = FMath::Max(
				PreviousForwardExtent + PlacementSpacing - AccumulatedDistance,
				0.0);
			if (CursorDistance + CandidateOffset > SegmentLength + UE_KINDA_SMALL_NUMBER)
			{
				break;
			}

			FHitResult PlacementHit = BuildSurfaceHit(CursorDistance + CandidateOffset);
			const FName PlacementTrashKindId = ResolveTrashKindId(PlacementHit);
			UStaticMesh* PlacementMesh = ResolveTrashMesh(PlacementTrashKindId);
			if (!IsValid(PlacementMesh))
			{
				DisplayWarning(LOCTEXT("MissingPreviewMesh", "The selected Trash Kind has no mesh for placement."));
				bIsStrokeActive = false;
				break;
			}

			bool bCandidateFitsSegment = true;
			bool bSpacingSolved = false;
			for (int32 SolveIteration = 0; SolveIteration < PlacementSpacingSolveIterations; ++SolveIteration)
			{
				if (CursorDistance + CandidateOffset > SegmentLength + UE_KINDA_SMALL_NUMBER)
				{
					bCandidateFitsSegment = false;
					break;
				}

				PlacementHit = BuildSurfaceHit(CursorDistance + CandidateOffset);
				const FTransform PlacementTransform = BuildPlacementTransform(PlacementHit, PlacementMesh);
				const double CandidateBackwardExtent = CalculatePlacementExtent(
					PlacementMesh,
					PlacementTransform,
					PlacementHit.ImpactPoint,
					-SegmentDirection);
				const double RequiredOffset = FMath::Max(
					PreviousForwardExtent + PlacementSpacing + CandidateBackwardExtent - AccumulatedDistance,
					0.0);
				if (RequiredOffset <= CandidateOffset + PlacementSpacingSolveTolerance)
				{
					bSpacingSolved = true;
					break;
				}

				CandidateOffset = RequiredOffset;
			}

			if (!bCandidateFitsSegment
				|| !bSpacingSolved
				|| CursorDistance + CandidateOffset > SegmentLength + UE_KINDA_SMALL_NUMBER)
			{
				break;
			}

			PlacementHit = BuildSurfaceHit(CursorDistance + CandidateOffset);
			if (IsPlacementAllowedAtHit(PlacementHit, PlacementTrashKindId))
			{
				if (!PlaceTrashAtHit(PlacementHit, PlacementTrashKindId))
				{
					bIsStrokeActive = false;
					break;
				}

				CursorDistance += CandidateOffset;
				AccumulatedDistance = 0.0;
			}
			else
			{
				const double RemainingDistance = SegmentLength - CursorDistance;
				const double SkipDistance = FMath::Min(
					CandidateOffset + PlacementSpacing,
					RemainingDistance);
				if (SkipDistance <= UE_KINDA_SMALL_NUMBER)
				{
					break;
				}

				CursorDistance += SkipDistance;
				AccumulatedDistance += SkipDistance;
			}
		}

		if (bIsStrokeActive)
		{
			DistanceSinceLastPlacement =
				AccumulatedDistance + FMath::Max(SegmentLength - CursorDistance, 0.0);
		}
	}

	PreviousSurfaceLocation = CurrentSurfaceLocation;
	PreviousSurfaceNormal = CurrentSurfaceNormal;
}

void UMDTrashPlacementTool::OnClickRelease(const FInputDeviceRay& ReleasePos)
{
	OnClickDrag(ReleasePos);
	FinishStroke();
}

void UMDTrashPlacementTool::OnTerminateDragSequence()
{
	FinishStroke();
}

bool UMDTrashPlacementTool::ValidatePlacementSettings() const
{
	if (!IsValid(TargetWorld) || !IsValid(Properties) || !GEditor)
	{
		DisplayWarning(LOCTEXT("InvalidToolState", "Trash Placement is not connected to a valid editor world."));
		return false;
	}

	if (!Properties->TrashItemClass)
	{
		DisplayWarning(LOCTEXT("MissingTrashClass", "Trash Item Class is not configured."));
		return false;
	}

	if (IsRandomSelectionMode())
	{
		if (BuildEligibleTrashKindIds().IsEmpty())
		{
			DisplayWarning(
				LOCTEXT(
					"NoEligibleTrashKinds",
					"No Trash Kind matches the selected random Category tags."));
			return false;
		}
	}
	else
	{
		const TArray<FName> AvailableTrashKinds = UMD_TrashLibrary::GetTrashKindIds();
		if (Properties->TrashKindId.IsNone() || !AvailableTrashKinds.Contains(Properties->TrashKindId))
		{
			DisplayWarning(LOCTEXT("InvalidTrashKind", "Select a valid Trash Kind before placing an actor."));
			return false;
		}
	}

	if (!TargetWorld->GetCurrentLevel())
	{
		DisplayWarning(LOCTEXT("MissingTargetLevel", "The editor world has no current level for trash placement."));
		return false;
	}

	return true;
}

bool UMDTrashPlacementTool::PlaceTrashAtHit(
	const FHitResult& HitResult,
	const FName TrashKindId)
{
	ULevel* TargetLevel = TargetWorld->GetCurrentLevel();
	if (!TargetLevel)
	{
		DisplayWarning(LOCTEXT("MissingTargetLevel", "The editor world has no current level for trash placement."));
		return false;
	}

	UStaticMesh* PreviewMesh = ResolveTrashMesh(TrashKindId);
	if (!IsValid(PreviewMesh))
	{
		DisplayWarning(LOCTEXT("MissingPreviewMesh", "The selected Trash Kind has no mesh for placement."));
		return false;
	}

	const FTransform InitialTransform = BuildPlacementTransform(HitResult, PreviewMesh);
	AMD_TrashItem* TrashItem = Cast<AMD_TrashItem>(GEditor->AddActor(
		TargetLevel,
		Properties->TrashItemClass.Get(),
		InitialTransform,
		false,
		RF_Transactional,
		true));

	if (!TrashItem)
	{
		DisplayWarning(LOCTEXT("SpawnFailed", "Failed to create the gameplay trash actor."));
		return false;
	}

	TrashItem->Modify();
	TrashItem->SetTrashKindId(TrashKindId);

	UStaticMesh* PlacedMesh = PreviewMesh;
	if (UStaticMeshComponent* MeshComponent = TrashItem->FindComponentByClass<UStaticMeshComponent>())
	{
		if (UStaticMesh* StaticMesh = MeshComponent->GetStaticMesh())
		{
			PlacedMesh = StaticMesh;
		}
	}

	const FTransform FinalTransform = BuildPlacementTransform(HitResult, PlacedMesh);
	TrashItem->SetActorTransform(
		FinalTransform,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	TrashItem->PostEditMove(true);
	TrashItem->MarkPackageDirty();
	LastPlacementMesh = PlacedMesh;
	LastPlacementTransform = FinalTransform;
	LastPlacementSurfaceLocation = HitResult.ImpactPoint;
	DistanceSinceLastPlacement = 0.0;
	StrokePlacedTrashItems.Add(TrashItem);
	++StrokePlacementCount;
	return true;
}

bool UMDTrashPlacementTool::EraseTrashItem(AMD_TrashItem* TrashItem)
{
	if (!IsValid(TrashItem))
	{
		return false;
	}

	UWorld* TrashWorld = TrashItem->GetWorld();
	if (!IsValid(TrashWorld))
	{
		return false;
	}

	TrashItem->Modify();
	if (!TrashWorld->EditorDestroyActor(TrashItem, true))
	{
		DisplayWarning(LOCTEXT("EraseFailed", "Failed to erase the targeted trash actor."));
		return false;
	}

	++StrokeEraseCount;
	return true;
}

FInputRayHit UMDTrashPlacementTool::BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos)
{
	FHitResult HitResult;
	if (IsEraseMode())
	{
		AMD_TrashItem* TrashItem = nullptr;
		return TraceTrash(PressPos, TrashItem, HitResult)
			? FInputRayHit(HitResult.Distance)
			: FInputRayHit();
	}

	return TraceWorld(PressPos, HitResult)
		? FInputRayHit(HitResult.Distance)
		: FInputRayHit();
}

void UMDTrashPlacementTool::OnBeginHover(const FInputDeviceRay& DevicePos)
{
	OnUpdateHover(DevicePos);
}

bool UMDTrashPlacementTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	FHitResult HitResult;
	if (IsEraseMode())
	{
		AMD_TrashItem* TrashItem = nullptr;
		bHasHoverHit = TraceTrash(DevicePos, TrashItem, HitResult);
	}
	else
	{
		bHasHoverHit = TraceWorld(DevicePos, HitResult);
	}

	if (bHasHoverHit)
	{
		UpdateHoverFromHit(HitResult);
	}
	else
	{
		HideGhostPreview();
	}

	return bHasHoverHit;
}

void UMDTrashPlacementTool::OnEndHover()
{
	bHasHoverHit = false;
	HideGhostPreview();
}

bool UMDTrashPlacementTool::TraceWorld(const FInputDeviceRay& DevicePos, FHitResult& OutHit) const
{
	if (!IsValid(TargetWorld))
	{
		return false;
	}

	const FVector RayStart = DevicePos.WorldRay.Origin;
	const FVector RayEnd = DevicePos.WorldRay.PointAt(PlacementTraceDistance);
	const FCollisionObjectQueryParams ObjectQueryParams(FCollisionObjectQueryParams::AllObjects);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MDTrashPlacement), true);
	for (const TWeakObjectPtr<AMD_TrashItem>& PlacedTrashItem : StrokePlacedTrashItems)
	{
		if (PlacedTrashItem.IsValid())
		{
			QueryParams.AddIgnoredActor(PlacedTrashItem.Get());
		}
	}

	return TargetWorld->LineTraceSingleByObjectType(
		OutHit,
		RayStart,
		RayEnd,
		ObjectQueryParams,
		QueryParams);
}

bool UMDTrashPlacementTool::TraceTrash(
	const FInputDeviceRay& DevicePos,
	AMD_TrashItem*& OutTrashItem,
	FHitResult& OutHit) const
{
	OutTrashItem = nullptr;
	if (!IsValid(TargetWorld))
	{
		return false;
	}

	const FVector RayStart = DevicePos.WorldRay.Origin;
	const FVector RayEnd = DevicePos.WorldRay.PointAt(PlacementTraceDistance);
	const FCollisionObjectQueryParams ObjectQueryParams(FCollisionObjectQueryParams::AllObjects);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MDTrashErase), true);
	if (!TargetWorld->LineTraceSingleByObjectType(
		OutHit,
		RayStart,
		RayEnd,
		ObjectQueryParams,
		QueryParams))
	{
		return false;
	}

	OutTrashItem = Cast<AMD_TrashItem>(OutHit.GetActor());
	return IsValid(OutTrashItem);
}

double UMDTrashPlacementTool::CalculateWheelAdjustedSpacing(int32 StepDirection) const
{
	if (!IsValid(Properties))
	{
		return 1.0;
	}

	const double SpacingStep = FMath::Max(Properties->WheelSpacingStep, 1.0);
	return FMath::Max(Properties->PlacementSpacing + SpacingStep * StepDirection, 1.0);
}

void UMDTrashPlacementTool::AdjustPlacementSpacing(int32 StepDirection)
{
	if (!IsValid(Properties) || bIsStrokeActive || StepDirection == 0)
	{
		return;
	}

	const double NewSpacing = CalculateWheelAdjustedSpacing(StepDirection);
	if (FMath::IsNearlyEqual(Properties->PlacementSpacing, NewSpacing))
	{
		return;
	}

	Properties->PlacementSpacing = NewSpacing;
	NotifyOfPropertyChangeByTool(Properties);
	GetToolManager()->DisplayMessage(
		FText::Format(
			LOCTEXT("PlacementSpacingChanged", "Placement Spacing: {0} cm"),
			FText::AsNumber(NewSpacing)),
		EToolMessageLevel::UserNotification);
}

bool UMDTrashPlacementTool::IsEraseMode() const
{
	return ResolveRequestedMode(bShiftEraseActive) == EMDTrashPlacementToolMode::Erase;
}

EMDTrashPlacementToolMode UMDTrashPlacementTool::ResolveRequestedMode(bool bShiftEraseRequested) const
{
	if (bShiftEraseRequested)
	{
		return EMDTrashPlacementToolMode::Erase;
	}

	return IsValid(Properties) ? Properties->Mode : EMDTrashPlacementToolMode::Place;
}

bool UMDTrashPlacementTool::IsRandomSelectionMode() const
{
	return IsValid(Properties)
		&& Properties->SelectionMode == EMDTrashPlacementSelectionMode::RandomByCategory;
}

TArray<FName> UMDTrashPlacementTool::BuildEligibleTrashKindIds() const
{
	TArray<FName> EligibleTrashKindIds;
	if (!IsValid(Properties) || Properties->RandomCategoryTags.IsEmpty())
	{
		return EligibleTrashKindIds;
	}

	for (const FName TrashKindId : UMD_TrashLibrary::GetTrashKindIds())
	{
		FMD_TrashKindRow TrashKindRow;
		if (!UMD_TrashLibrary::FindTrashKindRow(TrashKindId, TrashKindRow)
			|| !TrashKindRow.Category.IsValid())
		{
			continue;
		}

		for (const FGameplayTag& CategoryTag : Properties->RandomCategoryTags)
		{
			if (CategoryTag.IsValid() && TrashKindRow.Category.MatchesTag(CategoryTag))
			{
				EligibleTrashKindIds.Add(TrashKindId);
				break;
			}
		}
	}

	EligibleTrashKindIds.Sort(
		[](const FName Left, const FName Right)
		{
			return Left.LexicalLess(Right);
		});
	return EligibleTrashKindIds;
}

FName UMDTrashPlacementTool::ResolveTrashKindId(const FHitResult& HitResult) const
{
	if (!IsRandomSelectionMode())
	{
		return IsValid(Properties) ? Properties->TrashKindId : NAME_None;
	}

	const TArray<FName> EligibleTrashKindIds = BuildEligibleTrashKindIds();
	if (EligibleTrashKindIds.IsEmpty())
	{
		return NAME_None;
	}

	const uint32 SelectionSeed = HashCombine(
		HashCombine(GetTypeHash(Properties->RandomSeed), GetTypeHash(HitResult.ImpactPoint)),
		TrashKindSeedSalt);
	FRandomStream SelectionRandomStream(static_cast<int32>(SelectionSeed));
	return EligibleTrashKindIds[SelectionRandomStream.RandRange(0, EligibleTrashKindIds.Num() - 1)];
}

UStaticMesh* UMDTrashPlacementTool::ResolveTrashMesh(const FName TrashKindId) const
{
	if (!IsValid(Properties))
	{
		return nullptr;
	}

	FMD_TrashKindRow TrashKindRow;
	if (UMD_TrashLibrary::FindTrashKindRow(TrashKindId, TrashKindRow))
	{
		if (UStaticMesh* TrashKindMesh = TrashKindRow.Mesh.LoadSynchronous())
		{
			return TrashKindMesh;
		}
	}

	const AMD_TrashItem* DefaultTrashItem = Properties->TrashItemClass.GetDefaultObject();
	if (const UStaticMeshComponent* MeshComponent = DefaultTrashItem
		? DefaultTrashItem->FindComponentByClass<UStaticMeshComponent>()
		: nullptr)
	{
		return MeshComponent->GetStaticMesh();
	}

	return nullptr;
}

FTransform UMDTrashPlacementTool::BuildPlacementTransform(
	const FHitResult& HitResult,
	const UStaticMesh* StaticMesh) const
{
	const FVector SurfaceNormal = GetSafeSurfaceNormal(HitResult);
	const FVector PlacementUp = Properties->bAlignToSurface ? SurfaceNormal : FVector::UpVector;
	const uint32 PlacementSeed = HashCombine(
		GetTypeHash(Properties->RandomSeed),
		GetTypeHash(HitResult.ImpactPoint));
	FRandomStream PlacementRandomStream(static_cast<int32>(PlacementSeed));
	const float RotationRandomFraction = PlacementRandomStream.FRand();
	const float ScaleRandomFraction = PlacementRandomStream.FRand();

	float RotationDegrees = Properties->RotationOffsetDegrees;
	if (Properties->bRandomizeRotation)
	{
		const float RandomRotationRange = FMath::Abs(Properties->RandomRotationRangeDegrees);
		RotationDegrees += FMath::Lerp(-RandomRotationRange, RandomRotationRange, RotationRandomFraction);
	}

	float UniformScale = FMath::Max(Properties->UniformScale, 0.01f);
	if (Properties->bRandomizeScale)
	{
		const float MinimumScaleMultiplier = FMath::Max(
			FMath::Min(Properties->MinimumScaleMultiplier, Properties->MaximumScaleMultiplier),
			0.01f);
		const float MaximumScaleMultiplier = FMath::Max(
			FMath::Max(Properties->MinimumScaleMultiplier, Properties->MaximumScaleMultiplier),
			MinimumScaleMultiplier);
		UniformScale *= FMath::Lerp(MinimumScaleMultiplier, MaximumScaleMultiplier, ScaleRandomFraction);
	}

	const FQuat AlignmentRotation = FQuat::FindBetweenNormals(FVector::UpVector, PlacementUp);
	const FQuat OffsetRotation(PlacementUp, FMath::DegreesToRadians(RotationDegrees));
	const FQuat PlacementRotation = OffsetRotation * AlignmentRotation;
	const float LocalBottom = IsValid(StaticMesh) ? StaticMesh->GetBoundingBox().Min.Z : 0.0f;
	const FVector FinalLocation = HitResult.ImpactPoint - PlacementUp * LocalBottom * UniformScale;
	return FTransform(PlacementRotation, FinalLocation, FVector(UniformScale));
}

double UMDTrashPlacementTool::CalculatePlacementExtent(
	const UStaticMesh* StaticMesh,
	const FTransform& Transform,
	const FVector& SurfaceLocation,
	const FVector& Direction) const
{
	if (!IsValid(StaticMesh))
	{
		return 0.0;
	}

	const FVector UnitDirection = Direction.GetSafeNormal();
	if (UnitDirection.IsNearlyZero())
	{
		return 0.0;
	}

	const FBox LocalBounds = StaticMesh->GetBoundingBox();
	const FVector LocalExtent = LocalBounds.GetExtent();
	const FVector WorldBoundsCenter = Transform.TransformPosition(LocalBounds.GetCenter());
	const double HalfExtent =
		FMath::Abs(FVector::DotProduct(
			UnitDirection,
			Transform.TransformVector(FVector(LocalExtent.X, 0.0, 0.0))))
		+ FMath::Abs(FVector::DotProduct(
			UnitDirection,
			Transform.TransformVector(FVector(0.0, LocalExtent.Y, 0.0))))
		+ FMath::Abs(FVector::DotProduct(
			UnitDirection,
			Transform.TransformVector(FVector(0.0, 0.0, LocalExtent.Z))));
	const double CenterOffset = FVector::DotProduct(
		WorldBoundsCenter - SurfaceLocation,
		UnitDirection);
	return FMath::Max(CenterOffset + HalfExtent, 0.0);
}

bool UMDTrashPlacementTool::IsPlacementSafe(
	const UStaticMesh* StaticMesh,
	const FTransform& Transform) const
{
	if (!Properties->bPreventOverlaps)
	{
		return true;
	}

	if (!IsValid(TargetWorld) || !IsValid(StaticMesh))
	{
		return false;
	}

	const FBox LocalBounds = StaticMesh->GetBoundingBox();
	const FVector AbsoluteScale = Transform.GetScale3D().GetAbs();
	FVector BoxExtent = LocalBounds.GetExtent() * AbsoluteScale;
	const double Tolerance = FMath::Max(static_cast<double>(Properties->OverlapTolerance), 0.0);
	BoxExtent.X = FMath::Max(BoxExtent.X - Tolerance, 0.1);
	BoxExtent.Y = FMath::Max(BoxExtent.Y - Tolerance, 0.1);
	BoxExtent.Z = FMath::Max(BoxExtent.Z - Tolerance, 0.1);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MDTrashPlacementOverlap), false);
	TArray<FOverlapResult> Overlaps;
	const FVector BoxCenter = Transform.TransformPosition(LocalBounds.GetCenter());
	const FCollisionObjectQueryParams ObjectQueryParams(FCollisionObjectQueryParams::AllObjects);
	return !TargetWorld->OverlapMultiByObjectType(
		Overlaps,
		BoxCenter,
		Transform.GetRotation(),
		ObjectQueryParams,
		FCollisionShape::MakeBox(BoxExtent),
		QueryParams);
}

bool UMDTrashPlacementTool::IsPlacementAllowedAtHit(
	const FHitResult& HitResult,
	const FName TrashKindId) const
{
	UStaticMesh* StaticMesh = ResolveTrashMesh(TrashKindId);
	return IsValid(StaticMesh)
		&& IsPlacementSafe(StaticMesh, BuildPlacementTransform(HitResult, StaticMesh));
}

void UMDTrashPlacementTool::UpdateHoverFromHit(const FHitResult& HitResult)
{
	HoverLocation = HitResult.ImpactPoint;
	HoverNormal = GetSafeSurfaceNormal(HitResult);
	bHasHoverHit = true;
	UpdateGhostPreview(HitResult);
}

void UMDTrashPlacementTool::SetupGhostPreview()
{
	if (!IsValid(TargetWorld))
	{
		return;
	}

	UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, GhostMaterialPath);
	if (!IsValid(BaseMaterial))
	{
		return;
	}

	GhostPreviewComponent = NewObject<UStaticMeshComponent>(this, TEXT("TrashGhostPreview"), RF_Transient);
	GhostPreviewComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GhostPreviewComponent->SetGenerateOverlapEvents(false);
	GhostPreviewComponent->SetMobility(EComponentMobility::Movable);
	GhostPreviewComponent->SetCastShadow(false);
	GhostPreviewComponent->SetCanEverAffectNavigation(false);
	GhostPreviewComponent->SetHiddenInGame(false);
	GhostPreviewComponent->SetVisibility(false);
	GhostPreviewComponent->bSelectable = false;
	GhostPreviewComponent->RegisterComponentWithWorld(TargetWorld);

	GhostMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, GhostPreviewComponent);
	if (IsValid(GhostMaterial))
	{
		GhostMaterial->SetScalarParameterValue(GhostOpacityParameterName, 0.35f);
	}
}

void UMDTrashPlacementTool::UpdateGhostPreview(const FHitResult& HitResult)
{
	if (IsEraseMode() || !IsValid(GhostPreviewComponent) || !IsValid(GhostMaterial))
	{
		HideGhostPreview();
		return;
	}

	const FName TrashKindId = ResolveTrashKindId(HitResult);
	UStaticMesh* StaticMesh = ResolveTrashMesh(TrashKindId);
	if (!IsValid(StaticMesh))
	{
		HideGhostPreview();
		return;
	}

	const FTransform PreviewTransform = BuildPlacementTransform(HitResult, StaticMesh);
	bIsHoverPlacementValid = IsPlacementSafe(StaticMesh, PreviewTransform);
	GhostPreviewComponent->SetStaticMesh(StaticMesh);
	for (int32 MaterialIndex = 0; MaterialIndex < GhostPreviewComponent->GetNumMaterials(); ++MaterialIndex)
	{
		GhostPreviewComponent->SetMaterial(MaterialIndex, GhostMaterial);
	}
	GhostMaterial->SetVectorParameterValue(
		GhostColorParameterName,
		bIsHoverPlacementValid ? ValidGhostColor : InvalidGhostColor);
	GhostPreviewComponent->SetWorldTransform(PreviewTransform);
	GhostPreviewComponent->SetVisibility(true);
	GhostPreviewComponent->MarkRenderStateDirty();
}

void UMDTrashPlacementTool::HideGhostPreview()
{
	bIsHoverPlacementValid = false;
	if (IsValid(GhostPreviewComponent))
	{
		GhostPreviewComponent->SetVisibility(false);
	}
}

void UMDTrashPlacementTool::DestroyGhostPreview()
{
	HideGhostPreview();
	if (IsValid(GhostPreviewComponent))
	{
		GhostPreviewComponent->DestroyComponent();
	}
	GhostPreviewComponent = nullptr;
	GhostMaterial = nullptr;
}

void UMDTrashPlacementTool::BeginStrokeTransaction()
{
	if (bHasActiveUndoTransaction)
	{
		return;
	}

	const FText TransactionDescription = ActiveStrokeMode == EMDTrashPlacementToolMode::Erase
		? LOCTEXT("EraseTrashTransaction", "Erase Trash")
		: LOCTEXT("PlaceTrashTransaction", "Place Trash");
	GetToolManager()->BeginUndoTransaction(TransactionDescription);
	bHasActiveUndoTransaction = true;
}

void UMDTrashPlacementTool::CancelStroke()
{
	if (bHasActiveUndoTransaction)
	{
		GetToolManager()->CancelUndoTransaction();
		bHasActiveUndoTransaction = false;
	}

	bIsStrokeActive = false;
	bHasPreviousSurfaceHit = false;
	StrokePlacedTrashItems.Reset();
	LastPlacementMesh = nullptr;
	LastPlacementTransform = FTransform::Identity;
	LastPlacementSurfaceLocation = FVector::ZeroVector;
	DistanceSinceLastPlacement = 0.0;
	StrokePlacementCount = 0;
	StrokeEraseCount = 0;
}

void UMDTrashPlacementTool::FinishStroke()
{
	if (bHasActiveUndoTransaction)
	{
		GetToolManager()->EndUndoTransaction();
		bHasActiveUndoTransaction = false;
	}

	if (StrokePlacementCount > 0)
	{
		const FText PlacementMessage = IsRandomSelectionMode()
			? FText::Format(
				LOCTEXT("RandomPlacementStrokeSucceeded", "Placed {0} random trash actor(s) from the Category filter."),
				FText::AsNumber(StrokePlacementCount))
			: FText::Format(
				LOCTEXT("PlacementStrokeSucceeded", "Placed {0} trash actor(s) of kind '{1}'."),
				FText::AsNumber(StrokePlacementCount),
				FText::FromName(Properties->TrashKindId));
		GetToolManager()->DisplayMessage(PlacementMessage, EToolMessageLevel::UserMessage);
	}
	else if (StrokeEraseCount > 0)
	{
		GetToolManager()->DisplayMessage(
			FText::Format(
				LOCTEXT("EraseStrokeSucceeded", "Erased {0} trash actor(s)."),
				FText::AsNumber(StrokeEraseCount)),
			EToolMessageLevel::UserMessage);
	}

	bIsStrokeActive = false;
	bHasPreviousSurfaceHit = false;
	StrokePlacedTrashItems.Reset();
	LastPlacementMesh = nullptr;
	LastPlacementTransform = FTransform::Identity;
	LastPlacementSurfaceLocation = FVector::ZeroVector;
	DistanceSinceLastPlacement = 0.0;
	StrokePlacementCount = 0;
	StrokeEraseCount = 0;
}

void UMDTrashPlacementTool::DisplayWarning(const FText& Message) const
{
	GetToolManager()->DisplayMessage(Message, EToolMessageLevel::UserWarning);
}

#undef LOCTEXT_NAMESPACE
