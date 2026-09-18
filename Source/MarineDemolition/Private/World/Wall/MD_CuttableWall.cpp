#include "World/Wall/MD_CuttableWall.h"
#include "GameplayTags/MD_GameplayTags.h"

#include "Audio/MD_AudioSubsystem.h"
#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"
#include "Components/DecalComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Gameplay/Scrape/MD_ScrapeFaceProgress.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "World/Wall/MD_WallPiece.h"

AMD_CuttableWall::AMD_CuttableWall()
{
	PrimaryActorTick.bCanEverTick = false;
	// bHasSupport 복제용 (원격 클라이언트의 절단 판정 일치)
	bReplicates = true;


	// 배치된 격벽은 별도 설정 없이 절단 대상이 된다
	OwnedGameplayTags.AddTag(MDGameplayTags::Interaction_Cuttable);

	// 기본 태그. 대응 정의는 DA_MD_Audio_Cut 에 있고, 없으면 조용히 넘어간다
	DemolishSoundTag = FGameplayTag::RequestGameplayTag(
		TEXT("SFX.Cut.Wall.Break"), /*ErrorIfNotFound*/ false);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	// 특정 Cube를 벽 크기로 찌그러뜨리지 않는다. 실제 메시 또는 Blueprint가 크기를 결정한다.
	Mesh->SetRelativeScale3D(FVector::OneVector);

	FrontContaminationDecal = CreateDefaultSubobject<UDecalComponent>(
		TEXT("FrontContaminationDecal"));
	FrontContaminationDecal->SetupAttachment(Mesh);
	FrontContaminationDecal->SetAbsolute(false, false, true);
	FrontContaminationDecal->SetRelativeScale3D(FVector::OneVector);
	FrontContaminationDecal->SetFadeScreenSize(0.0f);
	FrontContaminationDecal->SetVisibility(false);
	FrontContaminationDecal->SetHiddenInGame(true);

	BackContaminationDecal = CreateDefaultSubobject<UDecalComponent>(
		TEXT("BackContaminationDecal"));
	BackContaminationDecal->SetupAttachment(Mesh);
	BackContaminationDecal->SetAbsolute(false, false, true);
	BackContaminationDecal->SetRelativeScale3D(FVector::OneVector);
	BackContaminationDecal->SetFadeScreenSize(0.0f);
	BackContaminationDecal->SetVisibility(false);
	BackContaminationDecal->SetHiddenInGame(true);

	ScrapeSurface = CreateDefaultSubobject<UMD_ScrapeSurfaceComponent>(
		TEXT("ScrapeSurface"));
	ScrapeSurface->ConfigureExternalComponents(
		Mesh,
		FrontContaminationDecal,
		BackContaminationDecal);
	SyncLegacySurfaceConfiguration();
}


void AMD_CuttableWall::PreRegisterAllComponents()
{
    // MeshUV uses its mesh overlay. Legacy hidden decals sometimes serialize
    // that surface material, which cannot render as a decal and opens a blocking
    // editor notification during PIE registration. Keep all valid decal bindings.
    if (GetWorld() && GetWorld()->IsGameWorld() && ScrapeSurface && ScrapeSurface->UsesMeshUVSurface())
    {
        for (UDecalComponent* Decal : {FrontContaminationDecal.Get(), BackContaminationDecal.Get()})
            if (Decal && (!Decal->IsVisible() || Decal->bHiddenInGame) && Decal->GetDecalMaterial()
                && Decal->GetDecalMaterial()->GetMaterial()->MaterialDomain != MD_DeferredDecal)
                Decal->SetDecalMaterial(nullptr);
    }
    Super::PreRegisterAllComponents();
}

void AMD_CuttableWall::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	SyncLegacySurfaceConfiguration();
	if (ScrapeSurface)
	{
		ScrapeSurface->ConfigureExternalComponents(
			Mesh,
			FrontContaminationDecal,
			BackContaminationDecal);
		ScrapeSurface->RefreshSurfaceGeometry();
	}

	// 스캔 전에는 타입 구분 없이 중립 회색
	ApplyNeutralWallColor();

}


void AMD_CuttableWall::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMD_CuttableWall, bHasSupport);
	DOREPLIFETIME(AMD_CuttableWall, bCut);
	DOREPLIFETIME(AMD_CuttableWall, CutProgressSeconds);
}


void AMD_CuttableWall::BeginPlay()
{
	Super::BeginPlay();
	SyncLegacySurfaceConfiguration();
	if (ScrapeSurface)
	{
		if (ScrapeSurface->IsContaminationEnabled()
			&& !ScrapeSurface->IsSurfaceInitialized())
		{
			ScrapeSurface->InitializeSurface();
		}
	}

	// 레벨 로드/PIE에서도 기존 벽 색상 설정을 적용한다.
	ApplyNeutralWallColor();

}


void AMD_CuttableWall::Cut(AActor* Cutter)
{
	CutFromHit(Cutter, nullptr);
}


void AMD_CuttableWall::CutFromHit(
	AActor* Cutter,
	const FHitResult* WallHit)
{
	if (!HasAuthority() || !CanCut())
	{
		return;
	}

	const float CutSurfaceCleanRatio = ResolveCutSurfaceCleanRatio(WallHit);
	bCut = true;
	CompleteCutWithCleanRatio(Cutter, CutSurfaceCleanRatio);
}


float AMD_CuttableWall::ResolveCutSurfaceCleanRatio(
	const FHitResult* WallHit)
{
	if (!ScrapeSurface)
	{
		return 0.0f;
	}

	if (WallHit
		&& WallHit->bBlockingHit
		&& WallHit->GetActor() == this)
	{
		EMD_ScrapeSurfaceFace CutSurfaceFace = EMD_ScrapeSurfaceFace::Front;
		bool bResolvedFace = ScrapeSurface->TryGetScrapeSurfaceFaceFromHit(
			*WallHit,
			CutSurfaceFace);
		if (!bResolvedFace)
		{
			// 모서리나 BP 추가 충돌 컴포넌트를 맞은 경우에도 GA 시점이
			// 벽의 어느 쪽에 있는지로 한쪽 면을 결정한다.
			bResolvedFace = ScrapeSurface->
				TryResolveContaminationFaceFromWorldPosition(
					WallHit->TraceStart,
					CutSurfaceFace);
		}

		if (bResolvedFace)
		{
			return ScrapeSurface->GetContaminationCleanRatio(CutSurfaceFace);
		}
	}

	// Hit이 없는 기존 Blueprint 직접 호출이나 기하 해석 불가 상태만
	// 이전 동작과 동일한 양면 전체 값으로 안전하게 폴백한다.
	return ScrapeSurface->GetContaminationCleanRatio();
}


void AMD_CuttableWall::CompleteCutWithCleanRatio(
	AActor* Cutter,
	const float CutSurfaceCleanRatio)
{
	// Keep the support links and authored collapse settings after the wall disappears.
	bAwaitingSupportCollapse = WallType == EMD_WallType::Strut && bHasSupport;
	if (bAwaitingSupportCollapse) bAlwaysRelevant = true;
	if (WallType == EMD_WallType::Strut && !bHasSupport)
	{
		CollapseIfUnsupported(Cutter);
	}
	else
	{
		// 칸막이 또는 지지 상태 지주 = 철거 성공 (무지지 지주는 붕괴로 전환, 점수 없음)
		OnWallDemolished.Broadcast(this, Cutter);
	}

	// 파편 드랍: GA가 타격한 한쪽 면의 남은 오염만으로 깨끗/오염 배분
	// (기획 §2.5: 제거율 70% → 7:3)
	const int32 SafePieceCount = FMath::Max(0, PieceCount);
	const int32 CleanCount = FMD_ScrapeFaceProgress::CalculateCleanPieceCount(
		SafePieceCount,
		CutSurfaceCleanRatio);

	const FVector Origin = GetActorLocation();
	const FVector Extent = Mesh->Bounds.BoxExtent;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	for (int32 i = 0; i < SafePieceCount; ++i)
	{
		const FVector SpawnLoc(
				Origin.X + FMath::FRandRange(-Extent.X, Extent.X) * 0.8f,
				Origin.Y + FMath::FRandRange(-Extent.Y, Extent.Y),
				Origin.Z + FMath::FRandRange(-Extent.Z, Extent.Z) * 0.8f);
		const FRotator SpawnRot(FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f), 0.f);

		AMD_WallPiece* Piece = GetWorld()->SpawnActor<AMD_WallPiece>(PieceClass, SpawnLoc, SpawnRot, SpawnParams);
		if (Piece)
		{
			Piece->SetActorScale3D(FVector(FMath::FRandRange(0.15f, 0.35f)));
			Piece->SetContaminated(i >= CleanCount);
		}
	}

	MulticastPlayDemolishEffect();

	// 경로 개방 (조각이 벽 자리를 대체).
	// 즉시 Destroy하면 방금 보낸 멀티캐스트가 채널과 함께 유실될 수 있어 잠깐 미룬다
	OnRep_Cut();
	SetLifeSpan(bAwaitingSupportCollapse ? 0.f : 0.2f);
	ForceNetUpdate();
}

void AMD_CuttableWall::OnRep_Cut()
{
	if (!bCut) return;
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	// The retained wall only tracks support. Release its contamination cells and GPU visuals.
	if (ScrapeSurface && HasAuthority()) ScrapeSurface->SetContaminationEnabled(false);
}

void AMD_CuttableWall::MulticastPlayDemolishEffect_Implementation()
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// 액터 원점이 아니라 메시 바운드 중심에서 터뜨린다
	const FVector BurstLocation = Mesh->Bounds.Origin;

	if (DemolishEffect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this, DemolishEffect, BurstLocation, GetActorRotation());
	}

	// 멀티캐스트라 모든 머신에서 한 번씩 울린다. 중계 컴포넌트를 거치지 않아
	// 절단한 본인도 그대로 듣는다
	if (DemolishSoundTag.IsValid())
	{
		const UWorld* const World = GetWorld();
		if (UMD_AudioSubsystem* Audio = World ? World->GetSubsystem<UMD_AudioSubsystem>() : nullptr)
		{
			Audio->PlayEventAtLocation(DemolishSoundTag, BurstLocation);
		}
	}
}


bool AMD_CuttableWall::CanCut() const
{
	return !bCut;
}


void AMD_CuttableWall::ScanHighlight(float Duration, AActor* Scanner)
{
	// 콘솔/외부 호출에서 0이 들어와도 기본 지속시간 보장
	if (Duration <= 0.f)
	{
		Duration = 3.f;
	}

	// 겹친 인프라(배관·배선)에 실루엣 노출 알림. 벽 타입 표시는 UI 담당
	OnWallScanned.Broadcast(this, Duration, Scanner);
}

void AMD_CuttableWall::OnScanned_Implementation(float Duration, AActor* Scanner)
{
	ScanHighlight(Duration, Scanner);
}

void AMD_CuttableWall::AddCutProgress(float DeltaSeconds, AActor* Cutter)
{
	AddCutProgressInternal(DeltaSeconds, Cutter, nullptr);
}


void AMD_CuttableWall::AddCutProgressFromHit(
	const float DeltaSeconds,
	AActor* Cutter,
	const FHitResult& WallHit)
{
	AddCutProgressInternal(DeltaSeconds, Cutter, &WallHit);
}


void AMD_CuttableWall::AddCutProgressInternal(
	const float DeltaSeconds,
	AActor* Cutter,
	const FHitResult* WallHit)
{
	// 협동 합산(기획 §4) 때문에 진행도는 벽이 소유하고 서버만 갱신한다.
	// 여러 명이 같은 벽을 자르면 각자 이 함수를 불러 자연히 합산된다.
	if (!HasAuthority() || !CanCut() || DeltaSeconds <= 0.0f)
	{
		return;
	}

	// 지점 제한이 걸린 벽(튜토리얼)은 허용된 자리를 쳤을 때만 진행도가 쌓인다.
	// 거부해도 어빌리티는 계속 돌아간다 — 절단기가 "안 먹는" 것으로 보이는 게 의도다.
	if (CutLocationFilter.IsBound() && !CutLocationFilter.Execute(WallHit))
	{
		return;
	}

	const float Required = GetRequiredCutSeconds();

	CutProgressSeconds = FMath::Clamp(CutProgressSeconds + DeltaSeconds, 0.0f, Required);
	const bool bCompletesCut = CutProgressSeconds >= Required;
	const float CompletionCleanRatio = bCompletesCut
		? ResolveCutSurfaceCleanRatio(WallHit)
		: 0.0f;
	if (bCompletesCut)
	{
		// 진행도 delegate의 재진입보다 먼저 파괴를 예약하고 비율을 고정한다.
		bCut = true;
	}

	// 서버에는 OnRep이 오지 않으므로 직접 부른다
	OnRep_CutProgress();

	if (bCompletesCut)
	{
		CompleteCutWithCleanRatio(Cutter, CompletionCleanRatio);
	}
}

float AMD_CuttableWall::GetRequiredCutSeconds() const
{
	return (WallType == EMD_WallType::Strut) ? StrutCutSeconds : PartitionCutSeconds;
}

float AMD_CuttableWall::GetCutProgressRatio() const
{
	const float Required = GetRequiredCutSeconds();

	return Required > 0.0f ? FMath::Clamp(CutProgressSeconds / Required, 0.0f, 1.0f) : 0.0f;
}


void AMD_CuttableWall::OnRep_CutProgress()
{
	const float ProgressRatio = GetCutProgressRatio();
	OnCutProgressChanged.Broadcast(this, ProgressRatio);

}

void AMD_CuttableWall::ApplyNeutralWallColor()
{
	// The mesh owns the material instance; the actor does not need a second reference.
	if (UMaterialInstanceDynamic* WallMaterial = Mesh->CreateAndSetMaterialInstanceDynamic(0))
	{
		WallMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.4f, 0.4f, 0.45f));
	}
}

void AMD_CuttableWall::SyncLegacySurfaceConfiguration()
{
	if (!ScrapeSurface)
	{
		return;
	}

	ScrapeSurface->ContaminationItemClass = ContaminationItemClass;
	ScrapeSurface->ContaminationItemsPerFace = ContaminationItemsPerFace;
	ScrapeSurface->bUseProceduralContamination = bUseProceduralContamination;
    ScrapeSurface->bForceFullContamination = bForceFullContamination;
	ScrapeSurface->ContaminationDecalMaterial = ContaminationDecalMaterial;
}
