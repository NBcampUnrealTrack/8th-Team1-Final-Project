#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Customization/MD_CharacterCustomizationTypes.h"
#include "MD_CharacterCustomizationSaveGame.generated.h"

UCLASS()
class MARINEDEMOLITION_API UMD_CharacterCustomizationSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Customization")
	TArray<FMDCharacterCustomizationSlotData> Slots;
};
