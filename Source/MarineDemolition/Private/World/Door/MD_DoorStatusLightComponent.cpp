#include "World/Door/MD_DoorStatusLightComponent.h"

#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

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

	bWasLocked = IsDoorLocked();
	bFaultLightOn = bWasLocked;
	ApplyEmissiveColor(bWasLocked ? FaultColor : NormalEmissiveColor);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			BlinkTimerHandle,
			this,
			&UMD_DoorStatusLightComponent::RefreshBlinkState,
			FMath::Max(BlinkIntervalSeconds, 0.1f),
			true);
	}
}

void UMD_DoorStatusLightComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BlinkTimerHandle);
	}

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

bool UMD_DoorStatusLightComponent::IsDoorLocked() const
{
	const AActor* Owner = GetOwner();
	if (!IsValid(Owner))
	{
		return false;
	}

	const FBoolProperty* LockedProperty = CastField<FBoolProperty>(
		Owner->GetClass()->FindPropertyByName(LockedPropertyName));
	return LockedProperty != nullptr
		&& LockedProperty->GetPropertyValue_InContainer(Owner);
}

void UMD_DoorStatusLightComponent::RefreshBlinkState()
{
	if (!IsValid(DisplayMaterial))
	{
		return;
	}

	const bool bLocked = IsDoorLocked();
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

	if (!bWasLocked)
	{
		bWasLocked = true;
		bFaultLightOn = true;
		ApplyEmissiveColor(FaultColor);
		return;
	}

	bFaultLightOn = !bFaultLightOn;
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
