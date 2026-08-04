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
		Resource_Storage_Content,
		"Resource.Storage.Content",
		"물리 보관함에 담을 수 있는 내용물 분류의 공통 루트");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Resource_Storage_Content_WallPiece,
		"Resource.Storage.Content.WallPiece",
		"물리 보관함에 담을 수 있는 벽 조각");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Resource_Storage_Content_Contamination,
		"Resource.Storage.Content.Contamination",
		"물리 보관함에 담을 수 있는 오염물");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Resource_Storage_Content_Sample,
		"Resource.Storage.Content.Sample",
		"물리 보관함에 담을 수 있는 샘플");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Interaction_Pickup_Physics,
		"Interaction.Pickup.Physics",
		"Carry the interacted actor as a world physics object");
	
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
	  Ability_PlaceSupport,
	  "Ability.PlaceSupport",
	  "임시 지지대 설치 어빌리티");
}
