#include "Wall/MD_CuttableWall.h"

#include "Materials/MaterialInstanceDynamic.h"
#include "Sector/MD_Sector.h"
#include "UObject/ConstructorHelpers.h"

AMD_CuttableWall::AMD_CuttableWall()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetRelativeScale3D(FVector(3.f, 0.2f, 2.5f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Mesh->SetStaticMesh(CubeMesh.Object);
	}
}

void AMD_CuttableWall::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 임시 시인성: 지주격벽은 붉게, 칸막이격벽은 회색으로
	if (UMaterialInstanceDynamic* MID = Mesh->CreateAndSetMaterialInstanceDynamic(0))
	{
		MID->SetVectorParameterValue(TEXT("Color"),
			bIsStrutWall ? FLinearColor(0.6f, 0.08f, 0.08f) : FLinearColor(0.4f, 0.4f, 0.45f));
	}
}

void AMD_CuttableWall::Cut(AActor* Cutter)
{
	if (bCut)
	{
		return;
	}
	bCut = true;

	if (bIsStrutWall && !bHasSupport)
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

	// 경로 개방 (임시: 벽 제거. 파편 드랍은 격벽 철거 카드에서)
	Destroy();
}
