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
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

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
	// 팔로워 메시의 소켓에 붙이면 부착 시스템이 신뢰할 수 없는 소켓 트랜스폼으로
	// 도구를 배치하고, 그 위에 UpdateFirstPersonToolTransform 이 다시 배치하면서
	// 두 힘이 프레임마다 경쟁한다. 값을 직접 넣는 피벗에 붙여 부모를 확정한다.
	FirstPersonToolMesh->SetupAttachment(FirstPersonArmsPivot);
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
	// 3인칭은 포인터 접점을 향해 본을 꺾지 않고 장착 애니메이션만 재생한다.
	AimDelta = FRotator::ZeroRotator;
	Active = false;
}

void AMD_Character::UpdateScrapeAnimationBlade(
	const bool Active,
	const double DeltaTime,
	FRotator& Swing)
{
	// 3인칭 도구는 손 소켓의 기본 부착 자세만 사용한다. 긁는 방향은 몽타주가 표현한다.
	(void)Active;
	(void)DeltaTime;
	Swing = FRotator::ZeroRotator;
	const UMD_ToolDefinition* CurrentTool = IsValid(EquipHotbarComponent)
		? EquipHotbarComponent->GetCurrentTool()
		: nullptr;
	const FTransform BaseAttachOffset = IsValid(CurrentTool)
		? CurrentTool->AttachOffset
		: FTransform::Identity;
	ScrapeAnimationToolRelativeRotation = BaseAttachOffset.Rotator();
	if (IsValid(ThirdPersonToolMesh))
	{
		ThirdPersonToolMesh->SetRelativeTransform(GetToolAttachTransformFromOffset(
			ThirdPersonToolMesh->GetStaticMesh(),
			BaseAttachOffset));
	}
}

void AMD_Character::GetScrapeAnimationHandIK(
	const double DeltaTime,
	FTransform& EffectorTransformComponentSpace,
	FVector& JointTargetLocationComponentSpace,
	float& Alpha,
	bool& Active,
	bool& Reachable,
	bool& TeleportOrSnap,
	int64& PoseGeneration)
{
	// 1인칭 헤라는 별도의 viewmodel transform이 직접 소유한다. 기존 ABP가
	// 함수를 계속 호출해도 3인칭 팔에 접촉 IK가 중복 적용되지 않도록 항상
	// 명시적인 비활성 값만 돌려준다.
	(void)DeltaTime;
	EffectorTransformComponentSpace = FTransform::Identity;
	JointTargetLocationComponentSpace = FVector::ZeroVector;
	Alpha = 0.0f;
	Active = false;
	Reachable = false;
	TeleportOrSnap = false;
	PoseGeneration = 0;
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
	ResetScrapeFocusInputState();

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
	UpdateFirstPersonToolTransform(DeltaSeconds);
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

void AMD_Character::SetFirstPersonScrapeViewModelActive(const bool bActive)
{
	if (bFirstPersonScrapeViewModelActive == bActive)
	{
		return;
	}

	bFirstPersonScrapeViewModelActive = bActive;
	if (bActive)
	{
		bFirstPersonArmsWereVisible = IsValid(FirstPersonMesh)
			&& FirstPersonMesh->IsVisible();
		if (IsValid(FirstPersonMesh))
		{
			// 독립 viewmodel 도구와 기존 팔/손 소켓이 서로 다른 위치에 보이지 않게
			// Focus 동안 1인칭 팔만 감춘다. 3인칭 본체/도구에는 영향이 없다.
			FirstPersonMesh->SetVisibility(false, true);
		}
		if (IsValid(FirstPersonToolMesh))
		{
			ScrapeViewModelWorldScale =
				FirstPersonToolMesh->GetComponentTransform().GetScale3D();
			if (ScrapeViewModelWorldScale.ContainsNaN()
				|| ScrapeViewModelWorldScale.IsNearlyZero())
			{
				ScrapeViewModelWorldScale = FVector::OneVector;
			}
		}
		LastScrapeViewModelPointerNormalized = IsValid(ScrapeToolComponent)
			? ScrapeToolComponent->GetScrapeFocusPointerNormalized()
			: FVector2D(0.5, 0.5);
		ScrapeViewModelScreenDirection = FVector2D(0.0, -1.0);
		LastScrapeViewModelStableWorldTangent = FVector::ZeroVector;
		ScrapeViewModelWorldRotation = FQuat::Identity;
		bScrapeViewModelDirectionLocked = false;
		bWasScrapingForViewModel = false;
		bHasScrapeViewModelRotation = false;
		return;
	}

	if (IsValid(FirstPersonMesh))
	{
		FirstPersonMesh->SetVisibility(bFirstPersonArmsWereVisible, true);
	}
	LastScrapeViewModelPointerNormalized = FVector2D(0.5, 0.5);
	ScrapeViewModelScreenDirection = FVector2D(0.0, -1.0);
	LastScrapeViewModelStableWorldTangent = FVector::ZeroVector;
	ScrapeViewModelWorldRotation = FQuat::Identity;
	ScrapeViewModelWorldScale = FVector::OneVector;
	bScrapeViewModelDirectionLocked = false;
	bWasScrapingForViewModel = false;
	bHasScrapeViewModelRotation = false;
}

bool AMD_Character::UpdateFirstPersonScrapeViewModel(const float DeltaSeconds)
{
	if (!bFirstPersonScrapeViewModelActive
		|| !IsValid(FirstPersonToolMesh)
		|| !IsValid(ScrapeToolComponent))
	{
		return false;
	}

	UStaticMesh* const ToolMesh = FirstPersonToolMesh->GetStaticMesh();
	if (!IsValid(ToolMesh))
	{
		return false;
	}
	const UStaticMeshSocket* BladeContactSocket =
		ToolMesh->FindSocket(ScrapeBladeTipSocketName);
	if (BladeContactSocket == nullptr
		&& !ScrapeBladeContactFallbackSocketName.IsNone())
	{
		BladeContactSocket = ToolMesh->FindSocket(
			ScrapeBladeContactFallbackSocketName);
	}
	if (BladeContactSocket == nullptr)
	{
		return false;
	}

	APlayerController* const PlayerController = Cast<APlayerController>(Controller);
	if (!IsValid(PlayerController) || !PlayerController->IsLocalController())
	{
		return false;
	}
	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	PlayerController->GetViewportSize(ViewportWidth, ViewportHeight);
	const FVector2D PointerNormalized =
		ScrapeToolComponent->GetScrapeFocusPointerNormalized();
	if (ViewportWidth <= 0
		|| ViewportHeight <= 0
		|| PointerNormalized.ContainsNaN())
	{
		return false;
	}

	const FVector2D PointerDeltaPixels(
		(PointerNormalized.X - LastScrapeViewModelPointerNormalized.X)
			* static_cast<double>(ViewportWidth),
		(PointerNormalized.Y - LastScrapeViewModelPointerNormalized.Y)
			* static_cast<double>(ViewportHeight));
	const bool bScraping = ScrapeToolComponent->IsScraping();
	if (!bScraping)
	{
		// Focus Ready에서는 포인터 위치만 따라가고 헤라 회전은 기본
		// contact frame을 유지한다. 직전 stroke의 부호도 기본 축에 영향을 주지 않는다.
		ScrapeViewModelScreenDirection = FVector2D(0.0, -1.0);
		bScrapeViewModelDirectionLocked = false;
		if (bWasScrapingForViewModel)
		{
			LastScrapeViewModelStableWorldTangent = FVector::ZeroVector;
		}
	}
	else
	{
		if (!bWasScrapingForViewModel)
		{
			// 새 LMB stroke의 방향 lock은 ScrapeToolComponent가 다시 결정한다.
			bScrapeViewModelDirectionLocked = false;
		}
		if (!bScrapeViewModelDirectionLocked
			&& PointerDeltaPixels.SizeSquared() >= 0.25)
		{
			ScrapeViewModelScreenDirection = PointerDeltaPixels.GetSafeNormal();
		}
	}
	bWasScrapingForViewModel = bScraping;
	LastScrapeViewModelPointerNormalized = PointerNormalized;

	const float ScreenX = static_cast<float>(
		PointerNormalized.X * static_cast<double>(ViewportWidth));
	const float ScreenY = static_cast<float>(
		PointerNormalized.Y * static_cast<double>(ViewportHeight));
	FVector RayOrigin = FVector::ZeroVector;
	FVector RayDirection = FVector::ZeroVector;
	if (!PlayerController->DeprojectScreenPositionToWorld(
			ScreenX,
			ScreenY,
			RayOrigin,
			RayDirection)
		|| RayOrigin.ContainsNaN()
		|| RayDirection.ContainsNaN()
		|| !RayDirection.Normalize())
	{
		return false;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	if (ViewLocation.ContainsNaN() || ViewRotation.ContainsNaN())
	{
		return false;
	}
	const FRotationMatrix ViewAxes(ViewRotation);
	const FVector ViewForward = ViewAxes.GetScaledAxis(EAxis::X).GetSafeNormal();
	const FVector ViewRight = ViewAxes.GetScaledAxis(EAxis::Y).GetSafeNormal();
	const FVector ViewUp = ViewAxes.GetScaledAxis(EAxis::Z).GetSafeNormal();
	const double RayPlaneDenominator = FVector::DotProduct(
		RayDirection,
		ViewForward);
	if (ViewForward.IsNearlyZero()
		|| ViewRight.IsNearlyZero()
		|| ViewUp.IsNearlyZero()
		|| RayPlaneDenominator <= UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}

	const FVector ViewPlanePoint = ViewLocation
		+ ViewForward * static_cast<double>(
			FMath::Max(ScrapeViewModelDistanceCm, 10.0f));
	const double RayDistance = FVector::DotProduct(
		ViewPlanePoint - RayOrigin,
		ViewForward) / RayPlaneDenominator;
	if (!FMath::IsFinite(RayDistance) || RayDistance <= 0.0)
	{
		return false;
	}
	const FVector ContactWorldLocation = RayOrigin + RayDirection * RayDistance;

	FVector ContactWorldTangent = bScraping
		? FVector::ZeroVector
		: ViewUp;
	FMD_ScrapeVisualContactPose VisualContactPose;
	if (bScraping
		&& ScrapeToolComponent->GetScrapeVisualContactPose(VisualContactPose)
		&& VisualContactPose.bDirectionLocked
		&& VisualContactPose.bTangentValid)
	{
		// Component가 고정한 단일 gesture 축을 소비한다. 해당 pose 경로가
		// 이미 0.01 normalized threshold와 180도 부호 안정화를 담당한다.
		ContactWorldTangent = FVector::VectorPlaneProject(
			VisualContactPose.StrokeWorldTangent,
			-ViewForward).GetSafeNormal();
		bScrapeViewModelDirectionLocked = !ContactWorldTangent.IsNearlyZero();
	}
	if (bScraping
		&& ContactWorldTangent.IsNearlyZero()
		&& bScrapeViewModelDirectionLocked)
	{
		// 잠깐 visual trace가 비어도 현재 stroke에서 이미 고정한 축은 유지한다.
		ContactWorldTangent = LastScrapeViewModelStableWorldTangent;
	}
	if (bScraping && ContactWorldTangent.IsNearlyZero())
	{
		ContactWorldTangent = ViewRight * ScrapeViewModelScreenDirection.X
			- ViewUp * ScrapeViewModelScreenDirection.Y;
		ContactWorldTangent = FVector::VectorPlaneProject(
			ContactWorldTangent,
			-ViewForward).GetSafeNormal();
	}
	if (ContactWorldTangent.IsNearlyZero())
	{
		ContactWorldTangent = LastScrapeViewModelStableWorldTangent;
	}
	if (ContactWorldTangent.IsNearlyZero())
	{
		ContactWorldTangent = ViewUp;
	}
	if (bScraping
		&& !LastScrapeViewModelStableWorldTangent.IsNearlyZero()
		&& !LastScrapeViewModelStableWorldTangent.ContainsNaN()
		&& !ContactWorldTangent.ContainsNaN()
		&& FVector::DotProduct(
			LastScrapeViewModelStableWorldTangent,
			ContactWorldTangent) < 0.0)
	{
		// 헤라 날의 진행 축은 180도 동치이므로, locked pose를 포함한
		// 새 tangent를 직전 안정 축과 같은 부호로 맞춰 한 프레임 flip을 막는다.
		ContactWorldTangent *= -1.0;
	}
	LastScrapeViewModelStableWorldTangent = ContactWorldTangent;

	const FQuat TargetContactRotation = FRotationMatrix::MakeFromXZ(
		-ViewForward,
		ContactWorldTangent).ToQuat();
	FQuat ContactRotation = TargetContactRotation;
	const double SafeDeltaSeconds = FMath::IsFinite(DeltaSeconds)
		? FMath::Max(static_cast<double>(DeltaSeconds), 0.0)
		: 0.0;
	const double SmoothingSeconds = FMath::Max(
		static_cast<double>(ScrapeViewModelRotationSmoothingSeconds),
		0.0);
	if (bHasScrapeViewModelRotation
		&& SmoothingSeconds > UE_DOUBLE_SMALL_NUMBER)
	{
		const double RotationBlend = 1.0 - FMath::Exp(
			-SafeDeltaSeconds / SmoothingSeconds);
		ContactRotation = FQuat::Slerp(
			ScrapeViewModelWorldRotation,
			TargetContactRotation,
			FMath::Clamp(RotationBlend, 0.0, 1.0)).GetNormalized();
	}
	ScrapeViewModelWorldRotation = ContactRotation;
	bHasScrapeViewModelRotation = true;

	// StaticMesh socket scale은 배치 계약에 쓰지 않는다. 현재 viewmodel world scale을
	// 보존하면서 Blade_Tip/Blade_Contact의 역변환으로 접점을 정확히 맞춘다.
	const FTransform BladeContactMeshLocal(
		BladeContactSocket->RelativeRotation,
		BladeContactSocket->RelativeLocation);
	const FTransform DesiredContactWorld(
		ContactRotation,
		ContactWorldLocation,
		ScrapeViewModelWorldScale);
	const FRotator SafeRotationOffset = ScrapeViewModelRotationOffset.ContainsNaN()
		? FRotator::ZeroRotator
		: ScrapeViewModelRotationOffset;
	const bool bScaleMultiplierUsable =
		FMath::IsFinite(ScrapeViewModelScaleMultiplier)
		&& ScrapeViewModelScaleMultiplier > UE_KINDA_SMALL_NUMBER;
	const float SafeScaleMultiplier = bScaleMultiplierUsable
		? ScrapeViewModelScaleMultiplier
		: 1.0f;
	const FTransform LocalVisualOffset(
		SafeRotationOffset.Quaternion(),
		FVector::ZeroVector,
		FVector(SafeScaleMultiplier));
	// VisualOffset은 contact frame 원점에서 적용되므로 회전·스케일을
	// 바꿔도 Blade_Tip/Blade_Contact의 world location은 포인터 접점에 남는다.
	FirstPersonToolMesh->SetWorldTransform(
		BladeContactMeshLocal.Inverse()
			* LocalVisualOffset
			* DesiredContactWorld);
	return true;
}

void AMD_Character::UpdateFirstPersonToolTransform(const float DeltaSeconds)
{
	const USkeletalMeshComponent* BodyMesh = GetMesh();
	if (!IsValid(FirstPersonToolMesh) || !IsValid(FirstPersonMesh) || !IsValid(BodyMesh)
		|| !IsLocallyControlled())
	{
		return;
	}

	const bool bWantsScrapeViewModel = IsScrapeToolEquipped()
		&& IsValid(ScrapeToolComponent)
		&& ScrapeToolComponent->IsScrapeFocusViewActive();
	bool bScrapeViewModelStructurallyReady = false;
	if (bWantsScrapeViewModel)
	{
		const UStaticMesh* const ToolMesh = FirstPersonToolMesh->GetStaticMesh();
		const UStaticMeshSocket* BladeContactSocket = IsValid(ToolMesh)
			? ToolMesh->FindSocket(ScrapeBladeTipSocketName)
			: nullptr;
		if (BladeContactSocket == nullptr
			&& IsValid(ToolMesh)
			&& !ScrapeBladeContactFallbackSocketName.IsNone())
		{
			BladeContactSocket = ToolMesh->FindSocket(
				ScrapeBladeContactFallbackSocketName);
		}

		const FVector CurrentWorldScale =
			FirstPersonToolMesh->GetComponentTransform().GetScale3D();
		const bool bWorldScaleUsable = !CurrentWorldScale.ContainsNaN()
			&& FMath::Abs(CurrentWorldScale.X) > UE_KINDA_SMALL_NUMBER
			&& FMath::Abs(CurrentWorldScale.Y) > UE_KINDA_SMALL_NUMBER
			&& FMath::Abs(CurrentWorldScale.Z) > UE_KINDA_SMALL_NUMBER;
		const APlayerController* const PlayerController =
			Cast<APlayerController>(Controller);
		const FVector2D PointerNormalized =
			ScrapeToolComponent->GetScrapeFocusPointerNormalized();
		bScrapeViewModelStructurallyReady = BladeContactSocket != nullptr
			&& bWorldScaleUsable
			&& FirstPersonToolMesh->IsVisible()
			&& IsValid(PlayerController)
			&& PlayerController->IsLocalController()
			&& !PointerNormalized.ContainsNaN();
	}

	SetFirstPersonScrapeViewModelActive(
		bWantsScrapeViewModel && bScrapeViewModelStructurallyReady);
	if (bWantsScrapeViewModel && bScrapeViewModelStructurallyReady)
	{
		if (UpdateFirstPersonScrapeViewModel(DeltaSeconds))
		{
			return;
		}
		if (bHasScrapeViewModelRotation)
		{
			// Viewport/deproject가 일시적으로 안 나오면 이전에 성공한
			// viewmodel pose만 유지하고 기본 소켓으로 튀지 않는다.
			return;
		}

		// 첫 pose를 한 번도 만들지 못했으면 팔을 감춘 채 손 위치의
		// 도구만 남기지 않고 즉시 기본 부착으로 fail-closed 한다.
		SetFirstPersonScrapeViewModelActive(false);
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
	// 날 축 roll 은 GetToolAttachTransform 안에 이미 들어 있다. 3인칭 메시의
	// 상대 회전을 읽어 오면 안 된다 — 그 컴포넌트는 블레이드 갱신과
	// RefreshScrapeLoadVisual 두 곳이 쓰는 경쟁 상태이고, 되돌려진 프레임을
	// 집으면 AttachOffset 으로 튀어 1인칭에서만 떨림으로 보인다.
	const FTransform Offset =
		GetToolAttachTransform(FirstPersonToolMesh->GetStaticMesh(), CurrentTool);

	// 부모가 피벗(값이 확정적인 SceneComponent)이므로 월드로 지정해도 역산 기준이
	// 흔들리지 않는다. 컴포넌트 계층이 어떻게 구성돼 있든 결과가 같도록
	// 1인칭 팔의 월드 트랜스폼에서 곧바로 합성한다.
	//   도구 →AttachOffset→ 본체 컴포넌트 공간 소켓 → 1인칭 팔 월드
	FirstPersonToolMesh->SetWorldTransform(
		Offset * SocketLocal * FirstPersonMesh->GetComponentTransform());

}

void AMD_Character::UpdateFirstPersonArms(float DeltaSeconds, bool bSnapToTarget)
{
	if (!IsValid(FirstPersonArmsPivot) || !IsValid(Camera) || !IsLocallyControlled())
	{
		return;
	}

	// 헤라 포커스 중에도 컨트롤 회전이 함께 잠긴다(측정으로 확인: 포커스 구간에서
	// 컨트롤 회전과 카메라 최종 회전이 동일하고 둘 다 프레임 간 변화 0).
	// 그래서 별도의 뷰 회전 조회 없이 컨트롤 회전만 따라가면 된다.
	const FRotator TargetRotation = GetControlRotation() + FirstPersonArmsRotationOffset;
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
	ResetScrapeFocusInputState();

	if (IsValid(InteractionComponent))
	{
		InteractionComponent->CancelInteraction();
	}

	Super::UnPossessed();

	RefreshFirstPersonArmsTick();
}

void AMD_Character::PawnClientRestart()
{
	// 입력 컴포넌트가 재생성되기 전에 기존 포커스와 입력 소유권을 정리한다.
	ResetScrapeFocusInputState();

	Super::PawnClientRestart();

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = GetEnhancedInputSubsystem())
	{
		if (IsValid(PlayerMappingContext))
		{
			Subsystem->AddMappingContext(PlayerMappingContext, 0);
		}
	}

	RefreshScrapeFocusInputState();
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

	if (!IsValid(InputConfig))
	{
		return;
	}

	if (IsValid(InputConfig->ScrapeFocusAction))
	{
		EIC->BindAction(
			InputConfig->ScrapeFocusAction,
			ETriggerEvent::Started,
			this,
			&AMD_Character::HandleScrapeFocusToggleInput);
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

	if (IsValid(ScrapeToolComponent) && ScrapeToolComponent->IsScrapeFocusViewActive())
	{
		return;
	}

	const FVector2D MoveInput = Value.Get<FVector2D>();

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

void AMD_Character::HandleScrapeFocusToggleInput()
{
	if (!IsValid(ScrapeToolComponent))
	{
		return;
	}

	if (ScrapeToolComponent->IsScrapeFocusViewActive())
	{
		CancelActiveScrapeAbilityAndEndFocus();
		return;
	}

	if (!IsScrapeToolEquipped())
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
	SetFirstPersonScrapeViewModelActive(false);
	UpdateFirstPersonToolTransform(0.0f);
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

bool AMD_Character::CanUnloadScrapeLoad() const
{
	return IsValid(ScrapeToolComponent)
		&& IsScrapeToolEquipped()
		&& !FMath::IsNearlyZero(ScrapeToolComponent->GetScrapeLoad());
}

bool AMD_Character::CanUnloadScrapeLoadForRequest(const float RequestedAmount) const
{
	if (!CanUnloadScrapeLoad())
	{
		return false;
	}

	return UMD_ScrapeToolComponent::CalculateActualScrapeLoadRemoval(
		ScrapeToolComponent->GetScrapeLoad(),
		RequestedAmount) > 0.0f;
}

float AMD_Character::TryUnloadScrapeLoad(const float RequestedAmount)
{
	if (!HasAuthority() || !CanUnloadScrapeLoadForRequest(RequestedAmount))
	{
		return 0.0f;
	}

	return ScrapeToolComponent->RemoveScrapeLoad(RequestedAmount);
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

void AMD_Character::RefreshScrapeFocusInputState()
{
	UMD_ToolDefinition* const CurrentTool = IsValid(EquipHotbarComponent)
		? EquipHotbarComponent->GetCurrentTool()
		: nullptr;
	if (ScrapeFocusInputTool.Get() != CurrentTool)
	{
		// 같은 도구의 수량 변경은 유지하고, 실제 장비 identity 변경만 정리한다.
		CancelActiveScrapeAbilityAndEndFocus();
	}
	ScrapeFocusInputTool = CurrentTool;
}

void AMD_Character::ResetScrapeFocusInputState()
{
	CancelActiveScrapeAbilityAndEndFocus();
	ScrapeFocusInputTool.Reset();
}

void AMD_Character::HandleHotbarChanged()
{
	UMD_ToolDefinition* CurrentTool = IsValid(EquipHotbarComponent)
		? EquipHotbarComponent->GetCurrentTool()
		: nullptr;
	RefreshScrapeFocusInputState();
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

	// 메시 교체 전에 이전 헤라 소켓/scale을 잡고 있던 독립 viewmodel 상태를 버린다.
	SetFirstPersonScrapeViewModelActive(false);
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

	// 소켓의 위치·회전·스케일을 그대로 따르도록 스냅 부착
	ThirdPersonToolMesh->AttachToComponent(
		GetMesh(), FAttachmentTransformRules::SnapToTargetIncludingScale, ThirdPersonSocketName);
	// 1인칭 도구의 배치는 UpdateFirstPersonToolTransform 이 매 프레임 계산한다.
	// 부모는 값이 확정적인 피벗이어야 한다.
	FirstPersonToolMesh->AttachToComponent(
		FirstPersonArmsPivot,
		FAttachmentTransformRules::KeepRelativeTransform);

	const FTransform AttachOffset = IsValid(CurrentTool)
		? CurrentTool->AttachOffset
		: FTransform::Identity;
	ScrapeAnimationToolRelativeRotation = AttachOffset.Rotator();

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
	// 1인칭 도구도 본체 소켓에서 배치를 유도하므로 그쪽 유무로 판정한다.
	const bool bHasFirstPersonSocket = IsValid(FirstPersonMesh)
		&& IsValid(GetMesh())
		&& GetMesh()->DoesSocketExist(ToolAttachSocketName);
	const bool bHasThirdPersonSocket = IsValid(GetMesh())
		&& GetMesh()->DoesSocketExist(ThirdPersonToolMesh->GetAttachSocketName());
	FirstPersonToolMesh->SetStaticMesh(NewToolMesh);
	FirstPersonToolMesh->SetVisibility(bShowTool && bHasFirstPersonSocket, true);
	ThirdPersonToolMesh->SetStaticMesh(NewToolMesh);
	ThirdPersonToolMesh->SetVisibility(bShowTool && bHasThirdPersonSocket, true);

	const FTransform ToolTransform = GetToolAttachTransform(NewToolMesh, CurrentTool);
	ThirdPersonToolMesh->SetRelativeTransform(ToolTransform);
	if (bFirstPersonScrapeViewModelActive)
	{
		// 오염 적재 메시가 바뀌어도 같은 프레임에 새 소켓을
		// 포인터에 다시 맞춘다. 구조적 요건이 깨지면 기본 부착으로 복귀한다.
		UpdateFirstPersonToolTransform(0.0f);
	}
	else
	{
		FirstPersonToolMesh->SetRelativeTransform(ToolTransform);
	}
}

FTransform AMD_Character::GetToolAttachTransform(
	const UStaticMesh* ToolMesh,
	const UMD_ToolDefinition* Tool) const
{
	FTransform Offset = IsValid(Tool) ? Tool->AttachOffset : FTransform::Identity;
	// 긁기 중 날 축 twist 는 AttachOffset 의 회전을 대체한다. 이 함수가 도구
	// 부착 자세의 유일한 출처여야 한다 — 3인칭 메시의 상대 트랜스폼을 읽어
	// 재사용하면 RefreshScrapeLoadVisual 이 같은 값을 되돌리는 것과 경쟁해
	// 프레임마다 AttachOffset 으로 튄다.
	Offset.SetRotation(ScrapeAnimationToolRelativeRotation.Quaternion());
	return GetToolAttachTransformFromOffset(ToolMesh, Offset);
}

FTransform AMD_Character::GetToolAttachTransformFromOffset(
	const UStaticMesh* ToolMesh,
	const FTransform& AttachOffset) const
{
	const FTransform Offset = AttachOffset;

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

void AMD_Character::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMD_Character, bCutterVFXActive);
}

void AMD_Character::SetCutterVFXActive(bool bActive)
{
	if (bCutterVFXActive == bActive)
	{
		return;
	}

	bCutterVFXActive = bActive;
	RefreshCutterVFX();
}

void AMD_Character::OnRep_CutterVFXActive()
{
	RefreshCutterVFX();
}

void AMD_Character::RefreshCutterVFX()
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	if (!bCutterVFXActive)
	{
		// 즉시 파괴하지 않고 비활성만 해 잔여 파티클이 자연 소멸하게 둔다
		if (IsValid(CutterVFXComponent))
		{
			CutterVFXComponent->Deactivate();
		}
		return;
	}

	if (!CutterCutEffect)
	{
		return;
	}

	UStaticMeshComponent* AttachMesh = IsLocallyControlled() ? FirstPersonToolMesh : ThirdPersonToolMesh;
	if (!IsValid(AttachMesh))
	{
		return;
	}

	// 빙의가 바뀌어 부착 대상 시점 메시가 달라졌으면 새로 붙인다
	if (IsValid(CutterVFXComponent) && CutterVFXComponent->GetAttachParent() != AttachMesh)
	{
		CutterVFXComponent->DestroyComponent();
		CutterVFXComponent = nullptr;
	}

	if (IsValid(CutterVFXComponent))
	{
		CutterVFXComponent->Activate(true);
		return;
	}

	CutterVFXComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
		CutterCutEffect, AttachMesh, CutterVFXSocketName,
		FVector::ZeroVector, FRotator::ZeroRotator,
		EAttachLocation::SnapToTarget, /*bAutoDestroy=*/false);
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
