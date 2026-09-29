// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/MD_Character.h"
#include "Player/MD_PlayerState.h"
#include "Data/Input/MD_InputConfig.h"
#include "Component/Character/MD_CharacterMovementComponent.h"
#include "Component/Scrape/MD_ScrapeToolComponent.h"
#include "Component/Scrape/MD_StainScraperComponent.h"
#include "Component/Scanning/MD_ScannerComponent.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Audio/MD_AudioRelayComponent.h"
#include "Component/Interaction/MD_InteractionComponent.h"
#include "Component/Support/MD_SupplyComponent.h"
#include "Component/Support/MD_SupportPlacementComponent.h"
#include "Data/Equipment/MD_SupplyItemDefinition.h"
#include "Component/Equipment/MD_FoamGunComponent.h"
#include "Component/Oxygen/MD_OxygenComponent.h"
#include "Component/Comfort/MD_ComfortComponent.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "Data/Equipment/MD_ConsumeItemDefinition.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/WidgetComponent.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMeshSocket.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Gameplay/Attribute/MD_CharacterAttributeSet.h"
#include "Gameplay/Settings/MD_ControlSettingCategory.h"
#include "Gameplay/Settings/MD_SettingsManager.h"
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
#include "UI/Customization/MD_NameTagWidget.h"
#include "Data/Customization/MD_CharacterRosterData.h"
#include "Gameplay/Customization/MD_CharacterAppearanceSettings.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/SkeletalMesh.h"
#include "Util/MD_LogUtility.h"

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
	StainScraperComponent = CreateDefaultSubobject<UMD_StainScraperComponent>(TEXT("StainScraperComponent"));
	ScannerComponent = CreateDefaultSubobject<UMD_ScannerComponent>(TEXT("ScannerComponent"));

	EquipHotbarComponent = CreateDefaultSubobject<UMD_EquipHotbarComponent>(TEXT("EquipHotbarComponent"));

	AudioRelayComponent = CreateDefaultSubobject<UMD_AudioRelayComponent>(TEXT("AudioRelayComponent"));

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

	// 톱날은 도구 메시의 소켓에 붙는다. 생성자 시점에는 도구 메시가 비어 있어
	// 소켓이 아직 없지만, 부착은 소켓 이름으로 남아 있다가 메시가 정해지면 풀린다.
	ThirdPersonCutterBlade = CreateDefaultSubobject<UStaticMeshComponent>(
		TEXT("ThirdPersonCutterBlade"));
	ThirdPersonCutterBlade->SetupAttachment(ThirdPersonToolMesh, CutterBladePivotSocketName);
	ThirdPersonCutterBlade->SetOwnerNoSee(true);
	ThirdPersonCutterBlade->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ThirdPersonCutterBlade->SetCanEverAffectNavigation(false);

	FirstPersonCutterBlade = CreateDefaultSubobject<UStaticMeshComponent>(
		TEXT("FirstPersonCutterBlade"));
	FirstPersonCutterBlade->SetupAttachment(FirstPersonToolMesh, CutterBladePivotSocketName);
	FirstPersonCutterBlade->SetOnlyOwnerSee(true);
	FirstPersonCutterBlade->SetOwnerNoSee(false);
	FirstPersonCutterBlade->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FirstPersonCutterBlade->SetCanEverAffectNavigation(false);

	ConfigureFirstPersonRendering();

	HoldPoint = CreateDefaultSubobject<USceneComponent>(TEXT("HoldPoint"));
	HoldPoint->SetupAttachment(Camera);
	HoldPoint->SetRelativeLocation(FVector(150.f, 0.f, 0.f));

	PhysicsHandle = CreateDefaultSubobject<UPhysicsHandleComponent>(TEXT("PhysicsHandle"));

	InteractionComponent = CreateDefaultSubobject<UMD_InteractionComponent>(TEXT("InteractionComponent"));
	InteractionComponent->SetInteractionDependencies(Camera, HoldPoint, PhysicsHandle);

	ComfortComponent = CreateDefaultSubobject<UMD_ComfortComponent>(TEXT("ComfortComponent"));

	SupplyComponent = CreateDefaultSubobject<UMD_SupplyComponent>(TEXT("SupplyComponent"));
	SupportPlacementComponent = CreateDefaultSubobject<UMD_SupportPlacementComponent>(TEXT("SupportPlacementComponent"));

	FoamGunComponent = CreateDefaultSubobject<UMD_FoamGunComponent>(TEXT("FoamGunComponent"));

	OxygenComponent = CreateDefaultSubobject<UMD_OxygenComponent>(TEXT("OxygenComponent"));

	// 캐릭터 머리 위에 원격 플레이어의 이름표를 배치한다.
	NameTagWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("NameTagWidgetComponent"));
	NameTagWidgetComponent->SetupAttachment(GetRootComponent());
	NameTagWidgetComponent->SetWidgetSpace(EWidgetSpace::World);
	NameTagWidgetComponent->SetDrawAtDesiredSize(true);
	NameTagWidgetComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 70.0f));
	NameTagWidgetComponent->SetOwnerNoSee(true);
	NameTagWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
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
	// 애니메이션 갱신 시에도 선택한 공통 아이템의 부착 설정 유지
	UMD_ItemDefinition* CurrentItem = IsValid(EquipHotbarComponent) == true ? EquipHotbarComponent->GetCurrentItem() : nullptr;
	// 소모품을 포함한 아이템별 오프셋의 삼인칭 메시 반영
	FTransform BaseAttachOffset = IsValid(CurrentItem) == true ? CurrentItem->AttachOffset : FTransform::Identity;
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

	// 시작 시점에 이미 준비된 PlayerState의 이름을 이름표에 반영한다.
	RefreshNameTag();

	// 기존 BP에 저장된 네이티브 컴포넌트 값이 C++ 기본값을 덮더라도
	// 실제 플레이에서는 팔·도구의 데칼 수신과 월드 메시 관통을 줄이도록 다시 보정한다.
	ConfigureFirstPersonRendering();
	RefreshFirstPersonArmsTick();

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
			// 조작 설정 키가 반영된 런타임 입력 컨텍스트 조회
			UInputMappingContext* MappingContext = ResolvePlayerMappingContext();
			// 준비된 입력 컨텍스트에 한정한 로컬 플레이어 등록
			if (IsValid(MappingContext) == true)
			{
				// 이동 키와 설정된 비이동 액션 키를 포함한 입력 컨텍스트 활성화
				Subsystem->AddMappingContext(MappingContext, 0);
			}
		}

	}

	if (IsValid(InteractionComponent))
	{
		// 핫바보다 먼저 묶어 두면 아래 HandleHotbarChanged 가 운반 상태를 반영한 첫 도구 표시를 만든다
		InteractionComponent->OnHeldActorChanged.AddDynamic(this, &AMD_Character::HandleHeldActorChanged);
	}

	if (IsValid(EquipHotbarComponent))
	{
		EquipHotbarComponent->OnHotbarChanged.AddDynamic(this, &AMD_Character::HandleHotbarChanged);
		HandleHotbarChanged();
	}
}

void AMD_Character::ConfigureFirstPersonRendering()
{
	if (IsValid(FirstPersonMesh))
	{
		FirstPersonMesh->SetReceivesDecals(false);
		FirstPersonMesh->SetFirstPersonPrimitiveType(
			EFirstPersonPrimitiveType::FirstPerson);
	}

	if (IsValid(FirstPersonToolMesh))
	{
		FirstPersonToolMesh->SetReceivesDecals(false);
		FirstPersonToolMesh->SetFirstPersonPrimitiveType(
			EFirstPersonPrimitiveType::FirstPerson);
	}

	if (IsValid(FirstPersonCutterBlade))
	{
		FirstPersonCutterBlade->SetReceivesDecals(false);
		FirstPersonCutterBlade->SetFirstPersonPrimitiveType(
			EFirstPersonPrimitiveType::FirstPerson);
	}

	if (IsValid(Camera))
	{
		Camera->SetEnableFirstPersonScale(true);
		Camera->SetFirstPersonScale(
			FMath::Clamp(FirstPersonRenderScale, 0.1f, 1.0f));
	}
}

void AMD_Character::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindAppearanceDelegate();
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

	// 월드 공간 이름표가 각 클라이언트의 현재 카메라를 계속 향하게 한다.
	UpdateNameTagRotation();

	// 톱날은 사본 프록시에서도 돌아야 한다. bCutterVFXActive 가 복제되므로
	// 로컬 여부를 가리지 않고 모든 클라이언트에서 갱신한다.
	UpdateCutterBladeSpin(DeltaSeconds);

	// 1인칭 팔과 도구는 해당 캐릭터를 직접 조작하는 로컬 플레이어만 갱신한다.
	if (IsLocallyControlled() == true)
	{
		UpdateFirstPersonArms(DeltaSeconds);
		UpdateFirstPersonToolTransform(DeltaSeconds);
	}
}

void AMD_Character::RefreshFirstPersonArmsTick()
{
	// 로컬 캐릭터는 기존 1인칭 팔 갱신을 위해 틱한다.
	bool bNeedsTick = IsLocallyControlled();

	// 렌더링하지 않는 전용 서버를 제외하고 원격 캐릭터도 이름표 회전을 위해 틱한다.
	if (GetNetMode() != NM_DedicatedServer)
	{
		if (IsValid(NameTagWidgetComponent) == true)
		{
			bNeedsTick = true;
		}
	}

	SetActorTickEnabled(bNeedsTick);

	if (bNeedsTick == false)
	{
		return;
	}

	// 원격 캐릭터는 이름표만 갱신하므로 1인칭 팔 초기화를 생략한다.
	if (IsLocallyControlled() == false)
	{
		return;
	}

	BindFirstPersonArmsToBodyPose();
	UpdateFirstPersonArms(0.f, /*bSnapToTarget=*/true);
}

void AMD_Character::UpdateNameTagRotation()
{
	// 렌더링하지 않는 전용 서버에서는 이름표 방향을 계산하지 않는다.
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// 이름표 컴포넌트가 준비되지 않은 경우 처리를 종료한다.
	if (IsValid(NameTagWidgetComponent) == false)
	{
		return;
	}

	// 현재 클라이언트의 주 로컬 카메라를 소유한 컨트롤러를 가져온다.
	APlayerController* LocalPlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (IsValid(LocalPlayerController) == false)
	{
		return;
	}

	// 카메라 위치를 기준으로 이름표가 바라볼 월드 방향을 계산한다.
	FVector CameraLocation = FVector::ZeroVector;
	FRotator IgnoredCameraRotation = FRotator::ZeroRotator;
	LocalPlayerController->GetPlayerViewPoint(CameraLocation, IgnoredCameraRotation);
	FVector NameTagLocation = NameTagWidgetComponent->GetComponentLocation();
	FVector CameraDirection = CameraLocation - NameTagLocation;
	if (CameraDirection.IsNearlyZero() == true)
	{
		return;
	}

	// 위젯 평면의 정면이 카메라를 향하도록 회전하되 화면이 기울어지지 않게 Roll을 제거한다.
	FRotator NameTagRotation = CameraDirection.Rotation();
	NameTagRotation.Roll = 0.0f;
	NameTagWidgetComponent->SetWorldRotation(NameTagRotation);
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
		ScrapeStrokeViewModelScreenDirection = FVector2D(0.0, -1.0);
		LastScrapeStrokeViewModelStableWorldTangent = FVector::ZeroVector;
		ScrapePointerViewModelWorldRotation = FQuat::Identity;
		ScrapeStrokeViewModelWorldRotation = FQuat::Identity;
		bScrapeStrokeViewModelDirectionLocked = false;
		bWasScrapingForViewModel = false;
		bHasScrapePointerViewModelRotation = false;
		bHasScrapeStrokeViewModelRotation = false;
		return;
	}

	if (IsValid(FirstPersonMesh))
	{
		FirstPersonMesh->SetVisibility(bFirstPersonArmsWereVisible, true);
	}
	LastScrapeViewModelPointerNormalized = FVector2D(0.5, 0.5);
	ScrapeStrokeViewModelScreenDirection = FVector2D(0.0, -1.0);
	LastScrapeStrokeViewModelStableWorldTangent = FVector::ZeroVector;
	ScrapePointerViewModelWorldRotation = FQuat::Identity;
	ScrapeStrokeViewModelWorldRotation = FQuat::Identity;
	ScrapeViewModelWorldScale = FVector::OneVector;
	bScrapeStrokeViewModelDirectionLocked = false;
	bWasScrapingForViewModel = false;
	bHasScrapePointerViewModelRotation = false;
	bHasScrapeStrokeViewModelRotation = false;
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

	const bool bScraping = ScrapeToolComponent->IsSurfaceScrapeVisualStrokeActive();
	const bool bScrapeModeChanged = bScraping != bWasScrapingForViewModel;
	const FVector2D PointerDeltaPixels = bScrapeModeChanged
		? FVector2D(0.0, 0.0)
		: FVector2D(
			(PointerNormalized.X - LastScrapeViewModelPointerNormalized.X)
				* static_cast<double>(ViewportWidth),
			(PointerNormalized.Y - LastScrapeViewModelPointerNormalized.Y)
				* static_cast<double>(ViewportHeight));
	if (!bScraping)
	{
		if (bWasScrapingForViewModel)
		{
			// 포인터 추적 모드로 돌아갈 때 직전 stroke의 방향 상태를 버린다.
			ScrapeStrokeViewModelScreenDirection = FVector2D(0.0, -1.0);
			LastScrapeStrokeViewModelStableWorldTangent = FVector::ZeroVector;
			bScrapeStrokeViewModelDirectionLocked = false;
		}
	}
	else
	{
		if (!bWasScrapingForViewModel)
		{
			// 새 stroke는 포인터 추적 중의 이동량이나 접선 상태를 상속하지 않는다.
			ScrapeStrokeViewModelScreenDirection = FVector2D(0.0, -1.0);
			LastScrapeStrokeViewModelStableWorldTangent = FVector::ZeroVector;
			bScrapeStrokeViewModelDirectionLocked = false;
		}
		if (!bScrapeStrokeViewModelDirectionLocked
			&& PointerDeltaPixels.SizeSquared() >= 0.25)
		{
			ScrapeStrokeViewModelScreenDirection = PointerDeltaPixels.GetSafeNormal();
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

	const float ConfiguredDistanceCm = bScraping
		? ScrapeStrokeViewModelDistanceCm
		: ScrapeViewModelDistanceCm;
	const FVector ViewPlanePoint = ViewLocation
		+ ViewForward * static_cast<double>(
			FMath::Max(ConfiguredDistanceCm, 10.0f));
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
		bScrapeStrokeViewModelDirectionLocked = !ContactWorldTangent.IsNearlyZero();
	}
	if (bScraping
		&& ContactWorldTangent.IsNearlyZero()
		&& bScrapeStrokeViewModelDirectionLocked)
	{
		// 잠깐 visual trace가 비어도 현재 stroke에서 이미 고정한 축은 유지한다.
		ContactWorldTangent = LastScrapeStrokeViewModelStableWorldTangent;
	}
	if (bScraping && ContactWorldTangent.IsNearlyZero())
	{
		ContactWorldTangent = ViewRight * ScrapeStrokeViewModelScreenDirection.X
			- ViewUp * ScrapeStrokeViewModelScreenDirection.Y;
		ContactWorldTangent = FVector::VectorPlaneProject(
			ContactWorldTangent,
			-ViewForward).GetSafeNormal();
	}
	if (bScraping && ContactWorldTangent.IsNearlyZero())
	{
		ContactWorldTangent = LastScrapeStrokeViewModelStableWorldTangent;
	}
	if (ContactWorldTangent.IsNearlyZero())
	{
		ContactWorldTangent = ViewUp;
	}
	if (bScraping
		&& !LastScrapeStrokeViewModelStableWorldTangent.IsNearlyZero()
		&& !LastScrapeStrokeViewModelStableWorldTangent.ContainsNaN()
		&& !ContactWorldTangent.ContainsNaN()
		&& FVector::DotProduct(
			LastScrapeStrokeViewModelStableWorldTangent,
			ContactWorldTangent) < 0.0)
	{
		// 헤라 날의 진행 축은 180도 동치이므로, locked pose를 포함한
		// 새 tangent를 직전 안정 축과 같은 부호로 맞춰 한 프레임 flip을 막는다.
		ContactWorldTangent *= -1.0;
	}
	if (bScraping)
	{
		LastScrapeStrokeViewModelStableWorldTangent = ContactWorldTangent;
	}

	const FQuat TargetContactRotation = FRotationMatrix::MakeFromXZ(
		-ViewForward,
		ContactWorldTangent).ToQuat();
	FQuat& CachedWorldRotation = bScraping
		? ScrapeStrokeViewModelWorldRotation
		: ScrapePointerViewModelWorldRotation;
	bool& bHasCachedWorldRotation = bScraping
		? bHasScrapeStrokeViewModelRotation
		: bHasScrapePointerViewModelRotation;
	FQuat ContactRotation = TargetContactRotation;
	const double SafeDeltaSeconds = FMath::IsFinite(DeltaSeconds)
		? FMath::Max(static_cast<double>(DeltaSeconds), 0.0)
		: 0.0;
	const double SmoothingSeconds = FMath::Max(
		static_cast<double>(ScrapeViewModelRotationSmoothingSeconds),
		0.0);
	if (bHasCachedWorldRotation
		&& SmoothingSeconds > UE_DOUBLE_SMALL_NUMBER)
	{
		const double RotationBlend = 1.0 - FMath::Exp(
			-SafeDeltaSeconds / SmoothingSeconds);
		ContactRotation = FQuat::Slerp(
			CachedWorldRotation,
			TargetContactRotation,
			FMath::Clamp(RotationBlend, 0.0, 1.0)).GetNormalized();
	}
	CachedWorldRotation = ContactRotation;
	bHasCachedWorldRotation = true;

	// StaticMesh socket scale은 배치 계약에 쓰지 않는다. 현재 viewmodel world scale을
	// 보존하면서 Blade_Tip/Blade_Contact의 역변환으로 접점을 정확히 맞춘다.
	const FTransform BladeContactMeshLocal(
		BladeContactSocket->RelativeRotation,
		BladeContactSocket->RelativeLocation);
	const FTransform DesiredContactWorld(
		ContactRotation,
		ContactWorldLocation,
		ScrapeViewModelWorldScale);
	const FRotator ConfiguredRotationOffset = bScraping
		? ScrapeStrokeViewModelRotationOffset
		: ScrapeViewModelRotationOffset;
	const FRotator SafeRotationOffset = ConfiguredRotationOffset.ContainsNaN()
		? FRotator::ZeroRotator
		: ConfiguredRotationOffset;
	const float ConfiguredScaleMultiplier = bScraping
		? ScrapeStrokeViewModelScaleMultiplier
		: ScrapeViewModelScaleMultiplier;
	const bool bScaleMultiplierUsable =
		FMath::IsFinite(ConfiguredScaleMultiplier)
		&& ConfiguredScaleMultiplier > UE_KINDA_SMALL_NUMBER;
	const float SafeScaleMultiplier = bScaleMultiplierUsable
		? ConfiguredScaleMultiplier
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
	// 실제 부착 소켓과 일인칭 자세를 조회할 메시 및 로컬 소유자 검사
	if (IsValid(FirstPersonToolMesh) == false || IsValid(ThirdPersonToolMesh) == false || IsValid(FirstPersonMesh) == false
		|| IsValid(BodyMesh) == false || IsLocallyControlled() == false)
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
		const bool bHasCurrentModeRotation = ScrapeToolComponent->IsSurfaceScrapeVisualStrokeActive()
			? bHasScrapeStrokeViewModelRotation
			: bHasScrapePointerViewModelRotation;
		if (bHasCurrentModeRotation)
		{
			// Viewport/deproject가 일시적으로 안 나오면 현재 모드에서 성공한
			// viewmodel pose만 유지하고 다른 모드나 기본 소켓으로 튀지 않는다.
			return;
		}

		// 첫 pose를 한 번도 만들지 못했으면 팔을 감춘 채 손 위치의
		// 도구만 남기지 않고 즉시 기본 부착으로 fail-closed 한다.
		SetFirstPersonScrapeViewModelActive(false);
	}

	// 삼인칭 부착에서 확정한 아이템별 소켓을 일인칭 배치에도 공유
	FName AttachSocketName = ThirdPersonToolMesh->GetAttachSocketName();
	// 지정 소켓과 기본 소켓이 모두 없는 상태의 위치 계산 차단
	if (AttachSocketName.IsNone() == true || BodyMesh->DoesSocketExist(AttachSocketName) == false)
	{
		return;
	}

	// 일인칭 배치에 사용할 선택 아이템의 공통 부착 설정 조회
	UMD_ItemDefinition* CurrentItem = IsValid(EquipHotbarComponent) == true ? EquipHotbarComponent->GetCurrentItem() : nullptr;

	// 본체의 실제 부착 소켓 자세를 일인칭 팔 공간에 합성하기 위한 조회
	const FTransform SocketLocal =
		BodyMesh->GetSocketTransform(AttachSocketName, RTS_Component);
	// 날 축 roll 은 GetToolAttachTransform 안에 이미 들어 있다. 3인칭 메시의
	// 상대 회전을 읽어 오면 안 된다 — 그 컴포넌트는 블레이드 갱신과
	// RefreshScrapeLoadVisual 두 곳이 쓰는 경쟁 상태이고, 되돌려진 프레임을
	// 집으면 AttachOffset 으로 튀어 1인칭에서만 떨림으로 보인다.
	const FTransform Offset =
		GetToolAttachTransform(FirstPersonToolMesh->GetStaticMesh(), CurrentItem);

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
		Camera->GetComponentLocation() + TargetRotation.RotateVector(FirstPersonArmsOffset
			+ (StainScraperComponent ? StainScraperComponent->GetHandSwingOffset() : FVector::ZeroVector));

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
	// 서버에서 빙의가 완료된 PlayerState의 이름을 이름표에 반영한다.
	RefreshNameTag();
	// 서버에서 빙의 시 PlayerState 외형을 캐릭터 메시에 적용한다.
	BindAppearanceDelegate();
	ApplyAppearanceFromPlayerState();
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
		// 재시작 이후 다시 사용할 런타임 입력 컨텍스트 조회
		UInputMappingContext* MappingContext = ResolvePlayerMappingContext();
		// 준비된 입력 컨텍스트에 한정한 로컬 플레이어 재등록
		if (IsValid(MappingContext) == true)
		{
			// 현재 조작 설정 키가 반영된 입력 컨텍스트 활성화
			Subsystem->AddMappingContext(MappingContext, 0);
		}
	}

	RefreshScrapeFocusInputState();
}

void AMD_Character::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	InitializeAbilitySystem();
	// 클라이언트에 PlayerState가 복제된 뒤 플랫폼 닉네임을 이름표에 반영한다.
	RefreshNameTag();
	// 클라이언트에서 PlayerState 복제 후 외형을 캐릭터 메시에 적용한다.
	BindAppearanceDelegate();
	ApplyAppearanceFromPlayerState();
}

void AMD_Character::RefreshNameTag()
{
	// 이름표에 표시할 복제된 플레이어 정보를 가져온다.
	// 이름표 컴포넌트가 준비되지 않은 경우 처리를 종료한다.
	APlayerState* CurrentPlayerState = GetPlayerState();
	if (IsValid(CurrentPlayerState) == false || IsValid(NameTagWidgetComponent) == false)
	{
		return;
	}

	// 설정된 이름표 UI 인스턴스를 프로젝트 위젯 형식으로 가져온다.
	UMD_NameTagWidget* NameTagWidget = Cast<UMD_NameTagWidget>(NameTagWidgetComponent->GetUserWidgetObject());
	if (IsValid(NameTagWidget) == false)
	{
		return;
	}

	// PlayerState가 모든 클라이언트에 복제한 플랫폼 닉네임을 표시한다.
	FText PlayerName = FText::FromString(CurrentPlayerState->GetPlayerName());
	NameTagWidget->SetTextContent(PlayerName);
}

void AMD_Character::CalcCamera(
	const float DeltaTime,
	FMinimalViewInfo& OutResult)
{
	Super::CalcCamera(DeltaTime, OutResult);
	if (IsValid(ScrapeToolComponent)
		&& ScrapeToolComponent->ApplyScrapeFocusCameraView(OutResult))
	{
		// First Person 전용 FOV를 따로 쓰지 않는 동안에는 Focus FOV와 맞춰야
		// 팔·도구가 Focus 시작 순간 갑자기 확대되거나 축소되지 않는다.
		if (!IsValid(Camera) || !Camera->bEnableFirstPersonFieldOfView)
		{
			OutResult.FirstPersonFOV = OutResult.FOV;
		}
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

	if (IsValid(InputConfig->ScannerModeSwitchAction))
	{
		EIC->BindAction(InputConfig->ScannerModeSwitchAction, ETriggerEvent::Started,
			this, &AMD_Character::HandleScannerModeSwitchInput);
	}

	if (IsValid(InputConfig->ScrapeFocusAction))
	{
		EIC->BindAction(
			InputConfig->ScrapeFocusAction,
			ETriggerEvent::Started,
			this,
			&AMD_Character::HandleScrapeFocusToggleInput);
		EIC->BindAction(
			InputConfig->ScrapeFocusAction,
			ETriggerEvent::Completed,
			this,
			&AMD_Character::HandleScrapeFocusReleasedInput);
		EIC->BindAction(
			InputConfig->ScrapeFocusAction,
			ETriggerEvent::Canceled,
			this,
			&AMD_Character::HandleScrapeFocusReleasedInput);
	}

	EIC->BindAction(InputConfig->MoveAction, ETriggerEvent::Triggered, this, &AMD_Character::HandleMoveInput);
	EIC->BindAction(InputConfig->LookAction, ETriggerEvent::Triggered, this, &AMD_Character::HandleLookInput);
	EIC->BindAction(InputConfig->JumpAction, ETriggerEvent::Started, this, &AMD_Character::HandleJumpOrSwimInput);
	EIC->BindAction(InputConfig->JumpAction, ETriggerEvent::Completed, this, &AMD_Character::HandleJumpOrSwimReleasedInput);
	EIC->BindAction(InputConfig->InteractAction, ETriggerEvent::Started, this, &AMD_Character::HandleInteractInput);
	EIC->BindAction(InputConfig->InteractAction, ETriggerEvent::Completed, this, &AMD_Character::HandleInteractReleasedInput);
	EIC->BindAction(InputConfig->InteractAction, ETriggerEvent::Canceled, this, &AMD_Character::HandleInteractReleasedInput);
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

UInputMappingContext* AMD_Character::ResolvePlayerMappingContext()
{
	// 설정 매니저나 카테고리를 찾지 못할 때 사용할 원본 입력 컨텍스트
	UInputMappingContext* ResolvedMappingContext = PlayerMappingContext;
	// 캐릭터에 원본 입력 컨텍스트가 없는 경우의 빈 결과 반환
	if (IsValid(ResolvedMappingContext) == false)
	{
		return ResolvedMappingContext;
	}
	// 현재 게임 인스턴스의 설정 매니저 조회
	UMD_SettingsManager* SettingsManager = UMD_SettingsManager::Get(this);
	// 설정 시스템이 없는 환경의 원본 입력 컨텍스트 유지
	if (IsValid(SettingsManager) == false)
	{
		return ResolvedMappingContext;
	}
	// 현재 적용된 조작 설정과 런타임 매핑을 소유한 카테고리 조회
	UMD_ControlSettingCategory* ControlCategory = SettingsManager->GetControlSettings();
	// 조작 카테고리가 없는 환경의 원본 입력 컨텍스트 유지
	if (IsValid(ControlCategory) == false)
	{
		return ResolvedMappingContext;
	}
	// 원본의 트리거와 모디파이어를 보존한 설정 전용 복제본 준비
	UInputMappingContext* ConfiguredMappingContext = ControlCategory->PrepareInputMappingContext(PlayerMappingContext);
	// 복제본 생성 성공에 한정한 반환 컨텍스트 교체
	if (IsValid(ConfiguredMappingContext) == true)
	{
		// 사용자 선택 키가 반영된 런타임 입력 컨텍스트 보관
		ResolvedMappingContext = ConfiguredMappingContext;
	}
	// 로컬 플레이어 서브시스템에 등록할 입력 컨텍스트 반환
	return ResolvedMappingContext;
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
		// 전후 및 좌우 입력 축만 전달하고 사다리 이동 방향 결정은 이동 컴포넌트에 위임
		MDMovement->ApplyLadderMovementInput(MoveInput.X, MoveInput.Y);
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
	// 카메라 감도와 축 반전 적용 전의 원본 시점 입력 보관
	FVector2D RawLookInput = Value.Get<FVector2D>();
	// 헤라 포커스의 원본 입력 우선 처리
	if (IsValid(ScrapeToolComponent) == true)
	{
		// 헤라 전용 감도와 제스처 방향을 유지한 입력 전달
		bool bConsumed = ScrapeToolComponent->ConsumeScrapeFocusLookInput(RawLookInput);
		// 포커스에서 소비한 입력의 카메라 중복 처리 차단
		if (bConsumed == true)
		{
			return;
		}
	}

	// 설정 매니저를 찾지 못한 경우 사용할 기본 조작 설정을 준비한다.
	FMD_ControlSettings AppliedControlSettings;
	// 현재 게임 인스턴스가 관리하는 설정 매니저를 찾는다.
	UMD_SettingsManager* SettingsManager = UMD_SettingsManager::Get(this);
	// 유효한 설정 매니저가 있으면 조작 카테고리 인스턴스를 조회한다.
	if (IsValid(SettingsManager) == true)
	{
		// 실제 입력에 반영된 조작 설정을 가진 카테고리를 찾는다.
		UMD_ControlSettingCategory* ControlSettings = SettingsManager->GetControlSettings();
		// 유효한 조작 설정 인스턴스가 있으면 적용된 값을 사용한다.
		if (IsValid(ControlSettings) == true)
		{
			// UI에서 편집 중인 값이 아닌 마지막으로 적용된 설정을 가져온다.
			AppliedControlSettings = ControlSettings->GetAppliedSettings();
		}
	}
	// 헤라에서 소비되지 않은 카메라 입력에 사용자 감도 적용
	FVector2D LookInput = RawLookInput * AppliedControlSettings.MouseSensitivity;
	// 카메라 상하 반전 설정의 적용 여부 확인
	if (AppliedControlSettings.bInvertVerticalLook == true)
	{
		// 카메라 세로 입력에 한정한 방향 반전
		LookInput.Y *= -1.0f;
	}

	// 멀미 완화 응답 커브를 먹인다. 스무딩이 켜져 있으면 컴포넌트가 누적분을 직접 흘려보내므로
	// 여기서는 빈 값이 돌아오고, 꺼져 있으면 지연 없이 그대로 돌아온다.
	const FVector2D ShapedLookInput = IsValid(ComfortComponent)
		? ComfortComponent->ShapeLookInput(LookInput)
		: LookInput;

	AddControllerYawInput(ShapedLookInput.X);
	AddControllerPitchInput(ShapedLookInput.Y);
}

void AMD_Character::HandleInteractInput(const FInputActionValue& Value)
{
	// 상호작용 시 해제도 헤라 포커스만. 폼건 조준은 RMB 릴리즈로만 풀린다
	CancelActiveScrapeAbilityAndEndFocus();

	if (IsValid(InteractionComponent))
	{
		InteractionComponent->Interact();
	}
}

void AMD_Character::HandleInteractReleasedInput(const FInputActionValue& Value)
{
	if (IsValid(InteractionComponent))
	{
		InteractionComponent->InteractReleased();
	}
}

void AMD_Character::HandleScannerModeSwitchInput(const FInputActionValue& Value)
{
	if (IsValid(ScannerComponent)) ScannerComponent->ToggleScanMode();
}

void AMD_Character::HandleAbilityInput(const FInputActionValue& Value)
{
	if (!IsValid(EquipHotbarComponent))
	{
		return;
	}

	// 모든 보관형 소모품의 사용 입력을 구매 GE가 부여한 GA에 전달
	if (IsValid(Cast<UMD_ConsumeItemDefinition>(EquipHotbarComponent->GetCurrentItem()))
		|| IsValid(Cast<UMD_SupplyItemDefinition>(EquipHotbarComponent->GetCurrentItem())))
	{
		// 선택한 아이템의 전체 AssetId에 귀속된 능력 한 개 활성화
		EquipHotbarComponent->TryActivateCurrentItemAbility();
		// 동일 입력의 일반 도구 처리 중복 방지
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
	if (bIsScrapeAbility && IsValid(ScrapeToolComponent) && !ScrapeToolComponent->IsScrapeFocusActive())
	{
		EquipHotbarComponent->TryActivateCurrentItemAbility();
		return;
	}
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

	// 같은 GA 태그를 공유하는 도구 사이에서도 선택 아이템의 부여 능력만 실행
	EquipHotbarComponent->TryActivateCurrentItemAbility();
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
	// 폼건: RMB를 누르는 동안 조준 궤적 표시 + 조준 자세
	if (IsFoamGunEquipped())
	{
		if (IsValid(FoamGunComponent))
		{
			FoamGunComponent->SetAimHeld(true);
		}
		SetFoamAiming(true);
		return;
	}

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

void AMD_Character::HandleScrapeFocusReleasedInput()
{
	// 폼건 조준만 홀드 방식이라 release에서 정리한다. 헤라 포커스는 토글이므로 여기서 건드리지 않는다
	if (IsValid(FoamGunComponent))
	{
		FoamGunComponent->SetAimHeld(false);
	}
	SetFoamAiming(false);
}

void AMD_Character::HandleScrapeFocusInputEnded()
{
	// 도구 전환·컨텍스트 제거 경로에서도 호출되므로 조준 상태를 항상 정리한다
	if (IsValid(FoamGunComponent))
	{
		FoamGunComponent->SetAimHeld(false);
	}
	SetFoamAiming(false);

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
		// 기본 슬롯 구성 이후 구매자 ASC에 아이템 GE 연결
		if (IsValid(EquipHotbarComponent) == true)
		{
			// 기본 및 저장 장비에도 구매와 동일한 능력 부여 적용
			EquipHotbarComponent->InitializeItemAbilities();
		}
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

	// 구매 GE의 SourceObject와 전체 AssetId로 현재 헤라의 능력 식별
	FGameplayAbilitySpecHandle CandidateHandle = EquipHotbarComponent->FindItemAbility(SourceTool);
	// 선택 헤라에 부여된 능력이 없는 실행 요청 차단
	if (CandidateHandle.IsValid() == false)
	{
		// 실제 실행할 헤라 능력 부재 전달
		return false;
	}

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
	if (StainScraperComponent) StainScraperComponent->CancelStroke();
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

bool AMD_Character::IsFoamGunEquipped() const
{
	if (!IsValid(EquipHotbarComponent))
	{
		return false;
	}

	const UMD_ToolDefinition* CurrentTool = EquipHotbarComponent->GetCurrentTool();
	return IsValid(CurrentTool)
		&& CurrentTool->ToolAbilityTag.MatchesTagExact(MDGameplayTags::Ability_UtilityRepair_Foam);
}

bool AMD_Character::IsCutterEquipped() const
{
	if (!IsValid(EquipHotbarComponent))
	{
		return false;
	}

	const UMD_ToolDefinition* CurrentTool = EquipHotbarComponent->GetCurrentTool();
	return IsValid(CurrentTool)
		&& CurrentTool->ToolAbilityTag.MatchesTagExact(MDGameplayTags::Ability_WallCut);
}

void AMD_Character::RefreshCutterBladeMesh()
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// 절단기를 들고, 톱날 메시가 있고, 본체에 회전 중심 소켓이 있어야 표시한다.
	// 소켓이 없으면 톱날이 도구 원점에 겹쳐 박히므로 숨기는 편이 낫다.
	const bool bWantsBlade = IsCutterEquipped() && IsValid(CutterBladeMesh) && !IsCarryingItem();

	const auto Apply = [this, bWantsBlade](UStaticMeshComponent* const Blade,
		const UStaticMeshComponent* const ToolMesh)
	{
		if (!IsValid(Blade))
		{
			return;
		}

		const UStaticMesh* const ParentMesh = IsValid(ToolMesh) ? ToolMesh->GetStaticMesh() : nullptr;
		const bool bHasPivot = ParentMesh != nullptr
			&& !CutterBladePivotSocketName.IsNone()
			&& ParentMesh->FindSocket(CutterBladePivotSocketName) != nullptr;
		const bool bShow = bWantsBlade && bHasPivot;

		Blade->SetStaticMesh(bShow ? CutterBladeMesh : nullptr);
		Blade->SetVisibility(bShow, true);

		if (!bShow)
		{
			// 다시 켤 때 이전 각도에서 이어지지 않도록 회전을 초기화한다
			Blade->SetRelativeRotation(FRotator::ZeroRotator);
		}
	};

	Apply(ThirdPersonCutterBlade, ThirdPersonToolMesh);
	Apply(FirstPersonCutterBlade, FirstPersonToolMesh);
}

void AMD_Character::UpdateCutterBladeSpin(const float DeltaSeconds)
{
	if (GetNetMode() == NM_DedicatedServer || !bCutterVFXActive)
	{
		return;
	}

	if (FMath::IsNearlyZero(CutterBladeSpinDegreesPerSecond))
	{
		return;
	}

	// 톱날 메시는 두께축이 Y 라 로컬 Y 를 축으로 돈다.
	const FRotator Step(CutterBladeSpinDegreesPerSecond * DeltaSeconds, 0.0f, 0.0f);
	for (UStaticMeshComponent* const Blade : { ThirdPersonCutterBlade.Get(), FirstPersonCutterBlade.Get() })
	{
		if (IsValid(Blade) && Blade->GetStaticMesh() != nullptr)
		{
			Blade->AddLocalRotation(Step);
		}
	}
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

	// 공통 아이템 상태를 조회할 Hotbar 유효성 검사
	if (IsValid(EquipHotbarComponent) == true)
	{
		// 도구와 소비 아이템을 함께 표시할 현재 보관 정의 조회
		UMD_ItemDefinition* CurrentItem = EquipHotbarComponent->GetCurrentItem();
		// 위치 표시와 보유 수량 조회에 사용할 선택 슬롯 확인
		int32 SlotIndex = EquipHotbarComponent->GetCurrentSlotIndex();
		// 실제 아이템 이름과 잔여 수량을 프로젝트 로그에 기록
		UE_LOG(MarineDemolition, Log, TEXT("[Hotbar] Slot %d/%d, Item: %s, Count: %d"), SlotIndex, EquipHotbarComponent->GetSlotCount(),
			IsValid(CurrentItem) == true ? *CurrentItem->ItemName.ToString() : TEXT("Empty"), EquipHotbarComponent->GetItemCountAtSlot(SlotIndex));
	}
}

void AMD_Character::HandleScrapeLoadChanged(const float NewScrapeLoad)
{
	RefreshScrapeLoadVisual();
}

void AMD_Character::HandleHeldActorChanged(AActor* HeldActor)
{
	// EndPlay 의 CancelInteraction 으로도 오므로 파괴 중엔 메시를 건드리지 않는다
	if (IsActorBeingDestroyed())
	{
		return;
	}
	// 슬롯은 그대로, 표시만 바꾼다. 도구 메시·톱날·헤라 적재 메시가 한 경로에서 갱신된다
	RefreshToolMesh();
}

bool AMD_Character::IsCarryingItem() const
{
	return IsValid(InteractionComponent) && InteractionComponent->IsHolding();
}

void AMD_Character::RefreshToolMesh()
{
	if (!IsValid(EquipHotbarComponent))
	{
		return;
	}

	// 메시 교체 전에 이전 헤라 소켓/scale을 잡고 있던 독립 viewmodel 상태를 버린다.
	SetFirstPersonScrapeViewModelActive(false);
	// 선택한 공통 아이템에서 메시 부착 설정 조회
	UMD_ItemDefinition* CurrentItem = EquipHotbarComponent->GetCurrentItem();
	// 아이템별 지정 소켓을 우선하고 미지정 시 캐릭터 기본 소켓 선택
	FName SocketName = IsValid(CurrentItem) == true && CurrentItem->AttachSocketName.IsNone() == false
		? CurrentItem->AttachSocketName : ToolAttachSocketName;
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

	// 도구와 소모품의 공통 오프셋으로 초기 부착 회전 설정
	FTransform AttachOffset = IsValid(CurrentItem) == true ? CurrentItem->AttachOffset : FTransform::Identity;
	ScrapeAnimationToolRelativeRotation = AttachOffset.Rotator();

	// 상대 트랜스폼 자체는 RefreshScrapeLoadVisual 에서 준다. 오염 적재 단계에 따라
	// 메시가 바뀌는데, 그립 소켓은 메시마다 다를 수 있어 최종 메시가 정해진 뒤에
	// 계산해야 한다.
	RefreshScrapeLoadVisual();
}

void AMD_Character::RefreshScrapeLoadVisual()
{
	// 도구와 소모품을 포함한 선택 슬롯의 공통 정의 조회
	UMD_ItemDefinition* CurrentItem = IsValid(EquipHotbarComponent) == true ? EquipHotbarComponent->GetCurrentItem() : nullptr;
	// 기존 도구 메시와 오염 적재 표현에 사용할 도구 정의 조회
	UMD_ToolDefinition* CurrentTool = Cast<UMD_ToolDefinition>(CurrentItem);
	// 아이템 유형에 관계없이 공통 기본 메시를 사용하고 빈 슬롯은 표시 해제
	UStaticMesh* NewToolMesh = IsValid(CurrentItem) == true ? CurrentItem->ToolMesh : nullptr;
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

	// 물건을 들고 있는 동안은 도구를 내린다 (슬롯 유지, 연출만)
	const bool bShowTool = IsValid(NewToolMesh) && !IsCarryingItem();
	const bool bHasThirdPersonSocket = IsValid(GetMesh())
		&& GetMesh()->DoesSocketExist(ThirdPersonToolMesh->GetAttachSocketName());
	// 일인칭도 기본 소켓 대신 실제 부착 소켓의 유효성으로 표시 여부 결정
	bool bHasFirstPersonSocket = IsValid(FirstPersonMesh) == true && bHasThirdPersonSocket == true;
	FirstPersonToolMesh->SetStaticMesh(NewToolMesh);
	FirstPersonToolMesh->SetVisibility(bShowTool && bHasFirstPersonSocket, true);
	ThirdPersonToolMesh->SetStaticMesh(NewToolMesh);
	ThirdPersonToolMesh->SetVisibility(bShowTool && bHasThirdPersonSocket, true);

	// 도구 본체 변경에 따른 절단기 톱날 메시 갱신
	RefreshCutterBladeMesh();

	// 공통 부착 오프셋과 최종 메시의 그립 소켓을 함께 반영
	FTransform ToolTransform = GetToolAttachTransform(NewToolMesh, CurrentItem);
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

FTransform AMD_Character::GetToolAttachTransform(const UStaticMesh* ToolMesh, UMD_ItemDefinition* Item) const
{
	// 선택 아이템의 공통 오프셋을 메시 그립 보정의 기준으로 사용
	FTransform Offset = IsValid(Item) == true ? Item->AttachOffset : FTransform::Identity;
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
	DOREPLIFETIME(AMD_Character, bFoamAiming);
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

float AMD_Character::PlayBarnacleScrapeMontage()
{
	if (!IsValid(BarnacleScrapeMontage) || !IsValid(GetMesh()))
	{
		return 0.f;
	}

	UAnimInstance* const AnimInstance = GetMesh()->GetAnimInstance();
	if (!IsValid(AnimInstance))
	{
		return 0.f;
	}

	// 같은 원샷이 아직 끝나기 전에 다시 타격하면 처음부터 다시 친다.
	return AnimInstance->Montage_Play(BarnacleScrapeMontage);
}

void AMD_Character::OnRep_CutterVFXActive()
{
	RefreshCutterVFX();
}

void AMD_Character::SetFoamAiming(const bool bAiming)
{
	if (bFoamAiming == bAiming)
	{
		return;
	}

	// 소유 클라는 서버 왕복을 기다리지 않고 즉시 자세를 바꾼다.
	// 서버가 같은 값을 굳히면 사본 프록시가 OnRep 으로 따라온다.
	bFoamAiming = bAiming;
	RefreshFoamAimMontage();

	if (!HasAuthority())
	{
		ServerSetFoamAiming(bAiming);
	}
}

void AMD_Character::ServerSetFoamAiming_Implementation(const bool bAiming)
{
	if (bFoamAiming == bAiming)
	{
		return;
	}

	bFoamAiming = bAiming;
	// 리슨 서버 호스트 화면에도 반영한다. OnRep 은 서버에서 불리지 않는다.
	RefreshFoamAimMontage();
}

void AMD_Character::OnRep_FoamAiming()
{
	RefreshFoamAimMontage();
}

void AMD_Character::RefreshFoamAimMontage()
{
	if (GetNetMode() == NM_DedicatedServer || !IsValid(FoamAimMontage))
	{
		return;
	}

	USkeletalMeshComponent* const BodyMesh = GetMesh();
	UAnimInstance* const AnimInstance = IsValid(BodyMesh) ? BodyMesh->GetAnimInstance() : nullptr;
	if (!IsValid(AnimInstance))
	{
		return;
	}

	if (!bFoamAiming)
	{
		if (AnimInstance->Montage_IsPlaying(FoamAimMontage))
		{
			AnimInstance->Montage_Stop(FoamAimMontageBlendOutSeconds, FoamAimMontage);
		}
		return;
	}

	if (AnimInstance->Montage_IsPlaying(FoamAimMontage))
	{
		return;
	}

	if (AnimInstance->Montage_Play(FoamAimMontage) <= 0.0f)
	{
		return;
	}

	// 한 프레임짜리 포즈 몽타주라 그냥 두면 곧바로 끝난다. 섹션이 자기 자신을
	// 다음으로 가리키게 해 조준을 놓을 때까지 자세를 유지시킨다.
	const FName SectionName = FoamAimMontage->GetSectionName(0);
	if (!SectionName.IsNone())
	{
		AnimInstance->Montage_SetNextSection(SectionName, SectionName, FoamAimMontage);
	}
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
		CutterVFXComponent->SetFirstPersonPrimitiveType(
			IsLocallyControlled()
				? EFirstPersonPrimitiveType::FirstPerson
				: EFirstPersonPrimitiveType::None);
		CutterVFXComponent->Activate(true);
		return;
	}

	CutterVFXComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
		CutterCutEffect, AttachMesh, CutterVFXSocketName,
		FVector::ZeroVector, FRotator::ZeroRotator,
		EAttachLocation::SnapToTarget, /*bAutoDestroy=*/false);
	if (IsValid(CutterVFXComponent))
	{
		// 로컬 절단 이펙트도 GPU에서 이동한 1인칭 도구와 같은 변환을 받게 한다.
		CutterVFXComponent->SetFirstPersonPrimitiveType(
			IsLocallyControlled()
				? EFirstPersonPrimitiveType::FirstPerson
				: EFirstPersonPrimitiveType::None);
	}
}

void AMD_Character::HandleHealthStateChanged(EMD_HealthState NewState)
{
	const bool bIsDead = (NewState == EMD_HealthState::Dead);
	if (bIsDead)
	{
		CancelActiveScrapeAbilityAndEndFocus();
		if (ScannerComponent) ScannerComponent->ClearScan();
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

void AMD_Character::ApplyAppearanceFromPlayerState()
{
	AMD_PlayerState* MDPlayerState = GetPlayerState<AMD_PlayerState>();
	if (IsValid(MDPlayerState) == false)
	{
		return;
	}

	ApplyAppearance(MDPlayerState->GetAppearance());
}

void AMD_Character::ApplyAppearance(const FMD_CharacterAppearance& Appearance)
{
	const UMD_CharacterAppearanceSettings* Settings = UMD_CharacterAppearanceSettings::Get();
	UMD_CharacterRosterData* Roster = IsValid(Settings) == true ? Settings->GetRoster() : nullptr;
	if (IsValid(Roster) == false)
	{
		return;
	}

	const FMD_CharacterAppearance Sanitized = Roster->Sanitize(Appearance);
	USkeletalMeshComponent* BodyMesh = GetMesh();
	bool bUseMaterialVariants = false;

	if (Roster->Characters.IsValidIndex(Sanitized.CharacterIndex) == true && IsValid(BodyMesh) == true)
	{
		const FMD_CharacterRosterEntry& Entry = Roster->Characters[Sanitized.CharacterIndex];
		bUseMaterialVariants = Entry.MaterialVariants.Num() > 0;

		// 몸 메시·애님은 바뀔 때만 교체한다. 같은 값 재적용은 포즈를 끊는다
		USkeletalMesh* NewBodyMesh = Entry.BodyMesh.LoadSynchronous();
		if (IsValid(NewBodyMesh) == true && BodyMesh->GetSkeletalMeshAsset() != NewBodyMesh)
		{
			BodyMesh->SetSkeletalMeshAsset(NewBodyMesh);
			BodyMesh->EmptyOverrideMaterials();
		}

		UClass* NewAnimClass = Entry.BodyAnimClass.LoadSynchronous();
		if (IsValid(NewAnimClass) == true && BodyMesh->GetAnimClass() != NewAnimClass)
		{
			BodyMesh->SetAnimInstanceClass(NewAnimClass);
		}

		// 1인칭 팔은 몸 포즈를 따라가므로 교체 후 LeaderPose를 다시 묶는다
		USkeletalMesh* NewArmsMesh = Entry.ArmsMesh.LoadSynchronous();
		if (IsValid(FirstPersonMesh) == true && IsValid(NewArmsMesh) == true
			&& FirstPersonMesh->GetSkeletalMeshAsset() != NewArmsMesh)
		{
			FirstPersonMesh->SetSkeletalMeshAsset(NewArmsMesh);
			FirstPersonMesh->EmptyOverrideMaterials();
			BindFirstPersonArmsToBodyPose();
		}

		if (GetNetMode() != NM_DedicatedServer)
		{
			const FMD_CharacterMaterialVariant* Variant = nullptr;
			if (bUseMaterialVariants)
			{
				const int32 VariantIndex = Entry.MaterialVariants.IsValidIndex(Sanitized.ColorIndex)
					? Sanitized.ColorIndex : 0;
				Variant = &Entry.MaterialVariants[VariantIndex];
			}

			// 이전 색의 슬롯이 남지 않도록 매번 원본을 기준으로 해석한다.
			auto ApplyMaterials = [bUseMaterialVariants](USkeletalMeshComponent* Component,
				const TArray<TSoftObjectPtr<UMaterialInterface>>* BaseOverrides,
				const TArray<TSoftObjectPtr<UMaterialInterface>>* VariantOverrides)
			{
				if (!IsValid(Component) || !IsValid(Component->GetSkeletalMeshAsset()))
				{
					return;
				}
				const TArray<FSkeletalMaterial>& Defaults = Component->GetSkeletalMeshAsset()->GetMaterials();
				for (int32 SlotIndex = 0; SlotIndex < Defaults.Num(); ++SlotIndex)
				{
					UMaterialInterface* Material = Defaults[SlotIndex].MaterialInterface;
					if (BaseOverrides && BaseOverrides->IsValidIndex(SlotIndex))
					{
						if (UMaterialInterface* Override = (*BaseOverrides)[SlotIndex].LoadSynchronous())
						{
							Material = Override;
						}
					}
					if (VariantOverrides && VariantOverrides->IsValidIndex(SlotIndex))
					{
						if (UMaterialInterface* Override = (*VariantOverrides)[SlotIndex].LoadSynchronous())
						{
							Material = Override;
						}
					}
					UMaterialInterface* Current = Component->GetMaterial(SlotIndex);
					UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Current);
					if (Current != Material && !(MID && !bUseMaterialVariants && MID->Parent == Material))
					{
						Component->SetMaterial(SlotIndex, Material);
					}
				}
			};

			if (bUseMaterialVariants || AppliedAppearance.CharacterIndex != Sanitized.CharacterIndex)
			{
				ApplyMaterials(BodyMesh, nullptr, Variant ? &Variant->BodyMaterials : nullptr);
			}
			ApplyMaterials(FirstPersonMesh, &Entry.ArmsMaterials, Variant ? &Variant->ArmsMaterials : nullptr);
		}
	}

	// 색상별 머티리얼은 텍스처 자체에 색이 있어 추가 틴트를 곱하지 않는다.
	if (GetNetMode() != NM_DedicatedServer && !bUseMaterialVariants)
	{
		const FLinearColor MainColor = Roster->GetPaletteColor(Sanitized.ColorIndex);
		ApplyAppearanceColor(BodyMesh, Roster->ColorParameterName, MainColor);
		ApplyAppearanceColor(FirstPersonMesh, Roster->ColorParameterName, MainColor);
	}

	AppliedAppearance = Sanitized;
}

void AMD_Character::ApplyAppearanceColor(USkeletalMeshComponent* MeshComponent, FName ParameterName, const FLinearColor& Color)
{
	if (IsValid(MeshComponent) == false || ParameterName.IsNone() == true)
	{
		return;
	}

	const int32 MaterialCount = MeshComponent->GetNumMaterials();
	for (int32 SlotIndex = 0; SlotIndex < MaterialCount; ++SlotIndex)
	{
		// 이미 MID면 재사용하고, 아니면 슬롯 머티리얼을 부모로 MID를 만든다
		UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(MeshComponent->GetMaterial(SlotIndex));
		if (IsValid(MID) == false)
		{
			MID = MeshComponent->CreateAndSetMaterialInstanceDynamic(SlotIndex);
		}
		if (IsValid(MID) == true)
		{
			MID->SetVectorParameterValue(ParameterName, Color);
		}
	}
}

bool AMD_Character::TryGetVisibleToolSocketLocation(FName SocketName, FVector& OutLocation) const
{
	if (SocketName.IsNone())
	{
		return false;
	}

	// 로컬 1인칭 화면이면 1인칭 도구, 거울(3인칭 전환)·타 클라이언트·서버는 3인칭 도구
	const bool bUseFirstPerson = IsLocallyControlled()
		&& !bLocalThirdPersonViewEnabled
		&& IsValid(FirstPersonToolMesh)
		&& FirstPersonToolMesh->IsVisible();
	const UStaticMeshComponent* ToolMesh = bUseFirstPerson ? FirstPersonToolMesh : ThirdPersonToolMesh;
	if (!IsValid(ToolMesh) || !ToolMesh->DoesSocketExist(SocketName))
	{
		return false;
	}

	const FVector SocketLocation = ToolMesh->GetSocketLocation(SocketName);
	OutLocation = bUseFirstPerson ? ToFirstPersonApparentWorldLocation(SocketLocation) : SocketLocation;
	return true;
}

FVector AMD_Character::ToFirstPersonApparentWorldLocation(const FVector& WorldLocation) const
{
	if (!IsValid(Camera) || !Camera->bEnableFirstPersonScale)
	{
		return WorldLocation;
	}

	// 1인칭 프리미티브는 뷰 원점을 기준으로 FirstPersonScale만큼 당겨 그려진다
	const APlayerController* PlayerController = Cast<APlayerController>(Controller);
	FVector ViewLocation = Camera->GetComponentLocation();
	FRotator ViewRotation = Camera->GetComponentRotation();
	if (IsValid(PlayerController))
	{
		PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	}

	const FTransform ViewTransform(ViewRotation, ViewLocation);
	FVector ViewSpace = ViewTransform.InverseTransformPositionNoScale(WorldLocation) * Camera->FirstPersonScale;

	// 1인칭 전용 FOV를 쓰면 화면 투영도 달라지므로 시야각 비율만큼 가로·세로를 보정한다
	if (Camera->bEnableFirstPersonFieldOfView && IsValid(PlayerController) && IsValid(PlayerController->PlayerCameraManager))
	{
		const float WorldFOV = PlayerController->PlayerCameraManager->GetFOVAngle();
		const float FirstPersonFOV = Camera->FirstPersonFieldOfView;
		if (WorldFOV > UE_KINDA_SMALL_NUMBER && FirstPersonFOV > UE_KINDA_SMALL_NUMBER)
		{
			const float Ratio = FMath::Tan(FMath::DegreesToRadians(WorldFOV * 0.5f))
				/ FMath::Tan(FMath::DegreesToRadians(FirstPersonFOV * 0.5f));
			ViewSpace.Y *= Ratio;
			ViewSpace.Z *= Ratio;
		}
	}

	return ViewTransform.TransformPositionNoScale(ViewSpace);
}

void AMD_Character::SetLocalThirdPersonViewEnabled(bool bEnabled)
{
	if (bLocalThirdPersonViewEnabled == bEnabled || IsLocallyControlled() == false)
	{
		return;
	}
	bLocalThirdPersonViewEnabled = bEnabled;

	// 외부 카메라에서 자기 몸이 보이도록 OwnerNoSee를 풀고, 1인칭 팔·도구는 숨긴다
	const bool bOwnerNoSee = !bEnabled;
	if (IsValid(GetMesh()) == true)
	{
		GetMesh()->SetOwnerNoSee(bOwnerNoSee);
	}
	if (IsValid(ThirdPersonToolMesh) == true)
	{
		ThirdPersonToolMesh->SetOwnerNoSee(bOwnerNoSee);
	}
	if (IsValid(ThirdPersonCutterBlade) == true)
	{
		ThirdPersonCutterBlade->SetOwnerNoSee(bOwnerNoSee);
	}
	if (IsValid(FirstPersonMesh) == true)
	{
		FirstPersonMesh->SetHiddenInGame(bEnabled, false);
	}
	if (IsValid(FirstPersonToolMesh) == true)
	{
		FirstPersonToolMesh->SetHiddenInGame(bEnabled, false);
	}
	if (IsValid(FirstPersonCutterBlade) == true)
	{
		FirstPersonCutterBlade->SetHiddenInGame(bEnabled, false);
	}
}

void AMD_Character::BindAppearanceDelegate()
{
	AMD_PlayerState* MDPlayerState = GetPlayerState<AMD_PlayerState>();
	if (IsValid(MDPlayerState) == false)
	{
		return;
	}

	// 중복 등록 방지
	if (MDPlayerState->OnAppearanceChanged.IsAlreadyBound(this, &AMD_Character::HandleAppearanceChanged) == true)
	{
		return;
	}

	MDPlayerState->OnAppearanceChanged.AddDynamic(this, &AMD_Character::HandleAppearanceChanged);
}

void AMD_Character::UnbindAppearanceDelegate()
{
	AMD_PlayerState* MDPlayerState = GetPlayerState<AMD_PlayerState>();
	if (IsValid(MDPlayerState) == false)
	{
		return;
	}

	MDPlayerState->OnAppearanceChanged.RemoveDynamic(this, &AMD_Character::HandleAppearanceChanged);
}

void AMD_Character::HandleAppearanceChanged()
{
	ApplyAppearanceFromPlayerState();
}
