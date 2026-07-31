// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MD_PortalActor.generated.h"

/*
 * 거리가 있는 서로 다른 두 지점을 연결하는 Portal
 */
UCLASS(Blueprintable)
class MARINEDEMOLITION_API AMD_PortalActor : public AActor
{
	GENERATED_BODY()

public:
	AMD_PortalActor();

	virtual void Tick(float DeltaSeconds) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

protected:
	// Portal의 Root Component
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Portal|Components")
	TObjectPtr<USceneComponent> SceneRoot;

	// 연결된 Portal에서 캡처한 RenderTarget을 표시하는 표면 StaticMesh Component
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Portal|Components")
	TObjectPtr<UStaticMeshComponent> SurfaceMesh;

	// Portal을 통과하는 Actor들을 감지하는 Collision Component
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Portal|Components")
	TObjectPtr<class UBoxComponent> BoxCollision;

	// 연결된 Portal이 자신의 표면에 표시할 장면을 현재 Portal 공간에서 캡처하는 Component
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Portal|Components")
	TObjectPtr<class USceneCaptureComponent2D> SceneCaptureComponent;

	// 연결된 다른 Portal
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Replicated, Category = "Portal")
	TWeakObjectPtr<AMD_PortalActor> LinkedPortal;

	// Portal을 통과하여 다른 장소로 이동 가능 여부를 나타내는 변수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_PortalActive, Category = "Portal")
	bool bIsPortalActive = true;

	// Portal 이용 시 진입 방향 제한 여부
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Portal")
	bool bFrontSideOnly = true;

	// Portal을 통한 이동 시 연결된 다른 Portal의 +X(Local)방향 위치 보정 값
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal", meta = (ClampMin = "0.0"))
	float ExitOffset = 1.0f;

	// Local 환경 기준 연결된 다른 Portal에 비춰지는 장면을 실시간으로 캡처하는 여부를 나타내는 변수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Rendering")
	bool bSurfaceRenderEnabled = true;

	// SurfaceMesh에 적용되는 Material의 Texture Parameter 이름
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal|Rendering")
	FName MaterialTextureParameterName = TEXT("TextureRenderTarget");

private:
	// 연결된 다른 Portal에서 캡처된 Scene을 저장하는 TextureRenderTarget2D
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> TextureRenderTarget;

	// 설정된 Material 또는 Material Instance를 통하여 Runtime에 생성되고 SurfaceMesh에 적용되는 Material Instance
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PortalMaterialInstance;

	// 감지 영역 내부에 존재하는 다른 Actor에 대하여 현재 Portal 사이의 거리 및 방향 정보를 저장하는 변수
	TMap<TWeakObjectPtr<AActor>, float> TrackedActorDistances;

	// 이동된 다른 Actor에 대하여 되돌아 가는 현상을 방지하기 위해 이동된 Actor를 저장하는 변수
	TSet<TWeakObjectPtr<AActor>> ArrivalGuardActors;

public:
	// 다른 Portal에 연결하는 함수
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Portal")
	bool ConnectToOtherPortal(AMD_PortalActor* OtherPortal);

	// 기존 Portal의 연결을 제거하는 함수
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Portal")
	void DisconnectPortal();

	// 연결된 Portal의 위치로 Actor를 이동시키는 함수
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Portal")
	bool TeleportActor(AActor* OtherActor);

	// 현재 Portal의 활성화 여부를 설정하는 함수
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Portal")
	void SetPortalActive(bool bNewActive);

	// 현재 Portal의 활성화 여부를 반환하는 함수
	UFUNCTION(BlueprintPure, Category = "Portal")
	bool IsPortalActive() const { return bIsPortalActive; }

	// 임의 Actor가 Portal 이용 가능 여부를 반환하는 함수
	UFUNCTION(BlueprintCallable, Category = "Portal")
	bool IsActorCanUsePortal(AActor* Actor) const;

protected:
	// 서버의 Portal 활성화 상태가 변경되었을 경우 Local에서 실행되는 함수
	UFUNCTION()
	void OnRep_PortalActive();

	UFUNCTION()
	void OnCollisionBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnCollisionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex);

	// 해당 위치 좌표에 대응하는 연결된 Portal 기준 위치 좌표를 계산하는 함수
	UFUNCTION(BlueprintCallable, Category = "Portal")
	FVector TransformLocationToLinkedPortal(const FVector& WorldLocation) const;

	// 해당 방향에 대응하는 연결된 Portal 기준 방향을 계산하는 함수
	UFUNCTION(BlueprintCallable, Category = "Portal")
	FVector TransformDirectionToLinkedPortal(const FVector& WorldDirection) const;

	// 해당 회전 값에 대응하는 연결된 Portal 기준 회전 값을 계산하는 함수
	UFUNCTION(BlueprintCallable, Category = "Portal")
	FRotator TransformRotationToLinkedPortal(const FRotator& WorldRotation) const;

private:
	// UTextureRenderTarget2D의 인스턴스를 생성하는 함수
	void CreatePortalTextureRenderTarget();

	// Portal의 SurfaceMesh에 Material Instance를 동적으로 생성 및 할당하고, TextureRenderTarget를 Parameter에 등록하는 함수
	void InitPortalTextureRenderTarget();

	// 현재 Portal의 표면에 필요한 Scene을 캡처하는 ScencCapture Component의 Transform을 업데이트하는 함수
	void UpdatePortalCapture();

	// 현재 Portal의 영역 내부에서 감지된 Actor들을 검토하고 Teleport 또는 목록에서 제거하는 함수
	void UpdateTrackedActors();

	// Portal의 현재 위치와 임의 좌표 사이의 부호가 포함된 거리를 계산하는 함수
	float GetSignedDistance(const FVector& WorldLocation) const;

	// 현재 Portal과 연결된 Portal 사이의 회전 값 차이를 계산하는 함수
	FQuat GetPortalQuatDelta() const;
};
