// Fill out your copyright notice in the Description page of Project Settings.


#include "Sector/MDSector.h"

#include "EngineUtils.h"
#include "Camera/CameraShakeBase.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

AMDSector::AMDSector()
{
	PrimaryActorTick.bCanEverTick = false;

	Volume = CreateDefaultSubobject<UBoxComponent>(TEXT("Volume"));
	SetRootComponent(Volume);
	Volume->SetBoxExtent(FVector(500.f, 500.f, 300.f));
	Volume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Volume->SetCollisionObjectType(ECC_WorldStatic);
	Volume->SetCollisionResponseToAllChannels(ECR_Ignore);
	Volume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Volume->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	Volume->SetGenerateOverlapEvents(true);
	
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		DebrisMesh = CubeMesh.Object;
	}

}

void AMDSector::BeginPlay()
{
	Super::BeginPlay();
	
	Volume->OnComponentBeginOverlap.AddDynamic(this, &AMDSector::OnVolumeBeginOverlap);
	Volume->OnComponentEndOverlap.AddDynamic(this, &AMDSector::OnVolumeEndOverlap);
}

AMDSector* AMDSector::FindSectorContaining(AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}
	
	for (TActorIterator<AMDSector> It(Actor->GetWorld()); It; ++It)
	{
		if (It->ContainsActor(Actor))
		{
			return *It;
		}
	}
	return nullptr;
}

void AMDSector::OnVolumeBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn)
	{
		return;
	}

	ActorsInside.Add(OtherActor);

	if (Pawn->IsPlayerControlled() && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(reinterpret_cast<uint64>(this), 3.f, FColor::Cyan,
				FString::Printf(TEXT("현재 섹터: %s"), *SectorName.ToString()));
	}
}

void AMDSector::OnVolumeEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	ActorsInside.Remove(OtherActor);
}

void AMDSector::CollapseCeiling()
{
	if (bCollapsed)
	{
		return;
	}
	bCollapsed = true;

	const FVector Origin = Volume->GetComponentLocation();
	const FVector Extent = Volume->GetScaledBoxExtent();

	// 1. 파편 낙하: 천장 높이에서 물리 큐브를 흩뿌린다
	for (int32 i = 0; i < DebrisCount; ++i)
	{
		const FVector SpawnLoc(
				Origin.X + FMath::FRandRange(-Extent.X, Extent.X) * 0.9f,
				Origin.Y + FMath::FRandRange(-Extent.Y, Extent.Y) * 0.9f,
				Origin.Z + Extent.Z - 30.f);
		const FRotator SpawnRot(FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f), 0.f);

		AStaticMeshActor* Debris = GetWorld()->SpawnActor<AStaticMeshActor>(
				AStaticMeshActor::StaticClass(), SpawnLoc, SpawnRot);
		if (!Debris)
		{
			continue;
		}

		Debris->SetMobility(EComponentMobility::Movable);
		UStaticMeshComponent* MeshComp = Debris->GetStaticMeshComponent();
		MeshComp->SetStaticMesh(DebrisMesh);
		Debris->SetActorScale3D(FVector(FMath::FRandRange(0.2f, 0.8f)));
		MeshComp->SetCollisionProfileName(TEXT("PhysicsActor"));
		MeshComp->SetSimulatePhysics(true);
		Debris->SetLifeSpan(12.f);
	}

	// 2. 섹터 내부 폰/샘플 피해
	TArray<AActor*> Overlapping;
	Volume->GetOverlappingActors(Overlapping);
	for (AActor* Actor : Overlapping)
	{
		UGameplayStatics::ApplyDamage(Actor, CollapseDamage, nullptr, this, nullptr);
	}

	// 3. 카메라 쉐이크 (BP 에셋을 CollapseShakeClass에 지정)
	if (CollapseShakeClass)
	{
		UGameplayStatics::PlayWorldCameraShake(this, CollapseShakeClass, Origin, 0.f, 8000.f);
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red,
				FString::Printf(TEXT("!! 섹터 [%s] 천장 붕괴 !!"), *SectorName.ToString()));
	}
}