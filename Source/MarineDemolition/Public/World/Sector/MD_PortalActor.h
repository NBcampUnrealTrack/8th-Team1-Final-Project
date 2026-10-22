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
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, ReplicatedUsing = OnRep_LinkedPortal, Category = "Portal")
	TWeakObjectPtr<AMD_PortalActor> LinkedPortal;

	// Portal을 통과하여 다른 장소로 이동 가능 여부를 나타내는 변수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_PortalActive, Category = "Portal")
	bool bIsPortalActive = true;

	// Portal 이용 시 진입 방향 제한 여부
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Portal")
	bool bFrontSideOnly = true;

	// Local 환경 기준 연결된 다른 Portal에 비춰지는 장면을 실시간으로 캡처하는 여부를 나타내는 변수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Rendering")
	bool bSurfaceRenderEnabled = true;

	// 화면 점유율과 접근 거리에 따른 캡처 품질 자동 조절 여부
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Rendering|Performance")
	bool bAdaptiveCaptureEnabled = true;

	// 작게 보이는 Portal의 캡처 해상도 배율
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Rendering|Performance", meta = (ClampMin = "0.25", ClampMax = "1.0"))
	float DistantCaptureResolutionScale = 0.5f;

	// 작게 보이는 Portal의 초당 최대 캡처 횟수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Rendering|Performance", meta = (ClampMin = "1.0", ClampMax = "120.0"))
	float DistantCaptureRate = 30.0f;

	// 가로 또는 세로 화면 점유율이 이 값 이상일 때 원본 품질로 전환하는 기준
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Rendering|Performance", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float FullQualityScreenRatio = 0.35f;

	// Portal 표면 경계까지의 거리가 이 값 이하일 때 매 프레임 원본 품질을 유지하는 거리
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Rendering|Performance", meta = (ClampMin = "0.0", Units = "cm"))
	float FullQualityDistance = 250.0f;

	// 원본 품질 Portal의 캡처 시점이 직전 캡처와 같을 때의 초당 최대 캡처 횟수 (0이면 매 프레임 캡처)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Rendering|Performance", meta = (ClampMin = "0.0", ClampMax = "120.0"))
	float NearIdleCaptureRate = 30.0f;

	// Portal을 통한 이동 시 연결된 다른 Portal의 +X(Local)방향 위치 보정 값
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal", meta = (ClampMin = "0.0"))
	float ExitOffset = 1.0f;

	// SurfaceMesh에 적용되는 Material의 Texture Parameter 이름
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal|Rendering")
	FName MaterialTextureParameterName = TEXT("TextureRenderTarget");

	// Clip Plane 오차에 의한 Portal의 표면 아래쪽이 잘리는 것을 방지하기 위한 Backward 방향 Offset
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal|Rendering")
	float ClipPlaneOffset = 1.0f;

	// 직전 프레임의 캡처 가능 여부
	bool bCaptureWasVisible = false;

	// 현재 적용 중인 원본 캡처 품질 여부
	bool bFullQualityCapture = true;

	// 연결 또는 활성화 상태 변경 직후 캡처 예약 여부
	bool bForceNextPortalCapture = true;

	// 다음 저빈도 캡처까지 남은 실제 시간
	float SecondsUntilNextCapture = 0.0f;

	// 마지막 캡처에 사용된 로컬 화면 크기
	FIntPoint LastCaptureViewSize = FIntPoint::ZeroValue;

	// 마지막 캡처에 사용된 캡처 위치
	FVector LastCaptureLocation = FVector::ZeroVector;

	// 마지막 캡처에 사용된 캡처 회전
	FQuat LastCaptureRotation = FQuat::Identity;

	// 마지막 캡처에 사용된 투영 행렬
	FMatrix LastCaptureProjection = FMatrix::Identity;

private:
	// 연결된 다른 Portal에서 캡처된 Scene을 저장하는 TextureRenderTarget2D
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> TextureRenderTarget;

	// SurfaceMesh에 적용된 최초 Material 또는 Material Instance를 통하여 Runtime에 생성되고 SurfaceMesh에 적용되는 Material Instance
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PortalMaterialInstance;

	// 감지 영역 내부에 존재하는 다른 Actor에 대하여 현재 Portal 사이의 거리 및 방향 정보를 저장하는 변수
	TMap<TWeakObjectPtr<AActor>, float> MapDistanceWithOverlappedActor;

	// 이동된 다른 Actor에 대하여 바로 되돌아 가는 현상을 방지하기 위해 이동된 Actor를 저장하는 변수
	TSet<TWeakObjectPtr<AActor>> IgnoreActors;

	// Portal을 관측하는 Local Player 객체
	UPROPERTY(Transient)
	TObjectPtr<ULocalPlayer> LocalPlayer;

public:
	// 다른 Portal에 일방적으로 연결하는 함수 (반대편에서 현재 Portal로 연결되지 않음)
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Portal")
	bool ConnectToOtherPortal(AMD_PortalActor* OtherPortal);

	// 기존 Portal의 연결을 제거하는 함수 (반대편의 Portal 연결 해제 포함)
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

	// 서로 다른 2개의 Portal을 연결하는 함수
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Portal")
	static void ConnectPortal(AMD_PortalActor* PortalA, AMD_PortalActor* PortalB);

	// 비균일 포털 크기에서도 같은 광선을 유지하는 캡처 카메라와 투영 계산
	static bool BuildPortalCaptureView(FTransform SourcePortal, FTransform DestinationPortal, FVector ViewOrigin, FMatrix ViewRotation, FMatrix Projection,
		FVector& OutLocation, FQuat& OutRotation, FMatrix& OutProjection);

	// 로컬 화면과 Portal 경계의 교차 여부 및 최대 화면 점유율 계산
	static bool GetPortalScreenRatio(FMatrix ViewProjection, FBoxSphereBounds SurfaceBounds, FIntPoint ViewSize, float& OutScreenRatio);

	// 축소 전환에 여유 구간을 적용하여 경계에서 품질의 반복 변경 방지
	static bool ShouldUseFullQualityCapture(bool bWasFullQuality, float ScreenRatio, float SurfaceDistance, float ScreenThreshold, float NearDistance);

	// 프레임 속도 변화에 따른 나머지 시간을 보존하는 수동 캡처 주기 계산
	static bool ShouldCapturePortal(float& SecondsUntilCapture, float DeltaSeconds, float CaptureRate, bool bForceCapture);

protected:
	// 서버의 LinkedPortal 변수 값이 변경되었을 경우 Local Client에서 실행되는 함수
	UFUNCTION()
	void OnRep_LinkedPortal();

	// 서버의 Portal 활성화 상태가 변경되었을 경우 Local Client에서 실행되는 함수
	UFUNCTION()
	void OnRep_PortalActive();

	UFUNCTION()
	void OnCollisionBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnCollisionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex);

	// 해당 위치 좌표에 대응하는 연결된 Portal 기준 위치 좌표를 계산하는 함수
	UFUNCTION()
	FVector TransformLocationToLinkedPortal(const FVector& WorldLocation) const;

	// 해당 방향에 대응하는 연결된 Portal 기준 방향을 계산하는 함수
	UFUNCTION()
	FVector TransformDirectionToLinkedPortal(const FVector& WorldDirection) const;

	// 해당 회전 값에 대응하는 연결된 Portal 기준 회전 값을 계산하는 함수
	UFUNCTION()
	FRotator TransformRotationToLinkedPortal(const FRotator& WorldRotation) const;

private:
	// Portal의 SurfaceMesh에 Material Instance를 동적으로 생성 및 할당하고, TextureRenderTarget를 Parameter에 등록하는 함수
	void InitPortalTextureRenderTarget();

	// 현재 Portal의 표면에 필요한 Scene을 캡처하는 SceneCapture Component의 Transform을 업데이트하는 함수
	void UpdatePortalCaptureScene(float DeltaSeconds);

	// 캡처 품질에 맞는 크기로 Texture Render Target을 변경하는 함수
	void UpdatePortalTextureRenderTargetSize(FIntPoint TargetSize);

	// 현재 Portal의 영역 내부에서 감지된 Actor들을 검토하고 Teleport 또는 목록에서 제거하는 함수
	void UpdateOverlappedActors();

	// Portal의 현재 위치와 임의 좌표 사이의 부호가 포함된 거리를 계산하는 함수
	float GetSignedDistance(const FVector& WorldLocation) const;

	// 현재 Portal과 연결된 Portal 사이의 회전 값 차이를 계산하는 함수
	FQuat GetPortalQuatDelta() const;

	// UMovementComponent의 Velocity를 변경하기 위한 RPC 함수
	UFUNCTION(NetMulticast, Reliable)
	void NetMulticastUpdateMovementComponentVelocity(class UMovementComponent* MovementComponent, FVector NewVelocity);
};
