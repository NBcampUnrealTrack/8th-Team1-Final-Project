#include "World/Interaction/MD_CustomizationConsole.h"

#include "Camera/CameraComponent.h"
#include "Character/MD_Character.h"
#include "CommonActivatableWidget.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UI/Customization/MD_CharacterSelectWidget.h"
#include "UI/MD_LocalUIManager.h"

AMD_CustomizationConsole::AMD_CustomizationConsole()
{
	// 접근 중에만 틱을 켠다
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);

	MirrorSurface = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MirrorSurface"));
	MirrorSurface->SetupAttachment(Mesh);
	MirrorSurface->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);

	// 기본 위치는 거울 앞 2m 뒤편 상단. 실제 값은 레벨 액터에서 잡는다
	ViewCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ViewCamera"));
	ViewCamera->SetupAttachment(Mesh);
	// 기본 구도: 캐릭터 오른쪽 뒤·가슴 높이에서 몸과 거울을 함께 잡는다. 사용자가 PIE 유령 시점으로 확정한 값
	ViewCamera->SetRelativeLocation(FVector(490.f, -108.f, 134.f));
	ViewCamera->SetRelativeRotation(FRotator(-7.1f, 162.4f, 0.f));
	ViewCamera->bUsePawnControlRotation = false;
	ViewCamera->FieldOfView = 75.f;

	// 거울 면에서 캐릭터 쪽(+X)을 본다. 평소엔 캡처하지 않는다
	MirrorCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("MirrorCapture"));
	MirrorCapture->SetupAttachment(MirrorSurface);
	MirrorCapture->bCaptureEveryFrame = false;
	MirrorCapture->bCaptureOnMovement = false;
	// 최종색(LDR) 캡처는 이 프로젝트의 수동 노출과 맞지 않아 검게 나온다. 씬 컬러를 받아 본 화면에서 톤매핑되게 둔다
	MirrorCapture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
	// 세로로 긴 렌더타깃이라 가로 FOV를 좁혀야 거울 속 캐릭터가 작아지지 않는다
	MirrorCapture->FOVAngle = 45.f;

	// 거울 앞 1.8m, 거울 축에서 왼쪽(+Y)으로 0.7m 비켜 서서 거울(-X)을 바라본다. 사용자 PIE 확정값
	StandPoint = CreateDefaultSubobject<USceneComponent>(TEXT("StandPoint"));
	StandPoint->SetupAttachment(Mesh);
	StandPoint->SetRelativeLocation(FVector(178.f, 71.f, 0.f));
	StandPoint->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));
}

void AMD_CustomizationConsole::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bApproaching == true)
	{
		TickApproach(DeltaSeconds);
	}
	// 회전 키 또는 버튼이 눌린 동안에만 연속 회전 처리
	else if (FacingTurnInput != 0.f)
	{
		TickFacing(DeltaSeconds);
	}
}

void AMD_CustomizationConsole::BeginPlay()
{
	Super::BeginPlay();

	if (GetNetMode() != NM_DedicatedServer)
	{
		InitializeMirror();
	}
}

void AMD_CustomizationConsole::InitializeMirror()
{
	if (IsValid(MirrorSurface) == false || IsValid(MirrorCapture) == false)
	{
		return;
	}

	MirrorRenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("MirrorRenderTarget"));
	MirrorRenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA16f;
	MirrorRenderTarget->ClearColor = FLinearColor::Black;
	MirrorRenderTarget->InitAutoFormat(FMath::Max(MirrorResolution.X, 16), FMath::Max(MirrorResolution.Y, 16));
	MirrorRenderTarget->UpdateResourceImmediate(true);

	MirrorCapture->TextureTarget = MirrorRenderTarget;

	// 거울 면 머티리얼을 MID로 바꿔 렌더타깃을 꽂는다
	MirrorMaterialInstance = MirrorSurface->CreateDynamicMaterialInstance(0, MirrorMaterial);
	if (IsValid(MirrorMaterialInstance) == true)
	{
		MirrorMaterialInstance->SetTextureParameterValue(MirrorTextureParameterName, MirrorRenderTarget);
	}
}

void AMD_CustomizationConsole::SetMirrorActive(bool bActive)
{
	if (IsValid(MirrorCapture) == false)
	{
		return;
	}

	MirrorCapture->bCaptureEveryFrame = bActive;
	if (bActive == true)
	{
		MirrorCapture->CaptureScene();
	}
	else if (IsValid(MirrorRenderTarget) == true)
	{
		// 닫으면 마지막 캡처가 남지 않도록 거울을 검게 되돌린다
		UKismetRenderingLibrary::ClearRenderTarget2D(this, MirrorRenderTarget, MirrorRenderTarget->ClearColor);
	}
}

void AMD_CustomizationConsole::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
	if (IsValid(Mesh) == true)
	{
		Mesh->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue);
		Mesh->SetRenderCustomDepth(bHighlighted);
	}
	if (IsValid(MirrorSurface) == true)
	{
		MirrorSurface->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue);
		MirrorSurface->SetRenderCustomDepth(bHighlighted);
	}
}

FText AMD_CustomizationConsole::GetInteractionDisplayName_Implementation() const
{
	return NSLOCTEXT("MDCustomization", "ConsoleName", "거울");
}

void AMD_CustomizationConsole::Interact_Implementation(AActor* Interactor)
{
	AMD_Character* InstigatorCharacter = Cast<AMD_Character>(Interactor);
	if (IsValid(InstigatorCharacter) == false)
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(InstigatorCharacter->GetController());
	if (IsValid(PC) == false || PC->IsLocalController() == false)
	{
		return;
	}

	ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
	UMD_LocalUIManager* LocalUIManager =
		IsValid(LocalPlayer) == true ? LocalPlayer->GetSubsystem<UMD_LocalUIManager>() : nullptr;
	// 태그 요청을 처리할 로컬 UI 관리자와 화면 태그 확인
	if (IsValid(LocalUIManager) == false || ConsoleWidgetTag.IsValid() == false)
	{
		return;
	}

	// 접근 중이거나 같은 선택 화면이 이미 열려 있으면 중복 진입을 막는다
	if (ActiveCharacter.IsValid() == true)
	{
		return;
	}
	// 화면 태그에 등록된 부모 레이어와 소프트 클래스 조회
	FGameplayTag LayerTag;
	// 콘솔이 직접 보관하지 않는 UI 설정의 등록 클래스
	TSoftClassPtr<UCommonActivatableWidget> RegisteredWidgetClass;
	// 등록되지 않은 화면의 카메라 전환과 접근 시작 차단
	if (LocalUIManager->ResolveWidgetRegistration(ConsoleWidgetTag, LayerTag, RegisteredWidgetClass) == false)
	{
		return;
	}
	// 태그에서 결정된 레이어의 현재 화면 조회
	UCommonActivatableWidget* ActiveScreen = LocalUIManager->GetActiveScreen(LayerTag);
	// 동일한 등록 화면이 이미 열린 경우 중복 접근 차단
	if (IsValid(ActiveScreen) == true && ActiveScreen->GetClass() == RegisteredWidgetClass.Get())
	{
		return;
	}

	BeginApproach(InstigatorCharacter, PC);
}

bool AMD_CustomizationConsole::PushSelectWidget(APlayerController* PC)
{
	ULocalPlayer* LocalPlayer = IsValid(PC) == true ? PC->GetLocalPlayer() : nullptr;
	UMD_LocalUIManager* LocalUIManager =
		IsValid(LocalPlayer) == true ? LocalPlayer->GetSubsystem<UMD_LocalUIManager>() : nullptr;
	// 태그 요청을 처리할 로컬 UI 관리자와 화면 태그 확인
	if (IsValid(LocalUIManager) == false || ConsoleWidgetTag.IsValid() == false)
	{
		return false;
	}

	// 접근 완료 시점에 화면 태그의 최신 레이어 등록 조회
	FGameplayTag LayerTag;
	// UI 설정에서 선택된 소프트 클래스의 동기 로드 준비
	TSoftClassPtr<UCommonActivatableWidget> RegisteredWidgetClass;
	// 접근 중 레이어나 등록이 사라진 경우 화면 생성 중단
	if (LocalUIManager->ResolveWidgetRegistration(ConsoleWidgetTag, LayerTag, RegisteredWidgetClass) == false)
	{
		return false;
	}
	// 콘솔의 직접 에셋 참조 없이 등록된 화면 클래스 로드
	UClass* ScreenClass = RegisteredWidgetClass.LoadSynchronous();
	// 콘솔 회전과 닫기 복구를 지원하는 선택 화면인지 확인
	if (IsValid(ScreenClass) == false || ScreenClass->IsChildOf(UMD_CharacterSelectWidget::StaticClass()) == false)
	{
		return false;
	}
	// 태그에서 결정된 레이어에 활성화 전 초기화를 적용하여 화면 생성
	UCommonActivatableWidget* PushedScreen = LocalUIManager->PushScreenToLayerInitialized(LayerTag, ScreenClass,
		[this](UCommonActivatableWidget& Screen)
		{
			// 등록 클래스에서 보장한 선택 화면의 콘솔 연결 대상 조회
			UMD_CharacterSelectWidget* SelectWidget = Cast<UMD_CharacterSelectWidget>(&Screen);
			// 활성화 전에 현재 콘솔의 카메라 복구와 회전 경로 연결
			if (IsValid(SelectWidget) == true)
			{
				// 화면을 연 콘솔을 선택 위젯에 전달
				SelectWidget->SetOwningConsole(this);
			}
		});
	// 생성 실패 시 기존 접근 종료 경로에서 카메라와 입력 복구
	return IsValid(PushedScreen) == true;
}

void AMD_CustomizationConsole::BeginApproach(AMD_Character* Character, APlayerController* PC)
{
	// 카메라는 먼저 넘겨 걸어가는 뒷모습이 보이게 하고, 플레이어 입력은 도착할 때까지 막는다
	BeginCustomization(Character);
	if (ActiveCharacter.IsValid() == false)
	{
		return;
	}

	Character->DisableInput(PC);
	bApproaching = true;
	bReachedStandPoint = false;
	ApproachElapsed = 0.f;
	SetActorTickEnabled(true);
}

void AMD_CustomizationConsole::TickApproach(float DeltaSeconds)
{
	AMD_Character* Character = ActiveCharacter.Get();
	APlayerController* PC = IsValid(Character) == true ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (IsValid(Character) == false || IsValid(PC) == false || IsValid(StandPoint) == false)
	{
		EndCustomization(Character);
		return;
	}

	ApproachElapsed += DeltaSeconds;

	FVector ToTarget = StandPoint->GetComponentLocation() - Character->GetActorLocation();
	ToTarget.Z = 0.f;
	const float Distance = ToTarget.Size();
	// 한 번 도착하면 제동 중 반경을 벗어나도 다시 걷지 않는다
	if (bReachedStandPoint == false && (Distance <= ApproachAcceptRadius || ApproachElapsed >= ApproachTimeout))
	{
		bReachedStandPoint = true;
	}

	// 걷는 동안은 진행 방향을, 도착 후엔 StandPoint의 정면(거울)을 바라본다
	float TargetYaw = StandPoint->GetComponentRotation().Yaw;
	if (bReachedStandPoint == false)
	{
		const FVector Direction = ToTarget / Distance;
		Character->AddMovementInput(Direction, ApproachInputScale);
		TargetYaw = Direction.Rotation().Yaw;
	}

	const FRotator Desired(0.f, TargetYaw, 0.f);
	const FRotator NewRotation = FMath::RInterpTo(PC->GetControlRotation(), Desired, DeltaSeconds, ApproachTurnInterpSpeed);
	PC->SetControlRotation(NewRotation);

	if (bReachedStandPoint == true
		&& FMath::Abs(FRotator::NormalizeAxis(NewRotation.Yaw - TargetYaw)) <= 2.f
		&& FMath::Abs(FRotator::NormalizeAxis(NewRotation.Pitch)) <= 2.f)
	{
		PC->SetControlRotation(Desired);
		FinishApproach();
	}
}

void AMD_CustomizationConsole::FinishApproach()
{
	bApproaching = false;
	// 접근 완료 또는 화면 종료 시 이전 회전 입력 초기화
	FacingTurnInput = 0.f;
	SetActorTickEnabled(false);

	AMD_Character* Character = ActiveCharacter.Get();
	APlayerController* PC = IsValid(Character) == true ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (PushSelectWidget(PC) == false)
	{
		EndCustomization(Character);
	}
}

void AMD_CustomizationConsole::TurnFacing(float TurnInput)
{
	// 입력 장치와 무관하게 동일한 좌우 회전 범위 적용
	FacingTurnInput = FMath::Clamp(TurnInput, -1.f, 1.f);
	// 선택 대상이 없거나 접근 중인 상태의 회전 입력 차단
	if (IsValid(ActiveCharacter.Get()) == false || bApproaching == true)
	{
		// 적용할 수 없는 회전 입력 해제
		FacingTurnInput = 0.f;
	}
	// 접근 연출 또는 눌린 회전 입력이 있는 동안만 틱 유지
	SetActorTickEnabled(bApproaching == true || FacingTurnInput != 0.f);
}

void AMD_CustomizationConsole::TickFacing(float DeltaSeconds)
{
	// 현재 외형 선택 중인 캐릭터 조회
	AMD_Character* Character = ActiveCharacter.Get();
	// 연속 회전을 적용할 소유 컨트롤러 조회
	APlayerController* PC = IsValid(Character) == true ? Cast<APlayerController>(Character->GetController()) : nullptr;
	// 회전 대상이 사라진 경우 입력과 틱 정리
	if (IsValid(Character) == false || IsValid(PC) == false)
	{
		// 이후 선택에 남지 않도록 회전 입력 초기화
		FacingTurnInput = 0.f;
		// 불필요한 회전 틱 중단
		SetActorTickEnabled(false);
		return;
	}
	// 프레임률과 무관하게 누른 시간과 설정 속도만큼 방향 갱신
	PC->SetControlRotation(FRotator(0.f, FRotator::NormalizeAxis(PC->GetControlRotation().Yaw
		+ FacingTurnInput * FMath::Max(0.f, FacingTurnSpeed) * DeltaSeconds), 0.f));
}

void AMD_CustomizationConsole::BeginCustomization(AMD_Character* Character)
{
	if (IsValid(Character) == false || Character->IsLocallyControlled() == false)
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(Character->GetController());
	if (IsValid(PC) == false)
	{
		return;
	}

	ActiveCharacter = Character;
	if (bPlaceCameraBehindPlayer == true)
	{
		PlaceCameraBehind(Character);
	}
	AimMirrorCaptureAtStandPoint();
	PC->SetViewTargetWithBlend(this, CameraBlendTime);
	Character->SetLocalThirdPersonViewEnabled(true);
	SetMirrorActive(true);
}

void AMD_CustomizationConsole::AimMirrorCaptureAtStandPoint()
{
	if (IsValid(MirrorCapture) == false || IsValid(StandPoint) == false)
	{
		return;
	}

	const FVector LookAt = StandPoint->GetComponentLocation() + FVector(0.f, 0.f, MirrorCaptureLookAtHeight);
	MirrorCapture->SetWorldRotation((LookAt - MirrorCapture->GetComponentLocation()).Rotation());
}

void AMD_CustomizationConsole::PlaceCameraBehind(const AMD_Character* Character)
{
	if (IsValid(ViewCamera) == false || IsValid(Character) == false)
	{
		return;
	}

	// 캐릭터가 걸어가 설 StandPoint를 기준으로 뒷모습이 잡히도록 카메라를 놓는다
	const FVector MirrorLocation = IsValid(MirrorSurface) == true ? MirrorSurface->GetComponentLocation() : GetActorLocation();
	FVector PlayerLocation = Character->GetActorLocation();
	if (IsValid(StandPoint) == true)
	{
		PlayerLocation.X = StandPoint->GetComponentLocation().X;
		PlayerLocation.Y = StandPoint->GetComponentLocation().Y;
	}
	FVector AwayFromMirror = (PlayerLocation - MirrorLocation).GetSafeNormal2D();
	if (AwayFromMirror.IsNearlyZero() == true)
	{
		AwayFromMirror = GetActorForwardVector();
	}

	// 플레이어가 거울을 볼 때의 오른쪽. 이만큼 비껴야 거울 속 앞모습이 뒷모습에 가리지 않는다
	const FVector ViewerRight = FVector(AwayFromMirror.Y, -AwayFromMirror.X, 0.f);

	const float FootZ = PlayerLocation.Z - Character->GetDefaultHalfHeight();
	const FVector CameraLocation = PlayerLocation + AwayFromMirror * CameraDistanceBehindPlayer + ViewerRight * CameraSideOffset
		+ FVector(0.f, 0.f, FootZ + CameraHeight - PlayerLocation.Z);
	// 거울 중심과 StandPoint 사이를 바라봐 캐릭터 뒷모습(왼쪽)과 거울 속 앞모습(오른쪽)이 한 화면에 나뉘게 한다
	FVector LookAt = FMath::Lerp(MirrorLocation, PlayerLocation, CameraLookAtBlend);
	LookAt.Z = MirrorLocation.Z + CameraLookAtHeightOffset;
	ViewCamera->SetWorldLocationAndRotation(CameraLocation, (LookAt - CameraLocation).Rotation());
}

void AMD_CustomizationConsole::EndCustomization(AMD_Character* Character)
{
	AMD_Character* TargetCharacter = IsValid(Character) == true ? Character : ActiveCharacter.Get();
	ActiveCharacter.Reset();
	bApproaching = false;
	bReachedStandPoint = false;
	// 접근 완료 또는 화면 종료 시 이전 회전 입력 초기화
	FacingTurnInput = 0.f;
	SetActorTickEnabled(false);
	SetMirrorActive(false);

	if (IsValid(TargetCharacter) == false)
	{
		return;
	}

	TargetCharacter->SetLocalThirdPersonViewEnabled(false);

	APlayerController* PC = Cast<APlayerController>(TargetCharacter->GetController());
	if (IsValid(PC) == true && PC->IsLocalController() == true)
	{
		TargetCharacter->EnableInput(PC);
		PC->SetViewTargetWithBlend(TargetCharacter, CameraBlendTime);
	}
}
