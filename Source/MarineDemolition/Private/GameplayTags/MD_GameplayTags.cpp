// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayTags/MD_GameplayTags.h"

namespace MDGameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Ability_Scrape,
		"Ability.Scrape",
		"Scrape Ability");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Ability_WallCut,
		"Ability.WallCut",
		"격벽 절단 어빌리티");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Ability_WallScan,
		"Ability.WallScan",
		"격벽 스캔 어빌리티");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		State_Swimming,
		"State.Swimming",
		"수영 중. 정밀 작업 어빌리티 차단용");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Interaction_Pickup_Inventory,
		"Interaction.Pickup.Inventory",
		"Move the interacted actor into the player's inventory");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Interaction_Pickup_Physics,
		"Interaction.Pickup.Physics",
		"Carry the interacted actor as a world physics object");
	
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
	  Ability_PlaceSupport,
	  "Ability.PlaceSupport",
	  "임시 지지대 설치 어빌리티");
}
