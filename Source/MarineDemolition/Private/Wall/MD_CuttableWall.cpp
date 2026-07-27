#include "Wall/MD_CuttableWall.h"

#include "TimerManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sector/MD_Sector.h"
#include "Wall/MD_WallPiece.h"

namespace
{
	const FLinearColor NeutralWallColor(0.4f, 0.4f, 0.45f);
}

AMD_CuttableWall::AMD_CuttableWall()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetRelativeScale3D(FVector(3.f, 0.2f, 2.5f));

	PieceClass = AMD_WallPiece::StaticClass();
}

void AMD_CuttableWall::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 스캔 전에는 타입 구분 없이 중립 회색
	MID = Mesh->CreateAndSetMaterialInstanceDynamic(0);
	if (MID)
	{
		MID->SetVectorParameterValue(TEXT("Color"), NeutralWallColor);
	}
}

void AMD_CuttableWall::BeginPlay()
{
	Super::BeginPlay();

	// Transient인 MID는 레벨 로드/PIE 복제 시 유실되므로 런타임에서 재생성
	MID = Mesh->CreateAndSetMaterialInstanceDynamic(0);
	if (MID)
	{
		MID->SetVectorParameterValue(TEXT("Color"), NeutralWallColor);
	}
}

void AMD_CuttableWall::Cut(AActor* Cutter)
{
	if (bCut)
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
				                                 TEXT("전처리 미완료: %.0f%% / %.0f%%"), ContaminationRemoved, Gate));
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
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("MD_CuttableWall::Cut - 절단자가 속한 섹터를 찾지 못함"));
		}
	}

	// 파편 드랍: 제거율 비율로 깨끗/오염 배분 (기획 §2.5: 제거율 70% → 7:3)
	const int32 CleanCount = FMath::RoundToInt(PieceCount * ContaminationRemoved / 100.f);

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

void AMD_CuttableWall::AddPreprocess(float Amount)
{
	ContaminationRemoved = FMath::Clamp(ContaminationRemoved + Amount, 0.0f, 100.0f);
}

bool AMD_CuttableWall::CanCut() const
{
	const float Gate = (WallType == EMD_WallType::Strut) ? StrutCutGate : PartitionCutGate;
	return ContaminationRemoved >= Gate;
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
