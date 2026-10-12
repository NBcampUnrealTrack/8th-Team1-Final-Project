#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MD_FacilityPlayTestGameMode.generated.h"

/** Native test harness, never assigned to a content map or the project defaults. */
UCLASS(NotBlueprintable, Transient, HideDropdown)
class AMD_FacilityPlayTestGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	AMD_FacilityPlayTestGameMode();
};
