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

	// 시간 배율과 무관한 실제 프레임 간격으로 로컬 Portal 화면 캡처 갱신
	UpdatePortalCaptureScene(FApp::GetDeltaTime());
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
			NetMulticastUpdateMovementComponentVelocity(MovementComponent, TeleportedLinearVelocity);
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
	// 상태 변경 직후 오래된 화면을 교체하기 위한 즉시 캡처 예약
	bForceNextPortalCapture = true;

	// 캡처되는 Scene에서 제외되는 액터 목록 초기화
	SceneCaptureComponent->HiddenActors.Reset();

	// 연결된 Portal의 유효성 검사
	if (LinkedPortal.IsValid() == false)
	{
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
	// 상태 변경 직후 오래된 화면을 교체하기 위한 즉시 캡처 예약
	bForceNextPortalCapture = true;

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

bool AMD_PortalActor::BuildPortalCaptureView(FTransform SourcePortal, FTransform DestinationPortal, FVector ViewOrigin, FMatrix ViewRotation, FMatrix Projection,
	FVector& OutLocation, FQuat& OutRotation, FMatrix& OutProjection)
{
	// 역변환할 수 없는 포털 크기 제외
	if (SourcePortal.GetScale3D().GetAbsMin() <= UE_SMALL_NUMBER || DestinationPortal.GetScale3D().GetAbsMin() <= UE_SMALL_NUMBER)
	{
		// 축이 소실된 포털의 잘못된 캡처 방지
		return false;
	}
	// 양쪽 포털의 크기 차이와 반 바퀴 회전을 결합한 역방향 축 배율 계산
	FVector InverseScale = SourcePortal.GetScale3D() / DestinationPortal.GetScale3D() * FVector(-1.0, -1.0, 1.0);
	// 이동 성분을 제외한 목적지의 직교 회전 행렬 구성
	FMatrix DestinationRotation = FQuatRotationMatrix(DestinationPortal.GetRotation());
	// 범용 역행렬 없이 회전 전치와 축 배율로 목적지 광선의 역변환 구성
	FMatrix DestinationToSource = DestinationRotation.GetTransposed() * FScaleMatrix(InverseScale) * FQuatRotationMatrix(SourcePortal.GetRotation());
	// 직교 뷰 회전 행렬의 열에서 월드 시선 방향 직접 추출
	FVector ViewForward = ViewRotation.GetColumn(2);
	// 직교 뷰 회전 행렬의 열에서 월드 화면 위쪽 방향 직접 추출
	FVector ViewUp = ViewRotation.GetColumn(1);
	// 역전치 변환으로 비균일 스케일 이후에도 올바른 시선 평면 법선 계산
	FVector CaptureForward = FVector(DestinationToSource.GetTransposed().TransformVector(ViewForward));
	// 전체 위치 행렬을 만들지 않고 포털의 벡터 변환으로 화면 위쪽 방향 계산
	FVector CaptureUp = DestinationPortal.TransformVector(SourcePortal.InverseTransformVector(ViewUp) * FVector(-1.0, -1.0, 1.0));
	// 포털의 위치 변환을 직접 적용하여 캡처 카메라 위치 계산
	OutLocation = DestinationPortal.TransformPosition(SourcePortal.InverseTransformPosition(ViewOrigin) * FVector(-1.0, -1.0, 1.0));
	// 시선과 위쪽 방향을 한 번만 정규화하여 직교 카메라 축 구성
	FMatrix CaptureBasis = FRotationMatrix::MakeFromXZ(CaptureForward, CaptureUp);
	// 각도 변환 없이 SceneCapture에 전달할 회전 구성
	OutRotation = CaptureBasis.ToQuat();
	// 계산된 축을 오른쪽·위쪽·깊이 순서로 재배치하여 카메라 역변환 구성
	FMatrix CaptureToWorld(CaptureBasis.GetScaledAxis(EAxis::Y), CaptureBasis.GetScaledAxis(EAxis::Z), CaptureBasis.GetScaledAxis(EAxis::X), FVector::ZeroVector);
	// 이미 계산된 카메라 축을 재사용하여 포털 사이의 크기와 기울기 차이 반영
	OutProjection = CaptureToWorld * DestinationToSource * ViewRotation * Projection;
	// 엔진의 깊이 복원에 필요한 원근 나눗셈 계수 보관
	double PerspectiveScale = OutProjection.M[2][3];
	// 원근 투영이 성립하지 않는 입력 제외
	if (FMath::IsNearlyZero(PerspectiveScale) == true)
	{
		// 비정상 투영 행렬 적용 방지
		return false;
	}
	// 투영 결과를 유지하면서 깊이 계수를 표준 형태로 정규화
	OutProjection *= 1.0 / PerspectiveScale;
	// 유효한 캡처 설정 계산 완료 반환
	return true;
}

bool AMD_PortalActor::GetPortalScreenRatio(FMatrix ViewProjection, FBoxSphereBounds SurfaceBounds, FIntPoint ViewSize, float& OutScreenRatio)
{
	// 화면 밖인 경우에도 호출자가 사용할 수 있는 기본 비율 초기화
	OutScreenRatio = 0.0f;
	// 최소화되거나 크기가 없는 화면의 캡처 제외
	if (ViewSize.X <= 0 || ViewSize.Y <= 0)
	{
		// 표시 영역이 없는 상태 반환
		return false;
	}
	// 로컬 카메라의 가시 영역 저장 공간
	FConvexVolume Frustum;
	// 원근 투영 행렬에서 가시 영역 추출
	GetViewFrustumBounds(Frustum, ViewProjection, true);
	// Portal 경계와 로컬 가시 영역의 교차 여부 검사
	if (Frustum.IntersectBox(SurfaceBounds.Origin, SurfaceBounds.BoxExtent) == false)
	{
		// 화면 밖 Portal 제외
		return false;
	}
	// 투영된 경계의 최소 좌표 초기화
	FVector2D Minimum(DBL_MAX, DBL_MAX);
	// 투영된 경계의 최대 좌표 초기화
	FVector2D Maximum(-DBL_MAX, -DBL_MAX);
	// 모든 꼭짓점이 공유하는 중심의 동차 화면 좌표 계산
	FVector4 CenterClip = ViewProjection.TransformFVector4(FVector4(SurfaceBounds.Origin, 1.0));
	// 가로 경계 이동이 화면 좌표에 더하는 변화량 계산
	FVector4 ExtentX = FVector4(ViewProjection.M[0][0], ViewProjection.M[0][1], ViewProjection.M[0][2], ViewProjection.M[0][3]) * SurfaceBounds.BoxExtent.X;
	// 깊이 경계 이동이 화면 좌표에 더하는 변화량 계산
	FVector4 ExtentY = FVector4(ViewProjection.M[1][0], ViewProjection.M[1][1], ViewProjection.M[1][2], ViewProjection.M[1][3]) * SurfaceBounds.BoxExtent.Y;
	// 높이 경계 이동이 화면 좌표에 더하는 변화량 계산
	FVector4 ExtentZ = FVector4(ViewProjection.M[2][0], ViewProjection.M[2][1], ViewProjection.M[2][2], ViewProjection.M[2][3]) * SurfaceBounds.BoxExtent.Z;
	// 박스 경계의 여덟 꼭짓점 검토
	for (int32 Corner = 0; Corner < 8; ++Corner)
	{
		// 중심과 축별 변화량을 조합하여 꼭짓점별 행렬 곱셈 생략
		FVector4 Clip = CenterClip + (Corner & 1 ? ExtentX : -ExtentX) + (Corner & 2 ? ExtentY : -ExtentY) + (Corner & 4 ? ExtentZ : -ExtentZ);
		// 카메라 평면을 가로지르는 경계의 축소 방지
		if (Clip.W <= UE_SMALL_NUMBER)
		{
			// 접근 또는 통과 중인 Portal의 원본 품질 유지
			OutScreenRatio = 1.0f;
			// 보수적인 가시 상태 반환
			return true;
		}
		// 원근 나눗셈을 적용한 화면 좌표 계산
		FVector2D NDC(Clip.X / Clip.W, Clip.Y / Clip.W);
		// 전체 꼭짓점의 최소 가로 좌표 누적
		Minimum.X = FMath::Min(Minimum.X, NDC.X);
		// 전체 꼭짓점의 최소 세로 좌표 누적
		Minimum.Y = FMath::Min(Minimum.Y, NDC.Y);
		// 전체 꼭짓점의 최대 가로 좌표 누적
		Maximum.X = FMath::Max(Maximum.X, NDC.X);
		// 전체 꼭짓점의 최대 세로 좌표 누적
		Maximum.Y = FMath::Max(Maximum.Y, NDC.Y);
	}
	// 실제 화면과 겹치는 가로 비율 계산
	double WidthRatio = FMath::Max(0.0, FMath::Min(1.0, Maximum.X) - FMath::Max(-1.0, Minimum.X)) * 0.5;
	// 실제 화면과 겹치는 세로 비율 계산
	double HeightRatio = FMath::Max(0.0, FMath::Min(1.0, Maximum.Y) - FMath::Max(-1.0, Minimum.Y)) * 0.5;
	// 어느 방향으로도 한 픽셀을 차지하지 못하는 Portal 제외
	if (WidthRatio * ViewSize.X < 1.0 && HeightRatio * ViewSize.Y < 1.0)
	{
		// 식별할 수 없는 표면의 캡처 생략
		return false;
	}
	// 긴 방향의 화면 점유율을 품질 선택 기준으로 반환
	OutScreenRatio = FMath::Max(WidthRatio, HeightRatio);
	// 화면에 표시 가능한 상태 반환
	return true;
}

bool AMD_PortalActor::ShouldUseFullQualityCapture(bool bWasFullQuality, float ScreenRatio, float SurfaceDistance, float ScreenThreshold, float NearDistance)
{
	// 화면 점유율 설정의 유효 범위 보정
	ScreenThreshold = FMath::Clamp(ScreenThreshold, 0.05f, 1.0f);
	// 접근 거리 설정의 음수 방지
	NearDistance = FMath::Max(0.0f, NearDistance);
	// 원본 품질 상태에서는 더 작고 멀어진 이후 축소 허용
	if (bWasFullQuality == true)
	{
		// 화면 비율과 접근 거리의 여유 구간을 포함한 유지 여부 반환
		return ScreenRatio >= ScreenThreshold * 0.8f || SurfaceDistance <= NearDistance * 1.2f;
	}
	// 작은 화면에서 확대 또는 접근 시 즉시 원본 품질로 전환
	return ScreenRatio >= ScreenThreshold || SurfaceDistance <= NearDistance;
}

bool AMD_PortalActor::ShouldCapturePortal(float& SecondsUntilCapture, float DeltaSeconds, float CaptureRate, bool bForceCapture)
{
	// 무제한 캡처 또는 재진입에 필요한 즉시 갱신 처리
	if (CaptureRate <= 0.0f || bForceCapture == true)
	{
		// 강제 캡처 시점을 기준으로 다음 주기 초기화
		SecondsUntilCapture = CaptureRate > 0.0f ? 1.0f / CaptureRate : 0.0f;
		// 현재 프레임 캡처 허용
		return true;
	}
	// 역방향 시간 변화 없이 남은 대기 시간 감소
	SecondsUntilCapture -= FMath::Max(0.0f, DeltaSeconds);
	// 다음 캡처 주기 도달 여부 확인
	if (SecondsUntilCapture > UE_SMALL_NUMBER)
	{
		// 대기 중인 프레임의 캡처 생략
		return false;
	}
	// 목표 캡처 빈도의 실제 시간 간격 계산
	float Interval = 1.0f / CaptureRate;
	// 지연 이후 한 프레임에 캡처가 몰리지 않도록 지나간 주기 제거
	SecondsUntilCapture = Interval - FMath::Fmod(FMath::Max(-SecondsUntilCapture, 0.0f), Interval);
	// 이번 프레임의 단일 캡처 허용
	return true;
}

void AMD_PortalActor::UpdatePortalCaptureScene(float DeltaSeconds)
{
	// 화면 재진입 시 대기 주기 없이 캡처하기 위한 직전 상태 보관
	bool bWasVisible = bCaptureWasVisible;
	// 조기 종료된 프레임을 캡처 불가능 상태로 기록
	bCaptureWasVisible = false;

	// 전용 서버와 비활성 Portal의 렌더링 작업 제외
	if (GetNetMode() == NM_DedicatedServer || bIsPortalActive == false || bSurfaceRenderEnabled == false)
	{
		// 화면 갱신이 필요하지 않은 상태의 처리 종료
		return;
	}

	// 연결과 렌더링 리소스가 준비된 Portal만 캡처 허용
	if (LinkedPortal.IsValid() == false || IsValid(SurfaceMesh) == false || IsValid(SceneCaptureComponent) == false || IsValid(TextureRenderTarget) == false)
	{
		// 준비되지 않은 캡처 작업 제외
		return;
	}

	// 최근 렌더링되지 않은 Portal의 투영 계산 생략
	if (WasRecentlyRendered() == false)
	{
		// 화면에 표시되지 않은 Portal의 처리 종료
		return;
	}

	// 아직 참조하지 않은 로컬 플레이어 검색
	if (IsValid(LocalPlayer) == false)
	{
		// 로컬 컨트롤러 조회에 필요한 엔진 유효성 검사
		if (IsValid(GEngine) == true)
		{
			// 현재 월드에서 사용하는 첫 번째 로컬 컨트롤러 조회
			APlayerController* LocalPlayerController = GEngine->GetFirstLocalPlayerController(GetWorld());
			// 유효한 컨트롤러에서 로컬 플레이어 참조 보관
			LocalPlayer = IsValid(LocalPlayerController) == true ? LocalPlayerController->GetLocalPlayer() : nullptr;
		}
	}

	// 로컬 카메라를 조회할 수 없는 상태 제외
	if (IsValid(LocalPlayer) == false || IsValid(LocalPlayer->PlayerController) == false)
	{
		// 로컬 플레이어 준비 전 처리 종료
		return;
	}

	// 로컬 화면을 관리하는 클라이언트 조회
	UGameViewportClient* ViewportClient = LocalPlayer->ViewportClient;
	// 화면 클라이언트의 유효성 검사
	if (IsValid(ViewportClient) == false)
	{
		// 화면이 준비되지 않은 상태의 처리 종료
		return;
	}

	// 실제 화면 크기와 투영 정보를 제공하는 Viewport 조회
	FSceneViewport* Viewport = ViewportClient->Viewport;
	// Viewport 생성 전 또는 제거 이후 상태 제외
	if (Viewport == nullptr)
	{
		// 화면 참조가 없는 상태의 처리 종료
		return;
	}

	// 현재 로컬 카메라의 투영 정보 저장 공간
	FSceneViewProjectionData ProjectionData;
	// 유효한 투영 정보 확보 여부 검사
	if (LocalPlayer->GetProjectionData(Viewport, ProjectionData) == false)
	{
		// 투영할 수 없는 화면의 처리 종료
		return;
	}

	// 화면 비율 제한을 반영한 로컬 카메라 영역 크기
	FIntPoint ViewSize = ProjectionData.GetConstrainedViewRect().Size();
	// Portal 표면이 차지하는 최대 가로 또는 세로 비율 저장 공간
	float ScreenRatio = 0.0f;
	// 다른 Scene Capture의 렌더링 기록과 무관하게 실제 로컬 화면과의 교차 여부 검사
	if (GetPortalScreenRatio(ProjectionData.ComputeViewProjectionMatrix(), SurfaceMesh->Bounds, ViewSize, ScreenRatio) == false)
	{
		// 화면 밖 또는 한 픽셀 미만인 Portal의 캡처 생략
		return;
	}

	// 카메라에서 Portal 표면 경계까지의 거리 계산
	float SurfaceDistance = FMath::Sqrt(SurfaceMesh->Bounds.GetBox().ComputeSquaredDistanceToPoint(ProjectionData.ViewOrigin));
	// 화면 점유율과 접근 거리에 여유 구간을 적용한 품질 선택
	bool bUseFullQuality = bAdaptiveCaptureEnabled == false || ShouldUseFullQualityCapture(bFullQualityCapture, ScreenRatio, SurfaceDistance, FullQualityScreenRatio, FullQualityDistance);
	// 저해상도 설정의 런타임 범위 보정
	float ResolutionScale = bUseFullQuality == true ? 1.0f : FMath::Clamp(DistantCaptureResolutionScale, 0.25f, 1.0f);
	// 원본 화면 비율을 유지하는 캡처 대상 크기 계산
	FIntPoint TargetSize(FMath::Max(1, FMath::RoundToInt(ViewSize.X * ResolutionScale)), FMath::Max(1, FMath::RoundToInt(ViewSize.Y * ResolutionScale)));
	// 재진입과 품질 변경 및 창 크기 변경 시 즉시 캡처 조건 계산
	bool bForceCapture = bWasVisible == false || bForceNextPortalCapture == true || bFullQualityCapture != bUseFullQuality || LastCaptureViewSize != ViewSize
		|| TextureRenderTarget->SizeX != TargetSize.X || TextureRenderTarget->SizeY != TargetSize.Y;
	// 이번 프레임의 캡처 가능 상태 기록
	bCaptureWasVisible = true;
	// 다음 품질 판정에 사용할 현재 품질 기록
	bFullQualityCapture = bUseFullQuality;

	// 비균일 포털 크기를 반영한 캡처 위치 저장 공간
	FVector CaptureLocation;
	// 각도 왕복 변환 없이 적용할 캡처 회전 저장 공간
	FQuat CaptureRotation;
	// 카메라 위치와 같은 좌표 변환을 사용하는 투영 행렬 저장 공간
	FMatrix CaptureProjection;
	// 화면 UV와 목적지의 광선이 일치하도록 카메라 및 투영 계산
	if (BuildPortalCaptureView(GetActorTransform(), LinkedPortal->GetActorTransform(), ProjectionData.ViewOrigin, ProjectionData.ViewRotationMatrix,
		ProjectionData.ProjectionMatrix, CaptureLocation, CaptureRotation, CaptureProjection) == false)
	{
		// 크기가 소실된 포털의 잘못된 행렬 적용 방지
		return;
	}

	// 카메라와 두 Portal이 그대로라 직전 캡처와 같은 시점인지 확인
	bool bCaptureViewUnchanged = LastCaptureLocation.Equals(CaptureLocation, 0.01f) && LastCaptureRotation.Equals(CaptureRotation, 1.e-5f)
		&& LastCaptureProjection.Equals(CaptureProjection, 1.e-6f);
	// 작은 화면은 제한된 빈도로 캡처하도록 간격 선택
	float CaptureRate = FMath::Clamp(DistantCaptureRate, 1.0f, 120.0f);
	// 근접 화면은 시점이 바뀌면 매 프레임, 그대로면 정지 상태 빈도로 캡처
	if (bUseFullQuality == true)
	{
		CaptureRate = NearIdleCaptureRate > 0.0f && bCaptureViewUnchanged == true ? FMath::Clamp(NearIdleCaptureRate, 1.0f, 120.0f) : 0.0f;
	}
	// 캡처 시점에 도달한 프레임만 렌더링 작업 진행
	if (ShouldCapturePortal(SecondsUntilNextCapture, DeltaSeconds, CaptureRate, bForceCapture) == false)
	{
		// 다음 캡처까지 직전 화면 유지
		return;
	}

	// 실제 장면 캡처 구간의 CPU 추적 범위
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_Portal_CaptureVisibleScene);
	// 계산된 목적지 카메라 위치와 회전 적용
	SceneCaptureComponent->SetWorldLocationAndRotation(CaptureLocation, CaptureRotation);
	// 선택된 품질에 맞는 렌더 타깃 크기 적용
	UpdatePortalTextureRenderTargetSize(TargetSize);
	// 포털 크기와 시선 각도까지 반영한 투영 적용
	SceneCaptureComponent->CustomProjectionMatrix = CaptureProjection;
	// 목적지 이동과 실행 중 오프셋 변경을 반영한 최신 클립 평면 위치 적용
	SceneCaptureComponent->ClipPlaneBase = LinkedPortal->GetActorLocation() - ClipPlaneOffset * LinkedPortal->GetActorForwardVector();
	// 목적지 회전을 반영한 최신 클립 평면 방향 적용
	SceneCaptureComponent->ClipPlaneNormal = LinkedPortal->GetActorForwardVector();
	// 수동 장면 캡처 실행
	SceneCaptureComponent->CaptureScene();
	// 다음 창 크기 변경을 감지하기 위한 화면 크기 기록
	LastCaptureViewSize = ViewSize;
	// 다음 프레임의 시점 변경을 감지하기 위한 캡처 시점 기록
	LastCaptureLocation = CaptureLocation;
	LastCaptureRotation = CaptureRotation;
	LastCaptureProjection = CaptureProjection;
	// 이번 캡처에서 처리한 강제 갱신 예약 해제
	bForceNextPortalCapture = false;
}

void AMD_PortalActor::UpdatePortalTextureRenderTargetSize(FIntPoint TargetSize)
{
	// 이미 일치하는 크기의 렌더 타깃 재설정 생략
	if (TextureRenderTarget->SizeX == TargetSize.X && TextureRenderTarget->SizeY == TargetSize.Y)
	{
		// 기존 리소스 유지
		return;
	}

	// 화면 크기 또는 품질 단계가 변경된 경우에만 렌더 타깃 크기 조정
	TextureRenderTarget->ResizeTarget(TargetSize.X, TargetSize.Y);
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

	// 최대 네 명의 동시 통과는 별도 힙 할당 없이 보관하고 초과 객체는 자동 확장
	TArray<TWeakObjectPtr<AActor>, TInlineAllocator<4>> ActorsToBeTeleported;

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
	// 전용 서버에서는 화면 출력용 리소스 생성 제외
	if (GetNetMode() == NM_DedicatedServer)
	{
		// 렌더 타깃과 동적 머티리얼 생성 생략
		return;
	}

	// TextureRenderTarget 객체 생성 및 유효성 검사
	TextureRenderTarget = NewObject<UTextureRenderTarget2D>(this);
	if (IsValid(TextureRenderTarget) == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s Texture Render Target 객체가 생성되지 않았습니다."), *UE_CALL_LOCATIONINFO);
		return;
	}

	TextureRenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA16f;
	TextureRenderTarget->ClearColor = FLinearColor::Transparent;

	// 처음 화면에 나타날 때 필요한 크기로 확장하는 최소 렌더 타깃 생성
	TextureRenderTarget->InitAutoFormat(1, 1);

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

void AMD_PortalActor::NetMulticastUpdateMovementComponentVelocity_Implementation(UMovementComponent* MovementComponent, FVector NewVelocity)
{
	if (IsValid(MovementComponent) == false)
	{
		return;
	}

	MovementComponent->Velocity = NewVelocity;
	MovementComponent->UpdateComponentVelocity();
}
