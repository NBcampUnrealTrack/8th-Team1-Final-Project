// 프로젝트 코드

#include "World/Traversal/MD_Ladder.h"

#include "Character/MD_Character.h"
#include "Component/Character/MD_CharacterMovementComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"

/** 사다리 축 계산에 사용할 최소 길이 제곱 */
namespace MDLadderConstants
{
	/** 잘못 배치된 상하단 기준점의 영벡터 계산 방지값 */
	static constexpr float MinimumClimbLengthSquared = 1.0f;
}

AMD_Ladder::AMD_Ladder()
{
	PrimaryActorTick.bCanEverTick = false;

	// 서버가 확정한 현재 사다리 참조를 클라이언트가 해석하기 위한 액터 복제 활성화
	bReplicates = true;

	// 레벨에 고정된 사다리의 불필요한 변환 복제 비활성화
	SetReplicateMovement(false);

	// 모든 사다리 구성요소의 공통 변환 기준 생성
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	// 레벨 제작자가 사다리 외형을 지정할 정적 메시 구성요소 생성
	LadderMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LadderMesh"));
	LadderMesh->SetupAttachment(SceneRoot);
	LadderMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	// 캐릭터 캡슐만 감지하는 등반 진입 공간 생성
	ClimbTriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("ClimbTriggerBox"));
	ClimbTriggerBox->SetupAttachment(SceneRoot);
	ClimbTriggerBox->SetRelativeLocation(FVector(45.0f, 0.0f, 194.0f));
	ClimbTriggerBox->SetBoxExtent(FVector(85.0f, 80.0f, 194.0f));
	ClimbTriggerBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ClimbTriggerBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	ClimbTriggerBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	ClimbTriggerBox->SetGenerateOverlapEvents(true);
	ClimbTriggerBox->CanCharacterStepUpOn = ECB_No;		// MovementComponent가 발판으로 인식하여 Collilsion 위로 올라타지 않도록 설정
	ClimbTriggerBox->SetCanEverAffectNavigation(false);

	// 기본 캐릭터 캡슐 중심 높이를 고려한 하단 등반 기준점 생성
	BottomClimbPoint = CreateDefaultSubobject<USceneComponent>(TEXT("BottomClimbPoint"));
	BottomClimbPoint->SetupAttachment(SceneRoot);
	BottomClimbPoint->SetRelativeLocation(FVector(0.0f, 0.0f, 88.0f));

	// 레벨별 사다리 길이를 편집할 수 있는 상단 등반 기준점 생성
	TopClimbPoint = CreateDefaultSubobject<USceneComponent>(TEXT("TopClimbPoint"));
	TopClimbPoint->SetupAttachment(SceneRoot);
	TopClimbPoint->SetRelativeLocation(FVector(0.0f, 0.0f, 300.0f));

	// 상단 바닥 위에서 캡슐이 충돌 없이 설 수 있는 기본 이탈 기준점 생성
	TopExitPoint = CreateDefaultSubobject<USceneComponent>(TEXT("TopExitPoint"));
	TopExitPoint->SetupAttachment(SceneRoot);
	TopExitPoint->SetRelativeLocation(FVector(-75.0f, 0.0f, 388.0f));

	// 양의 X축을 사다리 표면 바깥 방향으로 정의하는 편집기 표시기 생성
	OutwardDirection = CreateDefaultSubobject<UArrowComponent>(TEXT("OutwardDirection"));
	OutwardDirection->SetupAttachment(SceneRoot);
	OutwardDirection->SetRelativeLocation(FVector(0.0f, 0.0f, 150.0f));
	OutwardDirection->SetRelativeRotation(FRotator::ZeroRotator);
	OutwardDirection->ArrowColor = FColor::Red;
	OutwardDirection->ArrowSize = 1.25f;
	OutwardDirection->SetHiddenInGame(true);
}

void AMD_Ladder::BeginPlay()
{
	// 부모 액터의 런타임 초기화 보장
	Super::BeginPlay();

	// 구성 오류가 있는 파생 블루프린트의 안전한 오버랩 연결 차단
	if (IsValid(ClimbTriggerBox) == false)
	{
		return;
	}

	// 캐릭터 캡슐 진입을 사다리 이벤트로 변환하는 콜백 연결
	ClimbTriggerBox->OnComponentBeginOverlap.AddDynamic(this, &AMD_Ladder::HandleClimbTriggerBeginOverlap);

	// 캐릭터 캡슐 이탈을 사다리 이벤트로 변환하는 콜백 연결
	ClimbTriggerBox->OnComponentEndOverlap.AddDynamic(this, &AMD_Ladder::HandleClimbTriggerEndOverlap);
}

FVector AMD_Ladder::GetClimbUpVector() const
{
	// 배치된 상하단 기준점에서 실제 등반 축 계산
	const FVector ClimbDelta = GetTopClimbPointLocation() - GetBottomClimbPointLocation();

	// 매우 근접한 위치로 설정된 기준점으로 인한 ZeroVector 방어
	if (ClimbDelta.SizeSquared() < 1.0f)
	{
		// 액터의 UpVector 사용
		return GetActorUpVector().GetSafeNormal();
	}

	// 회전되거나 기울어진 사다리에도 대응하는 정규화 축 반환
	return ClimbDelta.GetSafeNormal();
}

FVector AMD_Ladder::GetOutwardVector() const
{
	// 편집기 방향 표시기가 유효한 경우 명시적 바깥 방향 반환
	if (IsValid(OutwardDirection) == true)
	{
		return OutwardDirection->GetForwardVector().GetSafeNormal();
	}

	// 구성요소가 없는 파생 클래스의 안전한 액터 전방 방향 반환
	return GetActorForwardVector().GetSafeNormal();
}

FVector AMD_Ladder::GetBottomClimbPointLocation() const
{
	// 배치된 하단 기준점이 유효한 경우 캡슐 중심 위치 반환
	if (IsValid(BottomClimbPoint) == true)
	{
		return BottomClimbPoint->GetComponentLocation();
	}

	// 구성요소가 없는 파생 클래스의 안전한 액터 위치 반환
	return GetActorLocation();
}

FVector AMD_Ladder::GetTopClimbPointLocation() const
{
	// 배치된 상단 기준점이 유효한 경우 캡슐 중심 위치 반환
	if (IsValid(TopClimbPoint) == true)
	{
		return TopClimbPoint->GetComponentLocation();
	}

	// 구성요소가 없는 파생 클래스의 안전한 액터 위치 반환
	return GetActorLocation();
}

FVector AMD_Ladder::GetTopExitPointLocation() const
{
	// 배치된 상단 이탈 기준점이 유효한 경우 완료 위치 반환
	if (IsValid(TopExitPoint) == true)
	{
		return TopExitPoint->GetComponentLocation();
	}

	// 구성요소가 없는 파생 클래스의 안전한 상단 위치 반환
	return GetTopClimbPointLocation();
}

float AMD_Ladder::GetClimbLength() const
{
	FVector TopPoint = GetTopClimbPointLocation();
	FVector BottomPoint = GetBottomClimbPointLocation();

	// 경계 판정과 속도 진행도 계산에 사용할 상하단 거리 반환
	float Distance = FVector::Distance(BottomPoint, TopPoint);
	return Distance;
}

float AMD_Ladder::GetClimbProgress(const FVector& WorldLocation) const
{
	// 사다리 구간의 시작점과 전체 변위 계산
	FVector BottomLocation = GetBottomClimbPointLocation();
	FVector ClimbDelta = GetTopClimbPointLocation() - BottomLocation;
	float ClimbLengthSquared = ClimbDelta.SizeSquared();

	// 길이가 없는 잘못된 사다리 배치의 안정적인 하단 진행도 반환
	if (ClimbLengthSquared < 1)
	{
		return 0.0f;
	}

	// 사다리 선분 투영값을 정규화한 원본 진행도 계산
	float UnclampedFactor = FVector::DotProduct(WorldLocation - BottomLocation, ClimbDelta) / ClimbLengthSquared;

	// 상하단 밖의 위치가 경계 계산을 오염시키지 않는 진행도 제한
	float clampedFactor = FMath::Clamp(UnclampedFactor, 0.0f, 1.0f);
	return clampedFactor;
}

bool AMD_Ladder::IsWithinClimbBounds(const FVector& WorldLocation, float BoundaryTolerance) const
{
	// 음수 허용 오차로 경계 범위가 반전되는 문제 방지
	float SafeTolerance = FMath::Max(BoundaryTolerance, 0.0f);
	FVector BottomLocation = GetBottomClimbPointLocation();
	FVector ClimbDelta = GetTopClimbPointLocation() - BottomLocation;
	//float ClimbLength = ClimbDelta.Size();
	float ClimbLength = GetClimbLength();

	// 길이가 없는 잘못된 사다리 배치의 등반 허용 방지
	if (ClimbLength < 1)
	{
		return false;
	}

	// 등반 축을 따라 측정한 하단 기준 부호 거리 계산
	float ProjectedDistance = FVector::DotProduct(WorldLocation - BottomLocation, ClimbDelta / ClimbLength);

	// 상하단 허용 오차를 포함한 유효 범위 판정
	bool bIsInUppperArea = ProjectedDistance >= -SafeTolerance;
	bool bIsInLowerArea = ProjectedDistance <= ClimbLength + SafeTolerance;

	return bIsInUppperArea && bIsInLowerArea;
}

FVector AMD_Ladder::GetClosestClimbLocation(const FVector& WorldLocation, float StandOffDistance) const
{
	// 등반 기준점과 선분 변위 계산
	FVector BottomLocation = GetBottomClimbPointLocation();
	FVector ClimbDelta = GetTopClimbPointLocation() - BottomLocation;
	float ClimbLengthSquared = ClimbDelta.SizeSquared();

	// 음수 간격으로 캐릭터가 사다리 뒤에 배치되는 문제 방지
	float SafeStandOffDistance = FMath::Max(StandOffDistance, 0.0f);

	// 길이가 없는 잘못된 사다리 배치의 안전한 스냅 위치 반환
	if (ClimbLengthSquared < 1)
	{
		return BottomLocation + GetOutwardVector() * SafeStandOffDistance;
	}

	// 입력 위치와 가장 가까운 사다리 선분 진행도 계산
	float UnclampedProgressFactor = FVector::DotProduct(WorldLocation - BottomLocation, ClimbDelta) / ClimbLengthSquared;
	float ClimbProgress = FMath::Clamp(UnclampedProgressFactor, 0.0f, 1.0f);

	// 사다리 축 투영과 캡슐 표면 간격을 결합한 최종 스냅 위치 반환
	FVector ClosetLocationFromLadder = BottomLocation + (ClimbDelta * ClimbProgress) + (GetOutwardVector() * SafeStandOffDistance);
	return ClosetLocationFromLadder;
}

bool AMD_Ladder::IsCharacterInsideTrigger(APawn* Pawn) const
{
	// 구성요소 또는 캐릭터가 없는 호출의 안전한 거부
	if (IsValid(ClimbTriggerBox) == false || IsValid(Pawn) == false)
	{
		return false;
	}

	// 서버 진입 검증에 사용할 실제 액터 오버랩 상태 반환
	return ClimbTriggerBox->IsOverlappingActor(Pawn);
}

void AMD_Ladder::HandleClimbTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// 사다리 상태 변경의 단일 기준을 유지하기 위한 클라이언트 이벤트 차단
	if (HasAuthority() == false)
	{
		return;
	}

	// 폰 오버랩 대상을 프로젝트 캐릭터로 제한하는 형식 확인
	AMD_Character* MDCharacter = Cast<AMD_Character>(OtherActor);

	// 프로젝트 캐릭터가 아니거나 캡슐 외 컴포넌트에서 발생한 중복 진입 이벤트 차단
	if (IsValid(MDCharacter) == false || OtherComponent != MDCharacter->GetCapsuleComponent())
	{
		return;
	}

	// 캐릭터 이동 컴포넌트에 현재 사다리를 전달하여 등반 이동 모드 시작
	UMD_CharacterMovementComponent* MDMovement = Cast<UMD_CharacterMovementComponent>(MDCharacter->GetCharacterMovement());
	if (IsValid(MDMovement) == true)
	{
		MDMovement->StartClimbing(this);
	}
}

void AMD_Ladder::HandleClimbTriggerEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex)
{
	// 사다리 상태 변경의 단일 기준을 유지하기 위한 클라이언트 이벤트 차단
	if (HasAuthority() == false)
	{
		return;
	}

	// 폰 오버랩 대상을 프로젝트 캐릭터로 제한하는 형식 확인
	AMD_Character* MDCharacter = Cast<AMD_Character>(OtherActor);

	// 프로젝트 캐릭터가 아니거나 캡슐 외 컴포넌트에서 발생한 중복 이탈 이벤트 차단
	if (IsValid(MDCharacter) == false || OtherComponent != MDCharacter->GetCapsuleComponent())
	{
		return;
	}

	// 유효한 이동 컴포넌트가 이 사다리를 이용 중인 경우에만 종료 처리
	UMD_CharacterMovementComponent* MDMovement = Cast<UMD_CharacterMovementComponent>(MDCharacter->GetCharacterMovement());
	if (IsValid(MDMovement) == false || MDMovement->GetCurrentLadder() != this)
	{
		return;
	}

	// 하단 도달 시 오버랩 이탈보다 바닥 착지 판정을 우선 적용
	float SpeedAtLadder = MDMovement->GetLadderClimbSpeed();
	if (MDMovement->TryLeaveLadderAtBottom(SpeedAtLadder) == true)
	{
		return;
	}

	// 상단 자동 이탈 중이 아닌 경우 낙하 이동 모드로 복귀
	if (MDMovement->IsTopDismounting() == false)
	{
		MDMovement->StopClimbing(MOVE_Falling);
	}
}
