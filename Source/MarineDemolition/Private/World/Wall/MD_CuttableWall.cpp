#include "World/Wall/MD_CuttableWall.h"

#include "TimerManager.h"
#include "Components/DecalComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Gameplay/Scrape/MD_ContaminationNoise.h"
#include "Gameplay/Scrape/MD_ScrapeDecalLayout.h"
#include "Gameplay/Scrape/MD_ScrapeEvaluator.h"
#include "Gameplay/Scrape/MD_ScrapeFaceProgress.h"
#include "Gameplay/Scrape/MD_ScrapeMaskAtlas.h"
#include "Gameplay/Scrape/MD_ScrapeMeshSurfaceProfile.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceGeometry.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceLayout.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "Net/UnrealNetwork.h"
#include "RHITypes.h"
#include "Stats/Stats.h"
#include "World/Contamination/MD_Barnacle.h"
#include "World/Contamination/MD_ContaminationItem.h"
#include "World/Sector/MD_Sector.h"
#include "World/Wall/MD_WallPiece.h"
#include "World/Wall/MD_CuttableWallInternal.h"

// 이 파일: 액터 수명 · 철거(Cut/CanCut) · 스캔.
// 오염 표면 구현은 MD_CuttableWall_*.cpp로 나뉘어 있다.
// 파일별 책임은 MD_CuttableWallInternal.h의 표를 참고한다.

// 통계 정의는 이 파일 한 곳에만 둔다. 선언은 내부 헤더에 있다.
DEFINE_STAT(STAT_MD_ScrapeStrokeTotal);
DEFINE_STAT(STAT_MD_ScrapeEvaluate);
DEFINE_STAT(STAT_MD_ScrapeCleanRatio);
DEFINE_STAT(STAT_MD_ScrapeMaskUpdate);
DEFINE_STAT(STAT_MD_ScrapeOverlayUpdate);
DEFINE_STAT(STAT_MD_ScrapePublish);
DEFINE_STAT(STAT_MD_ScrapeFullyRemovedCheck);

namespace MD_CuttableWallInternal
{
	const FLinearColor NeutralWallColor(0.4f, 0.4f, 0.45f);
	const FName ContaminationMaskTextureParameterName(TEXT("MaskTexture"));
	const FName ContaminationMaskVScaleParameterName(TEXT("MaskVScale"));
	const FName ContaminationMaskVOffsetParameterName(TEXT("MaskVOffset"));
	const FName ContaminationMaskUFlipParameterName(TEXT("MaskUFlip"));
	const FName ContaminationMaskHalfTexelUParameterName(TEXT("MaskHalfTexelU"));
	const FName ContaminationMaskHalfTexelVParameterName(TEXT("MaskHalfTexelV"));
}

using namespace MD_CuttableWallInternal;

AMD_CuttableWall::AMD_CuttableWall()
{
	PrimaryActorTick.bCanEverTick = false;
	// bHasSupport 복제용 (원격 클라이언트의 절단 판정 일치)
	bReplicates = true;
	ContaminationGridDimensions = FIntPoint(30, 25);
	InitialContaminationPerCell = 1.0f;
	bUseProceduralContamination = false;
	ContaminationNoiseScaleCm = 75.0f;
	ContaminationNoiseSeed = 1337;
	ContaminationCoverage = 0.55f;
	ContaminationMinimumAmount = 0.25f;
	ContaminationSurfaceSizeOverrideCm = FVector2D::ZeroVector;
	ContaminationSurfaceNormalAxis = EMD_ScrapeSurfaceNormalAxis::Auto;
	ContaminationOverlayOffsetCm = 0.25f;
	ContaminationOverlayThicknessCm = 0.35f;
	bShowContaminationOverlay = true;
	ContaminationDecalMaterial = nullptr;
	bShowContaminationDecals = true;
	ContaminationDecalProjectionDepthCm = 1.0f;
	bAutoFitContaminationDecalDepth = true;
	ContaminationDecalDepthFitMarginCm = 1.0f;
	ContaminationDecalSurfaceOffsetCm = 0.05f;
	ContaminationDecalEdgeInsetCm = 2.0f;
	ContaminationDecalSortOrder = 0;
	ResolvedContaminationSurfaceSizeCm = FVector2D::ZeroVector;
	ResolvedContaminationSurfaceNormalAxis =
		EMD_ScrapeSurfaceNormalAxis::Y;
	ResolvedFrontDecalDepthCm = 0.0f;
	ResolvedBackDecalDepthCm = 0.0f;
	ResolvedContaminationDecalEdgeInsetCm = 0.0f;
	ContaminationSurfaceCenterLocal = FVector::ZeroVector;
	ContaminationMeshLocalBoundsMinimum = FVector::ZeroVector;
	ContaminationMeshLocalBoundsMaximum = FVector::ZeroVector;
	CachedFrontContaminationCleanRatio = 0.0f;
	CachedBackContaminationCleanRatio = 0.0f;
	ContaminationMaskAtlasDimensions = FIntPoint::ZeroValue;
	bContaminationDecalsConfigured = false;
	bContaminationSurfaceInitialized = false;
	bContaminationCompletionBroadcast = false;
	ReplicatedContaminationState.SetOwner(this);

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
}


void AMD_CuttableWall::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 에디터 변경 시 작업면 크기 갱신
	ResolveContaminationSurfaceGeometry();

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
	DOREPLIFETIME(AMD_CuttableWall, ReplicatedContaminationState);
}


void AMD_CuttableWall::BeginPlay()
{
	Super::BeginPlay();
	ReplicatedContaminationState.SetOwner(this);

	// 런타임 오염 상태 초기화
	InitializeContaminationSurface();

	// Transient인 MID는 레벨 로드/PIE 복제 시 유실되므로 런타임에서 재생성
	MID = Mesh->CreateAndSetMaterialInstanceDynamic(0);
	if (MID)
	{
		MID->SetVectorParameterValue(TEXT("Color"), NeutralWallColor);
	}

	InitializeContaminationVisualization();

	if (!HasAuthority())
	{
		ApplyAllReplicatedContaminationState();
	}
}


void AMD_CuttableWall::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	SetContaminationDecalsVisible(false);
	ReleaseContaminationMask();
	FrontContaminationDecalMID = nullptr;
	BackContaminationDecalMID = nullptr;
	Super::EndPlay(EndPlayReason);
}


void AMD_CuttableWall::Cut(AActor* Cutter)
{
	if (!HasAuthority() || bCut)
	{
		return;
	}

	// 전처리 미달이면 절단 잠김
	if (!CanCut())
	{
		const float Gate = (WallType == EMD_WallType::Strut) ? StrutCutGate : PartitionCutGate;
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Yellow,
			                                 FString::Printf(
				                                 TEXT("전처리 미완료: %.0f%% / %.0f%%"),
				                                 GetContaminationDemolitionReadyRatio() * 100.0f,
				                                 Gate));
		}
		return;
	}

	bCut = true;

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

	// 파편 드랍: 제거율 비율로 깨끗/오염 배분 (기획 §2.5: 제거율 70% → 7:3)
	const int32 CleanCount = FMath::RoundToInt(
		static_cast<float>(PieceCount) * GetContaminationCleanRatio());

	const FVector Origin = GetActorLocation();
	const FVector Extent = Mesh->Bounds.BoxExtent;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	for (int32 i = 0; i < PieceCount; ++i)
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

	// 경로 개방 (조각이 벽 자리를 대체)
	Destroy();
}


bool AMD_CuttableWall::CanCut() const
{
	// 초기화 실패 시 절단 금지
	if (!bContaminationSurfaceInitialized)
	{
		return false;
	}

	const float Gate = (WallType == EMD_WallType::Strut) ? StrutCutGate : PartitionCutGate;
	const float SafeGate = FMath::IsFinite(Gate)
		? FMath::Clamp(Gate, 0.0f, 100.0f)
		: 100.0f;

	return FMD_ScrapeFaceProgress::IsEitherFaceReadyForCut(
		GetContaminationCleanRatio(EMD_ScrapeSurfaceFace::Front),
		GetContaminationCleanRatio(EMD_ScrapeSurfaceFace::Back),
		SafeGate / 100.0f);
}


void AMD_CuttableWall::ScanHighlight(float Duration)
{
	// 콘솔/외부 호출에서 0이 들어와도 기본 지속시간 보장
	if (Duration <= 0.f)
	{
		Duration = 3.f;
	}

	if (!MID)
	{
		return;
	}
	
	const bool bStrut = (WallType == EMD_WallType::Strut);
	MID->SetVectorParameterValue(TEXT("Color"),
			bStrut ? FLinearColor(0.7f, 0.05f, 0.05f) : FLinearColor(0.05f, 0.6f, 0.1f));

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(reinterpret_cast<uint64>(this), Duration, FColor::Cyan,
				bStrut ? TEXT("스캔: 지주격벽 (위험)") : TEXT("스캔: 칸막이격벽 (자유 철거)"));
	}
	
	GetWorldTimerManager().SetTimer(ScanTimerHandle, this, &AMD_CuttableWall::RestoreScanColor, Duration, false);
}


void AMD_CuttableWall::RestoreScanColor()
{
	if (MID)
	{
		MID->SetVectorParameterValue(TEXT("Color"), NeutralWallColor);
	}
}
