#include "World/Door/MD_DoorStatusLightComponent.h"

#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "TimerManager.h"
#include "World/Door/MD_DoorFaultComponent.h"
#include "GameFramework/GameStateBase.h"

UMD_DoorStatusLightComponent::UMD_DoorStatusLightComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UMD_DoorStatusLightComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!InitializeDisplayMaterial())
	{
		return;
	}

	bWasLocked = IsDoorFaultActive();
	bFaultLightOn = bWasLocked;
	ApplyEmissiveColor(bWasLocked ? FaultColor : NormalEmissiveColor);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			BlinkTimerHandle,
			this,
			&UMD_DoorStatusLightComponent::RefreshBlinkState,
			0.1f,
			true);
	}
}

void UMD_DoorStatusLightComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BlinkTimerHandle);
	}

	if (BoundFault.IsValid()) BoundFault->OnStateChanged.RemoveDynamic(this, &ThisClass::HandleFaultChanged);
	RestoreNormalColor();
	Super::EndPlay(EndPlayReason);
}

bool UMD_DoorStatusLightComponent::InitializeDisplayMaterial()
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner))
	{
		return false;
	}

	TArray<UStaticMeshComponent*> MeshComponents;
	Owner->GetComponents<UStaticMeshComponent>(MeshComponents);

	for (UStaticMeshComponent* Component : MeshComponents)
	{
		if (IsValid(Component) && Component->GetFName() == DisplayComponentName)
		{
			DisplayMesh = Component;
			break;
		}
	}

	if (!IsValid(DisplayMesh))
	{
		UE_LOG(LogTemp, Warning, TEXT("[DoorStatusLight] %s: Display component '%s' not found"),
			*Owner->GetName(), *DisplayComponentName.ToString());
		return false;
	}

	int32 MaterialIndex = DisplayMesh->GetMaterialIndex(DisplayMaterialSlotName);
	if (MaterialIndex == INDEX_NONE)
	{
		for (int32 Index = 0; Index < DisplayMesh->GetNumMaterials(); ++Index)
		{
			const UMaterialInterface* Material = DisplayMesh->GetMaterial(Index);
			if (IsValid(Material) && Material->GetName().Contains(TEXT("MI_Light_A")))
			{
				MaterialIndex = Index;
				break;
			}
		}
	}

	if (MaterialIndex == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DoorStatusLight] %s: light material slot '%s' not found"),
			*Owner->GetName(), *DisplayMaterialSlotName.ToString());
		return false;
	}

	DisplayMaterial = DisplayMesh->CreateAndSetMaterialInstanceDynamic(MaterialIndex);
	if (!IsValid(DisplayMaterial))
	{
		return false;
	}

	NormalEmissiveColor = DisplayMaterial->K2_GetVectorParameterValue(EmissiveColorParameterName);
	return true;
}

bool UMD_DoorStatusLightComponent::IsDoorFaultActive() const
{
	const UMD_DoorFaultComponent* Fault = IsValid(GetOwner()) ? GetOwner()->FindComponentByClass<UMD_DoorFaultComponent>() : nullptr;
	return Fault && Fault->IsFaultActive();
}

void UMD_DoorStatusLightComponent::HandleFaultChanged(UMD_FacilityFaultComponent*, const FMD_FacilitySnapshot&, const FMD_FacilitySnapshot&)
{
	RefreshBlinkState();
}

void UMD_DoorStatusLightComponent::RefreshBlinkState()
{
	if (!IsValid(DisplayMaterial))
	{
		return;
	}

	UMD_DoorFaultComponent* Fault = GetOwner()->FindComponentByClass<UMD_DoorFaultComponent>();
	if (BoundFault.Get() != Fault)
	{
		if (BoundFault.IsValid()) BoundFault->OnStateChanged.RemoveDynamic(this, &ThisClass::HandleFaultChanged);
		BoundFault = Fault;
		if (Fault) Fault->OnStateChanged.AddUniqueDynamic(this, &ThisClass::HandleFaultChanged);
	}
	const bool bLocked = IsDoorFaultActive();
	if (!bLocked)
	{
		if (bWasLocked)
		{
			RestoreNormalColor();
		}
		bWasLocked = false;
		bFaultLightOn = false;
		return;
	}

	bWasLocked = true;
	const AGameStateBase* GS = GetWorld()->GetGameState();
	const double Now = GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	const double Elapsed = FMath::Max(0.0, Now - Fault->GetSnapshot().FaultStartedAt);
	bFaultLightOn = (static_cast<int64>(Elapsed / FMath::Max(0.1f, BlinkIntervalSeconds)) % 2) == 0;
	ApplyEmissiveColor(bFaultLightOn ? FaultColor : FLinearColor::Black);
}

void UMD_DoorStatusLightComponent::ApplyEmissiveColor(const FLinearColor& Color)
{
	if (IsValid(DisplayMaterial))
	{
		DisplayMaterial->SetVectorParameterValue(EmissiveColorParameterName, Color);
	}
}

void UMD_DoorStatusLightComponent::RestoreNormalColor()
{
	ApplyEmissiveColor(NormalEmissiveColor);
}
