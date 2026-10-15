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

namespace
{
	// Only format a failure when it changes. Keep the original check and its numeric limits visible.
	FString PlacementErrorInfo(const FString& Error)
	{
		struct FReason { const TCHAR* Match; const TCHAR* Explanation; };
		static const FReason Reasons[] =
		{
			{ TEXT("bounded local continuity"), TEXT("표면 연결을 확인하는 계산 한도에 도달했습니다. 실제 구멍이 있다는 판정은 아닙니다. 폭을 줄이거나 더 단순한 표면에서 다시 확인하세요.") },
			{ TEXT("outline reaches beyond"), TEXT("오염물 윤곽 일부에 받쳐 줄 표면이 없습니다. 가장자리에서 안쪽으로 옮기거나 폭을 줄이세요.") },
			{ TEXT("Could not project the original coating shape"), TEXT("원형을 받는 표면으로 투영하지 못했습니다. 위치를 옮기거나 폭을 줄여 다시 확인하세요.") },
			{ TEXT("Surface sampling produced no coating volume"), TEXT("읽은 표면에서 입체 오염물 형상을 생성하지 못했습니다. 위치를 옮기거나 폭을 줄여 다시 확인하세요.") },
			{ TEXT("opening"), TEXT("오염물이 받쳐 줄 표면이 없는 틈이나 구멍을 가로지릅니다. 위치를 옮기거나 폭을 줄이세요.") },
			{ TEXT("too thin"), TEXT("얇은 부분을 현재 표면에서 끊김 없이 만들려면 0.5cm를 넘는 두께 보정이 필요합니다. 폭을 줄이거나 다른 원형을 선택하세요.") },
			{ TEXT("4,096 local receiver"), TEXT("이 범위의 표면이 4,096개 삼각형을 넘습니다. 폭을 줄이거나 표면 메시를 단순화하세요.") },
			{ TEXT("geometry budget"), TEXT("표면 모양을 유지하면서 만들 수 있는 형상 한도를 넘었습니다. 폭을 줄이거나 표면 메시를 단순화하세요.") },
			{ TEXT("snapshot budget"), TEXT("주변 표면을 읽는 250,000개 삼각형 한도를 넘었습니다. 폭을 줄이거나 주변 메시를 단순화하세요.") },
			{ TEXT("50,000 triangles"), TEXT("배치 범위의 표면이 50,000개 삼각형을 넘습니다. 폭을 줄이거나 표면 메시를 단순화하세요.") },
			{ TEXT("topology budget"), TEXT("표면을 따라가는 계산 한도를 넘었습니다. 폭을 줄이거나 더 단순한 표면에서 다시 확인하세요.") },
			{ TEXT("4096 triangles"), TEXT("생성되는 오염물의 4,096개 삼각형 한도를 넘었습니다. 폭을 줄이세요.") },
			{ TEXT("limit: 512"), TEXT("배치 범위에 지원되는 표면이 없거나 표면이 512개를 넘습니다. 폭을 줄이고 충돌 설정을 확인하세요.") },
			{ TEXT("changed"), TEXT("계산 중 원형이나 받는 표면이 변경되었습니다. 커서를 옮기거나 다시 클릭해 새 상태로 확인하세요.") },
			{ TEXT("surface moved"), TEXT("계산 중 받는 표면이 이동했습니다. 새 위치에서 다시 배치하세요.") },
			{ TEXT("Instanced surfaces"), TEXT("반복 배치된 인스턴스 표면은 지원하지 않습니다. 개별 Static Mesh 표면을 사용하세요.") },
			{ TEXT("Receives Decals enabled"), TEXT("이 스티커는 개별 Static Mesh의 Visibility 충돌과 Receives Decals 설정이 필요합니다. 두 설정을 확인하세요.") },
			{ TEXT("Visibility"), TEXT("개별 Static Mesh의 Visibility 충돌이 필요합니다. 표면의 충돌 설정을 확인하세요.") },
			{ TEXT("locked"), TEXT("대상 또는 배치 레벨이 잠겨 있거나 로드되지 않았습니다. 레벨 상태를 확인하세요.") },
			{ TEXT("planar support within 0.1"), TEXT("Follow Surface를 끈 이 원형은 0.1cm 이내의 평평한 지지가 필요합니다. Follow Surface를 켜거나 굴곡과 턱을 피하세요.") },
			{ TEXT("full footprint needs one"), TEXT("Follow Surface를 끈 원형 전체에 같은 표면의 지지가 필요합니다. 틈·가장자리·다른 물체를 피하거나 Follow Surface를 켜세요.") },
			{ TEXT("planar profile supports"), TEXT("Follow Surface를 끈 원형의 오염량 한도를 넘었습니다. 아래 검사 기록의 범위로 줄이거나 Follow Surface를 켜세요.") },
			{ TEXT("edge profile supports"), TEXT("이 모서리 원형의 오염량 한도를 넘었습니다. 아래 검사 기록의 범위로 줄이세요.") },
			{ TEXT("No supported convex"), TEXT("이 원형에 맞는 볼록한 직각 모서리가 가까이 없습니다. 모서리 안쪽 평면으로 옮기거나 다른 원형을 선택하세요.") },
			{ TEXT("edge angle or rounding"), TEXT("모서리의 각도나 둥근 정도가 원형에 맞지 않습니다. 맞는 모서리 원형이나 다른 표면을 선택하세요.") },
			{ TEXT("same receiving mesh"), TEXT("본체와 흐름이 같은 표면에 닿지 않습니다. 틈·얇은 가장자리·다른 물체를 피하세요.") },
			{ TEXT("Point at the main flat face"), TEXT("모서리 안쪽의 주 평면에 커서를 두세요.") },
			{ TEXT("readable"), TEXT("배치 지점에서 읽을 수 있는 바깥쪽 표면을 찾지 못했습니다. 메시와 충돌 설정을 확인하거나 위치를 옮기세요.") },
			{ TEXT("disconnected"), TEXT("맞춘 형상이 여러 덩어리로 끊어졌습니다. 폭을 줄이거나 연결된 표면 안쪽으로 옮기세요.") },
			{ TEXT("closed coating"), TEXT("청소 가능한 닫힌 형상으로 만들지 못했습니다. 폭을 줄이거나 다른 위치와 원형을 선택하세요.") },
			{ TEXT("connected"), TEXT("배치 범위의 표면 연결을 확인하지 못했습니다. 틈을 피하거나 폭을 줄여 다시 확인하세요.") },
			{ TEXT("requires Follow Surface"), TEXT("이 원형은 Follow Surface가 필요합니다. 해당 옵션을 켜세요.") },
			{ TEXT("supports widths"), TEXT("입력 폭이 현재 원형과 배치 방식의 허용 범위를 벗어났습니다. 아래 검사 기록의 폭 범위로 조정하세요.") },
			{ TEXT("valid stain definition"), TEXT("사용 가능한 오염물 원형과 머티리얼을 선택하세요.") },
			{ TEXT("usable volume"), TEXT("사용 가능한 입체 원형과 받는 표면이 필요합니다. 원형 목록과 표면을 확인하세요.") },
			{ TEXT("100-10000"), TEXT("유효한 편집 표면·입체 원형과 오염량 100~10,000이 필요합니다. 표면·원형·입력값을 확인하세요.") },
			{ TEXT("cancelled"), TEXT("표면 계산이 취소되었습니다. 원하는 위치에서 다시 확인하세요.") },
			{ TEXT("Could not build"), TEXT("준비한 형상을 메시로 만들지 못했습니다. 다시 확인하거나 폭을 줄이세요.") },
			{ TEXT("not valid for placement"), TEXT("준비한 형상이 배치 조건을 만족하지 못했습니다. 다른 위치나 더 작은 폭으로 다시 확인하세요.") },
			{ TEXT("Could not transfer"), TEXT("준비한 오염물을 레벨에 적용하지 못했습니다. 레벨 상태를 확인한 뒤 다시 배치하세요.") },
			{ TEXT("editor could not create"), TEXT("레벨에 오염물 액터를 만들지 못했습니다. 편집 가능한 레벨인지 확인하고 다시 배치하세요.") }
		};
		for (const FReason& Reason : Reasons)
			if (Error.Contains(Reason.Match))
				return FString(Reason.Explanation) + TEXT("\n검사 기록: ") + Error;
		if (Error.IsEmpty()) return TEXT("배치 검사를 완료하지 못했습니다. 위치·크기·원형을 바꿔 다시 확인하세요.");
		for (TCHAR Character : Error)
			if (Character >= 0xAC00 && Character <= 0xD7A3) return Error;
		return TEXT("검사에서 배치를 허용하지 않았습니다. 아래 검사 기록을 확인하세요.\n검사 기록: ") + Error;
	}
}

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
	Properties->WallPalette = Settings->WallStainPalette;
	StickerPreviewMesh = Settings->StickerPreviewMesh.LoadSynchronous();
	AddToolPropertySource(Properties);
	InitializeWidth();
	FString SizeError;
	UMD_SurfaceStainDefinition* InitialDefinition = ResolveDefinition();
	const int32 InitialPreviewSize = ResolveSize(InitialDefinition, SizeError);
	UpdateSizeInfo(InitialDefinition, InitialPreviewSize, SizeError);
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
	UpdateMeasuredSize();
	GetToolManager()->DisplayMessage(LOCTEXT("Help", "Choose stain definitions in Palette. Click/drag to place; Shift erases stains; wheel changes spacing."), EToolMessageLevel::UserNotification);
}

void UMD_StainPlacementTool::Shutdown(EToolShutdownType Type)
{
	if (ActiveBake) ActiveBake->Cancel();
	ActiveBake.Reset(); PendingPlacements.Reset(); SourceCache.Reset();
	FinishStroke();
	Properties->SaveProperties(this);
	LoadedWallDefinitions.Reset(); FailedWallLoads.Reset();
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
	{ Error = TEXT("A valid editor surface, volume profile, and size 100-10000 are required."); return nullptr; }
	FActorSpawnParameters Params; Params.ObjectFlags = RF_Transient; Params.bHideFromSceneOutliner = true;
	AActor* Helper = World->SpawnActor<AActor>(AActor::StaticClass(), ContactTransform, Params);
	USceneComponent* Root = NewObject<USceneComponent>(Helper); Helper->SetRootComponent(Root); Root->RegisterComponent(); Helper->SetActorTransform(ContactTransform);
	FMD_StainStickerPatch Patch; UStaticMesh* Unused = nullptr;
	UMD_SurfaceStainDefinition* Fit = nullptr;
	if (FMD_StainStickerBaker::Build(*Helper, FMD_StainSurfaceFit::HalfSize(*Definition, InitialSize), InitialSize,
		Definition->VolumeMaterial, Patch, Unused, Error, true))
		Fit = FMD_StainSurfaceFit::Build(*Helper, *Definition, Patch, InitialSize, Error);
	if (!Fit) { World->DestroyActor(Helper); return nullptr; }
	FScopedTransaction Transaction(LOCTEXT("PlaceFittedVolume", "Place Surface-Fitted Slime"));
	AMD_SurfaceStain* Stain = Cast<AMD_SurfaceStain>(GEditor->AddActor(World->GetCurrentLevel(), AMD_SurfaceStain::StaticClass(), ContactTransform, true, RF_Transactional, false));
	if (!Stain) { World->DestroyActor(Helper); return nullptr; }
	UMD_SurfaceStainDefinition* Final = FMD_StainSurfaceFit::BuildFinal(*Stain, *Fit, Error);
	World->DestroyActor(Helper);
	if (!Final)
	{
		World->EditorDestroyActor(Stain, true);
		Transaction.Cancel();
		return nullptr;
	}
	Stain->Modify(); Stain->GetRootComponent()->Modify();
	Stain->ConfigurePlacement(Final,InitialSize,Definition); Receiver->Modify();
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

bool UMD_StainPlacementTool::IsWallDefinitionCompatible(const UMD_SurfaceStainDefinition& Definition, FString& Error) const
{
	FVector Dimensions;
	if (!Definition.UsesSharedVolume() || !Definition.IsUsable()
		|| Definition.VolumeStages.Num() != UMD_SurfaceStainDefinition::VolumeStageCount
		|| Definition.VolumePlacementPolicy != EMD_SurfaceStainVolumePlacement::PlanarSupport
		|| Definition.MaximumPlanarInitialSize != FMD_SurfaceStainSize::Reference
		|| !FMD_StainSurfaceFit::GetSourceDimensions(Definition, Dimensions)
		|| !FMath::IsNearlyEqual(Dimensions.X, UMD_StainPlacementProperties::WallReferenceWidthCm, 1.e-4)
		|| !Definition.CanPlacePlanarVolume(FMD_SurfaceStainSize::Reference, Error))
	{
		Error = TEXT("벽 전용 원형의 제작 정보가 맞지 않습니다. 60cm 기준의 승인된 벽 원형 목록을 확인하세요.");
		return false;
	}
	return true;
}

UMD_SurfaceStainDefinition* UMD_StainPlacementTool::ResolveDefinition()
{
	DefinitionError.Reset();
	if (!Properties) return nullptr;
	FRandomStream Random(HashCombine(GetTypeHash(Properties->RandomSeed), GetTypeHash(PlacementIndex)));
	if (Properties->PaletteGroup == EMD_StainPaletteGroup::Wall)
	{
		if (Properties->WallPalette.Num() != UMD_StainPlacementProperties::WallProfileCount)
		{ DefinitionError = TEXT("벽 원형 목록에 네 종류가 필요합니다. 프로젝트 설정의 벽 목록을 확인하세요."); return nullptr; }
		const int32 Index = Properties->bRandomVariant ? Random.RandRange(0, Properties->WallPalette.Num() - 1) : Properties->WallVariantIndex;
		if (!Properties->WallPalette.IsValidIndex(Index))
		{ DefinitionError = TEXT("벽 원형은 0부터 3까지 선택하세요."); return nullptr; }
		const TSoftObjectPtr<UMD_SurfaceStainDefinition>& Reference = Properties->WallPalette[Index];
		UMD_SurfaceStainDefinition* Definition = Reference.Get();
		if (!Definition && !Reference.IsNull() && !FailedWallLoads.Contains(Reference.ToSoftObjectPath()))
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(MD_Placement_WallSourceLoad);
			Definition = Reference.LoadSynchronous();
			if (!IsValid(Definition)) FailedWallLoads.Add(Reference.ToSoftObjectPath());
		}
		if (!IsValid(Definition))
		{ DefinitionError = TEXT("선택한 벽 원형을 읽지 못했습니다. 벽 목록을 확인하고 도구를 다시 여세요."); return nullptr; }
		LoadedWallDefinitions.AddUnique(Definition);
		return IsWallDefinitionCompatible(*Definition, DefinitionError) ? Definition : nullptr;
	}
	if (Properties->Palette.IsEmpty()) return nullptr;
	const int32 Index = Properties->bRandomVariant ? Random.RandRange(0, Properties->Palette.Num() - 1) : Properties->VariantIndex;
	UMD_SurfaceStainDefinition* Definition = Properties->Palette.IsValidIndex(Index) ? Properties->Palette[Index] : nullptr;
	return IsValid(Definition) && Definition->IsUsable() && (Definition->UsesSharedVolume() || Definition->StickerMaterial) ? Definition : nullptr;
}

bool UMD_StainPlacementTool::ResolveWidthRange(FVector2D& Range, FString& Error) const
{
	if (Properties->PaletteGroup == EMD_StainPaletteGroup::Wall)
	{
		// All four sources are authored to one explicit width contract; do not load them to intersect ranges.
		const int32 Maximum = Properties->bFollowSurface ? FMD_SurfaceStainSize::MaximumPlacement : FMD_SurfaceStainSize::Reference;
		const double Width = UMD_StainPlacementProperties::WallReferenceWidthCm;
		Range = FVector2D(Width * FMath::Sqrt(double(FMD_SurfaceStainSize::Minimum) / FMD_SurfaceStainSize::Reference),
			Width * FMath::Sqrt(double(Maximum) / FMD_SurfaceStainSize::Reference));
		Error = Properties->WallPalette.Num() == UMD_StainPlacementProperties::WallProfileCount
			&& (Properties->bRandomVariant || Properties->WallPalette.IsValidIndex(Properties->WallVariantIndex))
			? FString() : TEXT("벽 원형 목록과 0–3의 선택 번호를 확인하세요.");
		if (!bWidthRangeCached || CachedPaletteGroup != EMD_StainPaletteGroup::Wall
			|| CachedWidthRange != Range || CachedWidthRangeError != Error
			|| bCachedRandomVariant != Properties->bRandomVariant || CachedVariantIndex != Properties->WallVariantIndex)
		{
			++WidthRangeVersion; bWidthRangeCached = true; CachedPaletteGroup = EMD_StainPaletteGroup::Wall;
			CachedWidthRange = Range; CachedWidthRangeError = Error; WidthRangeDefinitions.Reset();
			bCachedRandomVariant = Properties->bRandomVariant; CachedVariantIndex = Properties->WallVariantIndex;
		}
		return Error.IsEmpty();
	}
	const int32 Count = Properties->bRandomVariant ? Properties->Palette.Num() : 1;
	bool bCurrent = bWidthRangeCached && CachedPaletteGroup == EMD_StainPaletteGroup::Existing && bCachedRandomVariant == Properties->bRandomVariant
		&& bCachedFollowSurface == Properties->bFollowSurface && CachedVariantIndex == Properties->VariantIndex
		&& WidthRangeDefinitions.Num() == Count;
	for (int32 I = 0; bCurrent && I < Count; ++I)
	{
		const int32 Index = Properties->bRandomVariant ? I : Properties->VariantIndex;
		UMD_SurfaceStainDefinition* Definition = Properties->Palette.IsValidIndex(Index) ? Properties->Palette[Index].Get() : nullptr;
		bCurrent = WidthRangeDefinitions[I].Key.Get() == Definition
			&& WidthRangeDefinitions[I].Value == (Definition ? Definition->GetAuthoringRevision() : 0);
	}
	if (!bCurrent)
	{
		++WidthRangeVersion;
		bWidthRangeCached = true; CachedPaletteGroup = EMD_StainPaletteGroup::Existing; bCachedRandomVariant = Properties->bRandomVariant;
		bCachedFollowSurface = Properties->bFollowSurface; CachedVariantIndex = Properties->VariantIndex;
		WidthRangeDefinitions.Reset(Count); CachedWidthRangeError.Reset();
		CachedWidthRange = FVector2D(0, TNumericLimits<double>::Max());
		for (int32 I = 0; I < Count; ++I)
		{
			const int32 Index = Properties->bRandomVariant ? I : Properties->VariantIndex;
			UMD_SurfaceStainDefinition* Definition = Properties->Palette.IsValidIndex(Index) ? Properties->Palette[Index].Get() : nullptr;
			WidthRangeDefinitions.Emplace(Definition, Definition ? Definition->GetAuthoringRevision() : 0);
			FVector2D Candidate; FString CandidateError;
			if (!Definition || !FMD_StainSurfaceFit::GetAllowedWidthRange(*Definition, Properties->bFollowSurface, Candidate, CandidateError))
			{
				if (CachedWidthRangeError.IsEmpty()) CachedWidthRangeError = TEXT("원본 입체 오염물의 허용 폭을 확인할 수 없습니다. Palette와 Follow Surface 설정을 확인하거나 오염량 입력을 사용하세요.");
				continue;
			}
			CachedWidthRange.X = FMath::Max(CachedWidthRange.X, Candidate.X);
			CachedWidthRange.Y = FMath::Min(CachedWidthRange.Y, Candidate.Y);
		}
		if (Count == 0 || CachedWidthRange.X <= 0 || CachedWidthRange.Y < CachedWidthRange.X)
			CachedWidthRangeError = TEXT("선택한 원본들의 공통 허용 폭이 없습니다. Palette 범위를 줄이거나 오염량 입력을 사용하세요.");
	}
	Range = CachedWidthRange; Error = CachedWidthRangeError;
	return Error.IsEmpty();
}

int32 UMD_StainPlacementTool::ResolveSize(UMD_SurfaceStainDefinition* Definition, FString& Error) const
{
	Error.Reset();
	if (Properties->PaletteGroup == EMD_StainPaletteGroup::Wall && !Definition)
	{ Error = DefinitionError.IsEmpty() ? TEXT("승인된 벽 원형을 선택하세요.") : DefinitionError; return 0; }
	if (Properties->SizeInput == EMD_StainSizeInput::WidthCm)
	{
		FVector2D CommonRange;
		if (!Definition || !ResolveWidthRange(CommonRange, Error)) return 0;
		if (!FMath::IsFinite(Properties->WidthCm) || Properties->WidthCm < CommonRange.X || Properties->WidthCm > CommonRange.Y)
		{
			Error = FString::Printf(TEXT("입력 폭이 허용 범위를 벗어났습니다. 약 %.3f–%.3f cm 범위 안에서 입력하세요."), CommonRange.X, CommonRange.Y);
			return 0;
		}
		if (Properties->PaletteGroup == EMD_StainPaletteGroup::Wall)
		{
			const double Ratio = Properties->WidthCm / UMD_StainPlacementProperties::WallReferenceWidthCm;
			const int32 Size = FMath::RoundToInt(double(FMD_SurfaceStainSize::Reference) * Ratio * Ratio);
			const int32 Maximum = Properties->bFollowSurface ? FMD_SurfaceStainSize::MaximumPlacement : FMD_SurfaceStainSize::Reference;
			if (Size < FMD_SurfaceStainSize::Minimum || Size > Maximum)
			{ Error = TEXT("입력 폭을 벽 원형의 허용 범위 안으로 조정하세요."); return 0; }
			return Size;
		}
		int32 Size; FVector Applied; FVector2D Range;
		return FMD_StainSurfaceFit::ResolveWidth(*Definition, Properties->WidthCm, Properties->bFollowSurface, Size, Applied, Range, Error) ? Size : 0;
	}
	const int32 Seed = HashCombine(GetTypeHash(Properties->RandomSeed), GetTypeHash(PlacementIndex) ^ 0x57A1u);
	return Properties->bRandomSize
		? FMD_SurfaceStainSize::RandomInitial(Properties->MinSize, Properties->MaxSize, Seed)
		: FMD_SurfaceStainSize::ClampPlacement(Properties->InitialSize);
}

void UMD_StainPlacementTool::InitializeWidth()
{
	// Only initialize the new field in legacy amount settings; malformed cm input stays rejected.
	if (Properties->SizeInput != EMD_StainSizeInput::ContaminationAmount || Properties->WidthCm != 0) return;
	UMD_SurfaceStainDefinition* Definition = ResolveDefinition();
	FVector Base;
	if (Definition && Properties->InitialSize >= FMD_SurfaceStainSize::Minimum
		&& Properties->InitialSize <= FMD_SurfaceStainSize::Maximum
		&& FMD_StainSurfaceFit::GetSourceDimensions(*Definition, Base))
		Properties->WidthCm = Base.X * FMath::Sqrt(double(Properties->InitialSize) / FMD_SurfaceStainSize::Reference);
}

void UMD_StainPlacementTool::UpdateSizeInfo(UMD_SurfaceStainDefinition* Definition, int32 Size, const FString& Error)
{
	FVector2D Range; FString RangeError;
	const bool bRange = ResolveWidthRange(Range, RangeError);
	const uint32 Revision = Definition ? Definition->GetAuthoringRevision() : 0;
	if (SizeInfoDefinition.Get() == Definition && SizeInfoRevision == Revision && SizeInfoBudget == Size
		&& bSizeInfoFollowSurface == Properties->bFollowSurface && SizeInfoError == Error
		&& DisplayedWidthRangeVersion == WidthRangeVersion) return;
	SizeInfoDefinition = Definition; SizeInfoRevision = Revision; SizeInfoBudget = Size;
	bSizeInfoFollowSurface = Properties->bFollowSurface; SizeInfoError = Error;
	FVector Dimensions;
	const bool bDimensions = Definition && FMD_StainSurfaceFit::GetSizeDimensions(*Definition, Size, Dimensions, Properties->bFollowSurface);
	NominalDimensions = bDimensions ? Dimensions : FVector::ZeroVector;
	const FString Info = !Error.IsEmpty() ? Error : bDimensions
		? FString::Printf(TEXT("원형 기준 %.2f × %.2f cm / 두께 %.2f cm\n적용 오염량 %d · 가로·세로 비율 유지"), Dimensions.X, Dimensions.Y, Dimensions.Z, Size)
		: TEXT("입체 원본을 선택하면 기준 치수를 표시합니다. 기존 오염량 입력은 그대로 사용할 수 있습니다.");
	const FString RangeInfo = bRange
		? FString::Printf(TEXT("허용 폭 약 %.3f–%.3f cm%s"), FMath::CeilToDouble(Range.X * 1000) / 1000,
			FMath::FloorToDouble(Range.Y * 1000) / 1000, Properties->PaletteGroup == EMD_StainPaletteGroup::Wall ? TEXT(" · 벽 원형 60cm 제작 기준") : Properties->bRandomVariant ? TEXT(" · Palette 공통 범위") : TEXT(""))
		: RangeError;
	DisplayedWidthRangeVersion = WidthRangeVersion;
	if (Properties->SizeInfo != Info || Properties->WidthRangeInfo != RangeInfo)
	{
		Properties->SizeInfo = Info; Properties->WidthRangeInfo = RangeInfo;
		NotifyOfPropertyChangeByTool(Properties);
	}
}

void UMD_StainPlacementTool::UpdateMeasuredSize()
{
	FString Info = bHover ? TEXT("최종 치수 계산 중 · 원형 기준 치수와 감싼 표면의 투영 치수는 다를 수 있습니다.")
		: TEXT("표면에 커서를 두면 맞춤 후 배치축 치수를 표시합니다.");
	if (bHover && bPreviewRejected) Info = TEXT("최종 치수가 없습니다. 배치 상태와 불가 이유를 확인하세요.");
	if (bHover && bPreviewValid && Preview && Preview->GetStaticMesh())
	{
		const FVector Dimensions = Preview->GetStaticMesh()->GetBounds().BoxExtent * 2 * Preview->GetRelativeScale3D().GetAbs();
		if (!Dimensions.ContainsNaN() && Dimensions.GetMin() > 0)
			Info = FString::Printf(TEXT("배치축 최종 X × Y × Z 범위 %.2f × %.2f × %.2f cm\nZ는 감싼 형상 전체의 깊이 범위입니다."), Dimensions.X, Dimensions.Y, Dimensions.Z);
	}
	FString Status = TEXT("대기 · 배치할 표면에 커서를 두세요.");
	if (WantsErase()) Status = TEXT("지우기 모드 · 오염물을 클릭하거나 드래그해 지웁니다.");
	else if (bHover)
	{
		if (bPreviewRejected) Status = TEXT("배치 불가\n") + PreviewErrorInfo;
		else if (!PreviewDefinition.IsValid()) Status = TEXT("배치 불가 · 사용 가능한 오염물 원형을 선택하세요.");
		else if (bPreviewValid) Status = TEXT("배치 가능 · 클릭하거나 드래그하세요.");
		else Status = TEXT("표면 확인 중 · 노란 표시는 계산 중입니다. 초록 표시가 되면 배치할 수 있습니다.");
	}
	if (Properties->MeasuredSizeInfo != Info || Properties->PlacementStatusInfo != Status)
	{
		Properties->MeasuredSizeInfo = Info;
		Properties->PlacementStatusInfo = Status;
		NotifyOfPropertyChangeByTool(Properties);
	}
}

FTransform UMD_StainPlacementTool::MakeTransform(const FHitResult& Hit) const
{
	FRandomStream Random(HashCombine(GetTypeHash(Properties->RandomSeed), GetTypeHash(PlacementIndex)));
	const bool bWall = Properties->PaletteGroup == EMD_StainPaletteGroup::Wall;
	const double RandomRange = bWall ? 8.0 : 180.0;
	const double Roll = Properties->RotationDegrees + (Properties->bRandomRotation ? Random.FRandRange(-RandomRange, RandomRange) : 0.0f);
	const FVector Down = FVector(0, 0, -1) - Hit.ImpactNormal * FVector::DotProduct(FVector(0, 0, -1), Hit.ImpactNormal);
	// Authored wall +Y points down; at nearly horizontal poles keep a deterministic manual basis.
	const FQuat SurfaceRotation = bWall && Down.SquaredLength() >= 0.01
		? FRotationMatrix::MakeFromZY(Hit.ImpactNormal, Down.GetSafeNormal()).ToQuat()
		: FRotationMatrix::MakeFromZ(Hit.ImpactNormal).ToQuat();
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
		SetPreviewError(DefinitionError.IsEmpty() ? TEXT("Select a valid stain definition with its presentation material and slime item class.") : DefinitionError);
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
	if (!Fit || !ValidateRequest(Request, Error)) { ++StrokeFailed; ReportPlacementError(Request, Error); return; }
	BeginStrokeTransaction();
	AMD_SurfaceStain* Stain = Cast<AMD_SurfaceStain>(GEditor->AddActor(Request.Level.Get(),
		AMD_SurfaceStain::StaticClass(), Request.Transform, true, RF_Transactional, false));
	if (!Stain) { ++StrokeFailed; ReportPlacementError(Request, TEXT("The editor could not create the surface stain.")); return; }
	UMD_SurfaceStainDefinition* Final = FMD_StainSurfaceFit::BuildFinal(*Stain, *Fit, Error);
	if (!Final)
	{
		TargetWorld->EditorDestroyActor(Stain, true);
		++StrokeFailed; ReportPlacementError(Request, Error); return;
	}
	Stain->Modify(); Stain->GetRootComponent()->Modify();
	Stain->ConfigurePlacement(Final, Request.Size, Request.Definition.Get());
	Request.Surface->Modify(); Stain->AttachToComponent(Request.Surface.Get(), FAttachmentTransformRules::KeepWorldTransform);
	Stain->MarkPackageDirty(); ++StrokePlaced;
	if (bHover && MatchesPreview(Request)) { ClearPreviewError(); UpdateMeasuredSize(); }
}

void UMD_StainPlacementTool::CommitPrepared(const FMD_StainPlacementRequest& Request, FMD_StainStickerPatch Patch, UStaticMesh* Mesh)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_Placement_StainCommit);
	FString Error;
	if (!ValidateRequest(Request, Error)) { ++StrokeFailed; ReportPlacementError(Request, Error); return; }
	if (Request.Definition->UsesSharedVolume())
	{
		// PlaceVolume opens the nested transaction; the stroke owns the outer Undo grouping.
		BeginStrokeTransaction();
		if (PlaceVolume(TargetWorld, Request.Definition.Get(), Request.Surface.Get(), Request.Transform, Request.Size, Error))
		{
			++StrokePlaced;
			if (bHover && MatchesPreview(Request)) { ClearPreviewError(); UpdateMeasuredSize(); }
		}
		else { ++StrokeFailed; ReportPlacementError(Request, Error); }
		return;
	}
	BeginStrokeTransaction();
	AMD_SurfaceStain* Stain = Cast<AMD_SurfaceStain>(GEditor->AddActor(Request.Level.Get(),
		AMD_SurfaceStain::StaticClass(), Request.Transform, true, RF_Transactional, false));
	if (!Stain) { ReportPlacementError(Request, TEXT("The editor could not create the surface stain.")); return; }
	Stain->Modify();
	Stain->ConfigurePlacement(Request.Definition.Get(), Request.Size);
	Request.Surface->Modify();
	Stain->AttachToComponent(Request.Surface.Get(), FAttachmentTransformRules::KeepWorldTransform);
	if (MatchesPreview(Request)) PreviewPatch = Patch;
	if (!Stain->ApplyPreparedSticker(MoveTemp(Patch), Mesh))
	{
		ReportPlacementError(Request, TEXT("Could not transfer the prepared sticker to the level."));
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
			ClearPreviewError(); UpdateMeasuredSize();
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
	if (bHover && Properties->PaletteGroup == EMD_StainPaletteGroup::Wall)
	{
		const FVector Down = FVector(0, 0, -1) - HoverHit.ImpactNormal * FVector::DotProduct(FVector(0, 0, -1), HoverHit.ImpactNormal);
		const FString Info = Down.SquaredLength() < 0.01
			? TEXT("수평면에 가까워 아래 방향을 정할 수 없습니다. 지정 회전으로 방향을 맞추세요. 무작위 회전은 ±8°입니다.")
			: TEXT("벽의 아래 방향에 맞춥니다. 무작위 회전은 ±8°이며, 지정 회전은 추가로 적용합니다.");
		if (Properties->WallDirectionInfo != Info)
		{ Properties->WallDirectionInfo = Info; NotifyOfPropertyChangeByTool(Properties); }
	}
	UMD_SurfaceStainDefinition* Definition = ResolveDefinition();
	FString SizeError;
	const int32 Size = ResolveSize(Definition, SizeError);
	UpdateSizeInfo(Definition, Size, SizeError);
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
		if (bHover && !WantsErase() && !Definition)
			SetPreviewError(SizeError.IsEmpty() ? TEXT("Select a valid stain definition and its presentation material.") : SizeError);
		else ClearPreviewError();
		UpdateMeasuredSize();
	}
	else
	{
		if (Size <= 0)
		{
			if (ActiveBake && !bBakeForPlacement) ActiveBake->Cancel();
			PreparedFit = nullptr; PreparedSurface.Reset(); DraftPreview = nullptr;
			PreviewSize = 0; bPreviewPending = false;
			SetPreviewError(SizeError);
			UpdateMeasuredSize();
			return true;
		}
		const FTransform Transform = MakeTransform(HoverHit);
		const auto* Surface = Cast<UStaticMeshComponent>(HoverHit.GetComponent());
		UStaticMesh* SourceMesh = Surface ? Surface->GetStaticMesh() : nullptr;
		const FGuid SourceGuid = SourceMesh ? SourceMesh->GetLightingGuid() : FGuid();
		if (Definition != PreviewDefinition || Definition->GetAuthoringRevision() != PreviewDefinitionRevision || Size != PreviewSize || SourceMesh != PreviewSourceMesh || SourceGuid != PreviewSourceGuid || !Transform.Equals(PreviewInputTransform, 0.01)
			|| Properties->bFollowSurface != bPreviewFollowSurface || PreviewTarget != HoverHit.GetComponent() || !PreviewTargetTransform.Equals(HoverHit.GetComponent()->GetComponentTransform(), 0.01))
		{
			PreviewSourceMesh = SourceMesh; PreviewSourceGuid = SourceGuid;
			PreviewTarget = HoverHit.GetComponent();
			PreviewTargetTransform = HoverHit.GetComponent()->GetComponentTransform();
			PreviewInputTransform = Transform;
			PreviewTransform = Transform;
			PreviewDefinition = Definition;
			PreviewDefinitionRevision = Definition->GetAuthoringRevision();
			PreviewSize = Size;
			bPreviewFollowSurface = Properties->bFollowSurface;
			PreviewChangedAt = FPlatformTime::Seconds();
			bPreviewPending = true;
			bPreviewValid = false;
			ClearPreviewError();
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
		UpdateMeasuredSize();
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
			UpdateMeasuredSize();
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
		UpdateMeasuredSize();
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
	UpdateMeasuredSize();
}

void UMD_StainPlacementTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	Super::OnPropertyModified(PropertySet, Property);
	InitializeWidth();
	if (Property && (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMD_StainPlacementProperties, PaletteGroup)
		|| Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMD_StainPlacementProperties, WallVariantIndex)
		|| Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMD_StainPlacementProperties, bRandomVariant)
		|| Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMD_StainPlacementProperties, RandomSeed))) FailedWallLoads.Reset();
	// Compare the effective request in OnUpdateHover. Equal rounded sizes reuse the prepared geometry.
	if (bHover) OnUpdateHover(LastHoverRay);
	else
	{
		UMD_SurfaceStainDefinition* Definition = ResolveDefinition(); FString Error;
		const int32 Size = ResolveSize(Definition, Error);
		UpdateSizeInfo(Definition, Size, Error);
		ClearPreviewError();
		UpdateMeasuredSize();
	}
}

void UMD_StainPlacementTool::SetPreviewError(const FString& Error)
{
	bPreviewValid = false;
	bPreviewRejected = true;
	bPreviewPending = false;
	if (Preview) Preview->SetVisibility(false);
	const bool bChanged = Error != PreviewError || PreviewErrorInfo.IsEmpty();
	if (bChanged)
	{
		PreviewError = Error;
		PreviewErrorInfo = PlacementErrorInfo(Error);
	}
	UpdateMeasuredSize();
	if (bChanged) GetToolManager()->DisplayMessage(FText::FromString(TEXT("배치 불가\n") + PreviewErrorInfo), EToolMessageLevel::UserWarning);
}

void UMD_StainPlacementTool::ClearPreviewError()
{
	const bool bHadError = bPreviewRejected || !PreviewErrorInfo.IsEmpty();
	bPreviewRejected = false;
	PreviewError.Reset(); PreviewErrorInfo.Reset();
	if (bHadError) GetToolManager()->DisplayMessage(FText::GetEmpty(), EToolMessageLevel::UserWarning);
}

void UMD_StainPlacementTool::ReportPlacementError(const FMD_StainPlacementRequest& Request, const FString& Error)
{
	// A completed queued request describes its old stamp, not the surface under the current cursor.
	if (bHover && !WantsErase() && MatchesPreview(Request)) SetPreviewError(Error);
	else GetToolManager()->DisplayMessage(FText::FromString(TEXT("이전 배치 실패\n") + PlacementErrorInfo(Error)), EToolMessageLevel::UserWarning);
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
	if (Request.Size < FMD_SurfaceStainSize::Minimum || Request.Size > FMD_SurfaceStainSize::MaximumPlacement)
	{ Error = TEXT("유효한 오염량 또는 허용 범위 안의 cm 폭을 입력하세요."); return false; }
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
		ReportPlacementError(Request, Error);
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
		bPreviewValid = true; ClearPreviewError();
		ShowMovingPreview();
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
						bPreviewValid = true; ClearPreviewError(); ShowMovingPreview();
					}
				}
				else { if (bBakeForPlacement) ++StrokeFailed; ReportPlacementError(ActiveRequest, Error); }
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
					bPreviewValid = true; ClearPreviewError(); UpdateMeasuredSize();
				}
			}
			else if (!Error.IsEmpty())
			{
				if (bBakeForPlacement) ++StrokeFailed;
				ReportPlacementError(ActiveRequest, Error);
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
		ClearPreviewError();
		DraftPreview = nullptr; PreparedFit = nullptr; PreparedSurface.Reset();
		PreviewPatch = FMD_StainStickerPatch();
		if (Preview) Preview->SetStaticMesh(nullptr);
		if (ActiveBake && !bBakeForPlacement) ActiveBake->Cancel();
		UpdateMeasuredSize();
	}
	// Definition edits/Undo and target movement invalidate settled previews without rebuilding geometry per frame.
	if (bHover && !WantsErase())
	{
		UMD_SurfaceStainDefinition* CurrentDefinition = ResolveDefinition();
		FString SizeError;
		const int32 CurrentSize = ResolveSize(CurrentDefinition, SizeError);
		if (DisplayedWidthRangeVersion != WidthRangeVersion) UpdateSizeInfo(CurrentDefinition, CurrentSize, SizeError);
		const UStaticMeshComponent* CurrentSurface = Cast<UStaticMeshComponent>(PreviewTarget.Get());
		const bool bDefinitionChanged = CurrentDefinition != PreviewDefinition
			|| (CurrentDefinition && CurrentDefinition->GetAuthoringRevision() != PreviewDefinitionRevision);
		const bool bSurfaceChanged = CurrentDefinition && (!CurrentSurface
			|| CurrentSurface->GetStaticMesh() != PreviewSourceMesh
			|| !PreviewTargetTransform.Equals(CurrentSurface->GetComponentTransform(), 0.01)
			|| (PreviewSourceMesh.IsValid() && PreviewSourceGuid != PreviewSourceMesh->GetLightingGuid()));
		if (bDefinitionChanged || CurrentSize != PreviewSize || Properties->bFollowSurface != bPreviewFollowSurface || bSurfaceChanged || (PreparedSurface && !PreparedSurface->SourcesAreCurrent())
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
	UpdateMeasuredSize();
}

void UMD_StainPlacementTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	Super::Render(RenderAPI);
	if (!bHover) return;
	const FLinearColor Color = WantsErase() || !ResolveDefinition() || bPreviewRejected ? FLinearColor::Red
		: bPreviewValid ? FLinearColor::Green : FLinearColor::Yellow;
	RenderAPI->GetPrimitiveDrawInterface()->DrawPoint(HoverHit.ImpactPoint + HoverHit.ImpactNormal, Color, 10.0f, SDPG_Foreground);
	if (!Properties->bShowSizeRuler || WantsErase() || NominalDimensions.X <= 0 || NominalDimensions.Y <= 0) return;
	FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
	const FVector X = PreviewTransform.GetUnitAxis(EAxis::X), Y = PreviewTransform.GetUnitAxis(EAxis::Y);
	const FVector Origin = PreviewTransform.GetLocation() + PreviewTransform.GetUnitAxis(EAxis::Z) * 0.8;
	const FLinearColor RulerColor(0.10f, 0.80f, 0.90f);
	auto DrawRuler = [&](const FVector& Center, const FVector& Axis, const FVector& TickAxis, double Length)
	{
		const FVector A = Center - Axis * Length * 0.5, B = Center + Axis * Length * 0.5;
		PDI->DrawLine(A, B, RulerColor, SDPG_Foreground, 1.5f);
		PDI->DrawLine(A - TickAxis * 2, A + TickAxis * 2, RulerColor, SDPG_Foreground, 1.5f);
		PDI->DrawLine(B - TickAxis * 2, B + TickAxis * 2, RulerColor, SDPG_Foreground, 1.5f);
	};
	DrawRuler(Origin - Y * (NominalDimensions.Y * 0.5 + 3), X, Y, NominalDimensions.X);
	DrawRuler(Origin - X * (NominalDimensions.X * 0.5 + 3), Y, X, NominalDimensions.Y);
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
