#include "Component/Customization/MD_CharacterCustomizationComponent.h"

#include "Gameplay/Customization/MD_CharacterCustomizationSubsystem.h"
#include "Components/MeshComponent.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture.h"

UMD_CharacterCustomizationComponent::UMD_CharacterCustomizationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UMD_CharacterCustomizationComponent::BeginPlay()
{
	Super::BeginPlay();

	CurrentAppearance.MigrateLegacyFieldsIfNeeded();
	if (CurrentAppearance.ExpressionTexturePNG.Num() > 0)
	{
		ApplyAppearance(CurrentAppearance);
	}
}

bool UMD_CharacterCustomizationComponent::ApplyAppearance(const FMDCharacterCustomizationAppearance& Appearance)
{
	FMDCharacterCustomizationAppearance MigratedAppearance = Appearance;
	MigratedAppearance.MigrateLegacyFieldsIfNeeded();
	CurrentAppearance = MigratedAppearance;
	ApplyPartColors(CurrentAppearance);

	if (CurrentAppearance.ExpressionTexturePNG.Num() > 0)
	{
		if (UMD_CharacterCustomizationSubsystem* Subsystem = UMD_CharacterCustomizationSubsystem::Get(this))
		{
			ReleaseRuntimeExpressionTexture();
			RuntimeExpressionTexture = Subsystem->CreateFaceTextureFromPNG(
				CurrentAppearance.ExpressionTexturePNG,
				FString::Printf(TEXT("MD_RuntimeExpression_%s"), *GetName()));
			ApplyExpressionTexture(RuntimeExpressionTexture);
		}
	}

	bool bAnyApplied = false;
	for (UMeshComponent* Mesh : GatherTargetMeshes())
	{
		bAnyApplied |= ApplyToMesh(Mesh);
	}

	return bAnyApplied;
}

void UMD_CharacterCustomizationComponent::ApplyPartColors(const FMDCharacterCustomizationAppearance& Appearance)
{
	CurrentAppearance.VisorColor = Appearance.VisorColor;
	CurrentAppearance.FaceColor = Appearance.FaceColor;
	CurrentAppearance.TorsoColor = Appearance.TorsoColor;
	CurrentAppearance.HandsColor = Appearance.HandsColor;
	CurrentAppearance.RimColor = Appearance.RimColor;

	for (UMeshComponent* Mesh : GatherTargetMeshes())
	{
		if (UMaterialInstanceDynamic* DynamicMaterial = GetOrCreateDynamicMaterial(Mesh))
		{
			ApplyParametersToMaterial(DynamicMaterial);
		}
	}
}

void UMD_CharacterCustomizationComponent::ApplyPartColor(EMDCharacterCustomizationPart Part, FLinearColor NewColor)
{
	CurrentAppearance.SetPartColor(Part, NewColor);

	for (UMeshComponent* Mesh : GatherTargetMeshes())
	{
		if (UMaterialInstanceDynamic* DynamicMaterial = GetOrCreateDynamicMaterial(Mesh))
		{
			DynamicMaterial->SetVectorParameterValue(GetParameterNameForPart(Part), NewColor);
		}
	}
}

void UMD_CharacterCustomizationComponent::ApplyExpressionTexture(UTexture* ExpressionTexture)
{
	RuntimeExpressionTexture = ExpressionTexture;
	const float Blend = ExpressionTexture ? 1.f : 0.f;
	for (UMeshComponent* Mesh : GatherTargetMeshes())
	{
		if (UMaterialInstanceDynamic* DynamicMaterial = GetOrCreateDynamicMaterial(Mesh))
		{
			if (ExpressionTexture)
			{
				DynamicMaterial->SetTextureParameterValue(ExpressionTextureParameter, ExpressionTexture);
			}
			DynamicMaterial->SetScalarParameterValue(ExpressionBlendParameter, Blend);
		}
	}
}

void UMD_CharacterCustomizationComponent::SetCurrentAppearance(const FMDCharacterCustomizationAppearance& Appearance)
{
	CurrentAppearance = Appearance;
}

bool UMD_CharacterCustomizationComponent::ApplyToMesh(UMeshComponent* MeshComponent)
{
	if (!MeshComponent)
	{
		return false;
	}

	UMaterialInstanceDynamic* DynamicMaterial = GetOrCreateDynamicMaterial(MeshComponent);
	if (!DynamicMaterial)
	{
		return false;
	}

	ApplyParametersToMaterial(DynamicMaterial);
	if (RuntimeExpressionTexture)
	{
		DynamicMaterial->SetTextureParameterValue(ExpressionTextureParameter, RuntimeExpressionTexture);
		DynamicMaterial->SetScalarParameterValue(ExpressionBlendParameter, 1.f);
	}
	else
	{
		DynamicMaterial->SetScalarParameterValue(ExpressionBlendParameter, 0.f);
	}

	return true;
}

UMaterialInstanceDynamic* UMD_CharacterCustomizationComponent::GetOrCreateDynamicMaterial(UMeshComponent* MeshComponent)
{
	if (!MeshComponent)
	{
		return nullptr;
	}

	if (TObjectPtr<UMaterialInstanceDynamic>* Existing = DynamicMaterials.Find(MeshComponent))
	{
		if (*Existing)
		{
			return Existing->Get();
		}
	}

	UMaterialInterface* SourceMaterial = MeshComponent->GetMaterial(MaterialSlotIndex);
	if (!SourceMaterial || !SourceMaterial->GetMaterial())
	{
		return nullptr;
	}

	UMaterialInstanceDynamic* DynamicMaterial = MeshComponent->CreateDynamicMaterialInstance(MaterialSlotIndex, SourceMaterial);
	if (DynamicMaterial)
	{
		DynamicMaterials.Add(MeshComponent, DynamicMaterial);
	}

	return DynamicMaterial;
}

void UMD_CharacterCustomizationComponent::ApplyParametersToMaterial(UMaterialInstanceDynamic* DynamicMaterial)
{
	if (!DynamicMaterial)
	{
		return;
	}

	DynamicMaterial->SetVectorParameterValue(VisorColorParameter, CurrentAppearance.VisorColor);
	DynamicMaterial->SetVectorParameterValue(FaceColorParameter, CurrentAppearance.FaceColor);
	DynamicMaterial->SetVectorParameterValue(TorsoColorParameter, CurrentAppearance.TorsoColor);
	DynamicMaterial->SetVectorParameterValue(HandsColorParameter, CurrentAppearance.HandsColor);
	DynamicMaterial->SetVectorParameterValue(RimColorParameter, CurrentAppearance.RimColor);
	// Keep FaceColor visible until an expression texture is actually applied.
	if (!RuntimeExpressionTexture)
	{
		DynamicMaterial->SetScalarParameterValue(ExpressionBlendParameter, 0.f);
	}
}

void UMD_CharacterCustomizationComponent::ReleaseRuntimeExpressionTexture()
{
	RuntimeExpressionTexture = nullptr;
}

TArray<UMeshComponent*> UMD_CharacterCustomizationComponent::GatherTargetMeshes() const
{
	TArray<UMeshComponent*> MeshesToUpdate;
	if (bApplyToAllMeshComponents && GetOwner())
	{
		GetOwner()->GetComponents<UMeshComponent>(MeshesToUpdate);
	}
	else
	{
		for (UMeshComponent* Mesh : TargetMeshes)
		{
			if (Mesh)
			{
				MeshesToUpdate.Add(Mesh);
			}
		}
	}

	return MeshesToUpdate;
}

FName UMD_CharacterCustomizationComponent::GetParameterNameForPart(EMDCharacterCustomizationPart Part) const
{
	switch (Part)
	{
	case EMDCharacterCustomizationPart::Visor: return VisorColorParameter;
	case EMDCharacterCustomizationPart::Face: return FaceColorParameter;
	case EMDCharacterCustomizationPart::Torso: return TorsoColorParameter;
	case EMDCharacterCustomizationPart::Hands: return HandsColorParameter;
	case EMDCharacterCustomizationPart::Rim: return RimColorParameter;
	default: return NAME_None;
	}
}
