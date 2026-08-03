#include "Types/Customization/MD_CharacterCustomizationTypes.h"

FLinearColor FMDCharacterCustomizationAppearance::GetPartColor(EMDCharacterCustomizationPart Part) const
{
	switch (Part)
	{
	case EMDCharacterCustomizationPart::Visor: return VisorColor;
	case EMDCharacterCustomizationPart::Face: return FaceColor;
	case EMDCharacterCustomizationPart::Torso: return TorsoColor;
	case EMDCharacterCustomizationPart::Hands: return HandsColor;
	case EMDCharacterCustomizationPart::Rim: return RimColor;
	default: return FLinearColor::White;
	}
}

void FMDCharacterCustomizationAppearance::SetPartColor(EMDCharacterCustomizationPart Part, FLinearColor NewColor)
{
	switch (Part)
	{
	case EMDCharacterCustomizationPart::Visor: VisorColor = NewColor; break;
	case EMDCharacterCustomizationPart::Face: FaceColor = NewColor; break;
	case EMDCharacterCustomizationPart::Torso: TorsoColor = NewColor; break;
	case EMDCharacterCustomizationPart::Hands: HandsColor = NewColor; break;
	case EMDCharacterCustomizationPart::Rim: RimColor = NewColor; break;
	default: break;
	}
}

void FMDCharacterCustomizationAppearance::MigrateLegacyFieldsIfNeeded()
{
	if (ExpressionTexturePNG.Num() == 0 && FaceTexturePNG.Num() > 0)
	{
		ExpressionTexturePNG = FaceTexturePNG;
	}

	const bool bHasLegacyPrimary = !SuitPrimaryColor.Equals(FLinearColor(0.92f, 0.94f, 0.96f, 1.f));
	const bool bHasLegacyAccent = !SuitAccentColor.Equals(FLinearColor(0.12f, 0.42f, 0.88f, 1.f));

	if (bHasLegacyPrimary)
	{
		TorsoColor = SuitPrimaryColor;
		HandsColor = SuitPrimaryColor;
	}

	if (bHasLegacyAccent)
	{
		RimColor = SuitAccentColor;
	}

	// Old Feet swatch folded into Hands when Feet was customized away from default.
	const FLinearColor DefaultFeet(0.92f, 0.94f, 0.96f, 1.f);
	if (!FeetColor.Equals(DefaultFeet) && HandsColor.Equals(DefaultFeet))
	{
		HandsColor = FeetColor;
	}
}
