// Fill out your copyright notice in the Description page of Project Settings.


#include "World/Sector/MD_PortalActor.h"
#include "Components/BoxComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "GameFramework/MovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "Util/MD_LogUtility.h"

AMD_PortalActor::AMD_PortalActor()
{
	// Set this actor to call Tick() every frame. You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	SetReplicates(true);

	// 플레이어와 거리가 멀어져도 Network 동기화를 유지하기 위한 설정
	bAlwaysRelevant = true;

	SetReplicateMovement(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	SurfaceMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SurfaceMesh"));
	SurfaceMesh->SetupAttachment(SceneRoot);
	// Collision 및 그림자 제거
	SurfaceMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SurfaceMesh->SetCastShadow(false);

	BoxCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("BoxCollision"));
	BoxCollision->SetupAttachment(SceneRoot);
	// 물리적 충돌 검사에서 Portal의 Collision 제외
	BoxCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	// 일부 Collision 채널에 대해서만 Overlap Event 실행
	BoxCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	BoxCollision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	BoxCollision->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
	BoxCollision->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	BoxCollision->SetGenerateOverlapEvents(true);

	SceneCaptureComponent = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SceneCaptureComponent"));
	SceneCaptureComponent->SetupAttachment(SceneRoot);
	// 매 프레임 또는 컴포넌트의 Transform 값 변화 시 자동 Scene Capture 미 사용
	SceneCaptureComponent->bCaptureEveryFrame = false;
	SceneCaptureComponent->bCaptureOnMovement = false;
	// 캡처된 Scene에 대하여 특정 평면으로 나누어 일부만 표시하도록 설정
	SceneCaptureComponent->bEnableClipPlane = true;
	SceneCaptureComponent->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
	SceneCaptureComponent->bUseCustomProjectionMatrix = true;

	// Player Camera의 위치가 변경된 이후 Portal에 비치는 Scene을 Capture하기 위하여 Tick Group 설정
	SetTickGroup(ETickingGroup::TG_PostUpdateWork);
}

void AMD_PortalActor::BeginPlay()
{
	Super::BeginPlay();

	// TextureRenderTarget 초기화 및 현재 Portal에 적용되는 Material Instance의 Parameter 등록
	InitPortalTextureRenderTarget();
	
	BoxCollision->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::OnCollisionBeginOverlap);

	BoxCollision->OnComponentEndOverlap.AddDynamic(this, &ThisClass::OnCollisionEndOverlap);

	// 서버와 로컬의 현재 Portal 연결 및 동작 여부 동기화
	OnRep_LinkedPortal();
	OnRep_PortalActive();
}

void AMD_PortalActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 서버 환경 확인
	if (HasAuthority() == true)
	{
		// 통과 영역에 있는 액터의 현재 위치 검사.
		UpdateOverlappedActors();
	}

	// 로컬 플레이어의 카메라를 기준으로 Portal의 화면 캡처 갱신
	UpdatePortalCaptureScene();
}

void AMD_PortalActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ThisClass, LinkedPortal);
	DOREPLIFETIME(ThisClass, bFrontSideOnly);
	DOREPLIFETIME(ThisClass, bIsPortalActive);
}

bool AMD_PortalActor::ConnectToOtherPortal(AMD_PortalActor* OtherPortal)
{
	// Client 환경 실행 제한
	if (HasAuthority() == false)
	{
		return false;
	}

	// 연결하려는 Portal이 유효하지 않거나 자신과 연결하려는 경우 함수 조기 종료
	if (IsValid(OtherPortal) == false || OtherPortal == this)
	{
		return false;
	}

	// 기존에 연결된 Portal에 연결하려는 경우 함수 조기 종료
	if (LinkedPortal.IsValid() == true && LinkedPortal.Get() == OtherPortal)
	{
		return true;
	}

	// 이전 연결 Portal의 연결 제거
	DisconnectPortal();

	// 연결된 Portal 등록
	LinkedPortal = OtherPortal;

	// 연결된 Portal 변화 Notify 함수 실행 (Listen Server 환경 고려)
	OnRep_LinkedPortal();

	return true;
}

void AMD_PortalActor::DisconnectPortal()
{
	// Client 환경 실행 제한
	if (HasAuthority() == false)
	{
		return;
	}

	// 연결된 Portal이 존재하지 않으면 함수 조기 종료
	if (LinkedPortal.IsValid() == false)
	{
		return;
	}

	// 기존 연결된 Portal 참조
	AMD_PortalActor* OtherPortal = LinkedPortal.Get();

	// Portal 연결 해제
	LinkedPortal.Reset();

	// 기존 연결된 Portal의 연결 끊기
	if (IsValid(OtherPortal) == true && OtherPortal->LinkedPortal == this)
	{
		OtherPortal->DisconnectPortal();
	}

	OnRep_LinkedPortal();
}

bool AMD_PortalActor::TeleportActor(AActor* OtherActor)
{
	// Client 환경 실행 제한
	if (HasAuthority() == false)
	{
		return false;
	}
	
	// 현재 Portal이 비활성화 또는 연결된 다른 Portal이 존재하지 않는 경우 함수 조기 종료 
	if (bIsPortalActive == false || LinkedPortal.IsValid() == false)
	{
		return false;
	}

	// Actor가 Portal을 사용하지 못하는 경우 함수 조기 종료
	if (IsActorCanUsePortal(OtherActor) == false)
	{
		return false;
	}

	// Actor의 RootComponent를 PrimitiveComponent로 변환 (물리량 정보를 얻기 위함)
	UPrimitiveComponent* PrimitiveRoot = Cast<UPrimitiveComponent>(OtherActor->GetRootComponent());

	// Chaos Physics Simulation 사용 여부 검사
	bool bUseSimulatesPhysics = IsValid(PrimitiveRoot) == true && PrimitiveRoot->IsSimulatingPhysics() == true;

	// 이동 속도 및 회전 속도 저장
	FVector LinearVelocity = bUseSimulatesPhysics == true ? PrimitiveRoot->GetPhysicsLinearVelocity() : OtherActor->GetVelocity();
	FVector AngularVelocity = bUseSimulatesPhysics == true ? PrimitiveRoot->GetPhysicsAngularVelocityInRadians() : FVector::ZeroVector;

	// Teleport 이후의 위치 및 회전 값 계산
	FVector TeleportedLocation = TransformLocationToLinkedPortal(OtherActor->GetActorLocation()) + ExitOffset * LinkedPortal->GetActorForwardVector();
	FRotator TeleportedRotation = TransformRotationToLinkedPortal(OtherActor->GetActorRotation());

	// Teleport 이후의 이동 속도 및 회전 속도 계산
	FVector TeleportedLinearVelocity = TransformDirectionToLinkedPortal(LinearVelocity);
	FVector TeleportedAngularVelocity = TransformDirectionToLinkedPortal(AngularVelocity);

	// 연결된 다른 Portal에서 다시 Teleport 되는 것을 방지하기 위하여 연결된 Portal의 Guard 목록에 Actor 추가
	LinkedPortal->IgnoreActors.Add(OtherActor);

	// Teleport 실행
	bool bWasTeleportSuccessful = OtherActor->SetActorLocationAndRotation(TeleportedLocation, TeleportedRotation, false, nullptr, ETeleportType::TeleportPhysics);

	// Teleport 실패 시 함수 조기 종료
	if (bWasTeleportSuccessful == false)
	{
		// 연결된 다른 Portal의 Guard 목록에서 Actor 제거
		LinkedPortal->IgnoreActors.Remove(OtherActor);

		return false;
	}

	// Teleport 이후 계산된 물리량 적용
	if (bUseSimulatesPhysics == true)
	{
		PrimitiveRoot->SetPhysicsLinearVelocity(TeleportedLinearVelocity);
		PrimitiveRoot->SetPhysicsAngularVelocityInRadians(TeleportedAngularVelocity);
	}
	else
	{
		// Chaos Physics Simulation 미사용 Actor의 경우 MovementComponent 검사
		UMovementComponent* MovementComponent = OtherActor->FindComponentByClass<UMovementComponent>();
		if (IsValid(MovementComponent) == true)
		{
			// Teleport 이후 이동 속도 적용
			MovementComponent->Velocity = TeleportedLinearVelocity;

			// ComponentVelocity를 적용된 이동 속도와 동기화
			//MovementComponent->UpdateComponentVelocity();
			ClientUpdateMovementComponentVelocity(MovementComponent, TeleportedLinearVelocity);
		}

		// Controller에 의하여 제어되고 있는 경우 Controller에 Teleport 이후 회전 값 적용
		APawn* Pawn = Cast<APawn>(OtherActor);
		if (IsValid(Pawn) == true)
		{
			AController* Controller = Pawn->GetController();
			if (IsValid(Controller) == true)
			{
				// Teleport 이후 회전값 계산
				FRotator TeleportedControllerRotation = TransformRotationToLinkedPortal(Controller->GetControlRotation());

				// 회전 값 적용
				//Controller->SetControlRotation(TeleportedControllerRotation);
				Controller->ClientSetRotation(TeleportedControllerRotation, true);
			}
		}
	}

	// 현재 Portal의 추적 목록에서 Actor 제거
	MapDistanceWithOverlappedActor.Remove(OtherActor);

	// Actor의 위치, 회전, 물리량 등의 정보 변경에 대하여 즉시 동기화 요청
	OtherActor->ForceNetUpdate();

	return true;
}

void AMD_PortalActor::SetPortalActive(bool bNewActive)
{
	// Client 환경 실행 제한
	if (HasAuthority() == false)
	{
		return;
	}

	// 이전 상태와 동일한 값으로 변경하려는 경우 함수 조기 종료
	if (bIsPortalActive == bNewActive)
	{
		return;
	}

	// Portal 활성화 상태 변경
	bIsPortalActive = bNewActive;

	// Listen Server 환경 고려, 상태 변경 이벤트 실행
	OnRep_PortalActive();

	// 네트워크 동기화 강제 실행
	ForceNetUpdate();
}

bool AMD_PortalActor::IsActorCanUsePortal(AActor* OtherActor) const
{
	// Portal을 사용하려는 액터의 유효성 검사
	if (IsValid(OtherActor) == false)
	{
		return false;
	}

	// 현재 Portal과 동일한 Actor 여부 검사
	if (OtherActor == this)
	{
		return false;
	}

	// 다른 Portal Actor의 경우 Portal 이용 불가
	if (LinkedPortal.Get() == OtherActor || OtherActor->IsA<AMD_PortalActor>() == true)
	{
		return false;
	}

	// Root Component가 이동 불가능한 유형의 경우 Portal 이용 불가
	USceneComponent* Root = OtherActor->GetRootComponent();
	if (IsValid(Root) == false || Root->GetMobility() != EComponentMobility::Movable)
	{
		return false;
	}

	// 다른 Portal으로부터 현재 Portal의 영역으로 Teleport된 Actor의 경우 Portal 이용 불가 판정
	if (IgnoreActors.Contains(OtherActor) == true)
	{
		return false;
	}

	return true;
}

void AMD_PortalActor::ConnectPortal(AMD_PortalActor* PortalA, AMD_PortalActor* PortalB)
{
	// 연결하려는 Portal의 유효성 검사
	if (IsValid(PortalA) == false || IsValid(PortalB) == false)
	{
		return;
	}

	// 서버 환경 검사
	if (PortalA->HasAuthority() == false || PortalB->HasAuthority() == false)
	{
		return;
	}

	// 재귀적 연결 검사
	if (PortalA == PortalB)
	{
		return;
	}

	// Portal의 기존 연결 제거
	PortalA->DisconnectPortal();
	PortalB->DisconnectPortal();

	// Portal 연결
	PortalA->ConnectToOtherPortal(PortalB);
	PortalB->ConnectToOtherPortal(PortalA);
}

void AMD_PortalActor::OnRep_LinkedPortal()
{
	// 연결된 Portal의 유효성 검사
	if (LinkedPortal.IsValid() == false)
	{
		SceneCaptureComponent->HiddenActors.Reset();
		return;
	}

	// 캡처되는 Scene을 연결된 Portal의 위치 기준 정면 방향으로 제한
	SceneCaptureComponent->ClipPlaneBase = LinkedPortal->GetActorLocation() - ClipPlaneOffset * LinkedPortal->GetActorForwardVector();
	SceneCaptureComponent->ClipPlaneNormal = LinkedPortal->GetActorForwardVector();

	// 연결된 Portal을 캡처되는 Scene에서 제외
	SceneCaptureComponent->HiddenActors.Add(LinkedPortal.Get());
}

void AMD_PortalActor::OnRep_PortalActive()
{
	// Portal이 비활성화 된 경우 추적 중이던 액터 목록 초기화
	if (bIsPortalActive == false)
	{
		MapDistanceWithOverlappedActor.Reset();
		IgnoreActors.Reset();
	}
}

void AMD_PortalActor::OnCollisionBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// Server 환경이 아닌 경우 함수 조기 종료
	if (HasAuthority() == false)
	{
		return;
	}

	// Portal이 비활성화 상태인 경우 함수 조기 종료
	if (bIsPortalActive == false)
	{
		return;
	}

	// Overlap된 Actor의 유효성 검사
	if (IsValid(OtherActor) == false)
	{
		return;
	}

	// 최상위 부모 액터 얻기
	while (OtherActor->GetAttachParentActor() != nullptr)
	{
		OtherActor = OtherActor->GetAttachParentActor();
	}

	// 최상위 부모 액터가 Portal과 Overlap 상태가 아닌 경우 함수 조기 종료
	if (BoxCollision->IsOverlappingActor(OtherActor) == false)
	{
		return;
	}

	// Portal을 이용할 수 없는 Actor의 경우 함수 조기 종료
	if (IsActorCanUsePortal(OtherActor) == false)
	{
		return;
	}

	// 최근 Portal을 통한 이동이 완료된 객체 또는 이미 추적중인 객체의 경우 함수 조기 종료
	if (IgnoreActors.Contains(OtherActor) == true || MapDistanceWithOverlappedActor.Contains(OtherActor) == true)
	{
		return;
	}

	// Portal과 액터 사이의 방향이 포함된 거리 저장
	float SignedDistance = GetSignedDistance(OtherActor->GetActorLocation());
	MapDistanceWithOverlappedActor.Add(OtherActor, SignedDistance);
}

void AMD_PortalActor::OnCollisionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex)
{
	// Server 환경이 아닌 경우 함수 조기 종료
	if (HasAuthority() == false)
	{
		return;
	}

	// Overlap된 Actor의 유효성 검사
	if (IsValid(OtherActor) == false)
	{
		return;
	}

	// 최상위 부모 액터 얻기
	while (OtherActor->GetAttachParentActor() != nullptr)
	{
		OtherActor = OtherActor->GetAttachParentActor();
	}

	// OtherActor가 Portal 영역의 내부에 겹쳐있는 경우 함수 조기 종료
	if (BoxCollision->IsOverlappingActor(OtherActor) == true)
	{
		return;
	}

	// 현재 Portal의 영역에서 떠난 Actor의 추적 제거
	MapDistanceWithOverlappedActor.Remove(OtherActor);

	// Portal 사용이 불가능한 블랙리스트에서 Actor 제거
	IgnoreActors.Remove(OtherActor);
}

FVector AMD_PortalActor::TransformLocationToLinkedPortal(const FVector& WorldLocation) const
{
	// 연결된 Portal이 유효하지 않으면 함수 조기 종료
	if (LinkedPortal.IsValid() == false)
	{
		return WorldLocation;
	}

	// 입력 받은 World 좌표를 현재 포탈 액터 기준 상대 좌표로 변환
	const FTransform& ThisPortalTransform = GetActorTransform();
	FVector ThisRelativeLocation = ThisPortalTransform.InverseTransformPosition(WorldLocation);

	// 현재 포탈의 +Z(Local)축을 기준으로 180도 회전
	FVector FlippedRelativeLocation = ThisRelativeLocation.RotateAngleAxis(180, FVector::UpVector);

	// 연결된 다른 Portal 기준 상대 좌표를 World 좌표로 변환
	const FTransform& LinkedPortalTransform = LinkedPortal->GetActorTransform();
	FVector LinkedWorldLocation = LinkedPortalTransform.TransformPosition(FlippedRelativeLocation);
	
	return LinkedWorldLocation;
}

FVector AMD_PortalActor::TransformDirectionToLinkedPortal(const FVector& EnterDirection) const
{
	// 연결된 다른 Portal 사이의 회전 값 차이 얻기
	FQuat PortalQuatDelta = GetPortalQuatDelta();

	// Vector 회전
	FVector RotatedDirction = PortalQuatDelta.RotateVector(EnterDirection);

	return RotatedDirction;
}

FRotator AMD_PortalActor::TransformRotationToLinkedPortal(const FRotator& WorldRotation) const
{
	// 연결된 다른 Portal 사이의 회전 값 차이 얻기
	FQuat PortalQuatDelta = GetPortalQuatDelta();

	// Quaternion 회전 값 추가
	FQuat QuatTemp = PortalQuatDelta * WorldRotation.Quaternion();

	return QuatTemp.Rotator();
}

void AMD_PortalActor::UpdatePortalCaptureScene()
{
	// Dedicated Server 환경의 경우 함수 조기 종료
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// 최근 렌더링되지 않은 경우 함수 조기 종료
	if (WasRecentlyRendered() == false)
	{
		return;
	}

	// 참조하는 Local Player가 존재하지 않으면 함수 조기 종료
	if (IsValid(LocalPlayer) == false)
	{
		if (IsValid(GEngine) == true)
		{
			// Local Player 참조 시도
			APlayerController* LocalPlayerController = GEngine->GetFirstLocalPlayerController(GetWorld());
			LocalPlayer = IsValid(LocalPlayerController) == true ? LocalPlayerController->GetLocalPlayer() : nullptr;
		}
		return;
	}

	// Portal이 비활성화 상태 또는 Surface에 화면 Rendering 비활성화 상태인 경우 함수 조기 종료
	if (bIsPortalActive == false || bSurfaceRenderEnabled == false)
	{
		return;
	}

	// 연결된 다른 Portal이 존재하지않는 경우 함수 조기 종료
	if (LinkedPortal.IsValid() == false)
	{
		return;
	}

	// 첫번째 Local Player의 Controller 얻기 및 유효성 검사
	if (IsValid(LocalPlayer->PlayerController) == false)
	{
		return;
	}

	// Player 시점의 위치 및 회전 값 얻기
	FVector PlayerViewpointLocation;
	FRotator PlayerViewpointRotation;
	LocalPlayer->PlayerController->GetPlayerViewPoint(PlayerViewpointLocation, PlayerViewpointRotation);

	// SceneCapture Component의 위치 및 회전 값 계산
	FVector CaptureLocation = TransformLocationToLinkedPortal(PlayerViewpointLocation);
	FRotator CaptureRotation = TransformRotationToLinkedPortal(PlayerViewpointRotation);

	// Portal의 SceneCaptureComponent 위치 및 회전값 변경
	SceneCaptureComponent->SetWorldLocation(CaptureLocation);
	SceneCaptureComponent->SetWorldRotation(CaptureRotation);

	// Player의 Viewport를 관리하는 GameViewportClient Instance 얻기 및 유효성 검사
	UGameViewportClient* ViewportClient = LocalPlayer->ViewportClient;
	if (IsValid(ViewportClient) == false)
	{
		return;
	}

	// Player의 Viewport 얻기 및 유효성 검사
	FSceneViewport* Viewport = ViewportClient->Viewport;
	if (Viewport == nullptr)
	{
		return;
	}

	// 필요한 경우 Texture Render Target의 크기 조정
	UpdatePortalTextureRenderTargetSize();

	// Viewport의 Projection 데이터 얻기
	FSceneViewProjectionData ProjectionData;
	if (LocalPlayer->GetProjectionData(Viewport, ProjectionData) == false)
	{
		return;
	}

	// Portal의 SceneCapture Component가 Viewport의 Projection Matrix를 사용하도록 설정
	SceneCaptureComponent->CustomProjectionMatrix = ProjectionData.ProjectionMatrix;

	// SceneCapture 동작을 명시적으로 실행
	SceneCaptureComponent->CaptureScene();
}

void AMD_PortalActor::UpdatePortalTextureRenderTargetSize()
{
	// Dedicated Server 환경의 경우 함수 조기 종료
	if (GetNetMode() == ENetMode::NM_DedicatedServer)
	{
		return;
	}

	// Viewport 크기를 얻어올 Local Player와 크기를 조정할 Texture의 유효성 검사
	if (IsValid(LocalPlayer) == false || IsValid(TextureRenderTarget) == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 참조된 Local Player 객체 또는 생성된 Texture Render Target가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);
		return;
	}

	// Viewport 크기를 얻어올 Local Player와 크기를 조정할 Texture의 유효성 검사
	if (IsValid(LocalPlayer->ViewportClient) == false)
	{
		UE_LOG(MarineDemolition, Log, TEXT("%s 참조된 Local Player 객체 또는 생성된 Texture Render Target가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);
		return;
	}

	// Viewport의 크기 얻기
	FVector2D ViewportSize;
	LocalPlayer->ViewportClient->GetViewportSize(ViewportSize);

	// Texture Render Target 2D의 크기 조정
	TextureRenderTarget->ResizeTarget(ViewportSize.X, ViewportSize.Y);
}

void AMD_PortalActor::UpdateOverlappedActors()
{
	// 현재 Portal이 비활성화 상태 또는 연결된 다른 Portal이 없으면 함수 조기 종료
	if (bIsPortalActive == false || LinkedPortal.IsValid() == false)
	{
		return;
	}

	// 현재 Portal 이용 블랙리스트 검토
	for (auto Iterator = IgnoreActors.CreateIterator(); Iterator.operator bool() == true; ++Iterator)
	{
		// 액터가 유효하지 않거나 Portal 영역 밖으로 이동된 경우 목록에서 제거
		if (Iterator->IsValid() == false || BoxCollision->IsOverlappingActor(Iterator->Get()) == false)
		{
			Iterator.RemoveCurrent();
		}
	}

	// 이번 프레임에 Teleport된 Actor 목록
	TArray<TWeakObjectPtr<AActor>> ActorsToBeTeleported;

	// 현재 Portal 영역과 겹쳐있는 Actor 목록 검토
	for (auto Iterator = MapDistanceWithOverlappedActor.CreateIterator(); Iterator.operator bool() == true; ++Iterator)
	{
		TWeakObjectPtr<AActor> OtherActor = Iterator.Key();

		// Actor의 Portal 이용 가능성 검사
		if (IsActorCanUsePortal(OtherActor.Get()) == false)
		{
			// 추적 목록에서 제거
			Iterator.RemoveCurrent();
			continue;
		}

		// 이전에 측정 및 저장된 거리 및 방향 정보 얻기
		float PreviousSignedDistance = Iterator.Value();
		
		// 해당 Actor와 현재 Portal 사이의 거리 및 방향 측정
		float CurrentSignedDistance = GetSignedDistance(OtherActor->GetActorLocation());

		// Portal 위치 좌표의 미세한 흔들림으로 인한 이전 거리값이 0으로 초기화되는 상황 방지
		if (FMath::IsNearlyZero(CurrentSignedDistance, KINDA_SMALL_NUMBER) == true)
		{
			continue;
		}

		// 현재 Portal의 Surface를 통과하였는지 검사
		if (PreviousSignedDistance * CurrentSignedDistance < 0)
		{
			// Portal의 후면 방향 출입 허용 또는 Actor가 정면 방향으로 진입하였는지 검사
			if (bFrontSideOnly == false || PreviousSignedDistance > 0)
			{
				// Teleport할 Actor 목록에 추가
				ActorsToBeTeleported.Add(OtherActor.Get());
				continue;
			}
		}

		// 측정 거리 및 방향 업데이트
		Iterator->Value = CurrentSignedDistance;
	}

	for (const TWeakObjectPtr<AActor>& OtherActor : ActorsToBeTeleported)
	{
		if (OtherActor.IsValid() == true)
		{
			TeleportActor(OtherActor.Get());
		}
	}
}

void AMD_PortalActor::InitPortalTextureRenderTarget()
{
	// TextureRenderTarget 객체 생성 및 유효성 검사
	TextureRenderTarget = NewObject<UTextureRenderTarget2D>(this);
	if (IsValid(TextureRenderTarget) == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s Texture Render Target 객체가 생성되지 않았습니다."), *UE_CALL_LOCATIONINFO);
		return;
	}

	TextureRenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA16f;
	TextureRenderTarget->ClearColor = FLinearColor::Transparent;

	FVector2D ViewportSize = FVector2D(128, 128);
	if (IsValid(GEngine) == true && IsValid(GEngine->GameViewport) == true)
	{
		GEngine->GameViewport->GetViewportSize(ViewportSize);
	}
	ViewportSize = FVector2D::Max(ViewportSize, FVector2D::One());
	TextureRenderTarget->InitAutoFormat(ViewportSize.X, ViewportSize.Y);

	SceneCaptureComponent->TextureTarget = TextureRenderTarget;

	// SurfaceMesh의 Material이 존재하지 않으면 함수 조기 종료
	if (SurfaceMesh->GetNumMaterials() <= 0)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s Portal 표면에 적용되는 Material이 존재하지 않습니다."), *UE_CALL_LOCATIONINFO);
		return;
	}

	// 현재 Portal에서 사용될 Material Instance 동적 생성 및 저장
	PortalMaterialInstance = SurfaceMesh->CreateAndSetMaterialInstanceDynamic(0);

	// SurfaceMesh에서 사용되는 Material Instance의 Texture 파라미터에 TextureRenderTarget 등록
	if (IsValid(PortalMaterialInstance) == true)
	{
		PortalMaterialInstance->SetTextureParameterValue(MaterialTextureParameterName, TextureRenderTarget);
	}
}

float AMD_PortalActor::GetSignedDistance(const FVector& WorldLocation) const
{
	// 위치 차이 계산
	FVector DeltaLocation = WorldLocation - GetActorLocation();

	// Portal의 정면 Vector와 내적 계산
	float SignedDistance = FVector::DotProduct(DeltaLocation, GetActorForwardVector());

	return SignedDistance;
}

FQuat AMD_PortalActor::GetPortalQuatDelta() const
{
	// 연결된 Portal이 유효하지 않으면 함수 조기 종료
	if (LinkedPortal.IsValid() == false)
	{
		return FQuat::Identity;
	}

	// 현재 Portal의 World 회전 값 얻기
	FQuat ThisPortalWorldQuat = GetActorQuat();

	// 현재 Portal의 회전 값을 역회전으로 변환
	FQuat WorldToThisPortalQuat = ThisPortalWorldQuat.Inverse();

	// 연결된 Portal의 World 회전 값 얻기
	FQuat LinkedPortalWorldQuat = LinkedPortal->GetActorQuat();

	// 연결된 Portal의 회전 값에 Z축 기준 180도 회전 적용
	FQuat FlippedLinkedPortalQuat = LinkedPortalWorldQuat * FQuat(FVector::UpVector, PI);

	// 현재 Portal에서 연결된 Portal로의 회전 변환량을 계산합니다.
	FQuat PortalQuatDelta = (FlippedLinkedPortalQuat * WorldToThisPortalQuat).GetNormalized();

	return PortalQuatDelta;
}

void AMD_PortalActor::ClientUpdateMovementComponentVelocity_Implementation(UMovementComponent* MovementComponent, FVector NewVelocity)
{
	if (IsValid(MovementComponent) == false)
	{
		return;
	}

	MovementComponent->Velocity = NewVelocity;
	MovementComponent->UpdateComponentVelocity();
}
