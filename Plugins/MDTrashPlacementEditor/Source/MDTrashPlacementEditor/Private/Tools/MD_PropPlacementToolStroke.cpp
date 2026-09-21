#include "Tools/MD_PropPlacementTool.h"

#include "Tools/MD_PlacementGeometry.h"
#include "CollisionQueryParams.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/HitResult.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "InteractiveToolManager.h"
#include "PrimitiveDrawingUtils.h"
#include "SceneManagement.h"
#include "ToolContextInterfaces.h"

#define LOCTEXT_NAMESPACE "MDPropPlacementStroke"

namespace
{
	const FName PlacedByPropToolTag(TEXT("MD.PropPlacement.Created"));
}

FInputRayHit UMD_PropPlacementTool::CanBeginClickDragSequence(const FInputDeviceRay& PressPos)
{
	if (!Properties || !CanEditLevel())
	{
		return FInputRayHit();
	}
	FHitResult Hit;
	// A stroke can start on an unmarked surface and later reach an eligible erase target.
	return TraceSurface(PressPos, Hit) ? FInputRayHit(Hit.Distance) : FInputRayHit();
}

void UMD_PropPlacementTool::OnClickPress(const FInputDeviceRay& PressPos)
{
	FinishStroke();
	if (!CanEditLevel())
	{
		return;
	}
	if (bPreviewDirty)
	{
		RebuildPreview();
	}
	StrokeAction = GetEffectiveAction();
	StrokeLevel = TargetWorld->GetCurrentLevel();
	bStrokeActive = true;
	LastStrokeRay = PressPos;
	LastHoverRay = PressPos;
	bHasHoverRay = true;
	if (StrokeAction == EMD_PropBrushAction::Erase)
	{
		TryEraseAtRay(PressPos);
	}
	else
	{
		FHitResult Hit;
		if (TraceSurface(PressPos, Hit))
		{
			StrokePath.Begin(Hit);
			TryPlaceAtHit(Hit);
		}
	}
	HasReachedStrokeLimit();
	OnUpdateHover(PressPos);
}

void UMD_PropPlacementTool::OnClickDrag(const FInputDeviceRay& DragPos)
{
	if (!bStrokeActive || !Properties)
	{
		return;
	}
	if (!CanEditLevel() || StrokeLevel.Get() != TargetWorld->GetCurrentLevel())
	{
		FinishStroke();
		return;
	}
	if (!Properties->bContinuous)
	{
		return;
	}
	const bool bRayMoved = !LastStrokeRay.WorldRay.Origin.Equals(DragPos.WorldRay.Origin, 0.001)
		|| !LastStrokeRay.WorldRay.Direction.Equals(DragPos.WorldRay.Direction, 0.000001);
	LastStrokeRay = DragPos;
	LastHoverRay = DragPos;
	if (StrokeAction == EMD_PropBrushAction::Erase)
	{
		// Repeated events at the same pointer location must not drill through a stack of actors.
		if (bRayMoved)
		{
			TryEraseAtRay(DragPos);
		}
		HasReachedStrokeLimit();
		return;
	}
	FHitResult CurrentHit;
	if (!TraceSurface(DragPos, CurrentHit))
	{
		StrokePath.Reset();
		HidePreview();
		return;
	}
	TArray<FHitResult> Samples;
	StrokePath.Sample(CurrentHit, Properties->Spacing, Samples);
	for (const FHitResult& Sample : Samples)
	{
		if (!bStrokeActive || !CanEditLevel() || StrokeLevel.Get() != TargetWorld->GetCurrentLevel())
		{
			FinishStroke();
			break;
		}
		FHitResult SurfaceHit;
		if (!TraceStrokeSample(Sample, SurfaceHit))
		{
			StrokePath.Reset();
			break;
		}
		TryPlaceAtHit(SurfaceHit);
		if (HasReachedStrokeLimit())
		{
			break;
		}
	}
	if (bStrokeActive)
	{
		OnUpdateHover(DragPos);
	}
}

void UMD_PropPlacementTool::OnClickRelease(const FInputDeviceRay& ReleasePos)
{
	// A click erases only the press target; release must not delete the actor revealed behind it.
	if (StrokeAction == EMD_PropBrushAction::Place)
	{
		OnClickDrag(ReleasePos);
	}
	FinishStroke();
	OnUpdateHover(ReleasePos);
}

void UMD_PropPlacementTool::OnTerminateDragSequence()
{
	FinishStroke();
}

void UMD_PropPlacementTool::BeginStrokeTransaction()
{
	if (!bStrokeTransaction)
	{
		GetToolManager()->BeginUndoTransaction(StrokeAction == EMD_PropBrushAction::Erase
			? LOCTEXT("EraseTransaction", "Erase Props") : LOCTEXT("PlaceTransaction", "Place Props"));
		bStrokeTransaction = true;
	}
}

void UMD_PropPlacementTool::FinishStroke()
{
	if (bStrokeTransaction)
	{
		if (StrokeChangeCount > 0)
		{
			GetToolManager()->EndUndoTransaction();
		}
		else
		{
			GetToolManager()->CancelUndoTransaction();
		}
		bStrokeTransaction = false;
	}
	if (StrokeChangeCount > 0 && StrokeAction == EMD_PropBrushAction::Erase && GEditor)
	{
		GEditor->NoteSelectionChange();
	}
	bStrokeActive = false;
	StrokeChangeCount = 0;
	StrokeLevel.Reset();
	StrokePlacedActors.Reset();
	StrokePath.Reset();
	EraseHoverActor.Reset();
}

bool UMD_PropPlacementTool::HasReachedStrokeLimit()
{
	if (StrokeChangeCount < FMath::Clamp(Properties->MaxPerStroke, 1, 5000))
	{
		return false;
	}
	FinishStroke();
	GetToolManager()->DisplayMessage(LOCTEXT("StrokeLimit", "Max Per Stroke에 도달했습니다. 버튼을 놓고 다시 시작하세요.\n\n이번 드래그 결과는 Undo 한 번으로 되돌릴 수 있습니다."), EToolMessageLevel::UserNotification);
	return true;
}

bool UMD_PropPlacementTool::TraceStrokeSample(const FHitResult& Sample, FHitResult& Hit) const
{
	if (!TargetWorld.IsValid())
	{
		return false;
	}
	const FVector Normal = MDPlacementGeometry::GetSurfaceNormal(Sample);
	const double ProbeDistance = FMath::Clamp(Properties->Spacing * 0.5, 1.0, 25.0);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(MDPropStrokeSurface), true);
	for (const TWeakObjectPtr<AActor>& Actor : StrokePlacedActors)
	{
		if (Actor.IsValid())
		{
			Query.AddIgnoredActor(Actor.Get());
		}
	}
	return TargetWorld->LineTraceSingleByChannel(Hit, Sample.ImpactPoint + Normal * ProbeDistance,
		Sample.ImpactPoint - Normal * ProbeDistance, ECC_Visibility, Query)
		&& FVector::DotProduct(MDPlacementGeometry::GetSurfaceNormal(Hit), Normal) >= 0.5;
}

bool UMD_PropPlacementTool::TryPlaceAtHit(const FHitResult& Hit)
{
	if (bPreviewDirty)
	{
		RebuildPreview();
	}
	if (!bStrokeActive || !CanEditLevel() || StrokeLevel.Get() != TargetWorld->GetCurrentLevel()
		|| !PreviewError.IsEmpty() || PreviewCandidate.Parts.IsEmpty() || GhostComponents.IsEmpty())
	{
		return false;
	}
	const FMD_PropSurfacePlacement Placement = BuildPlacement(Hit);
	const FTransform& Transform = Placement.Transform;
	if (!IsPlacementSafe(Placement))
	{
		return false;
	}
	UClass* Class = PreviewCandidate.Mesh ? AStaticMeshActor::StaticClass() : PreviewCandidate.ActorClass.Get();
	if (!IsValid(Class))
	{
		return false;
	}
	BeginStrokeTransaction();
	AActor* Actor = GEditor->AddActor(StrokeLevel.Get(), Class, Transform, false, RF_Transactional, false);
	if (!IsValid(Actor))
	{
		GetToolManager()->DisplayMessage(LOCTEXT("SpawnFailed", "소품을 생성하지 못했습니다. 에셋과 현재 레벨 상태를 확인하세요."), EToolMessageLevel::UserWarning);
		return false;
	}
	Actor->Modify();
	if (PreviewCandidate.Mesh)
	{
		UStaticMeshComponent* MeshComponent = CastChecked<AStaticMeshActor>(Actor)->GetStaticMeshComponent();
		MeshComponent->Modify();
		MeshComponent->SetStaticMesh(PreviewCandidate.Mesh);
	}
	FActorLabelUtilities::SetActorLabelUnique(Actor, PreviewCandidate.Label);
	Actor->SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
	Actor->PostEditMove(true);
	if (!IsValid(Actor))
	{
		return false;
	}
	Actor->Tags.AddUnique(PlacedByPropToolTag);
	Actor->MarkPackageDirty();
	StrokePlacedActors.Add(Actor);
	++StrokeChangeCount;
	ConsumeMixCandidate();
	++PreviewCandidateIndex;
	RefreshCandidatePreview();
	return true;
}

bool UMD_PropPlacementTool::IsEraseTarget(const AActor* Actor) const
{
	return IsValid(Actor) && CanEditLevel() && Actor->GetLevel() == TargetWorld->GetCurrentLevel()
		&& Actor->ActorHasTag(PlacedByPropToolTag) && Actor->IsEditable()
		&& !Actor->IsHiddenEd() && !Actor->IsChildActor();
}

bool UMD_PropPlacementTool::TraceEraseTarget(const FInputDeviceRay& Ray, FHitResult& Hit) const
{
	if (!CanEditLevel())
	{
		return false;
	}
	FCollisionQueryParams Query(SCENE_QUERY_STAT(MDPropErase), true);
	return TargetWorld->LineTraceSingleByChannel(Hit, Ray.WorldRay.Origin, Ray.WorldRay.PointAt(10000000.0), ECC_Visibility, Query)
		&& IsEraseTarget(Hit.GetActor());
}

bool UMD_PropPlacementTool::TryEraseAtRay(const FInputDeviceRay& Ray)
{
	FHitResult Hit;
	if (!TraceEraseTarget(Ray, Hit))
	{
		EraseHoverActor.Reset();
		return false;
	}
	AActor* Actor = Hit.GetActor();
	BeginStrokeTransaction();
	Actor->Modify();
	if (Actor->IsSelected())
	{
		GEditor->SelectActor(Actor, false, false);
	}
	if (!TargetWorld->EditorDestroyActor(Actor, true))
	{
		GetToolManager()->DisplayMessage(LOCTEXT("EraseFailed", "가리킨 소품을 삭제하지 못했습니다."), EToolMessageLevel::UserWarning);
		return false;
	}
	++StrokeChangeCount;
	EraseHoverActor.Reset();
	return true;
}

void UMD_PropPlacementTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	Super::Render(RenderAPI);
	const AActor* Actor = EraseHoverActor.Get();
	if (Properties && GetEffectiveAction() == EMD_PropBrushAction::Erase && IsEraseTarget(Actor))
	{
		DrawWireBox(RenderAPI->GetPrimitiveDrawInterface(), Actor->GetComponentsBoundingBox(), FLinearColor::Red, SDPG_Foreground, 1.5f);
	}
}

#undef LOCTEXT_NAMESPACE
