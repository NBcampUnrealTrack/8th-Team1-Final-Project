#include "World/Wall/MD_CuttableWall.h"
#include "GameplayTags/MD_GameplayTags.h"

#include "TimerManager.h"
#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"
#include "Components/DecalComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Gameplay/Scrape/MD_ScrapeFaceProgress.h"
#include "Engine/Engine.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "World/Sector/MD_Sector.h"
#include "World/Wall/MD_WallPiece.h"

namespace
{
	const FLinearColor NeutralWallColor(0.4f, 0.4f, 0.45f);
}

AMD_CuttableWall::AMD_CuttableWall()
{
	PrimaryActorTick.bCanEverTick = false;
	// bHasSupport 복제용 (원격 클라이언트의 절단 판정 일치)
	bReplicates = true;

	// 배치된 격벽은 별도 설정 없이 절단 대상이 된다
	OwnedGameplayTags.AddTag(MDGameplayTags::Interaction_Cuttable);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	// 특정 Cube를 벽 크기로 찌그러뜨리지 않는다. 실제 메시 또는 Blueprint가 크기를 결정한다.
	Mesh->SetRelativeScale3D(FVector::OneVector);

	ContaminationOverlay = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ContaminationOverlay"));
	ContaminationOverlay->SetupAttachment(Mesh);
	ContaminationOverlay->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ContaminationOverlay->SetGenerateOverlapEvents(false);
	ContaminationOverlay->SetCanEverAffectNavigation(false);
	ContaminationOverlay->SetReceivesDecals(false);
	ContaminationOverlay->CastShadow = false;
	ContaminationOverlay->SetVisibility(false);
	ContaminationOverlay->SetHiddenInGame(true);

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


void AMD_CuttableWall::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	SyncLegacySurfaceConfiguration();
	if (ContaminationOverlay)
	{
		ContaminationOverlay->ClearInstances();
		ContaminationOverlay->SetVisibility(false);
		ContaminationOverlay->SetHiddenInGame(true);
	}
	if (ScrapeSurface)
	{
		ScrapeSurface->ConfigureExternalComponents(
			Mesh,
			FrontContaminationDecal,
			BackContaminationDecal);
		ScrapeSurface->RefreshSurfaceGeometry();
	}

	// 스캔 전에는 타입 구분 없이 중립 회색
	MID = Mesh->CreateAndSetMaterialInstanceDynamic(0);
	if (MID)
	{
		MID->SetVectorParameterValue(TEXT("Color"), NeutralWallColor);
	}

}


void AMD_CuttableWall::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMD_CuttableWall, bHasSupport);
	DOREPLIFETIME(AMD_CuttableWall, CutProgressSeconds);
}


void AMD_CuttableWall::BeginPlay()
{
	Super::BeginPlay();
	if (ContaminationOverlay)
	{
		ContaminationOverlay->ClearInstances();
		ContaminationOverlay->SetVisibility(false);
		ContaminationOverlay->SetHiddenInGame(true);
	}
	SyncLegacySurfaceConfiguration();
	if (ScrapeSurface)
	{
		if (ScrapeSurface->IsContaminationEnabled()
			&& !ScrapeSurface->IsSurfaceInitialized())
		{
			ScrapeSurface->InitializeSurface();
		}
	}

	// Transient인 MID는 레벨 로드/PIE 복제 시 유실되므로 런타임에서 재생성
	MID = Mesh->CreateAndSetMaterialInstanceDynamic(0);
	if (MID)
	{
		MID->SetVectorParameterValue(TEXT("Color"), NeutralWallColor);
	}

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
	if (WallType == EMD_WallType::Strut && !bHasSupport)
	{
		// 벽이 아니라 "절단자가 서 있는 섹터"가 무너진다
		if (AMD_Sector* Sector = AMD_Sector::FindSectorContaining(Cutter))
		{
			Sector->CollapseCeiling();
			OnStrutCollapsed.Broadcast(this, Cutter);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("MD_CuttableWall::Cut - 절단자가 속한 섹터를 찾지 못함"));
		}
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
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetLifeSpan(0.2f);
}

void AMD_CuttableWall::MulticastPlayDemolishEffect_Implementation()
{
	if (GetNetMode() == NM_DedicatedServer || !DemolishEffect)
	{
		return;
	}

	// 액터 원점이 아니라 메시 바운드 중심에서 터뜨린다
	UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		this, DemolishEffect, Mesh->Bounds.Origin, GetActorRotation());
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

	// 벽 타입 확인용 디버그 로그 (화면 표시는 UI 담당)
	UE_LOG(LogTemp, Log, TEXT("MD_CuttableWall::ScanHighlight - %s 스캔: %s, 지속 %.1fs"),
			*GetName(),
			WallType == EMD_WallType::Strut ? TEXT("지주격벽 (위험)") : TEXT("칸막이격벽 (자유 철거)"),
			Duration);

	// 겹친 인프라(배관·배선)에 실루엣 노출 알림. 벽 타입 표시는 UI 담당
	OnWallScanned.Broadcast(this, Duration, Scanner);
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
	if (!HasAuthority() || bCut || DeltaSeconds <= 0.0f)
	{
		return;
	}

	if (!CanCut())
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

	if (GEngine && GetNetMode() != NM_DedicatedServer)
	{
		const TCHAR* WallTypeLabel = WallType == EMD_WallType::Strut
			? TEXT("Strut")
			: TEXT("Partition");
		const TCHAR* NetRoleLabel = HasAuthority()
			? TEXT("Server")
			: TEXT("Client");
		const FColor ProgressColor = ProgressRatio >= 1.0f
			? FColor::Green
			: FColor::Cyan;

		GEngine->AddOnScreenDebugMessage(
			reinterpret_cast<uint64>(this),
			0.35f,
			ProgressColor,
			FString::Printf(
				TEXT("[Wall Cutting][%s] %s (%s) | %.1f%% | %.2f / %.2fs"),
				NetRoleLabel,
				*GetName(),
				WallTypeLabel,
				ProgressRatio * 100.0f,
				CutProgressSeconds,
				GetRequiredCutSeconds()));
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
	ScrapeSurface->bUseProceduralContamination =
		bUseProceduralContamination;
	ScrapeSurface->ContaminationDecalMaterial = ContaminationDecalMaterial;
}
