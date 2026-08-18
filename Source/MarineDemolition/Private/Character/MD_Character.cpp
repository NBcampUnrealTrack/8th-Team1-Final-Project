// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/MD_Character.h"
#include "Player/MD_PlayerState.h"
#include "Data/Input/MD_InputConfig.h"
#include "Component/Character/MD_CharacterMovementComponent.h"
#include "Component/Scrape/MD_ScrapeToolComponent.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Component/Interaction/MD_InteractionComponent.h"
#include "Component/Support/MD_SupplyComponent.h"
#include "Component/Oxygen/MD_OxygenComponent.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputCoreTypes.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "World/Traversal/MD_Ladder.h"
#include "Gameplay/Attribute/MD_CharacterAttributeSet.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayAbilitySpec.h"
#include "GameplayTagContainer.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	constexpr int32 ScrapeFocusInputPriority = 100;
}

// Sets default values
AMD_Character::AMD_Character(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UMD_CharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	// 1인칭 팔·도구 배치는 스켈레탈 메시의 애니메이션 평가가 끝난 뒤에 해야 한다.
	// 기본 TG_PrePhysics 로 두면 이번 프레임 포즈가 아직 없어서 소켓 트랜스폼과
	// 도구 회전을 한 프레임 늦게 읽고, 마우스를 드래그할 때 그 지연이 흔들림으로 보인다.
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = true;
	bUseControllerRotationRoll = false;

	GetCharacterMovement()->bUseControllerDesiredRotation = false;
	GetCharacterMovement()->bOrientRotationToMovement = false;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 540.0f, 0.0f);

	// 사다리 감지 영역과 캐릭터 캡슐 사이의 오버랩 이벤트 생성 활성화
	GetCapsuleComponent()->SetGenerateOverlapEvents(true);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	Camera->SetRelativeLocation(FVector(0.f, 0.f, BaseEyeHeight)); //SkeltalMesh 눈높이
	Camera->bUsePawnControlRotation = true; // Camera Pitch Yaw == Controller Pitch Yaw, 기존3인칭의 SpringArm과 달리 카메라가 담당

	GetMesh()->SetOwnerNoSee(true);
	GetMesh()->SetOnlyOwnerSee(false);

	// 팔은 Camera가 아니라 별도 피벗에 붙는다. Camera 시점을 팔과 따로 잡기 위해서다.
	FirstPersonArmsPivot = CreateDefaultSubobject<USceneComponent>(TEXT("FirstPersonArmsPivot"));
	FirstPersonArmsPivot->SetupAttachment(GetCapsuleComponent());
	FirstPersonArmsPivot->SetRelativeLocation(FVector(0.f, 0.f, BaseEyeHeight));

	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonMesh"));
	FirstPersonMesh->SetupAttachment(FirstPersonArmsPivot);
	FirstPersonMesh->SetOnlyOwnerSee(true);
	FirstPersonMesh->SetOwnerNoSee(false);
	FirstPersonMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	ScrapeToolComponent = CreateDefaultSubobject<UMD_ScrapeToolComponent>(TEXT("ScrapeToolComponent"));

	EquipHotbarComponent = CreateDefaultSubobject<UMD_EquipHotbarComponent>(TEXT("EquipHotbarComponent"));

	ThirdPersonToolMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ThirdPersonToolMesh"));
	ThirdPersonToolMesh->SetupAttachment(GetMesh(), ToolAttachSocketName);
	ThirdPersonToolMesh->SetOwnerNoSee(true);
	ThirdPersonToolMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	FirstPersonToolMesh = CreateDefaultSubobject<UStaticMeshComponent>(
		TEXT("FirstPersonToolMesh"));
	FirstPersonToolMesh->SetupAttachment(FirstPersonMesh, ToolAttachSocketName);
	FirstPersonToolMesh->SetOnlyOwnerSee(true);
	FirstPersonToolMesh->SetOwnerNoSee(false);
	FirstPersonToolMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FirstPersonToolMesh->SetCanEverAffectNavigation(false);

	HoldPoint = CreateDefaultSubobject<USceneComponent>(TEXT("HoldPoint"));
	HoldPoint->SetupAttachment(Camera);
	HoldPoint->SetRelativeLocation(FVector(150.f, 0.f, 0.f));

	PhysicsHandle = CreateDefaultSubobject<UPhysicsHandleComponent>(TEXT("PhysicsHandle"));

	InteractionComponent = CreateDefaultSubobject<UMD_InteractionComponent>(TEXT("InteractionComponent"));
	InteractionComponent->SetInteractionDependencies(Camera, HoldPoint, PhysicsHandle);

	SupplyComponent = CreateDefaultSubobject<UMD_SupplyComponent>(TEXT("SupplyComponent"));

	OxygenComponent = CreateDefaultSubobject<UMD_OxygenComponent>(TEXT("OxygenComponent"));
}

void AMD_Character::ForceEnterWalkState()
{
	// TODO(김동연): GAS 스태미나 고갈 이벤트에서 호출 연결 예정
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
}

UAbilitySystemComponent* AMD_Character::GetAbilitySystemComponent() const
{
	AMD_PlayerState* MDPlayerState = GetPlayerState<AMD_PlayerState>();

	if (!IsValid(MDPlayerState))
	{
		return nullptr;
	}

	return MDPlayerState->GetAbilitySystemComponent();
}

void AMD_Character::GetScrapeAnimationAim(
	FRotator& AimDelta,
	bool& Active)
{
	AimDelta = FRotator::ZeroRotator;
	Active = false;
	if (!IsValid(ScrapeToolComponent))
	{
		return;
	}

	FVector ContactWorldLocation = FVector::ZeroVector;
	if (!ScrapeToolComponent->GetCurrentScrapeContact(ContactWorldLocation))
	{
		return;
	}

	const FVector HandWorldLocation = IsValid(GetMesh())
		? GetMesh()->GetSocketLocation(TEXT("hand_r"))
		: GetActorLocation();
	const FVector AimWorldDirection =
		(ContactWorldLocation - HandWorldLocation).GetSafeNormal();
	if (AimWorldDirection.IsNearlyZero())
	{
		return;
	}

	const FQuat CurrentHandAim = IsValid(ThirdPersonToolMesh)
		? (ThirdPersonToolMesh->GetSocketLocation(TEXT("Blade_Tip"))
			- HandWorldLocation).GetSafeNormal().ToOrientationQuat()
		: GetActorForwardVector().ToOrientationQuat();
	AimDelta = (AimWorldDirection.ToOrientationQuat()
		* CurrentHandAim.Inverse()).Rotator();
	// 소유 클라이언트는 예측 상태를, 서버와 simulated proxy는 서버 확정 상태를 쓴다.
	// bIsScraping은 non-owner에게만 복제되므로 FocusReady 접촉만으로 원격 긁기
	// 포즈가 켜지지 않는다.
	Active = ScrapeToolComponent->IsScraping();
}

void AMD_Character::UpdateScrapeAnimationBlade(
	const bool Active,
	const double DeltaTime,
	FRotator& Swing)
{
	Swing = FRotator::ZeroRotator;
	FVector ContactWorldLocation = FVector::ZeroVector;
	FVector BladeWorldDirection = FVector::ZeroVector;
	const bool bHasContactPose = IsValid(ScrapeToolComponent)
		&& ScrapeToolComponent->GetCurrentScrapeContactPose(
			ContactWorldLocation,
			BladeWorldDirection);

	if (bHasContactPose)
	{
		BladeWorldDirection = BladeWorldDirection.GetSafeNormal();
		if (bHasScrapeAnimationBladeDirection
			&& FVector::DotProduct(
				BladeWorldDirection,
				LastScrapeAnimationBladeWorldDirection) < 0.0f)
		{
			BladeWorldDirection *= -1.0f;
		}
		LastScrapeAnimationBladeWorldDirection = BladeWorldDirection;
		bHasScrapeAnimationBladeDirection = !BladeWorldDirection.IsNearlyZero();
	}
	else if (!Active)
	{
		LastScrapeAnimationBladeWorldDirection = FVector::ZeroVector;
		bHasScrapeAnimationBladeDirection = false;
	}

	const UMD_ToolDefinition* CurrentTool = IsValid(EquipHotbarComponent)
		? EquipHotbarComponent->GetCurrentTool()
		: nullptr;
	const FRotator BaseToolRotation = IsValid(CurrentTool)
		? CurrentTool->AttachOffset.Rotator()
		: FRotator::ZeroRotator;
	FRotator TargetToolRotation = BaseToolRotation;
	if (Active
		&& bHasScrapeAnimationBladeDirection
		&& IsValid(GetMesh())
		&& IsValid(ThirdPersonToolMesh))
	{
		UStaticMesh* const ToolMesh = ThirdPersonToolMesh->GetStaticMesh();
		if (ScrapeAnimationBladeReferenceMesh.Get() != ToolMesh)
		{
			ScrapeAnimationBladeReferenceMesh = ToolMesh;
			ScrapeAnimationBladeReferenceDirection = FVector::ZeroVector;
			ScrapeAnimationToolTwistAxis = FVector::ZeroVector;
			if (IsValid(ToolMesh))
			{
				const FBoxSphereBounds MeshBounds = ToolMesh->GetBounds();
				if (const UStaticMeshSocket* BladeTipSocket =
					ToolMesh->FindSocket(TEXT("Blade_Tip")))
				{
					ScrapeAnimationToolTwistAxis =
						(BladeTipSocket->RelativeLocation - MeshBounds.Origin)
						.GetSafeNormal();
				}
				if (ScrapeAnimationToolTwistAxis.IsNearlyZero())
				{
					const FVector Extent = MeshBounds.BoxExtent;
					ScrapeAnimationToolTwistAxis = Extent.X >= Extent.Y
						&& Extent.X >= Extent.Z ? FVector::ForwardVector
						: (Extent.Y >= Extent.Z
							? FVector::RightVector
							: FVector::UpVector);
				}

				// 헤라 메시의 가장 얇은 축은 blade depth(진행축)다. 손잡이
				// 축과 평행한 후보는 제외해 그 축 주위의 twist만 계산한다.
				const FVector Extent = MeshBounds.BoxExtent;
				const FVector CandidateAxes[] = {
					FVector::ForwardVector,
					FVector::RightVector,
					FVector::UpVector};
				const float CandidateExtents[] = {Extent.X, Extent.Y, Extent.Z};
				float BestExtent = TNumericLimits<float>::Max();
				for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
				{
					if (FMath::Abs(FVector::DotProduct(
							CandidateAxes[AxisIndex],
							ScrapeAnimationToolTwistAxis)) > 0.8f)
					{
						continue;
					}
					if (CandidateExtents[AxisIndex] < BestExtent)
					{
						BestExtent = CandidateExtents[AxisIndex];
						ScrapeAnimationBladeReferenceDirection =
							CandidateAxes[AxisIndex];
					}
				}
			}
		}

		// AttachOffset과 메시 기준축은 실제 도구 부착 소켓-local이다.
		// 복제된 world blade 축도 같은 공간으로 옮긴 뒤에만 twist를 계산한다.
		const FName ToolSocketName = ThirdPersonToolMesh->GetAttachSocketName()
			.IsNone()
			? ToolAttachSocketName
			: ThirdPersonToolMesh->GetAttachSocketName();
		const USceneComponent* const ToolAttachParent =
			ThirdPersonToolMesh->GetAttachParent();
		const FTransform ToolSocketWorldTransform = IsValid(ToolAttachParent)
			? ToolAttachParent->GetSocketTransform(ToolSocketName, RTS_World)
			: GetMesh()->GetSocketTransform(ToolSocketName, RTS_World);
		const FVector SocketSpaceBladeDirection = ToolSocketWorldTransform
			.InverseTransformVectorNoScale(
				LastScrapeAnimationBladeWorldDirection)
			.GetSafeNormal();
		const FVector ToolForwardAxis = BaseToolRotation.RotateVector(
			ScrapeAnimationToolTwistAxis).GetSafeNormal();
		const FVector ReferenceBladeDirection = BaseToolRotation.RotateVector(
			ScrapeAnimationBladeReferenceDirection).GetSafeNormal();
		const FVector ReferenceOnBladePlane = FVector::VectorPlaneProject(
			ReferenceBladeDirection,
			ToolForwardAxis).GetSafeNormal();
		const FVector TargetOnBladePlane = FVector::VectorPlaneProject(
			SocketSpaceBladeDirection,
			ToolForwardAxis).GetSafeNormal();
		if (!ToolForwardAxis.IsNearlyZero()
			&& !ReferenceOnBladePlane.IsNearlyZero()
			&& !TargetOnBladePlane.IsNearlyZero())
		{
			const float CosAngle = FMath::Clamp(FVector::DotProduct(
				ReferenceOnBladePlane,
				TargetOnBladePlane), -1.0f, 1.0f);
			const float SinAngle = FVector::DotProduct(
				ToolForwardAxis,
				FVector::CrossProduct(
					ReferenceOnBladePlane,
					TargetOnBladePlane));
			const float TwistDegrees = FMath::RadiansToDegrees(
				FMath::Atan2(SinAngle, CosAngle));
			const FQuat TwistRotation(ToolForwardAxis, FMath::DegreesToRadians(
				TwistDegrees));
			TargetToolRotation = (TwistRotation
				* BaseToolRotation.Quaternion()).Rotator();
			// ABP의 upperarm_r 입력은 기존에도 Pitch/Yaw swing이었다. 날 축의
			// roll은 도구 메시가 맡고 팔에는 과도한 회전을 전달하지 않는다.
			Swing = FRotator::ZeroRotator;
		}
	}

	const float SafeDeltaTime = FMath::IsFinite(DeltaTime)
		? static_cast<float>(FMath::Max(DeltaTime, 0.0))
		: 0.0f;
	ScrapeAnimationToolRelativeRotation = FMath::RInterpTo(
		ScrapeAnimationToolRelativeRotation,
		TargetToolRotation,
		SafeDeltaTime,
		10.0f);
	if (IsValid(ThirdPersonToolMesh))
	{
		ThirdPersonToolMesh->SetRelativeRotation(
			ScrapeAnimationToolRelativeRotation);
	}
}

void AMD_Character::CancelScrapeInteraction()
{
	CancelActiveScrapeAbilityAndEndFocus();
}

bool AMD_Character::RegisterActiveScrapeAbility(
	const FGameplayAbilitySpecHandle AbilityHandle,
	const uint64 FocusSessionId)
{
	if (!AbilityHandle.IsValid()
		|| FocusSessionId == 0
		|| !IsScrapeToolEquipped())
	{
		return false;
	}

	UAbilitySystemComponent* const AbilitySystem = GetAbilitySystemComponent();
	const FGameplayAbilitySpec* const ActivatedSpec = AbilitySystem
		? AbilitySystem->FindAbilitySpecFromHandle(AbilityHandle)
		: nullptr;
	if (!ActivatedSpec || !ActivatedSpec->IsActive())
	{
		return false;
	}

	if (ActiveScrapeAbilityHandle.IsValid()
		&& ActiveScrapeAbilityHandle != AbilityHandle)
	{
		const FGameplayAbilitySpec* const PreviousSpec =
			AbilitySystem->FindAbilitySpecFromHandle(ActiveScrapeAbilityHandle);
		if (PreviousSpec && PreviousSpec->IsActive())
		{
			return false;
		}
	}

	// A GameplayAbilitySpecHandle is reused between activations. The actual GA
	// callback is the ordering boundary that may replace an older session value.
	ActiveScrapeAbilityHandle = AbilityHandle;
	ActiveScrapeAbilitySessionId = FocusSessionId;
	ActiveScrapeAbilityTool = IsValid(EquipHotbarComponent)
		? EquipHotbarComponent->GetCurrentTool()
		: nullptr;
	return true;
}

void AMD_Character::UnregisterActiveScrapeAbility(
	const FGameplayAbilitySpecHandle AbilityHandle,
	const uint64 FocusSessionId)
{
	if (ActiveScrapeAbilityHandle != AbilityHandle
		|| ActiveScrapeAbilitySessionId != FocusSessionId)
	{
		return;
	}

	ActiveScrapeAbilityHandle = FGameplayAbilitySpecHandle();
	ActiveScrapeAbilitySessionId = 0;
	ActiveScrapeAbilityTool.Reset();
}

void AMD_Character::CancelScrapeAbilityForSession(const uint64 FocusSessionId)
{
	if (FocusSessionId == 0 || ActiveScrapeAbilitySessionId != FocusSessionId)
	{
		return;
	}

	CancelActiveScrapeAbility();
}

// Called when the game starts or when spawned
void AMD_Character::BeginPlay()
{
	Super::BeginPlay();

	RefreshFirstPersonArmsTick();

	// 사다리 진입과 이탈을 캐릭터 캡슐 기준으로 감지하기 위한 델리게이트 연결
	UCapsuleComponent* CharacterCapsule = GetCapsuleComponent();
	if (IsValid(CharacterCapsule) == true)
	{
		CharacterCapsule->OnComponentBeginOverlap.AddUniqueDynamic(this, &AMD_Character::HandleCollisionBeginOverlap);
		CharacterCapsule->OnComponentEndOverlap.AddUniqueDynamic(this, &AMD_Character::HandleCollisionEndOverlap);
	}

	if (IsValid(ScrapeToolComponent))
	{
		ScrapeToolComponent->OnScrapeLoadChanged.AddUniqueDynamic(
			this,
			&AMD_Character::HandleScrapeLoadChanged);
	}

	// 데미지 브리지: 서버에서 ApplyDamage(오염수 DoT·붕괴 등)를 Health 감소로 연결
	if (HasAuthority())
	{
		OnTakeAnyDamage.AddDynamic(this, &AMD_Character::HandleTakeAnyDamage);
	}

	if (IsLocallyControlled())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = GetEnhancedInputSubsystem())
		{
			if (IsValid(PlayerMappingContext))
			{
				Subsystem->AddMappingContext(PlayerMappingContext, 0);
			}
		}

	}

	if (IsValid(EquipHotbarComponent))
	{
		EquipHotbarComponent->OnHotbarChanged.AddDynamic(this, &AMD_Character::HandleHotbarChanged);
		HandleHotbarChanged();
	}
}

void AMD_Character::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemoveScrapeFocusInputContext();

	if (IsValid(InteractionComponent))
	{
		InteractionComponent->CancelInteraction();
	}

	Super::EndPlay(EndPlayReason);
}

void AMD_Character::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateFirstPersonArms(DeltaSeconds);
	UpdateFirstPersonToolTransform();
}

void AMD_Character::RefreshFirstPersonArmsTick()
{
	const bool bNeedsTick = IsLocallyControlled();
	SetActorTickEnabled(bNeedsTick);

	if (!bNeedsTick)
	{
		return;
	}

	BindFirstPersonArmsToBodyPose();
	UpdateFirstPersonArms(0.f, /*bSnapToTarget=*/true);
}

void AMD_Character::BindFirstPersonArmsToBodyPose()
{
	USkeletalMeshComponent* BodyMesh = GetMesh();
	if (!IsValid(FirstPersonMesh) || !IsValid(BodyMesh))
	{
		return;
	}

	// 1인칭 팔은 자체 AnimBP 없이 3인칭 메시의 포즈를 그대로 따라간다(본 이름이 동일).
	// BP 그래프의 SetLeaderPoseComponent에만 의존하면 BP를 되돌릴 때 조용히 끊긴다.
	if (FirstPersonMesh->LeaderPoseComponent != BodyMesh)
	{
		FirstPersonMesh->SetLeaderPoseComponent(BodyMesh);
	}
}

void AMD_Character::UpdateFirstPersonToolTransform()
{
	const USkeletalMeshComponent* BodyMesh = GetMesh();
	if (!IsValid(FirstPersonToolMesh) || !IsValid(FirstPersonMesh) || !IsValid(BodyMesh)
		|| !IsLocallyControlled())
	{
		return;
	}

	if (!BodyMesh->DoesSocketExist(ToolAttachSocketName))
	{
		return;
	}

	const UMD_ToolDefinition* CurrentTool = IsValid(EquipHotbarComponent)
		? EquipHotbarComponent->GetCurrentTool()
		: nullptr;

	// 본체가 확정한 소켓 자세를 컴포넌트 공간으로 받아, 1인칭 팔이 놓인 자리에
	// 그대로 얹는다. 팔로워가 자식 트랜스폼을 갱신하지 않는 문제를 우회한다.
	const FTransform SocketLocal =
		BodyMesh->GetSocketTransform(ToolAttachSocketName, RTS_Component);
	FTransform Offset =
		GetToolAttachTransform(FirstPersonToolMesh->GetStaticMesh(), CurrentTool);

	// 날 축 roll 은 UpdateScrapeAnimationBlade 가 ThirdPersonToolMesh 의 상대
	// 회전으로만 넣는다. 1인칭 도구는 그 처리에서 빠져 있어 같은 값을 얹어 준다.
	if (IsValid(ThirdPersonToolMesh))
	{
		Offset.SetRotation(ThirdPersonToolMesh->GetRelativeRotation().Quaternion());
	}

	FirstPersonToolMesh->SetWorldTransform(
		Offset * SocketLocal * FirstPersonMesh->GetComponentTransform());
}

void AMD_Character::UpdateFirstPersonArms(float DeltaSeconds, bool bSnapToTarget)
{
	if (!IsValid(FirstPersonArmsPivot) || !IsValid(Camera) || !IsLocallyControlled())
	{
		return;
	}

	// 헤라 포커스 중에는 화면이 ScrapeFocusViewRotation 으로 잠기고 마우스는
	// 화면 안의 포인터만 움직인다. 그 구간에 컨트롤 회전을 따라가면 화면은
	// 가만한데 팔만 매 틱 다시 세워져 떨린다. 그때는 CalcCamera 가 확정한
	// 뷰 회전을 쓴다. 포커스가 아닐 때는 지연이 없도록 컨트롤 회전 그대로.
	const bool bUseViewRotation = bHasLastViewRotation
		&& IsValid(ScrapeToolComponent)
		&& ScrapeToolComponent->IsScrapeFocusActive();
	const FRotator BaseRotation = bUseViewRotation ? LastViewRotation : GetControlRotation();
	const FRotator TargetRotation = BaseRotation + FirstPersonArmsRotationOffset;
	const FVector TargetLocation =
		Camera->GetComponentLocation() + TargetRotation.RotateVector(FirstPersonArmsOffset);

	// 리스폰·텔레포트로 멀리 튀었을 때 팔이 월드를 가로질러 날아오지 않게 끊어서 붙인다.
	static constexpr float SnapDistanceSq = 200.f * 200.f;
	const bool bSnap = bSnapToTarget
		|| FirstPersonArmsFollowSpeed <= 0.f
		|| FVector::DistSquared(FirstPersonArmsPivot->GetComponentLocation(), TargetLocation) > SnapDistanceSq;

	if (bSnap)
	{
		FirstPersonArmsPivot->SetWorldLocationAndRotation(TargetLocation, TargetRotation);
		return;
	}

	FirstPersonArmsPivot->SetWorldLocationAndRotation(
		FMath::VInterpTo(FirstPersonArmsPivot->GetComponentLocation(), TargetLocation,
			DeltaSeconds, FirstPersonArmsFollowSpeed),
		FMath::RInterpTo(FirstPersonArmsPivot->GetComponentRotation(), TargetRotation,
			DeltaSeconds, FirstPersonArmsFollowSpeed));
}

void AMD_Character::OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PrevMovementMode, PreviousCustomMode);

	// 커스텀 사다리 모드 확인과 기존 수영 진입 의사 해제를 함께 처리하기 위한 이동 컴포넌트 참조
	UMD_CharacterMovementComponent* MDMovement = Cast<UMD_CharacterMovementComponent>(GetCharacterMovement());
	if (IsValid(MDMovement) == true)
	{
		if (MDMovement->MovementMode == MOVE_Walking)
		{
			MDMovement->SetWantsToSwim(false);
		}
	}

	EMDMovementState NewState = GetCharacterMovement()->IsSwimming() ? EMDMovementState::Swimming : EMDMovementState::Walking;
	if (IsValid(MDMovement) == true && MDMovement->IsClimbing() == true)
	{
		NewState = EMDMovementState::Climbing;
	}

	if (NewState == CurrentMovementState)
	{
		return;
	}

	CurrentMovementState = NewState;
	CancelActiveScrapeAbilityAndEndFocus();
	OnMovementStateChanged.Broadcast(CurrentMovementState);

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{

		if (NewState == EMDMovementState::Swimming)
		{
			ASC->AddLooseGameplayTag(MDGameplayTags::State_Swimming);
		}
		else
		{
			ASC->RemoveLooseGameplayTag(MDGameplayTags::State_Swimming);
		}

		// 사다리 타기 상태 태그 추가 또는 제거
		if (NewState == EMDMovementState::Climbing)
		{
			ASC->AddLooseGameplayTag(MDGameplayTags::State_Climbing);
		}
		else
		{
			ASC->RemoveLooseGameplayTag(MDGameplayTags::State_Climbing);
		}
	}
}

void AMD_Character::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	InitializeAbilitySystem();
	GrantDefaultAbilities();
	RefreshFirstPersonArmsTick();
}

void AMD_Character::UnPossessed()
{
	RemoveScrapeFocusInputContext();

	if (IsValid(InteractionComponent))
	{
		InteractionComponent->CancelInteraction();
	}

	Super::UnPossessed();

	RefreshFirstPersonArmsTick();
}

void AMD_Character::PawnClientRestart()
{
	// 입력 컴포넌트가 재생성되기 전에 기존 포커스와 오버레이를 정리한다.
	RemoveScrapeFocusInputContext();

	Super::PawnClientRestart();

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = GetEnhancedInputSubsystem())
	{
		if (IsValid(PlayerMappingContext))
		{
			Subsystem->AddMappingContext(PlayerMappingContext, 0);
		}
	}

	RefreshScrapeFocusInputContext();
}

void AMD_Character::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	InitializeAbilitySystem();
}

void AMD_Character::CalcCamera(
	const float DeltaTime,
	FMinimalViewInfo& OutResult)
{
	Super::CalcCamera(DeltaTime, OutResult);
	if (IsValid(ScrapeToolComponent))
	{
		ScrapeToolComponent->ApplyScrapeFocusCameraView(OutResult);
	}

	// 포커스 조준 중에는 여기서 뷰가 잠긴 회전으로 덮인다. 팔 피벗이 화면과
	// 같은 기준을 쓸 수 있도록 확정된 값을 남긴다.
	LastViewRotation = OutResult.Rotation;
	bHasLastViewRotation = true;
}

void AMD_Character::OnRep_Controller()
{
	Super::OnRep_Controller();

	RefreshFirstPersonArmsTick();
}

// Called to bind functionality to input
void AMD_Character::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!IsValid(EIC))
	{
		return;
	}

	EnsureScrapeFocusInputObjects();
	if (IsValid(ScrapeFocusInputAction))
	{
		EIC->BindAction(
			ScrapeFocusInputAction,
			ETriggerEvent::Started,
			this,
			&AMD_Character::HandleScrapeFocusInputStarted);
		EIC->BindAction(
			ScrapeFocusInputAction,
			ETriggerEvent::Completed,
			this,
			&AMD_Character::HandleScrapeFocusInputEnded);
		EIC->BindAction(
			ScrapeFocusInputAction,
			ETriggerEvent::Canceled,
			this,
			&AMD_Character::HandleScrapeFocusInputEnded);
	}

	if (!IsValid(InputConfig))
	{
		return;
	}

	EIC->BindAction(InputConfig->MoveAction, ETriggerEvent::Triggered, this, &AMD_Character::HandleMoveInput);
	EIC->BindAction(InputConfig->LookAction, ETriggerEvent::Triggered, this, &AMD_Character::HandleLookInput);
	EIC->BindAction(InputConfig->JumpAction, ETriggerEvent::Started, this, &AMD_Character::HandleJumpOrSwimInput);
	EIC->BindAction(InputConfig->JumpAction, ETriggerEvent::Completed, this, &AMD_Character::HandleJumpOrSwimReleasedInput);
	EIC->BindAction(InputConfig->InteractAction, ETriggerEvent::Started, this, &AMD_Character::HandleInteractInput);
	EIC->BindAction(InputConfig->AbilityAction, ETriggerEvent::Started, this, &AMD_Character::HandleAbilityInput);
	EIC->BindAction(InputConfig->AbilityAction, ETriggerEvent::Completed, this, &AMD_Character::HandleAbilityReleasedInput);
	EIC->BindAction(InputConfig->AbilityAction, ETriggerEvent::Canceled, this, &AMD_Character::HandleAbilityReleasedInput);
	EIC->BindAction(InputConfig->SwapEquipmentAction, ETriggerEvent::Triggered, this, &AMD_Character::HandleSwapEquipmentInput);
	//EIC->BindAction(InputConfig->WallCutAction, ETriggerEvent::Started, this, &AMD_Character::HandleWallCutInput);
	//EIC->BindAction(InputConfig->WallScanAction, ETriggerEvent::Started, this, &AMD_Character::HandleWallScanInput);

	for (int32 SlotIndex = 0; SlotIndex < InputConfig->HotbarSlotActions.Num(); ++SlotIndex)
	{
		UInputAction* SlotAction = InputConfig->HotbarSlotActions[SlotIndex];

		if (!IsValid(SlotAction))
		{
			continue;
		}
		EIC->BindAction(SlotAction,ETriggerEvent::Started,this,&AMD_Character::HandleSelectHotbarSlotInput,SlotIndex);
	}
}

void AMD_Character::HandleMoveInput(const FInputActionValue& Value)
{
	if (!Controller)
	{
		return;
	}

	const FVector2D MoveInput = Value.Get<FVector2D>();
	if (MoveInput.SizeSquared() > FMath::Square(ScrapeFocusMoveInputDeadzone))
	{
		HandleScrapeFocusInputEnded();
	}

	// 사다리 타기 상태 확인
	UMD_CharacterMovementComponent* MDMovement = Cast<UMD_CharacterMovementComponent>(GetCharacterMovement());
	if (IsValid(MDMovement) == true && MDMovement->IsClimbing() == true)
	{
		// 사용중인 사다리가 유효하고, 종료 지점으로 이동중이 아닌 경우 입력값 적용
		AMD_Ladder* CurrentLadder = MDMovement->GetCurrentLadder();
		if (IsValid(CurrentLadder) == true && MDMovement->IsTopDismounting() == false)
		{
			AddMovementInput(CurrentLadder->GetClimbUpVector(), MoveInput.X);
		}
		return;
	}

	const FRotator ControlRotation = Controller->GetControlRotation();
	const FRotator MovementRotation = (CurrentMovementState == EMDMovementState::Swimming) ? ControlRotation : FRotator(0.f, ControlRotation.Yaw, 0.f);

	const FVector ForwardDirection = FRotationMatrix(MovementRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(MovementRotation).GetUnitAxis(EAxis::Y);

	AddMovementInput(ForwardDirection, MoveInput.X);
	AddMovementInput(RightDirection, MoveInput.Y);
}

void AMD_Character::HandleLookInput(const FInputActionValue& Value)
{
	const FVector2D LookInput = Value.Get<FVector2D>();
	if (IsValid(ScrapeToolComponent)
		&& ScrapeToolComponent->ConsumeScrapeFocusLookInput(LookInput))
	{
		// Focus 중에는 같은 입력을 가상 포인터에만 사용하고 카메라에는 전달하지 않는다.
		return;
	}

	AddControllerYawInput(LookInput.X);
	AddControllerPitchInput(LookInput.Y);
}

void AMD_Character::HandleInteractInput(const FInputActionValue& Value)
{
	HandleScrapeFocusInputEnded();

	if (IsValid(InteractionComponent))
	{
		InteractionComponent->Interact();
	}
}

void AMD_Character::HandleAbilityInput(const FInputActionValue& Value)
{
	if (!IsValid(EquipHotbarComponent))
	{
		return;
	}

	UMD_ToolDefinition* CurrentTool = EquipHotbarComponent->GetCurrentTool();
	if (!IsValid(CurrentTool) || !CurrentTool->ToolAbilityTag.IsValid())
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Yellow, TEXT("No tool equipped"));
		return;
	}
	const bool bIsScrapeAbility = CurrentTool->ToolAbilityTag.MatchesTagExact(
		MDGameplayTags::Ability_Scrape);
	bScrapeAbilityInputCaptured = bIsScrapeAbility;
	if (bIsScrapeAbility
		&& (!IsValid(ScrapeToolComponent)
			|| !ScrapeToolComponent->IsScrapeFocusReady()
			|| !ScrapeToolComponent->RefreshScrapeFocusAim()))
	{
		// RMB 진입 승인 전 또는 유효하지 않은 포인터에서 누른 LMB는 저장하지 않는다.
		return;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!IsValid(ASC))
	{
		return;
	}

	if (bIsScrapeAbility)
	{
		TryActivateScrapeAbility(ASC, CurrentTool->ToolAbilityTag, CurrentTool);
		return;
	}

	// 비헤라 도구는 기존 tag 기반 활성화 흐름을 유지한다.
	FGameplayTagContainer AbilityTags(CurrentTool->ToolAbilityTag);
	ASC->TryActivateAbilitiesByTag(AbilityTags);
}

void AMD_Character::HandleAbilityReleasedInput(const FInputActionValue& Value)
{
	if (bScrapeAbilityInputCaptured || ActiveScrapeAbilityHandle.IsValid())
	{
		// 장비가 LMB보다 먼저 바뀌어도 release를 새 도구의 tag 취소로 넘기지 않는다.
		bScrapeAbilityInputCaptured = false;
		CancelActiveScrapeAbility();
		return;
	}

	if (!IsValid(EquipHotbarComponent))
	{
		return;
	}

	UMD_ToolDefinition* CurrentTool = EquipHotbarComponent->GetCurrentTool();
	if (!IsValid(CurrentTool) || !CurrentTool->ToolAbilityTag.IsValid())
	{
		return;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!IsValid(ASC))
	{
		return;
	}

	FGameplayTagContainer AbilityTags;
	AbilityTags.AddTag(CurrentTool->ToolAbilityTag);
	ASC->CancelAbilities(&AbilityTags);
}

void AMD_Character::HandleScrapeFocusInputStarted()
{
	if (!IsScrapeToolEquipped() || !IsValid(ScrapeToolComponent))
	{
		return;
	}

	ScrapeToolComponent->TryBeginScrapeFocus();
}

void AMD_Character::HandleScrapeFocusInputEnded()
{
	CancelActiveScrapeAbilityAndEndFocus();
}

void AMD_Character::HandleSwapEquipmentInput(const FInputActionValue& Value)
{
	//GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Red, TEXT("SwapEquipment: Activated"));
	if (!IsValid(EquipHotbarComponent))
	{
		return;
	}

	const float ScrollValue = Value.Get<float>();

	UE_LOG(LogTemp, Log, TEXT("[SwapEquipment] ScrollValue: %.2f"), ScrollValue);

	if (FMath::IsNearlyZero(ScrollValue))
	{
		return;
	}
	HandleScrapeFocusInputEnded();

	EquipHotbarComponent->CycleSlot(ScrollValue > 0.f ? 1 : -1);
	UE_LOG(LogTemp, Log, TEXT("[SwapEquipment] Direction: %s"), ScrollValue > 0.f ? TEXT("Next") : TEXT("Prev"));
}

void AMD_Character::HandleSelectHotbarSlotInput(const FInputActionValue& Value, int32 SlotIndex)
{
	if (!IsValid(EquipHotbarComponent))
	{
		return;
	}
	if (SlotIndex == EquipHotbarComponent->GetCurrentSlotIndex())
	{
		return;
	}
	HandleScrapeFocusInputEnded();

	EquipHotbarComponent->SelectSlot(SlotIndex);
	UE_LOG(LogTemp, Log, TEXT("[Hotbar] 숫자키 슬롯 선택: %d"), SlotIndex);
}

void AMD_Character::HandleJumpOrSwimInput(const FInputActionValue& Value)
{
	HandleScrapeFocusInputEnded();

	UMD_CharacterMovementComponent* MDMovement = Cast<UMD_CharacterMovementComponent>(GetCharacterMovement());
	if (!IsValid(MDMovement))
	{
		return;
	}

	// 사다리 이동 상태 확인
	if (MDMovement->IsClimbing() == true)
	{
		// 클라이언트 환경에서 점프 처리
		bool bClientJump = MDMovement->JumpOffLadder();
		// 클라이언트에서 점프 성공 시 서버에게 캐릭터 점프 요청
		if (bClientJump == true && HasAuthority() == false)
		{
			ServerJumpOffLadder();
		}
		return;
	}

	//if (MDMovement->IsMovingOnGround() && MDMovement->IsInWater())
	//{
	//	MDMovement->EnterSwimmingFromGround();
	//	return;
	//}

	Jump();
}

void AMD_Character::HandleJumpOrSwimReleasedInput(const FInputActionValue& Value)
{
	StopJumping();
}

void AMD_Character::HandleCollisionBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// 사다리 영역 진입 확인
	AMD_Ladder* Ladder = Cast<AMD_Ladder>(OtherActor);
	if (IsValid(Ladder) == false || OtherComponent != Ladder->GetClimbTrigger())
	{
		return;
	}

	// 사다리 타기 시작
	UMD_CharacterMovementComponent* MDMovement = Cast<UMD_CharacterMovementComponent>(GetCharacterMovement());
	if (IsValid(MDMovement) == true)
	{
		MDMovement->StartClimbing(Ladder);
	}
}

void AMD_Character::HandleCollisionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex)
{
	// 사다리 영역 진입 확인
	AMD_Ladder* Ladder = Cast<AMD_Ladder>(OtherActor);
	if (IsValid(Ladder) == false || OtherComponent != Ladder->GetClimbTrigger())
	{
		return;
	}

	// 사다리 타기 종료 지점으로 이동중이 아닐 때, 낙하 모드로 복귀
	UMD_CharacterMovementComponent* MDMovement = Cast<UMD_CharacterMovementComponent>(GetCharacterMovement());
	if (IsValid(MDMovement) == true && MDMovement->GetCurrentLadder() == Ladder)
	{
		// 하단에 도달한 경우 오버랩 종료보다 바닥 착지 판정을 우선 적용
		float SpeedAtLadder = MDMovement->GetLadderClimbSpeed();
		if (MDMovement->TryLeaveLadderAtBottom(SpeedAtLadder) == true)
		{
			return;
		}

		if (MDMovement->IsTopDismounting() == false)
		{
			MDMovement->StopClimbing(MOVE_Falling);
		}
	}
}

void AMD_Character::ServerJumpOffLadder_Implementation()
{
	// 서버에서 사다리를 타고 있는 상태의 경우 점프 처리
	UMD_CharacterMovementComponent* MDMovement = Cast<UMD_CharacterMovementComponent>(GetCharacterMovement());
	if (IsValid(MDMovement) == true && MDMovement->IsClimbing() == true)
	{
		MDMovement->JumpOffLadder();
	}
}

/*void AMD_Character::HandleWallCutInput(const FInputActionValue& Value)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!IsValid(ASC))
	{
		return;
	}

	if (!WallCutAbilityClass)
	{
		return;
	}

	ASC->TryActivateAbilityByClass(WallCutAbilityClass);
} */

/*void AMD_Character::HandleWallScanInput(const FInputActionValue& Value)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!IsValid(ASC))
	{
		return;
	}

	if (!WallScanAbilityClass)
	{
		return;
	}

	ASC->TryActivateAbilityByClass(WallScanAbilityClass);
}*/

void AMD_Character::InitializeAbilitySystem()
{
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->InitAbilityActorInfo(GetPlayerState(), this);
	}
	
	//PlayerState가 호출되는 시점에 체력변화 델리게이트를 구독.
	AMD_PlayerState* MDPlayerState = GetPlayerState<AMD_PlayerState>();
	if (!IsValid(MDPlayerState))
	{
		return;
	}

	if (!MDPlayerState->OnHealthStateChanged.IsAlreadyBound(this, &AMD_Character::HandleHealthStateChanged))
	{
		MDPlayerState->OnHealthStateChanged.AddDynamic(this, &AMD_Character::HandleHealthStateChanged);
	}

	// The delegate can fire before possession/PlayerState replication finishes, so synchronize once on bind.
	HandleHealthStateChanged(MDPlayerState->GetHealthState());
}

void AMD_Character::GrantDefaultAbilities()
{
	if (!HasAuthority())
	{
		return;
	}
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!IsValid(ASC))
	{
		return;
	}

	for (const TSubclassOf<UGameplayAbility>& AbilityClass : DefaultAbilities)
	{
		if (AbilityClass)
		{
			ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, this));
		}
	}
}

bool AMD_Character::TryActivateScrapeAbility(
	UAbilitySystemComponent* AbilitySystem,
	const FGameplayTag& AbilityTag,
	UMD_ToolDefinition* SourceTool)
{
	const uint64 ExpectedFocusSessionId = IsValid(ScrapeToolComponent)
		? ScrapeToolComponent->GetActiveScrapeFocusSessionId()
		: 0;
	if (!IsValid(AbilitySystem)
		|| !AbilityTag.IsValid()
		|| !IsValid(SourceTool)
		|| ExpectedFocusSessionId == 0)
	{
		return false;
	}

	if (ActiveScrapeAbilityHandle.IsValid())
	{
		const FGameplayAbilitySpec* ActiveSpec = AbilitySystem->FindAbilitySpecFromHandle(
			ActiveScrapeAbilityHandle);
		if (ActiveSpec
			&& ActiveSpec->IsActive()
			&& ActiveScrapeAbilitySessionId == ExpectedFocusSessionId)
		{
			return true;
		}
		if (ActiveSpec && ActiveSpec->IsActive())
		{
			// A different session must never take ownership of a still-running GA.
			return false;
		}

		ActiveScrapeAbilityHandle = FGameplayAbilitySpecHandle();
		ActiveScrapeAbilitySessionId = 0;
		ActiveScrapeAbilityTool.Reset();
	}

	const FGameplayTagContainer AbilityTags(AbilityTag);
	TArray<FGameplayAbilitySpec*> MatchingSpecs;
	AbilitySystem->GetActivatableGameplayAbilitySpecsByAllMatchingTags(
		AbilityTags,
		MatchingSpecs,
		/*bOnlyAbilitiesThatSatisfyTagRequirements=*/true);

	MatchingSpecs.RemoveAll(
		[AbilityTag](const FGameplayAbilitySpec* Spec)
		{
			return !Spec
				|| !IsValid(Spec->Ability)
				|| !Spec->Ability->GetAssetTags().HasTagExact(AbilityTag);
		});
	if (MatchingSpecs.Num() != 1)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[Scrape] Expected exactly one activatable Ability spec with exact tag %s, found %d"),
			*AbilityTag.ToString(),
			MatchingSpecs.Num());
		return false;
	}

	FGameplayAbilitySpec* Spec = MatchingSpecs[0];
	if (!Spec || !Spec->Handle.IsValid())
	{
		return false;
	}

	const FGameplayAbilitySpecHandle CandidateHandle = Spec->Handle;
	if (!AbilitySystem->TryActivateAbility(CandidateHandle))
	{
		return false;
	}

	const FGameplayAbilitySpec* ActivatedSpec =
		AbilitySystem->FindAbilitySpecFromHandle(CandidateHandle);
	if (ActivatedSpec
		&& ActivatedSpec->IsActive()
		&& ActiveScrapeAbilityHandle == CandidateHandle
		&& ActiveScrapeAbilitySessionId == ExpectedFocusSessionId)
	{
		return true;
	}

	// Only the actual ActivateAbility callback may register ownership. This avoids
	// reviving a handle that EndAbility already cleared during synchronous activation.
	return false;
}

void AMD_Character::CancelActiveScrapeAbility()
{
	const FGameplayAbilitySpecHandle HandleToCancel = ActiveScrapeAbilityHandle;
	ActiveScrapeAbilityHandle = FGameplayAbilitySpecHandle();
	ActiveScrapeAbilitySessionId = 0;
	ActiveScrapeAbilityTool.Reset();

	if (!HandleToCancel.IsValid())
	{
		return;
	}

	if (UAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponent())
	{
		AbilitySystem->CancelAbilityHandle(HandleToCancel);
	}
}

void AMD_Character::CancelActiveScrapeAbilityAndEndFocus()
{
	// CancelAbilityHandle은 GA EndAbility를 통해 scraping만 먼저 멈춘다.
	CancelActiveScrapeAbility();
	if (IsValid(ScrapeToolComponent))
	{
		ScrapeToolComponent->EndScrapeFocus();
	}
}

void AMD_Character::EnsureScrapeFocusInputObjects()
{
	if (IsValid(ScrapeFocusInputAction) && IsValid(ScrapeFocusInputMappingContext))
	{
		return;
	}

	ScrapeFocusInputAction = NewObject<UInputAction>(this, NAME_None, RF_Transient);
	ScrapeFocusInputMappingContext = NewObject<UInputMappingContext>(this, NAME_None, RF_Transient);
	if (!IsValid(ScrapeFocusInputAction) || !IsValid(ScrapeFocusInputMappingContext))
	{
		return;
	}

	ScrapeFocusInputAction->ValueType = EInputActionValueType::Boolean;
	ScrapeFocusInputAction->bConsumeInput = true;
	ScrapeFocusInputMappingContext->MapKey(ScrapeFocusInputAction, EKeys::RightMouseButton);
}

bool AMD_Character::IsScrapeToolEquipped() const
{
	if (!IsValid(EquipHotbarComponent))
	{
		return false;
	}

	const UMD_ToolDefinition* CurrentTool = EquipHotbarComponent->GetCurrentTool();
	return IsValid(CurrentTool)
		&& CurrentTool->ToolAbilityTag.MatchesTagExact(MDGameplayTags::Ability_Scrape);
}

UEnhancedInputLocalPlayerSubsystem* AMD_Character::GetEnhancedInputSubsystem() const
{
	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!IsValid(PlayerController) || !PlayerController->IsLocalController())
	{
		return nullptr;
	}

	ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
	return IsValid(LocalPlayer)
		? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer)
		: nullptr;
}

void AMD_Character::RefreshScrapeFocusInputContext()
{
	UEnhancedInputLocalPlayerSubsystem* Subsystem = GetEnhancedInputSubsystem();
	if (!IsValid(Subsystem))
	{
		// 원격 Pawn/전용 서버에는 로컬 mapping context가 없다. gameplay Focus 정리는
		// 장비 identity가 실제로 바뀐 HandleHotbarChanged 또는 명시적 종료 사건이 맡는다.
		return;
	}
	if (!IsScrapeToolEquipped())
	{
		RemoveScrapeFocusInputContext();
		return;
	}

	EnsureScrapeFocusInputObjects();
	if (!IsValid(ScrapeFocusInputAction) || !IsValid(ScrapeFocusInputMappingContext))
	{
		RemoveScrapeFocusInputContext();
		return;
	}

	if (bScrapeFocusInputContextAdded && ScrapeFocusInputSubsystem.Get() != Subsystem)
	{
		RemoveScrapeFocusInputContext();
	}

	if (bScrapeFocusInputContextAdded)
	{
		return;
	}

	// 기본 옵션은 이미 눌린 키를 놓을 때까지 무시한다. 장착 순간 RMB 홀드로 자동 진입하지 않는다.
	const FModifyContextOptions Options;
	Subsystem->AddMappingContext(ScrapeFocusInputMappingContext, ScrapeFocusInputPriority, Options);
	ScrapeFocusInputSubsystem = Subsystem;
	ScrapeFocusContextTool = EquipHotbarComponent->GetCurrentTool();
	bScrapeFocusInputContextAdded = true;
}

void AMD_Character::RemoveScrapeFocusInputContext()
{
	CancelActiveScrapeAbilityAndEndFocus();

	if (bScrapeFocusInputContextAdded)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ScrapeFocusInputSubsystem.Get())
		{
			if (IsValid(ScrapeFocusInputMappingContext))
			{
				Subsystem->RemoveMappingContext(ScrapeFocusInputMappingContext);
			}
		}
	}

	ScrapeFocusInputSubsystem.Reset();
	ScrapeFocusContextTool.Reset();
	bScrapeFocusInputContextAdded = false;
}

void AMD_Character::HandleHotbarChanged()
{
	UMD_ToolDefinition* CurrentTool = IsValid(EquipHotbarComponent)
		? EquipHotbarComponent->GetCurrentTool()
		: nullptr;
	const bool bFocusToolChanged = bScrapeFocusInputContextAdded
		&& ScrapeFocusContextTool.Get() != CurrentTool;
	const bool bActiveScrapeToolChanged = ActiveScrapeAbilityHandle.IsValid()
		&& ActiveScrapeAbilityTool.Get() != CurrentTool;
	if (bFocusToolChanged || bActiveScrapeToolChanged)
	{
		// 컨텍스트 제거만으로는 입력 해제가 보장되지 않으므로 Ability와 Focus를 함께 정리한다.
		RemoveScrapeFocusInputContext();
	}
	if (HasAuthority()
		&& IsValid(CurrentTool)
		&& CurrentTool->ToolAbilityTag.MatchesTagExact(MDGameplayTags::Ability_Scrape)
		&& IsValid(ScrapeToolComponent)
		&& AppliedScrapeProfileTool.Get() != CurrentTool)
	{
		if (ScrapeToolComponent->SetScrapeToolSpec(CurrentTool->ScrapeProfile))
		{
			AppliedScrapeProfileTool = CurrentTool;
		}
	}
	else if (!IsValid(CurrentTool)
		|| !CurrentTool->ToolAbilityTag.MatchesTagExact(MDGameplayTags::Ability_Scrape))
	{
		AppliedScrapeProfileTool.Reset();
	}

	RefreshToolMesh();
	RefreshScrapeFocusInputContext();

	if (IsValid(EquipHotbarComponent))
	{
		UE_LOG(LogTemp, Log, TEXT("[Hotbar] Slot %d/%d, Tool: %s"),
			EquipHotbarComponent->GetCurrentSlotIndex(),
			EquipHotbarComponent->GetSlotCount(),
			IsValid(CurrentTool) ? *CurrentTool->ToolName.ToString() : TEXT("Empty"));
	}
}

void AMD_Character::HandleScrapeLoadChanged(const float NewScrapeLoad)
{
	RefreshScrapeLoadVisual();
}

void AMD_Character::RefreshToolMesh()
{
	if (!IsValid(EquipHotbarComponent))
	{
		return;
	}

	UMD_ToolDefinition* CurrentTool = EquipHotbarComponent->GetCurrentTool();

	// 도구별 소켓이 지정돼 있으면 그 소켓, 없으면 기본 소켓
	const FName SocketName =
		(IsValid(CurrentTool) && !CurrentTool->AttachSocketName.IsNone())
			? CurrentTool->AttachSocketName
			: ToolAttachSocketName;
	const auto ResolveAttachSocket = [this, SocketName](
		const USkeletalMeshComponent* SkeletalMesh) -> FName
	{
		if (!IsValid(SkeletalMesh))
		{
			return NAME_None;
		}

		if (!SocketName.IsNone() && SkeletalMesh->DoesSocketExist(SocketName))
		{
			return SocketName;
		}

		return !ToolAttachSocketName.IsNone()
			&& SkeletalMesh->DoesSocketExist(ToolAttachSocketName)
				? ToolAttachSocketName
				: NAME_None;
	};
	const FName ThirdPersonSocketName = ResolveAttachSocket(GetMesh());
	const FName FirstPersonSocketName = ResolveAttachSocket(FirstPersonMesh);

	// 소켓의 위치·회전·스케일을 그대로 따르도록 스냅 부착
	ThirdPersonToolMesh->AttachToComponent(
		GetMesh(), FAttachmentTransformRules::SnapToTargetIncludingScale, ThirdPersonSocketName);
	FirstPersonToolMesh->AttachToComponent(
		FirstPersonMesh,
		FAttachmentTransformRules::SnapToTargetIncludingScale,
		FirstPersonSocketName);

	const FTransform AttachOffset = IsValid(CurrentTool)
		? CurrentTool->AttachOffset
		: FTransform::Identity;
	ScrapeAnimationToolRelativeRotation = AttachOffset.Rotator();
	ScrapeAnimationBladeReferenceDirection = FVector::ZeroVector;
	ScrapeAnimationToolTwistAxis = FVector::ZeroVector;
	ScrapeAnimationBladeReferenceMesh.Reset();
	LastScrapeAnimationBladeWorldDirection = FVector::ZeroVector;
	bHasScrapeAnimationBladeDirection = false;

	// 상대 트랜스폼 자체는 RefreshScrapeLoadVisual 에서 준다. 오염 적재 단계에 따라
	// 메시가 바뀌는데, 그립 소켓은 메시마다 다를 수 있어 최종 메시가 정해진 뒤에
	// 계산해야 한다.
	RefreshScrapeLoadVisual();
}

void AMD_Character::RefreshScrapeLoadVisual()
{
	const UMD_ToolDefinition* CurrentTool = IsValid(EquipHotbarComponent)
		? EquipHotbarComponent->GetCurrentTool()
		: nullptr;
	UStaticMesh* NewToolMesh = IsValid(CurrentTool) ? CurrentTool->ToolMesh : nullptr;
	if (IsValid(CurrentTool) && IsValid(ScrapeToolComponent))
	{
		switch (ScrapeToolComponent->GetScrapeLoadStage())
		{
		case EMD_ScrapeLoadStage::Stage1:
			if (IsValid(CurrentTool->ScrapeLoadStage1Mesh))
			{
				NewToolMesh = CurrentTool->ScrapeLoadStage1Mesh;
			}
			break;

		case EMD_ScrapeLoadStage::Stage2:
			if (IsValid(CurrentTool->ScrapeLoadStage2Mesh))
			{
				NewToolMesh = CurrentTool->ScrapeLoadStage2Mesh;
			}
			break;

		case EMD_ScrapeLoadStage::Stage3:
			if (IsValid(CurrentTool->ScrapeLoadStage3Mesh))
			{
				NewToolMesh = CurrentTool->ScrapeLoadStage3Mesh;
			}
			break;

		case EMD_ScrapeLoadStage::Empty:
		default:
			break;
		}
	}

	const bool bShowTool = IsValid(NewToolMesh);
	const bool bHasFirstPersonSocket = IsValid(FirstPersonMesh)
		&& FirstPersonMesh->DoesSocketExist(FirstPersonToolMesh->GetAttachSocketName());
	const bool bHasThirdPersonSocket = IsValid(GetMesh())
		&& GetMesh()->DoesSocketExist(ThirdPersonToolMesh->GetAttachSocketName());
	FirstPersonToolMesh->SetStaticMesh(NewToolMesh);
	FirstPersonToolMesh->SetVisibility(bShowTool && bHasFirstPersonSocket, true);
	ThirdPersonToolMesh->SetStaticMesh(NewToolMesh);
	ThirdPersonToolMesh->SetVisibility(bShowTool && bHasThirdPersonSocket, true);

	const FTransform ToolTransform = GetToolAttachTransform(NewToolMesh, CurrentTool);
	FirstPersonToolMesh->SetRelativeTransform(ToolTransform);
	ThirdPersonToolMesh->SetRelativeTransform(ToolTransform);
}

FTransform AMD_Character::GetToolAttachTransform(
	const UStaticMesh* ToolMesh,
	const UMD_ToolDefinition* Tool) const
{
	const FTransform Offset = IsValid(Tool) ? Tool->AttachOffset : FTransform::Identity;

	if (!IsValid(ToolMesh) || ToolGripSocketName.IsNone())
	{
		return Offset;
	}

	const UStaticMeshSocket* Grip = ToolMesh->FindSocket(ToolGripSocketName);
	if (Grip == nullptr)
	{
		// 그립 소켓이 없는 도구는 예전처럼 원점이 손에 온다.
		return Offset;
	}

	// 메시 로컬의 그립 지점이 손 소켓 원점에 오도록 역변환한다. 그래야 메시
	// 에디터에서 소켓을 끌어 옮기는 것만으로 잡는 위치가 바뀐다. AttachOffset 은
	// 그 위에 얹는 미세 조정으로 남는다.
	//
	// 소켓 스케일은 일부러 무시한다. 역변환에 넣으면 소켓을 키웠을 때 도구가
	// 반대로 작아져서 조작이 직관과 어긋난다. 크기는 AttachOffset 으로 준다.
	const FTransform GripLocal(Grip->RelativeRotation, Grip->RelativeLocation);
	return GripLocal.Inverse() * Offset;
}

void AMD_Character::HandleHealthStateChanged(EMD_HealthState NewState)
{
	const bool bIsDead = (NewState == EMD_HealthState::Dead);
	if (bIsDead)
	{
		CancelActiveScrapeAbilityAndEndFocus();
	}

	if (IsValid(InteractionComponent))
	{
		InteractionComponent->SetInteractionBlocked(bIsDead);
	}

	// 이동 차단은 서버·클라 양쪽에서 걸어 입력 반응을 즉시 없앤다.
	ApplyDeathMovementLock(bIsDead);

	// 모선 이동은 사망→부활 전이에만. 건강→부상 전이에서 옮기면 안 된다.
	// 위치 이동은 서버 권한이다. 클라이언트가 옮기면 곧 서버 위치로 되돌아간다.
	if (!bIsDead && bWasDeadState && HasAuthority())
	{
		TeleportToMothership();
	}

	bWasDeadState = bIsDead;
}

void AMD_Character::HandleTakeAnyDamage(AActor* DamagedActor, float Damage, const UDamageType* DamageType,
	AController* InstigatedBy, AActor* DamageCauser)
{
	// 서버 권한에서만 판정. 회복(음수 데미지)·0 데미지는 무시
	if (!HasAuthority() || Damage <= 0.f)
	{
		return;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!IsValid(ASC))
	{
		return;
	}

	// 오염수 DoT·붕괴 등 모든 피해를 Health 하나로 귀결시킨다 (기획 02 §6).
	// 클램프(0~Max)는 AttributeSet, 부상/사망 전환은 PlayerState가 담당한다.
	ASC->ApplyModToAttribute(UMD_CharacterAttributeSet::GetHealthAttribute(),
		EGameplayModOp::Additive, -Damage);
}

void AMD_Character::ApplyDeathMovementLock(bool bLocked)
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!IsValid(Movement))
	{
		return;
	}

	if (bLocked)
	{
		Movement->DisableMovement();
		Movement->StopMovementImmediately();
		return;
	}

	// 수중·지상이 섞인 맵이라 특정 모드를 지정하지 않고 기본 판정에 맡긴다.
	Movement->SetDefaultMovementMode();
}

void AMD_Character::TeleportToMothership()
{
	TArray<AActor*> FoundStarts;
	UGameplayStatics::GetAllActorsOfClassWithTag(GetWorld(), APlayerStart::StaticClass(), MothershipRespawnTag, FoundStarts);

	if (FoundStarts.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Health] 리스폰 지점(%s)을 찾을 수 없다"),*MothershipRespawnTag.ToString());
		return;
	}

	const AActor* RespawnPoint = FoundStarts[0];
	if (IsValid(InteractionComponent))
	{
		InteractionComponent->CancelInteraction();
	}

	// 붕괴로 막힌 방에 되살아나지 않도록 죽은 자리가 아니라 모선으로 옮긴다. (기획 02 §6)
	SetActorLocationAndRotation(
		RespawnPoint->GetActorLocation(),
		RespawnPoint->GetActorRotation(),
		/*bSweep=*/false,
		nullptr,
		ETeleportType::TeleportPhysics);
}
