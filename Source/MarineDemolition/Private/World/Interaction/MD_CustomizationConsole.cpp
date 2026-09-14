#include "World/Interaction/MD_CustomizationConsole.h"

#include "Camera/CameraComponent.h"
#include "Character/MD_Character.h"
#include "CommonActivatableWidget.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UI/Customization/MD_CharacterSelectWidget.h"
#include "UI/MD_LocalUIManager.h"

AMD_CustomizationConsole::AMD_CustomizationConsole()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);

	MirrorSurface = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MirrorSurface"));
	MirrorSurface->SetupAttachment(Mesh);
	MirrorSurface->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);

	// 기본 위치는 거울 앞 2m 뒤편 상단. 실제 값은 레벨 액터에서 잡는다
	ViewCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ViewCamera"));
	ViewCamera->SetupAttachment(Mesh);
	ViewCamera->SetRelativeLocation(FVector(-350.f, 0.f, 120.f));
	ViewCamera->bUsePawnControlRotation = false;

	// 거울 면에서 캐릭터 쪽(+X)을 본다. 평소엔 캡처하지 않는다
	MirrorCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("MirrorCapture"));
	MirrorCapture->SetupAttachment(MirrorSurface);
	MirrorCapture->bCaptureEveryFrame = false;
	MirrorCapture->bCaptureOnMovement = false;
	// 최종색(LDR) 캡처는 이 프로젝트의 수동 노출과 맞지 않아 검게 나온다. 씬 컬러를 받아 본 화면에서 톤매핑되게 둔다
	MirrorCapture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
	MirrorCapture->FOVAngle = 60.f;
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

	// 같은 선택 화면이 이미 열려 있으면 중복 Push를 막는다
	UCommonActivatableWidget* ActiveGameMenuWidget =
		LocalUIManager->GetActiveScreen(MDGameplayTags::UI_Layer_GameMenu);
	if (IsValid(ActiveGameMenuWidget) == true && ActiveGameMenuWidget->IsA(SelectWidgetClass) == true)
	{
		return;
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

	if (IsValid(PushedScreen) == true)
	{
		BeginCustomization(InstigatorCharacter);
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
	PlaceCameraBehind(Character);
	PC->SetViewTargetWithBlend(this, CameraBlendTime);
	Character->SetLocalThirdPersonViewEnabled(true);
	SetMirrorActive(true);
}

void AMD_CustomizationConsole::PlaceCameraBehind(const AMD_Character* Character)
{
	if (IsValid(ViewCamera) == false || IsValid(Character) == false)
	{
		return;
	}

	// 플레이어가 거울 앞 어디에 서 있든 뒷모습이 잡히도록 카메라를 플레이어 기준으로 놓는다
	const FVector MirrorLocation = IsValid(MirrorSurface) == true ? MirrorSurface->GetComponentLocation() : GetActorLocation();
	const FVector PlayerLocation = Character->GetActorLocation();
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
	const FVector LookAt = MirrorLocation + FVector(0.f, 0.f, CameraLookAtHeightOffset);
	ViewCamera->SetWorldLocationAndRotation(CameraLocation, (LookAt - CameraLocation).Rotation());
}

void AMD_CustomizationConsole::EndCustomization(AMD_Character* Character)
{
	AMD_Character* TargetCharacter = IsValid(Character) == true ? Character : ActiveCharacter.Get();
	ActiveCharacter.Reset();
	SetMirrorActive(false);

	if (IsValid(TargetCharacter) == false)
	{
		return;
	}

	TargetCharacter->SetLocalThirdPersonViewEnabled(false);

	APlayerController* PC = Cast<APlayerController>(TargetCharacter->GetController());
	if (IsValid(PC) == true && PC->IsLocalController() == true)
	{
		PC->SetViewTargetWithBlend(TargetCharacter, CameraBlendTime);
	}
}
