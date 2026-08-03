// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MD_Sector.generated.h"

class UBoxComponent;
class UCameraShakeBase;

// 방 단위 섹터 볼륨. 내부 폰을 추적하고 천장 붕괴를 실행

UCLASS()
class MARINEDEMOLITION_API AMD_Sector : public AActor
{
	GENERATED_BODY()

public:
	AMD_Sector();

	// Actor가 현재 속한 섹터를 반환. 없으면 nullptr
	static AMD_Sector* FindSectorContaining(AActor* Actor);

	// 이 섹터의 천장 붕괴: 파편 낙하 + 내부 피해 + 카메라 쉐이크
	UFUNCTION(BlueprintCallable, Category = "Sector")
	void CollapseCeiling();

	bool ContainsActor(AActor* Actor) const { return ActorsInside.Contains(Actor); }

	UPROPERTY(EditAnywhere, Category = "Sector")
	FName SectorName = NAME_None;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnVolumeBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	                          UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
	                          const FHitResult& SweepResult);

	UFUNCTION()
	void OnVolumeEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	                        UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	UPROPERTY(VisibleAnywhere, Category = "Sector")
	TObjectPtr<UBoxComponent> Volume;

	// 붕괴 시 낙하시킬 파편 개수
	UPROPERTY(EditAnywhere, Category = "Collapse")
	int32 DebrisCount = 30;

	// 붕괴 시 섹터 내부 폰/샘플에 주는 피해
	UPROPERTY(EditAnywhere, Category = "Collapse")
	float CollapseDamage = 40.f;

	UPROPERTY(EditAnywhere, Category = "Collapse")
	TObjectPtr<UStaticMesh> DebrisMesh;

	// 붕괴 연출용 카메라 쉐이크. BP 에셋을 에디터에서 지정
	UPROPERTY(EditAnywhere, Category = "Collapse")
	TSubclassOf<UCameraShakeBase> CollapseShakeClass;

	UPROPERTY(Transient)
	TSet<TObjectPtr<AActor>> ActorsInside;

	bool bCollapsed = false;
};
