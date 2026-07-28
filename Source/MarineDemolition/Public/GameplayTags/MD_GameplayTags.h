// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

namespace MDGameplayTags
{
	MARINEDEMOLITION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Scrape);

	/** 상호작용 시 월드 액터를 플레이어 인벤토리로 옮기는 대상 */
	MARINEDEMOLITION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Interaction_Pickup_Inventory);

	/** 상호작용 시 월드의 물리 오브젝트 상태로 운반하는 대상 */
	MARINEDEMOLITION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Interaction_Pickup_Physics);

	/** 조사 임무에서 회수하는 샘플 아이템 */
	MARINEDEMOLITION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Item_Type_Sample);

	/** 절단된 격벽에서 생성된 물리 조각 */
	MARINEDEMOLITION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Item_Type_WallPiece);

	/** 오염이 남아 있는 아이템 상태 */
	MARINEDEMOLITION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Item_State_Contaminated);
}
