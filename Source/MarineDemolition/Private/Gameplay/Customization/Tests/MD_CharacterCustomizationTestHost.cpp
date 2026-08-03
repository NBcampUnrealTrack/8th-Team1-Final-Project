#include "Gameplay/Customization/Tests/MD_CharacterCustomizationTestHost.h"

#include "Blueprint/UserWidget.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Component/Customization/MD_CharacterCustomizationComponent.h"
#include "Gameplay/Customization/MD_CharacterCustomizationSettings.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "TimerManager.h"
#include "UI/Customization/MD_CharacterCustomizationWidget.h"

AMD_CharacterCustomizationTestHost::AMD_CharacterCustomizationTestHost()
{
	PrimaryActorTick.bCanEverTick = false;

	CharacterMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CharacterMesh"));
	SetRootComponent(CharacterMesh);
	CharacterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CharacterMesh->SetCastShadow(true);
	CharacterMesh->SetReceivesDecals(false);
	CharacterMesh->bVisibleInRayTracing = false;
	CharacterMesh->bCastContactShadow = false;
	CharacterMesh->SetCastInsetShadow(false);

	CustomizationComponent = CreateDefaultSubobject<UMD_CharacterCustomizationComponent>(TEXT("CustomizationComponent"));

	KeyLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("KeyLight"));
	KeyLight->SetupAttachment(CharacterMesh);
	KeyLight->SetRelativeLocation(FVector(-120.f, 80.f, 160.f));
	KeyLight->SetIntensity(2500.f);
	KeyLight->SetAttenuationRadius(800.f);
	KeyLight->SetCastShadows(false);
	KeyLight->SetCastRaytracedShadow(false);
}

void AMD_CharacterCustomizationTestHost::BeginPlay()
{
	Super::BeginPlay();

	if (const UMD_CharacterCustomizationSettings* Settings = UMD_CharacterCustomizationSettings::Get())
	{
		if (!Settings->CustomizationWidgetClass.IsNull())
		{
			CustomizationWidgetClass = Settings->CustomizationWidgetClass;
		}
		if (!Settings->PreviewSkeletalMesh.IsNull())
		{
			CharacterMeshAsset = Settings->PreviewSkeletalMesh;
		}
		if (!Settings->PreviewMaterial.IsNull())
		{
			CharacterMaterial = Settings->PreviewMaterial;
		}
		bOpenUIOnBeginPlay = Settings->bOpenUIOnBeginPlay;
		bUseUIOnlyInput = Settings->bUseUIOnlyInput;
		WidgetZOrder = Settings->WidgetZOrder;
	}

	ApplyDefaultAssets();

	// Test level host always opens the customization UI (settings flag is for other spawners).
	FTimerHandle TimerHandle;
	GetWorldTimerManager().SetTimer(
		TimerHandle,
		this,
		&AMD_CharacterCustomizationTestHost::OpenCustomizationUI,
		1.0f,
		false);
}

void AMD_CharacterCustomizationTestHost::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearAllTimersForObject(this);
	}

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		PC->SetIgnoreLookInput(false);
		PC->SetIgnoreMoveInput(false);
		PC->SetInputMode(FInputModeGameOnly());
	}

	if (ActiveWidget)
	{
		ActiveWidget->RemoveFromParent();
		ActiveWidget = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void AMD_CharacterCustomizationTestHost::ApplyDefaultAssets()
{
	if (!CharacterMesh)
	{
		return;
	}

	CharacterMesh->bVisibleInRayTracing = false;

	if (USkeletalMesh* Mesh = CharacterMeshAsset.LoadSynchronous())
	{
		CharacterMesh->SetSkeletalMesh(Mesh);
	}

	if (UMaterialInterface* Material = CharacterMaterial.LoadSynchronous())
	{
		if (Material->GetMaterial())
		{
			CharacterMesh->SetMaterial(0, Material);
		}
	}
}

void AMD_CharacterCustomizationTestHost::OpenCustomizationUI()
{
	if (ActiveWidget || !IsValid(this))
	{
		return;
	}

	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC)
	{
		UE_LOG(LogTemp, Warning, TEXT("MD Customization TestHost: no PlayerController yet — retrying"));
		FTimerHandle RetryHandle;
		GetWorldTimerManager().SetTimer(
			RetryHandle,
			this,
			&AMD_CharacterCustomizationTestHost::OpenCustomizationUI,
			0.5f,
			false);
		return;
	}

	if (CustomizationWidgetClass.IsNull())
	{
		if (const UMD_CharacterCustomizationSettings* Settings = UMD_CharacterCustomizationSettings::Get())
		{
			CustomizationWidgetClass = Settings->CustomizationWidgetClass;
		}
	}

	UClass* WidgetClass = CustomizationWidgetClass.LoadSynchronous();
	if (!WidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("MD Customization TestHost: missing widget class %s"),
			*CustomizationWidgetClass.ToSoftObjectPath().ToString());
		return;
	}

	ActiveWidget = CreateWidget<UMD_CharacterCustomizationWidget>(PC, WidgetClass);
	if (!ActiveWidget)
	{
		return;
	}

	ActiveWidget->SetAutoSpawnPreviewActor(true);
	ActiveWidget->InitializeCustomization(CustomizationComponent);
	ActiveWidget->AddToViewport(WidgetZOrder);

	PC->bShowMouseCursor = true;
	PC->SetIgnoreLookInput(true);
	PC->SetIgnoreMoveInput(true);
	if (bUseUIOnlyInput)
	{
		// Must be UIOnly — GameAndUI still lets the viewport camera eat drag when paint misses.
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(ActiveWidget->TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(InputMode);
	}
	else
	{
		FInputModeGameAndUI InputMode;
		InputMode.SetWidgetToFocus(ActiveWidget->TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(InputMode);
	}
}
