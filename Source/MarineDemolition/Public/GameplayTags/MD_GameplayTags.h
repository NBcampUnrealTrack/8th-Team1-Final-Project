// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

namespace MDGameplayTags
{
	MARINEDEMOLITION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Scrape);
	MARINEDEMOLITION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_WallCut);
	MARINEDEMOLITION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_WallScan);

	MARINEDEMOLITION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Swimming);

	/** 물리 보관함에 담을 수 있는 내용물 분류의 공통 루트 */
	MARINEDEMOLITION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Resource_Storage_Content);

	/** 물리 보관함에 담을 수 있는 벽 조각 */
	MARINEDEMOLITION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Resource_Storage_Content_WallPiece);

	/** 물리 보관함에 담을 수 있는 오염물 */
	MARINEDEMOLITION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Resource_Storage_Content_Contamination);

	/** 물리 보관함에 담을 수 있는 샘플 */
	MARINEDEMOLITION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Resource_Storage_Content_Sample);

	/** 상호작용한 대상을 월드의 물리 오브젝트로 운반하는 픽업 방식 */
	MARINEDEMOLITION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Interaction_Pickup_Physics);
	
	MARINEDEMOLITION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_PlaceSupport);

	/*샘플 등급 분류. 퀘스트 목표 달성 여부 확인*/
	MARINEDEMOLITION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Resource_Sample_Grade);

	MARINEDEMOLITION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Resource_Sample_Grade_C);
	MARINEDEMOLITION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Resource_Sample_Grade_B);
	MARINEDEMOLITION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Resource_Sample_Grade_A);
	MARINEDEMOLITION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Resource_Sample_Grade_S);
}
