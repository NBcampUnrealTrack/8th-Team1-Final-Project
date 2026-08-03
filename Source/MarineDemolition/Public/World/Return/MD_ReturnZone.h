// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MD_ReturnZone.generated.h"

class UBoxComponent;

UCLASS()
class MARINEDEMOLITION_API AMD_ReturnZone : public AActor
{
	GENERATED_BODY()
	
public:	
	AMD_ReturnZone();

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Return Zone")
	TObjectPtr<UBoxComponent> ReturnZoneCollision;

	UFUNCTION()
	void OnReturnZoneBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult
	);
};
