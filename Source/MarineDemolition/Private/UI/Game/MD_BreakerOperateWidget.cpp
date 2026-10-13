#include "UI/Game/MD_BreakerOperateWidget.h"
#include "World/Facility/MD_RepairInstruction.h"
#include "Component/Interaction/MD_InteractionComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "TimerManager.h"

UMD_BreakerOperateWidget::UMD_BreakerOperateWidget()
{
	bIsBackHandler=true; bIsModal=true; SetIsFocusable(true);
}

TSharedRef<SWidget> UMD_BreakerOperateWidget::RebuildWidget()
{
	if(!WidgetTree) WidgetTree=NewObject<UWidgetTree>(this,TEXT("WidgetTree"));
	if(!WidgetTree->RootWidget)
	{
		auto* Root=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("PointerSurface"));
		Root->SetPadding(FMargin(0)); Root->SetBrushColor(FLinearColor::Transparent);
		auto* Overlay=WidgetTree->ConstructWidget<UOverlay>(); Root->SetContent(Overlay);
		auto* Hint=WidgetTree->ConstructWidget<UTextBlock>();
		Hint->SetText(NSLOCTEXT("MDInstruction","DragHint","노브: 좌클릭 후 드래그\n놓으면 가까운 구멍에 고정\n우클릭 / ESC: 닫기"));
		auto Font=Hint->GetFont(); Font.Size=18; Hint->SetFont(Font);
Hint->SetColorAndOpacity(FSlateColor(FLinearColor::White)); Hint->SetVisibility(ESlateVisibility::HitTestInvisible);
		auto* HintSlot=Overlay->AddChildToOverlay(Hint); HintSlot->SetHorizontalAlignment(HAlign_Left); HintSlot->SetVerticalAlignment(VAlign_Top); HintSlot->SetPadding(FMargin(24));
		WidgetTree->RootWidget=Root;
	}
	return Super::RebuildWidget();
}

void UMD_BreakerOperateWidget::InitializeOperation(AMD_RepairInstructionController* InBreaker, UMD_InteractionComponent* InInteraction, int32 Revision, int32 Id)
{
	Breaker=InBreaker; Interaction=InInteraction; ExpectedRevision=Revision; ExpectedOperationId=Id;
	DragColumn=PendingColumn=PendingRow=INDEX_NONE; bConfirmedOperation=false;
}

TOptional<FUIInputConfig> UMD_BreakerOperateWidget::GetDesiredInputConfig() const
{
	return FUIInputConfig(ECommonInputMode::Menu,EMouseCaptureMode::NoCapture);
}

void UMD_BreakerOperateWidget::NativeOnActivated()
{
	Super::NativeOnActivated(); auto* PC=GetOwningPlayer();
	if(!PC || !IsValid(Breaker) || !Interaction || !PC->IsLocalController()) { DeactivateWidget(); return; }
	bRestored=false; OpenedAt=GetWorld()->GetTimeSeconds(); PreviousViewTarget=PC->GetViewTarget(); bPreviousCursor=PC->bShowMouseCursor;
	PC->SetIgnoreMoveInput(true); PC->SetIgnoreLookInput(true); PC->bShowMouseCursor=true;
	AddedHiddenActors.Reset();
	auto HideLocally=[&](AActor* Actor)
	{
		if(IsValid(Actor) && !PC->HiddenActors.Contains(Actor)) { PC->HiddenActors.Add(Actor); AddedHiddenActors.Add(Actor); }
	};
	if(APawn* Pawn=PC->GetPawn())
	{
		HideLocally(Pawn);
		TArray<AActor*> Attached; Pawn->GetAttachedActors(Attached,true,true);
for(AActor* Actor:Attached) HideLocally(Actor);
	}
	FActorSpawnParameters Params; Params.ObjectFlags|=RF_Transient; Params.Owner=PC;
	OperationCamera=GetWorld()->SpawnActor<ACameraActor>(Params);
	if(!OperationCamera) { DeactivateWidget(); return; }
	const auto Bounds=Breaker->BreakerBody->CalcBounds(Breaker->BreakerBody->GetComponentTransform());
	const FVector Normal=Breaker->GetPanelNormal(); const FRotator Rotation=(-Normal).Rotation();
	int32 W=0,H=0; PC->GetViewportSize(W,H); const double Aspect=H>0?double(W)/H:16./9.;
	const FQuat ViewRotation=Rotation.Quaternion();
	const double Width=FVector::DotProduct(Bounds.BoxExtent,ViewRotation.GetRightVector().GetAbs())*2;
	const double Height=FVector::DotProduct(Bounds.BoxExtent,ViewRotation.GetUpVector().GetAbs())*2;
	constexpr float Fov=50.f; const double Distance=FMath::Max(35.,FMath::Max(Width,Height*Aspect)/(2*FMath::Tan(FMath::DegreesToRadians(Fov*.5)))*1.2);
	OperationCamera->SetActorLocationAndRotation(Bounds.Origin+Normal*Distance,Rotation);
	auto* Camera=OperationCamera->GetCameraComponent(); Camera->SetFieldOfView(Fov);
Camera->SetConstraintAspectRatio(false); Camera->bOverrideAspectRatioAxisConstraint=true; Camera->AspectRatioAxisConstraint=AspectRatio_MaintainXFOV;
	PC->SetViewTargetWithBlend(OperationCamera,.3f,VTBlend_Cubic,0.f,true);
	if(bFallbackInput) { FInputModeUIOnly Mode; Mode.SetWidgetToFocus(TakeWidget()); Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock); PC->SetInputMode(Mode); }
	SetUserFocus(PC);
	// CommonUI intentionally disables native widget tick; lifetime checks must not depend on Slate visibility.
	GetWorld()->GetTimerManager().SetTimer(OperationTimer,this,&ThisClass::TickOperation,1.f/30.f,true);
}

void UMD_BreakerOperateWidget::RestoreOperation()
{
	if(bRestored) return; bRestored=true; CancelDrag();
	if(GetWorld()) GetWorld()->GetTimerManager().ClearTimer(OperationTimer);
	UE_LOG(LogTemp,Verbose,TEXT("[BreakerUI] Closing rev=%d/%d id=%d/%d active=%d read=%d confirmed=%d interact=%d"),ExpectedRevision,Breaker?Breaker->State.Revision:-1,ExpectedOperationId,Breaker?Breaker->OperationId:-1,Breaker?Breaker->State.bActive:0,Breaker?Breaker->State.bGuideRead:0,bConfirmedOperation,Interaction?Interaction->CanOwnerInteract():0);
	if(Interaction) { Interaction->RequestEndBreakerOperation(Breaker,ExpectedOperationId); Interaction->NotifyBreakerWidgetClosed(this); }
	if(auto* PC=GetOwningPlayer())
	{
		for(const auto& Actor:AddedHiddenActors) PC->HiddenActors.Remove(Actor.Get());
AddedHiddenActors.Reset();
PC->SetIgnoreMoveInput(false); PC->SetIgnoreLookInput(false); PC->bShowMouseCursor=bPreviousCursor;
		if(OperationCamera && (PC->GetViewTarget()==OperationCamera || PC->GetViewTarget()==PreviousViewTarget.Get()))
			PC->SetViewTargetWithBlend(PreviousViewTarget.IsValid()?PreviousViewTarget.Get():PC->GetPawn(),.2f,VTBlend_Cubic);
		if(bFallbackInput) PC->SetInputMode(FInputModeGameOnly());
	}
	if(OperationCamera) { OperationCamera->SetLifeSpan(.3f); OperationCamera=nullptr; }
}

void UMD_BreakerOperateWidget::NativeOnDeactivated()
{
	UE_LOG(LogTemp,Verbose,TEXT("[BreakerUI] Deactivated activated=%d"),IsActivated());
	RestoreOperation(); Super::NativeOnDeactivated(); if(bFallbackInput) RemoveFromParent();
}

void UMD_BreakerOperateWidget::NativeDestruct() { UE_LOG(LogTemp,Verbose,TEXT("[BreakerUI] Destruct activated=%d"),IsActivated()); RestoreOperation(); Super::NativeDestruct(); }

void UMD_BreakerOperateWidget::TickOperation()
{
	if(!IsActivated() || bRestored) return;
	auto* PC=GetOwningPlayer(); const APawn* Pawn=PC?PC->GetPawn():nullptr;
	if(!IsValid(Breaker) || !Interaction || !Interaction->CanOwnerInteract() || Interaction->IsHolding() || !Pawn
		|| !Breaker->State.bActive || !Breaker->State.bGuideRead || Breaker->State.Revision!=ExpectedRevision) { UE_LOG(LogTemp,Verbose,TEXT("[BreakerUI] Tick invalid state pawn=%s hold=%d"),*GetNameSafe(Pawn),Interaction?Interaction->IsHolding():0); DeactivateWidget(); return; }
	if(Breaker->OperatingPawn==Pawn && Breaker->OperationId==ExpectedOperationId) bConfirmedOperation=true;
	else if(bConfirmedOperation || GetWorld()->GetTimeSeconds()-OpenedAt>1.5) { UE_LOG(LogTemp,Verbose,TEXT("[BreakerUI] Tick operator mismatch self=%s op=%s"),*GetNameSafe(Pawn),*GetNameSafe(Breaker->OperatingPawn)); DeactivateWidget(); return; }
	if(DragColumn!=INDEX_NONE)
		Breaker->PreviewKnobAt(DragColumn,FMath::Lerp(Breaker->GetKnobSlotCenter(DragColumn,0),Breaker->GetKnobSlotCenter(DragColumn,4),DragAlpha));
	else if(PendingColumn!=INDEX_NONE)
	{
		if(Breaker->State.CurrentRows[PendingColumn]==PendingRow || GetWorld()->GetTimeSeconds()-PendingAt>1.5) { PendingColumn=PendingRow=INDEX_NONE; Breaker->RefreshKnobs(); }
		else Breaker->PreviewKnobAt(PendingColumn,Breaker->GetKnobSlotCenter(PendingColumn,PendingRow));
	}
}

bool UMD_BreakerOperateWidget::RayToPanel(const FVector& Origin, const FVector& Direction, FVector& Point) const
{
	if(!IsValid(Breaker) || Origin.ContainsNaN() || Direction.ContainsNaN()) return false;
	const FVector Normal=Breaker->GetPanelNormal(); const FVector Center=Breaker->GetActorTransform().TransformPosition(Breaker->GetKnobSlotCenter(0,2));
	const double Denominator=FVector::DotProduct(Direction,Normal); if(FMath::Abs(Denominator)<1e-6) return false;
	const double T=FVector::DotProduct(Center-Origin,Normal)/Denominator; if(T<=0) return false;
	Point=Breaker->GetActorTransform().InverseTransformPosition(Origin+Direction*T); return !Point.ContainsNaN();
}

bool UMD_BreakerOperateWidget::PointerPressedRay(const FVector& Origin, const FVector& Direction)
{
	if(!IsActivated() || bRestored || DragColumn!=INDEX_NONE || PendingColumn!=INDEX_NONE || GetWorld()->GetTimeSeconds()-OpenedAt<.3) return false;
	FVector Point; if(!RayToPanel(Origin,Direction,Point)) return false;
	for(int32 Col=0;Col<4;++Col)
	{
		auto* Mesh=Breaker->KnobMeshes[Col].Get(); const auto Bounds=Mesh->CalcBounds(Mesh->GetRelativeTransform());
		const FVector Top=Breaker->GetKnobSlotCenter(Col,0), Rail=Breaker->GetKnobSlotCenter(Col,4)-Top;
		if(Rail.SizeSquared()<SMALL_NUMBER) return false;
		const FVector Along=Rail.GetSafeNormal();
		const FVector Normal=Breaker->GetActorTransform().InverseTransformVectorNoScale(Breaker->GetPanelNormal()).GetSafeNormal();
		const FVector Across=FVector::CrossProduct(Normal,Along).GetSafeNormal();
		const FVector Offset=Point-Breaker->GetKnobSlotCenter(Col,Breaker->State.CurrentRows[Col]);
		if(FMath::Abs(FVector::DotProduct(Offset,Across))>FMath::Max(3.5,FVector::DotProduct(Bounds.BoxExtent,Across.GetAbs())+1.5)
			|| FMath::Abs(FVector::DotProduct(Offset,Along))>FVector::DotProduct(Bounds.BoxExtent,Along.GetAbs())+1.5) continue;
		DragColumn=Col; DragAlpha=FVector::DotProduct(Breaker->GetKnobSlotCenter(Col,Breaker->State.CurrentRows[Col])-Top,Rail)/Rail.SizeSquared();
		DragOffset=FVector::DotProduct(Point-Top,Rail)/Rail.SizeSquared()-DragAlpha;
		return true;
	}
	return false;
}

void UMD_BreakerOperateWidget::PointerMovedRay(const FVector& Origin, const FVector& Direction)
{
	if(DragColumn==INDEX_NONE) return; FVector Point; if(!RayToPanel(Origin,Direction,Point)) return;
	const FVector Top=Breaker->GetKnobSlotCenter(DragColumn,0), Rail=Breaker->GetKnobSlotCenter(DragColumn,4)-Top;
	if(Rail.SizeSquared()<SMALL_NUMBER) return;
	DragAlpha=FMath::Clamp(FVector::DotProduct(Point-Top,Rail)/Rail.SizeSquared()-DragOffset,0.,1.);
	Breaker->PreviewKnobAt(DragColumn,Top+Rail*DragAlpha);
}

bool UMD_BreakerOperateWidget::PointerReleased()
{
	if(DragColumn==INDEX_NONE || !IsValid(Breaker) || !Interaction) return false;
	const int32 Col=DragColumn; const FVector Point=FMath::Lerp(Breaker->GetKnobSlotCenter(Col,0),Breaker->GetKnobSlotCenter(Col,4),DragAlpha);
	const int32 Row=Breaker->FindNearestKnobSlot(Col,Point); DragColumn=INDEX_NONE;
	if(Row==INDEX_NONE) return false;
	PendingColumn=Col; PendingRow=Row; PendingAt=GetWorld()->GetTimeSeconds(); Breaker->PreviewKnobAt(Col,Breaker->GetKnobSlotCenter(Col,Row));
	Interaction->RequestBreakerSlot(Breaker,Col,Row,ExpectedRevision,ExpectedOperationId); return true;
}

void UMD_BreakerOperateWidget::CancelDrag()
{
	DragColumn=PendingColumn=PendingRow=INDEX_NONE; if(IsValid(Breaker)) Breaker->RefreshKnobs();
}

void UMD_BreakerOperateWidget::HandleSlotResult(AMD_RepairInstructionController* Source, int32 Id, int32 Col, int32 Row, bool bAccepted)
{
	if(Source!=Breaker || Id!=ExpectedOperationId || Col!=PendingColumn || Row!=PendingRow) return;
	if(!bAccepted) { PendingColumn=PendingRow=INDEX_NONE; if(IsValid(Breaker)) Breaker->RefreshKnobs(); }
}

bool UMD_BreakerOperateWidget::EventRay(const FPointerEvent& Event, FVector& Origin, FVector& Direction) const
{
	auto* PC=GetOwningPlayer(); if(!PC) return false;
	const FGeometry Viewport=UWidgetLayoutLibrary::GetViewportWidgetGeometry(this); const FVector2D Size=Viewport.GetLocalSize();
	if(Size.X<=0 || Size.Y<=0) return false;
	int32 W=0,H=0; PC->GetViewportSize(W,H); if(W<=0 || H<=0) return false;
	const FVector2D Local=Viewport.AbsoluteToLocal(Event.GetScreenSpacePosition());
	return PC->DeprojectScreenPositionToWorld(Local.X/Size.X*W,Local.Y/Size.Y*H,Origin,Direction);
}

FReply UMD_BreakerOperateWidget::NativeOnMouseButtonDown(const FGeometry&, const FPointerEvent& Event)
{
	if(Event.GetEffectingButton()==EKeys::RightMouseButton) { DeactivateWidget(); return FReply::Handled().ReleaseMouseCapture(); }
	FVector Origin,Direction;
	if(Event.GetEffectingButton()==EKeys::LeftMouseButton && EventRay(Event,Origin,Direction) && PointerPressedRay(Origin,Direction))
		return FReply::Handled().CaptureMouse(TakeWidget());
	return FReply::Handled();
}

FReply UMD_BreakerOperateWidget::NativeOnMouseMove(const FGeometry&, const FPointerEvent& Event)
{
	FVector Origin,Direction; if(EventRay(Event,Origin,Direction)) PointerMovedRay(Origin,Direction); return FReply::Handled();
}

FReply UMD_BreakerOperateWidget::NativeOnMouseButtonUp(const FGeometry&, const FPointerEvent& Event)
{
	if(Event.GetEffectingButton()==EKeys::LeftMouseButton) { PointerReleased(); return FReply::Handled().ReleaseMouseCapture(); }
	return FReply::Handled();
}

void UMD_BreakerOperateWidget::NativeOnMouseCaptureLost(const FCaptureLostEvent& Event)
{
	if(DragColumn!=INDEX_NONE) CancelDrag(); Super::NativeOnMouseCaptureLost(Event);
}

FReply UMD_BreakerOperateWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if(Event.GetKey()==EKeys::Escape) { DeactivateWidget(); return FReply::Handled().ReleaseMouseCapture(); }
	return Super::NativeOnKeyDown(Geometry,Event);
}

bool UMD_BreakerOperateWidget::NativeOnHandleBackAction() { DeactivateWidget(); return true; }
