// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

namespace MDGameplayTags
{
	MARINEDEMOLITION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Scrape);

	/** 상호작용한 대상을 플레이어 인벤토리로 옮기는 픽업 방식 */
	MARINEDEMOLITION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Interaction_Pickup_Inventory);

	/** 상호작용한 대상을 월드의 물리 오브젝트로 운반하는 픽업 방식 */
	MARINEDEMOLITION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Interaction_Pickup_Physics);
}
