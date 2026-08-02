#include "Support/MD_SupportBrace.h"

#include "Components/StaticMeshComponent.h"
#include "Wall/MD_CuttableWall.h"

AMD_SupportBrace::AMD_SupportBrace()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	// 프로토 임시: 세로로 긴 기둥 형태. 메시는 BP에서 지정
	Mesh->SetRelativeScale3D(FVector(0.3f, 0.3f, 2.2f));
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

void AMD_SupportBrace::Weaken()
{
	if (SupportedWall)
	{
		SupportedWall->SetSupported(false);
	}

	AddActorLocalRotation(FRotator(0.f, 0.f, FMath::FRandRange(8.f, 15.f)));
}
