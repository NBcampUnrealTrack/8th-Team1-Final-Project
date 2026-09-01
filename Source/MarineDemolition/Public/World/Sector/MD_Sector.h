// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MD_Sector.generated.h"

class UBoxComponent;
class UCameraShakeBase;
class UNiagaraSystem;
class AMD_WallPiece;

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

	// 붕괴 순간 서버가 쏘고 모든 클라이언트가 코스메틱(쉐이크 + VFX)을 재생한다
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayCollapseFX();

	UPROPERTY(VisibleAnywhere, Category = "Sector")
	TObjectPtr<UBoxComponent> Volume;

	// 붕괴 시 낙하시킬 파편 개수
	UPROPERTY(EditAnywhere, Category = "Collapse")
	int32 DebrisCount = 30;

	// 붕괴 시 섹터 내부 폰/샘플에 주는 피해
	UPROPERTY(EditAnywhere, Category = "Collapse")
	float CollapseDamage = 40.f;

	// 붕괴 잔해로 스폰할 조각벽 클래스. CuttableWall의 PieceClass와 같은 BP를 에디터에서 지정
	UPROPERTY(EditAnywhere, Category = "Collapse")
	TSubclassOf<AMD_WallPiece> DebrisPieceClass;

	// 붕괴 잔해 랜덤 스케일 최소값
	UPROPERTY(EditAnywhere, Category = "Collapse", meta = (ClampMin = "0.01"))
	float DebrisScaleMin = 0.2f;

	// 붕괴 잔해 랜덤 스케일 최대값
	UPROPERTY(EditAnywhere, Category = "Collapse", meta = (ClampMin = "0.01"))
	float DebrisScaleMax = 0.8f;

	// 붕괴 연출용 카메라 쉐이크. BP 에셋을 에디터에서 지정
	UPROPERTY(EditAnywhere, Category = "Collapse")
	TSubclassOf<UCameraShakeBase> CollapseShakeClass;

	// 붕괴 순간 볼륨 중심에 원샷 재생할 잔해 VFX (NS_Sector_Collapse)
	UPROPERTY(EditAnywhere, Category = "Collapse")
	TObjectPtr<UNiagaraSystem> CollapseEffect;

	UPROPERTY(Transient)
	TSet<TObjectPtr<AActor>> ActorsInside;

	bool bCollapsed = false;
};
