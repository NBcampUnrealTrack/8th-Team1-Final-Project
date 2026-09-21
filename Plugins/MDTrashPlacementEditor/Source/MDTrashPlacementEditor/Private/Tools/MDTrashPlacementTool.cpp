#include "Tools/MDTrashPlacementTool.h"

#include "MD_TrashPlacementSettings.h"
#include "Tools/MD_PlacementGeometry.h"

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
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Templates/TypeHash.h"
#include "ToolContextInterfaces.h"
#include "World/Trash/MD_TrashItem.h"

#define LOCTEXT_NAMESPACE "MDTrashPlacementTool"

namespace
{
	constexpr double PlacementTraceDistance = 10000000.0;
	constexpr float HoverNormalLength = 25.0f;
	constexpr float HoverPointSize = 12.0f;
	const FName GhostColorParameterName(TEXT("HighlightColor"));
	const FName GhostOpacityParameterName(TEXT("OpacityAmount"));
	const FLinearColor ValidGhostColor(0.05f, 1.0f, 0.1f, 1.0f);
	const FLinearColor InvalidGhostColor(1.0f, 0.05f, 0.05f, 1.0f);
	constexpr uint32 TrashKindSeedSalt = 0x6D445450u;
	constexpr uint32 TrashVariantSeedSalt = 0x56415249u;
	constexpr uint32 TransformSeedSalt = 0x54524E53u;
	constexpr int32 PlacementSpacingSolveIterations = 8;
	constexpr double PlacementSpacingSolveTolerance = 0.1;
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
	RandomCategoryTags.Add(MDGameplayTags::Score_Sorting);
}

TArray<FName> UMDTrashPlacementToolProperties::GetTrashKindIdOptions() const
{
	return UMD_TrashLibrary::GetTrashKindIds();
}

TArray<FName> UMDTrashPlacementToolProperties::GetTrashVariantIdOptions() const
{
	TArray<FName> Ids = UMD_TrashLibrary::GetTrashVariantIds(TrashKindId);
	return Ids;
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
	// 에디터에서 지정한 참조만 사용한다. 이전 도구 캐시의 클래스도 설정값으로 덮어쓴다.
	Properties->TrashItemClass = GetDefault<UMD_TrashPlacementSettings>()->TrashItemClass.LoadSynchronous();
	if (Properties->TrashVariantId.IsNone())
	{
		Properties->TrashVariantId = UMD_TrashLibrary::GetDefaultTrashVariantId(Properties->TrashKindId);
	}
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
	ResolvedMeshes.Reset();

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

	const FName PlacementTrashKindId = ResolveTrashKindId();
	FName PlacementVariantId;
	UStaticMesh* PlacementMesh = nullptr;
	const bool bResolvedAppearance = ResolveTrashAppearance(PlacementTrashKindId, PlacementVariantId, PlacementMesh);
	UpdateHoverFromHit(HitResult);
	if (!bResolvedAppearance || !IsPlacementAllowedAtHit(HitResult, PlacementMesh))
	{
		DisplayWarning(
			LOCTEXT(
				"UnsafePlacement",
				"Placement is blocked because the preview mesh is missing or overlaps another object."));
		return;
	}

	BeginStrokeTransaction();
	bIsStrokeActive = true;
	if (!PlaceTrashAtHit(HitResult, PlacementTrashKindId, PlacementVariantId, PlacementMesh))
	{
		CancelStroke();
		return;
	}

	PreviousSurfaceLocation = HitResult.ImpactPoint;
	PreviousSurfaceNormal = MDPlacementGeometry::GetSurfaceNormal(HitResult);
	DistanceSinceLastPlacement = 0.0;
	bHasPreviousSurfaceHit = true;
	UpdateGhostPreview(HitResult);
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
	const FVector CurrentSurfaceNormal = MDPlacementGeometry::GetSurfaceNormal(CurrentHit);

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
			const FName PlacementTrashKindId = ResolveTrashKindId();
			FName PlacementVariantId;
			UStaticMesh* PlacementMesh = nullptr;
			// 간격 계산으로 위치가 바뀌어도 이 후보의 품목/Variant는 다시 뽑지 않는다.
			if (!ResolveTrashAppearance(PlacementTrashKindId, PlacementVariantId, PlacementMesh))
			{
				DisplayWarning(LOCTEXT("MissingPreviewMesh", "The selected kind or Variant has no loadable mesh for placement."));
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
			if (IsPlacementAllowedAtHit(PlacementHit, PlacementMesh))
			{
				if (!PlaceTrashAtHit(PlacementHit, PlacementTrashKindId, PlacementVariantId, PlacementMesh))
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
	UpdateGhostPreview(CurrentHit);
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
		DisplayWarning(LOCTEXT("MissingTrashClass", "Assign Trash Item Class in Project Settings > Plugins > MD Trash Placement, then reopen the tool."));
		return false;
	}

	if (!IsValid(GhostMaterial))
	{
		DisplayWarning(LOCTEXT("MissingGhostMaterial", "Assign Ghost Preview Material in Project Settings > Plugins > MD Trash Placement, then reopen the tool."));
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

	if (!IsRandomSelectionMode() && !Properties->bRandomVariant
		&& !UMD_TrashLibrary::GetTrashVariantIds(Properties->TrashKindId).Contains(Properties->TrashVariantId))
	{
		DisplayWarning(LOCTEXT("InvalidTrashVariant", "Select a valid Variant with a mesh."));
		return false;
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
	const FName TrashKindId,
	const FName VariantId,
	UStaticMesh* PreviewMesh)
{
	ULevel* TargetLevel = TargetWorld->GetCurrentLevel();
	if (!TargetLevel)
	{
		DisplayWarning(LOCTEXT("MissingTargetLevel", "The editor world has no current level for trash placement."));
		return false;
	}

	if (!IsValid(PreviewMesh))
	{
		DisplayWarning(LOCTEXT("MissingPreviewMesh", "The selected kind or Variant has no loadable mesh for placement."));
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
	if (!TrashItem->SetTrashAppearance(TrashKindId, VariantId))
	{
		TargetWorld->EditorDestroyActor(TrashItem, true);
		DisplayWarning(LOCTEXT("VariantChangedBeforeSpawn", "The selected kind or Variant is no longer available."));
		return false;
	}

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
	++NextPlacementIndex;
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
	if (GEditor && TrashItem->IsSelected()) GEditor->SelectActor(TrashItem, false, true);
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
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_Placement_TrashHover);
	LastHoverRay = DevicePos;
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

void UMDTrashPlacementTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	Super::OnPropertyModified(PropertySet, Property);
	if (PropertySet == Properties && Property
		&& Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMDTrashPlacementToolProperties, TrashKindId))
	{
		Properties->TrashVariantId = UMD_TrashLibrary::GetDefaultTrashVariantId(Properties->TrashKindId);
		NotifyOfPropertyChangeByTool(Properties);
	}
	if (PropertySet == Properties && Property
		&& Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMDTrashPlacementToolProperties, RandomSeed))
	{
		NextPlacementIndex = 0;
	}
	// 설정 변경은 실패한 경로를 다시 시도할 기회이기도 하다.
	ResolvedMeshes.Reset();
	if (bHasHoverHit) OnUpdateHover(LastHoverRay);
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
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MDTrashPlacement), true);
	for (const TWeakObjectPtr<AMD_TrashItem>& PlacedTrashItem : StrokePlacedTrashItems)
	{
		if (PlacedTrashItem.IsValid())
		{
			QueryParams.AddIgnoredActor(PlacedTrashItem.Get());
		}
	}

	return TargetWorld->LineTraceSingleByChannel(
		OutHit,
		RayStart,
		RayEnd,
		ECC_Visibility,
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
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MDTrashErase), true);
	if (!TargetWorld->LineTraceSingleByChannel(
		OutHit,
		RayStart,
		RayEnd,
		ECC_Visibility,
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

uint32 UMDTrashPlacementTool::GetPlacementRandomSeed(const uint32 Salt) const
{
	return HashCombine(HashCombine(GetTypeHash(Properties->RandomSeed), NextPlacementIndex), Salt);
}

FName UMDTrashPlacementTool::ResolveTrashKindId() const
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

	const uint32 SelectionSeed = GetPlacementRandomSeed(TrashKindSeedSalt);
	FRandomStream SelectionRandomStream(static_cast<int32>(SelectionSeed));
	return EligibleTrashKindIds[SelectionRandomStream.RandRange(0, EligibleTrashKindIds.Num() - 1)];
}

bool UMDTrashPlacementTool::ResolveTrashAppearance(
	const FName TrashKindId, FName& OutVariantId, UStaticMesh*& OutMesh)
{
	OutVariantId = NAME_None;
	OutMesh = nullptr;
	if (!IsValid(Properties)) return false;
	if (Properties->bRandomVariant)
	{
		const uint32 Seed = GetPlacementRandomSeed(HashCombine(GetTypeHash(TrashKindId.ToString()), TrashVariantSeedSalt));
		if (!UMD_TrashLibrary::SelectRandomTrashVariant(TrashKindId, static_cast<int32>(Seed), OutVariantId)) return false;
	}
	else
	{
		OutVariantId = IsRandomSelectionMode()
			? UMD_TrashLibrary::GetDefaultTrashVariantId(TrashKindId) : Properties->TrashVariantId;
	}
	if (OutVariantId.IsNone()) return false;
	OutMesh = ResolveTrashMesh(TrashKindId, OutVariantId);
	return IsValid(OutMesh);
}

UStaticMesh* UMDTrashPlacementTool::ResolveTrashMesh(const FName TrashKindId, const FName VariantId)
{
	if (!IsValid(Properties)) return nullptr;
	TSoftObjectPtr<UStaticMesh> MeshReference;
	if (!UMD_TrashLibrary::ResolveTrashKindMesh(TrashKindId, VariantId, MeshReference)) return nullptr;
	if (!MeshReference.IsNull())
	{
		const FSoftObjectPath Path = MeshReference.ToSoftObjectPath();
		if (const TObjectPtr<UStaticMesh>* Cached = ResolvedMeshes.Find(Path)) return Cached->Get();
		UStaticMesh* SelectedMesh = MeshReference.Get();
		if (!SelectedMesh) SelectedMesh = MeshReference.LoadSynchronous();
		ResolvedMeshes.Add(Path, SelectedMesh); // 실패도 캐시하여 Hover마다 재시도하지 않는다.
		return SelectedMesh;
	}

	return nullptr;
}

FTransform UMDTrashPlacementTool::BuildPlacementTransform(
	const FHitResult& HitResult,
	const UStaticMesh* StaticMesh) const
{
	const FVector SurfaceNormal = MDPlacementGeometry::GetSurfaceNormal(HitResult);
	const FVector PlacementUp = Properties->bAlignToSurface ? SurfaceNormal : FVector::UpVector;
	const uint32 PlacementSeed = GetPlacementRandomSeed(TransformSeedSalt);
	FRandomStream PlacementRandomStream(static_cast<int32>(PlacementSeed));
	// 기존 Yaw/Scale 난수 순서를 보존하고, 축 체크 여부와 무관하게 각 난수를 고정한다.
	const float YawRandomFraction = PlacementRandomStream.FRand();
	const float ScaleRandomFraction = PlacementRandomStream.FRand();
	const float PitchRandomFraction = PlacementRandomStream.FRand();
	const float RollRandomFraction = PlacementRandomStream.FRand();

	// Random 축은 고정 각도를 더하지 않고, 해당 Min/Max에서 선택한 값으로 대체한다.
	FRotator RotationOffset(Properties->PitchOffsetDegrees, Properties->RotationOffsetDegrees, Properties->RollOffsetDegrees);
	if (Properties->bRandomizeRotation)
	{
		RotationOffset.Yaw = FMath::Lerp(
			FMath::Min(Properties->MinimumYawDegrees, Properties->MaximumYawDegrees),
			FMath::Max(Properties->MinimumYawDegrees, Properties->MaximumYawDegrees),
			YawRandomFraction);
	}
	if (Properties->bRandomizePitch)
	{
		RotationOffset.Pitch = FMath::Lerp(
			FMath::Min(Properties->MinimumPitchDegrees, Properties->MaximumPitchDegrees),
			FMath::Max(Properties->MinimumPitchDegrees, Properties->MaximumPitchDegrees),
			PitchRandomFraction);
	}
	if (Properties->bRandomizeRoll)
	{
		RotationOffset.Roll = FMath::Lerp(
			FMath::Min(Properties->MinimumRollDegrees, Properties->MaximumRollDegrees),
			FMath::Max(Properties->MinimumRollDegrees, Properties->MaximumRollDegrees),
			RollRandomFraction);
	}
	if (Properties->bInvertPitch)
	{
		RotationOffset.Pitch = -RotationOffset.Pitch;
	}
	if (Properties->bInvertRoll)
	{
		RotationOffset.Roll = -RotationOffset.Roll;
	}
	if (Properties->bInvertYaw)
	{
		RotationOffset.Yaw = -RotationOffset.Yaw;
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
	const FQuat PlacementRotation = AlignmentRotation * RotationOffset.Quaternion();
	FVector FinalLocation = HitResult.ImpactPoint;
	if (IsValid(StaticMesh))
	{
		// 회전된 Bounds의 가장 낮은 점을 실제 표면에 맞춰 Pitch/Roll 관통을 방지한다.
		const FBox LocalBounds = StaticMesh->GetBoundingBox();
		const double LowestProjection = MDPlacementGeometry::GetLowestProjection(
			LocalBounds, FTransform(PlacementRotation, FVector::ZeroVector, FVector::OneVector), SurfaceNormal);
		FinalLocation -= SurfaceNormal * LowestProjection * UniformScale;
	}
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
	// Trigger/sensor overlaps are not physical obstructions. Keep blocking walls and trash.
	QueryParams.bIgnoreTouches = true;
	TArray<FOverlapResult> Overlaps;
	const FVector BoxCenter = Transform.TransformPosition(LocalBounds.GetCenter());
	return !TargetWorld->OverlapMultiByChannel(
		Overlaps,
		BoxCenter,
		Transform.GetRotation(),
		ECC_Pawn,
		FCollisionShape::MakeBox(BoxExtent),
		QueryParams);
}

bool UMDTrashPlacementTool::IsPlacementAllowedAtHit(
	const FHitResult& HitResult,
	const UStaticMesh* StaticMesh) const
{
	return IsValid(StaticMesh)
		&& IsPlacementSafe(StaticMesh, BuildPlacementTransform(HitResult, StaticMesh));
}

void UMDTrashPlacementTool::UpdateHoverFromHit(const FHitResult& HitResult)
{
	HoverLocation = HitResult.ImpactPoint;
	HoverNormal = MDPlacementGeometry::GetSurfaceNormal(HitResult);
	bHasHoverHit = true;
	UpdateGhostPreview(HitResult);
}

void UMDTrashPlacementTool::SetupGhostPreview()
{
	if (!IsValid(TargetWorld))
	{
		return;
	}

	UMaterialInterface* BaseMaterial = GetDefault<UMD_TrashPlacementSettings>()->GhostPreviewMaterial.LoadSynchronous();
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

	const FName TrashKindId = ResolveTrashKindId();
	FName VariantId;
	UStaticMesh* StaticMesh = nullptr;
	if (!ResolveTrashAppearance(TrashKindId, VariantId, StaticMesh))
	{
		HideGhostPreview();
		return;
	}

	const FTransform PreviewTransform = BuildPlacementTransform(HitResult, StaticMesh);
	const bool bWasPlacementValid = bIsHoverPlacementValid;
	bIsHoverPlacementValid = IsPlacementSafe(StaticMesh, PreviewTransform);
	const bool bMeshChanged = GhostPreviewComponent->GetStaticMesh() != StaticMesh;
	if (bMeshChanged)
	{
		GhostPreviewComponent->SetStaticMesh(StaticMesh);
		for (int32 MaterialIndex = 0; MaterialIndex < GhostPreviewComponent->GetNumMaterials(); ++MaterialIndex)
		{
			GhostPreviewComponent->SetMaterial(MaterialIndex, GhostMaterial);
		}
	}
	if (bMeshChanged || bWasPlacementValid != bIsHoverPlacementValid || !GhostPreviewComponent->IsVisible())
	{
		GhostMaterial->SetVectorParameterValue(
			GhostColorParameterName,
			bIsHoverPlacementValid ? ValidGhostColor : InvalidGhostColor);
	}
	GhostPreviewComponent->SetWorldTransform(PreviewTransform);
	GhostPreviewComponent->SetVisibility(true);
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
