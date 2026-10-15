#include "Tools/MD_StainRefit.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Engine/Selection.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Gameplay/Contamination/MD_StainStickerBaker.h"
#include "Gameplay/Contamination/MD_StainSurfaceFit.h"
#include "Gameplay/Contamination/MD_SurfaceStainDefinition.h"
#include "Gameplay/Contamination/MD_SurfaceStainSize.h"
#include "LevelUtils.h"
#include "ScopedTransaction.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "World/Contamination/MD_SurfaceStain.h"

#define LOCTEXT_NAMESPACE "MDStainRefit"

namespace
{
	TUniquePtr<FMD_StainRefit> Refit;

	bool CanEdit(const AMD_SurfaceStain* Actor)
	{
		return IsValid(Actor) && !Actor->IsActorBeingDestroyed() && !Actor->IsTemplate()
			&& GEditor && !GEditor->PlayWorld && Actor->GetWorld()
			&& Actor->GetWorld() == GEditor->GetEditorWorldContext().World()
			&& Actor->GetWorld()->WorldType == EWorldType::Editor
			&& Actor->GetLevel() && Actor->GetWorld()->GetLevels().Contains(Actor->GetLevel())
			&& Actor->GetRootComponent() && Actor->GetRootComponent()->IsRegistered()
			&& !FLevelUtils::IsLevelLocked(Actor->GetLevel());
	}

	bool FindSurface(AMD_SurfaceStain& Actor, FHitResult& Closest)
	{
		const double Range = Actor.GetRefitSearchDistance();
		if (!FMath::IsFinite(Range) || Range < 1 || Range > 1500) return false;
		const FVector Origin = Actor.GetActorLocation(), Up = Actor.GetActorUpVector();
		FCollisionQueryParams Query(SCENE_QUERY_STAT(MDStainRefit), true, &Actor);
		TArray<FOverlapResult> Nearby;
		Actor.GetWorld()->OverlapMultiByChannel(Nearby, Origin, FQuat::Identity,
			ECC_Visibility, FCollisionShape::MakeSphere(Range), Query);
		if (Nearby.Num() > 512) return false;
		double Best = TNumericLimits<double>::Max();
		for (const FOverlapResult& Overlap : Nearby)
		{
			auto* Component = Cast<UStaticMeshComponent>(Overlap.GetComponent());
			if (!Component || Cast<UInstancedStaticMeshComponent>(Component) || !Component->IsVisible()
				|| !Component->GetStaticMesh() || Cast<AMD_SurfaceStain>(Component->GetOwner())
				|| Component->GetCollisionResponseToChannel(ECC_Visibility) != ECR_Block) continue;
			FHitResult Hit;
			if (!Component->LineTraceComponent(Hit, Origin + Up * Range, Origin - Up * Range, Query)
				|| Hit.ImpactNormal.ContainsNaN() || FVector::DotProduct(Hit.ImpactNormal, Up) < 0.1) continue;
			const double Distance = FVector::DistSquared(Hit.ImpactPoint, Origin);
			if (Distance < Best) { Best = Distance; Closest = Hit; }
		}
		return Best < TNumericLimits<double>::Max();
	}
}

FMD_StainRefit& FMD_StainRefit::Get()
{
	if (!Refit) Refit = MakeUnique<FMD_StainRefit>();
	return *Refit;
}

void FMD_StainRefit::Shutdown() { Refit.Reset(); }
FMD_StainRefit::~FMD_StainRefit()
{
	if (Bake) Bake->Cancel();
	if (auto Item = Notification.Pin())
	{
		Item->SetCompletionState(SNotificationItem::CS_Fail);
		Item->ExpireAndFadeout();
	}
	Release();
}

bool FMD_StainRefit::Start(AMD_SurfaceStain* Stain)
{
	return Start(Stain, IsValid(Stain) ? Stain->GetInitialSize() : 0);
}

bool FMD_StainRefit::Start(AMD_SurfaceStain* Stain, int32 TargetSize, bool bInFollowSelection)
{
	if (IsRunning()) return false;
	if (!CanEdit(Stain) || !Stain->UsesSharedVolume())
	{ Finish(LOCTEXT("NotEditable", "편집 가능한 입체 오염물 하나를 선택하세요. 플레이 중에는 실행할 수 없습니다."), false); return false; }
	OriginalTransform = Stain->GetActorTransform();
	if (!OriginalTransform.IsValid() || !OriginalTransform.GetScale3D().Equals(FVector::OneVector, 0.001))
	{ Finish(LOCTEXT("Scale", "Transform Scale을 1로 설정하고 Initial Size로 크기를 조절한 뒤 다시 실행하세요."), false); return false; }
	Source.Reset(Stain->GetRefitSource().LoadSynchronous());
	if (!Source.IsValid() || !Source->IsAsset() || !Source->IsUsable() || !Source->UsesSharedVolume()
		|| Source->VolumePlacementPolicy == EMD_SurfaceStainVolumePlacement::ManualOnly)
	{ Finish(LOCTEXT("Source", "Refit Source에 배치에 사용한 원본 오염물 정의를 지정하세요. 표면에 맞춰 생성된 결과는 원본으로 사용할 수 없습니다."), false); return false; }
	OriginalSize = Stain->GetInitialSize();
	Size = TargetSize;
	if (Size < FMD_SurfaceStainSize::Minimum || Size > FMD_SurfaceStainSize::MaximumPlacement)
	{ Finish(LOCTEXT("Size", "새 오염량은 100~10,000 사이여야 합니다. 기존 오염물은 변경하지 않았습니다."), false); return false; }
	FHitResult Hit;
	if (!FindSurface(*Stain, Hit))
	{ Finish(LOCTEXT("Surface", "가까운 표면을 찾지 못했습니다. 오염물의 파란 Z축이 표면 밖을 향하도록 놓고 Refit Search Distance를 확인하세요."), false); return false; }
	auto* Component = Cast<UStaticMeshComponent>(Hit.GetComponent());
	if (!Component || !Component->GetOwner() || FLevelUtils::IsLevelLocked(Component->GetOwner()->GetLevel()))
	{ Finish(LOCTEXT("Locked", "대상 표면의 레벨이 잠겨 있어 다시 맞출 수 없습니다."), false); return false; }
	// Keep the designer's orientation. Re-aligning to a triangle normal on each refit
	// drifts at shared vertices (for example a sphere pole), even when nothing moved.
	ContactTransform = FTransform(OriginalTransform.GetRotation(), Hit.ImpactPoint);
	Target = Stain; Receiver = Component; PreviousDefinition = Stain->GetDefinition();
	bFollowSelection = bInFollowSelection;
	OriginalParent = Stain->GetRootComponent()->GetAttachParent();
	SourceRevision = Source->GetAuthoringRevision();
	FActorSpawnParameters Params;
	Params.ObjectFlags = RF_Transient;
	Params.bHideFromSceneOutliner = true;
	Params.OverrideLevel = Stain->GetLevel();
	Helper.Reset(Stain->GetWorld()->SpawnActor<AActor>(Params));
	if (!Helper.IsValid())
	{ Finish(LOCTEXT("Helper", "표면 계산을 준비하지 못했습니다. 기존 오염물은 유지됩니다."), false); return false; }
	Helper->SetIsTemporarilyHiddenInEditor(true);
	auto* Root = NewObject<USceneComponent>(Helper.Get(), NAME_None, RF_Transient);
	Helper->SetRootComponent(Root); Root->RegisterComponent(); Helper->SetActorTransform(ContactTransform);
	bCancelling = false;
	Bake = FMD_StainStickerBaker::StartAsync(*Helper, FMD_StainSurfaceFit::HalfSize(*Source, Size), Size, nullptr, Source.Get(), false);
	Status = FText::Format(LOCTEXT("Working", "{0}: 표면에 다시 맞추는 중입니다. 성공할 때까지 기존 크기와 모형을 유지합니다."), FText::FromString(Stain->GetActorLabel()));
	FNotificationInfo Info(Status);
	Info.bFireAndForget = false;
	Info.ButtonDetails.Emplace(LOCTEXT("Cancel", "Cancel"), LOCTEXT("CancelTip", "계산을 취소하고 기존 모형을 유지합니다."),
		FSimpleDelegate::CreateLambda([] { if (Refit) Refit->Cancel(); }));
	Notification = FSlateNotificationManager::Get().AddNotification(Info);
	if (auto Item = Notification.Pin()) Item->SetCompletionState(SNotificationItem::CS_Pending);
	return true;
}

bool FMD_StainRefit::IsRunningFor(const AMD_SurfaceStain* Stain) const
{
	return IsRunning() && Target.Get() == Stain;
}

bool FMD_StainRefit::IsCurrent() const
{
	const auto* Actor = Target.Get();
	if (bFollowSelection && (!GEditor || !GEditor->GetSelectedActors()
		|| GEditor->GetSelectedActors()->Num() != 1 || !GEditor->GetSelectedActors()->IsSelected(Actor))) return false;
	return CanEdit(Actor) && Receiver.IsValid() && Receiver->IsRegistered() && Receiver->GetOwner()
		&& !FLevelUtils::IsLevelLocked(Receiver->GetOwner()->GetLevel())
		&& Actor->GetActorTransform().Equals(OriginalTransform, 0.0001)
		&& Actor->GetRootComponent()->GetAttachParent() == OriginalParent.Get()
		&& Actor->GetInitialSize() == OriginalSize && Actor->GetDefinition() == PreviousDefinition.Get()
		&& Actor->GetRefitSource().Get() == Source.Get() && Source->GetAuthoringRevision() == SourceRevision
		&& Bake->SourcesAreCurrent();
}

void FMD_StainRefit::Cancel()
{
	if (!Bake || bCancelling) return;
	bCancelling = true; Bake->Cancel();
	Status = LOCTEXT("Cancelling", "표면 다시 맞추기를 취소했습니다. 기존 오염물은 유지됩니다.");
	if (auto Item = Notification.Pin()) Item->SetText(Status);
}

void FMD_StainRefit::Tick(float DeltaTime)
{
	if (!Bake) return;
	if (bCancelling)
	{
		if (Bake->IsReady()) Finish(Status, false);
		return;
	}
	if (!IsCurrent())
	{
		Cancel();
		Status = LOCTEXT("Changed", "오염물·표면·원본 설정 또는 선택·편집 상태가 변경되어 계산을 취소했습니다. 조정을 마친 뒤 다시 실행하세요.");
		if (auto Item = Notification.Pin()) Item->SetText(Status);
		return;
	}
	if (!Bake->IsReady() || GEditor->IsTransactionActive()) return;
	FString Error;
	auto* Prepared = Bake->FinishVolume(*Helper, *Source, Error);
	auto* Actor = Target.Get();
	auto* Final = Prepared ? FMD_StainSurfaceFit::BuildFinal(*Actor, *Prepared, Error) : nullptr;
	if (!Final)
	{ Finish(FText::Format(LOCTEXT("Failed", "다시 맞추지 못해 기존 오염물을 유지했습니다. {0}"), FText::FromString(Error)), false); return; }
	{
		FScopedTransaction Transaction(LOCTEXT("Transaction", "Refit Stain to Surface"));
		Actor->Modify(); Actor->GetRootComponent()->Modify();
		if (auto* PreviousParent = Actor->GetRootComponent()->GetAttachParent()) PreviousParent->Modify();
		Receiver->Modify();
		Actor->SetActorTransform(ContactTransform);
		Actor->AttachToComponent(Receiver.Get(), FAttachmentTransformRules::KeepWorldTransform);
		Actor->ConfigurePlacement(Final, Size, Source.Get());
		Actor->MarkPackageDirty();
	}
	GEditor->RedrawLevelEditingViewports();
	Finish(LOCTEXT("Done", "현재 표면에 다시 맞췄습니다. Ctrl+Z로 되돌릴 수 있습니다. 레벨을 저장하세요."), true);
}

void FMD_StainRefit::Finish(const FText& Message, bool bSuccess)
{
	Status = Message;
	auto Item = Notification.Pin();
	if (!Item)
	{
		FNotificationInfo Info(Message); Info.ExpireDuration = 7;
		Item = FSlateNotificationManager::Get().AddNotification(Info);
	}
	if (Item)
	{
		Item->SetText(Message);
		Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		Item->ExpireAndFadeout();
	}
	Notification.Reset(); Release();
}

void FMD_StainRefit::Release()
{
	Bake.Reset();
	if (Helper.IsValid() && Helper->GetWorld()) Helper->GetWorld()->DestroyActor(Helper.Get());
	Helper.Reset(); Source.Reset(); Target.Reset(); Receiver.Reset(); PreviousDefinition.Reset();
	OriginalParent.Reset();
	bCancelling = false;
	bFollowSelection = false;
	Size = OriginalSize = 0;
}

#undef LOCTEXT_NAMESPACE
