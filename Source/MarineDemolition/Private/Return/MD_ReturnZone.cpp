// Fill out your copyright notice in the Description page of Project Settings.


#include "Return/MD_ReturnZone.h"
#include "Components/BoxComponent.h"
#include "Game/MD_GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "Player/MD_PlayerState.h"

AMD_ReturnZone::AMD_ReturnZone()
{
 	
	PrimaryActorTick.bCanEverTick = false;

	ReturnZoneCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("ReturnZoneCollision"));

	SetRootComponent(ReturnZoneCollision);

	ReturnZoneCollision->SetBoxExtent(FVector(200.0, 200.0, 100.0));

	ReturnZoneCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);

	ReturnZoneCollision->SetCollisionResponseToAllChannels(ECR_Ignore);

	ReturnZoneCollision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	ReturnZoneCollision->SetGenerateOverlapEvents(true);
}

void AMD_ReturnZone::BeginPlay()
{
	Super::BeginPlay();

	ReturnZoneCollision->OnComponentBeginOverlap.AddDynamic(
		this,
		&AMD_ReturnZone::OnReturnZoneBeginOverlap
	);
}

void AMD_ReturnZone::OnReturnZoneBeginOverlap(
	UPrimitiveComponent* OverlappedConponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult
)
{
	if (!HasAuthority())
	{
		return;
	}

	APawn* OverlappedPawn = Cast<APawn>(OtherActor);

	if (!IsValid(OverlappedPawn))
	{
		return;
	}

	AMD_PlayerState* MDPlayerState = OverlappedPawn->GetPlayerState<AMD_PlayerState>();

	if (!IsValid(MDPlayerState))
	{
		return;
	}

	if (MDPlayerState->GetReturnStatus() != EMD_ReturnStatus::Pending)
	{
		return;
	}

	const UWorld* World = GetWorld();

	if (!IsValid(World))
	{
		return;
	}

	const AMD_GameStateBase* MDGameState = World->GetGameState<AMD_GameStateBase>();

	if (!IsValid(MDGameState))
	{
		return;
	}

	if (MDGameState->HasReachedTime(18, 0))
	{
		return;
	}

	MDPlayerState->SetReturnStatus(EMD_ReturnStatus::Returned);

}

