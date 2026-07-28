// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayTags/MD_GameplayTags.h"

namespace MDGameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Ability_Scrape,
		"Ability.Scrape",
		"Scrape Ability");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Interaction_Pickup_Inventory,
		"Interaction.Pickup.Inventory",
		"Move the interacted actor into the player's inventory");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Interaction_Pickup_Physics,
		"Interaction.Pickup.Physics",
		"Carry the interacted actor as a world physics object");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Item_Type_Sample,
		"Item.Type.Sample",
		"Recoverable sample item");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Item_Type_WallPiece,
		"Item.Type.WallPiece",
		"Physics piece created from a cut wall");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Item_State_Contaminated,
		"Item.State.Contaminated",
		"Item still contains contamination");
}
