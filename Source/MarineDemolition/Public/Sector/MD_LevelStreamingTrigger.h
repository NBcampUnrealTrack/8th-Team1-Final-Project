// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MD_LevelStreamingTrigger.generated.h"

UCLASS()
class MARINEDEMOLITION_API AMD_LevelStreamingTrigger : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AMD_LevelStreamingTrigger();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"), Category = "Level Streaming")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"), Category = "Level Streaming")
	TObjectPtr<class UBoxComponent> BoxCollision;

	// Streaming 되어야 하는 Level
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level Streaming")
	TSoftObjectPtr<UWorld> StreamingMap;

	// 서로 다른 Streaming된 Level을 구분하는 이름
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level Streaming")
	FString LevelInstanceName;

	// 현재 Server 측에서 Level Streaming 요청이 처리된 이력을 나타내는 변수
	UPROPERTY(VisibleInstanceOnly, ReplicatedUsing = OnRep_ShouldLoad, Category = "Level Streaming")
	bool bShouldLoad;

	// Local Client 측에서 Level Streaming 요청이 처리된 이력을 나타내는 변수
	UPROPERTY(Transient)
	bool bLoadApplied = false;

	// Streaming 된 Level 인스턴스
	UPROPERTY(Transient)
	TObjectPtr<class ULevelStreamingDynamic> StreamedLevelInstance;

protected:
	// TriggerBox에 임의 플레이어 Pawn이 감지되면 설정된 Level을 Streaming하도록 요청하는 함수
	UFUNCTION()
	void OnCollisionBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	// Server의 bShouldLoad 변수 값이 변경되면 Local에서 호출되는 함수
	UFUNCTION()
	void OnRep_ShouldLoad();

	// 서버와 로컬의 현재 Level Streaming 상태를 동기화하는 함숫
	UFUNCTION()
	void ApplyStreamingState();
};
