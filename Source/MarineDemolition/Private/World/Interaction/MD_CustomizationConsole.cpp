#include "World/Interaction/MD_CustomizationConsole.h"

#include "Camera/CameraComponent.h"
#include "Character/MD_Character.h"
#include "CommonActivatableWidget.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "GameplayTags/MD_GameplayTags.h"
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
	if (IsValid(LocalUIManager) == false || IsValid(SelectWidgetClass) == false)
	{
		return;
	}

	// 접근 중이거나 같은 선택 화면이 이미 열려 있으면 중복 진입을 막는다
	if (ActiveCharacter.IsValid() == true)
	{
		return;
	}
	UCommonActivatableWidget* ActiveGameMenuWidget =
		LocalUIManager->GetActiveScreen(MDGameplayTags::UI_Layer_GameMenu);
	if (IsValid(ActiveGameMenuWidget) == true && ActiveGameMenuWidget->IsA(SelectWidgetClass) == true)
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
	if (IsValid(LocalUIManager) == false || IsValid(SelectWidgetClass) == false)
	{
		return false;
	}

	UCommonActivatableWidget* PushedScreen = LocalUIManager->PushScreenToLayerInitialized(
		MDGameplayTags::UI_Layer_GameMenu,
		SelectWidgetClass,
		[this](UCommonActivatableWidget& Screen)
		{
			UMD_CharacterSelectWidget* SelectWidget = Cast<UMD_CharacterSelectWidget>(&Screen);
			if (IsValid(SelectWidget) == true)
			{
				SelectWidget->SetOwningConsole(this);
			}
		});
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
	SetActorTickEnabled(false);

	AMD_Character* Character = ActiveCharacter.Get();
	APlayerController* PC = IsValid(Character) == true ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (PushSelectWidget(PC) == false)
	{
		EndCustomization(Character);
	}
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
