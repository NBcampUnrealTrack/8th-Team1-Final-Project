// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MD_SubLevelLoadTrigger.generated.h"

UCLASS()
class MARINEDEMOLITION_API AMD_SubLevelLoadTrigger : public AActor
{
	GENERATED_BODY()
	
public:	
	AMD_SubLevelLoadTrigger();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"), Category = "Level Streaming")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"), Category = "Level Streaming")
	TObjectPtr<class UBoxComponent> BoxCollision;

	// Sub Level로 Load되는 umap 에셋
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level Streaming")
	TSoftObjectPtr<UWorld> SubLevelAsset;

	// Streaming된 서로 다른 Level을 구분하는 이름
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level Streaming")
	FString LevelInstanceName = TEXT("NewSubLevel");

	// Sub Level 로드되는 Persistent Level 기준 위치 좌표 (로드되는 Sub Level의 원점(0, 0, 0)이 해당 좌표의 위치에 로드됨)
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Level Streaming")
	FVector StreamingLocation = FVector::ZeroVector;

	// Sub Level 로드되는 Persistent Level 기준 회전 값 (로드되는 Sub Level이 해당 수치만큼 회전되어 로드됨)
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Level Streaming")
	FRotator StreamingRotation = FRotator::ZeroRotator;

	// 현재 Server 측에서 설정된 Sub Level Load에 대한 요청이 처리된 이력을 나타내는 변수
	UPROPERTY(VisibleInstanceOnly, ReplicatedUsing = OnRep_ShouldBeLoaded, Category = "Level Streaming")
	bool bShouldBeLoaded;

	// Streaming 된 Sub Level 인스턴스
	UPROPERTY(Transient)
	TObjectPtr<class ULevelStreamingDynamic> StreamedLevelInstance;

public:
    // 구역 내부 액터를 복원할 수 있는 로드 및 표시 완료 여부
    bool IsPersistentStreamingReady(bool bExpectedLoaded);

	// 지정된 Sub Level이 Load되었는지를 반환하는 함수
	UFUNCTION(BlueprintCallable, Category = "Level Streaming")
	bool IsLevelLoaded() const;

	// 지정된 Sub Level을 Load하는 함수
	UFUNCTION(BlueprintCallable, Category = "Level Streaming")
	bool LoadSubLevel();

	// Sub Level을 Unload하는 함수
	UFUNCTION(BlueprintCallable, Category = "Level Streaming")
	bool UnloadSubLevel();

protected:
	// TriggerBox에 임의 플레이어 Pawn이 감지되면 설정된 Level을 Streaming하도록 요청하는 함수
	UFUNCTION()
	void OnCollisionBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	// Server의 bShouldLoad 변수 값이 변경되면 Local에서 호출되는 함수
	UFUNCTION()
	void OnRep_ShouldBeLoaded();

	// 서버와 로컬의 현재 Level Streaming 상태를 동기화하는 함수
	UFUNCTION()
	void ApplyStreamingState();
};
