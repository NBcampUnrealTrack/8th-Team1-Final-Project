#include "MD_FacilityPlayTestGameMode.h"
#include "Character/MD_Character.h"
#include "Game/MD_GameStateBase.h"
#include "Player/MD_PlayerState.h"
#include "UObject/ConstructorHelpers.h"

AMD_FacilityPlayTestGameMode::AMD_FacilityPlayTestGameMode()
{
	GameStateClass = AMD_GameStateBase::StaticClass();
	PlayerStateClass = AMD_PlayerState::StaticClass();
	DefaultPawnClass = AMD_Character::StaticClass();
#if WITH_EDITOR
	static ConstructorHelpers::FClassFinder<APawn> PlayerBP(TEXT("/Game/Blueprint/Character/BP_MDPlayerCharacter"));
	if (PlayerBP.Succeeded()) DefaultPawnClass = PlayerBP.Class;
#endif
}
