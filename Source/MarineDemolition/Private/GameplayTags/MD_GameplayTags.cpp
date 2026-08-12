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
		Resource_Storage_Content_Barnacle,
		"Resource.Storage.Content.Barnacle",
		"물리 보관함에 담을 수 있는 따개비");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Resource_Storage_Content_Sample,
		"Resource.Storage.Content.Sample",
		"물리 보관함에 담을 수 있는 샘플");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Resource_Storage_Content_Trash,
		"Resource.Storage.Content.Trash",
		"물리 보관함에 담을 수 있는 각종 쓰레기");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Interaction_Pickup_Physics,
		"Interaction.Pickup.Physics",
		"Carry the interacted actor as a world physics object");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
	  Ability_PlaceSupport,
	  "Ability.PlaceSupport",
	  "임시 지지대 설치 어빌리티");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Resource_Sample_Grade,
		"Resource.Sample.Grade",
		"샘플 등급 분류의 공통 루트");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Resource_Sample_Grade_C,
		"Resource.Sample.Grade.C",
		"C급 샘플. 퀘스트 보상 80 기준");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Resource_Sample_Grade_B,
		"Resource.Sample.Grade.B",
		"B급 샘플. 퀘스트 보상 160 기준");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Resource_Sample_Grade_A,
		"Resource.Sample.Grade.A",
		"A급샘플. 퀘스트 보상 280 기준");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Resource_Sample_Grade_S,
		"Resource.Sample.Grade.S",
		"S급샘플. 퀘스트 보상 450 기준");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Score_Sorting_Metal,
		"Score.Sorting.Metal",
		"금속 정분류");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Score_Sorting_General,
		"Score.Sorting.General",
		"일반쓰레기 정분류");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Score_Sorting_Plastic,
		"Score.Sorting.Plastic",
		"플라스틱 정분류");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Score_Sorting_Contaminated,
		"Score.Sorting.Contaminated",
		"오염된 쓰레기 정분류");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Score_Sorting_NonBurnable,
		"Score.Sorting.NonBurnable",
		"안타는 쓰레기 정분류");


	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Score_Sorting_Bonus_Tool,
		"Score.Sorting.Bonus.Tool",
		"폐공구 보너스");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Score_Sorting_Bonus_Workwear,
		"Score.Sorting.Bonus.Workwear",
		"작업복 보너스");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Score_Sorting_Bonus_Glass,
		"Score.Sorting.Bonus.Glass",
		"유리 보너스");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Score_Sorting_Bonus_Battery,
		"Score.Sorting.Bonus.Battery",
		"폐전지 보너스");


	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Score_Demolition_Partition,
		"Score.Demolition.Partition",
		"칸막이벽 철거 성공");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Score_Demolition_Strut,
		"Score.Demolition.Strut",
		"내력벽 철거 성공, 지지대 설치 상태");


	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Score_Daily_SectorClosed,
		"Score.Daily.SectorClosed",
		"구역 마감");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Score_Daily_Quest,
		"Score.Daily.Quest",
		"상인 퀘스트 성공");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Score_Daily_QuestS,
		"Score.Daily.QuestS",
		"S급 상인 퀘스트 성공");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Score_Daily_AllReturned,
		"Score.Daily.AllReturned",
		"전원 18:00 전 복귀");


	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Score_Penalty_Missort,
		"Score.Penalty.Missort",
		"오분류");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Score_Penalty_MissortContaminated,
		"Score.Penalty.MissortContaminated",
		"오염계 오분류");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Score_Penalty_UnsortedRemain,
		"Score.Penalty.UnsortedRemain",
		"모선 미분류 잔여");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Score_Penalty_UncollectedRemain,
		"Score.Penalty.UncollectedRemain",
		"현장 미수거");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Score_Penalty_Collapse_Unsupported,
		"Score.Penalty.Collapse.Unsupported",
		"내력 무지지 절단 천장 붕괴");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Score_Penalty_Collapse_Supported,
		"Score.Penalty.Collapse.Supported",
		"지지대 있어도 붕괴");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Score_Penalty_NotReturned,
		"Score.Penalty.NotReturned",
		"18:00 미복귀 인당");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Score_Penalty_Death,
		"Score.Penalty.Death",
		"사망 후 부활");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Score_Penalty_Warning,
		"Score.Penalty.Warning",
		"점검 실패 경고");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Score_Penalty_CableCut,
		"Score.Penalty.CableCut",
		"전선 절단, 섹터 암전");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Score_Penalty_PipeBurst,
		"Score.Penalty.PipeBurst",
		"수도관 파열");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Score_Penalty_PipeLeakTick,
		"Score.Penalty.PipeLeakTick",
		"수도 미밀봉 오염 확산 30초당");


	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		State_Swimming,
		"State.Swimming",
		"수영 중. 정밀 작업 어빌리티 차단용");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		State_Climbing,
		"State.Climbing",
		"사다리 등반 중");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		State_Injured,
		"State.Injured",
		"부상 상태. HP 30 이하");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		State_Dead,
		"State.Dead",
		"사망 상태. HP 0");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		State_Submerged,
		"State.Submerged",
		"완전 잠수 상태. 산소 소모 GE의 조건");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		State_Oxygen_Depleted,
		"State.Oxygen.Depleted",
		"산소 0 상태. 잠수중이면 질식 피해 -15/s");

	// For Save Data
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Save_Data_GameState,
		"Save.Data.GameState",
		"저장된 게임 데이터로부터 점수, 인게임 시간 등의 정보를 얻는 키");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Save_Data_Merchant,
		"Save.Data.Merchant",
		"저장된 게임 데이터로부터 상인 관련 정보를 얻는 키");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Save_Data_Player,
		"Save.Data.Player",
		"저장된 게임 데이터로부터 플레이어 정보를 얻는 키");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Save_Data_World,
		"Save.Data.World",
		"저장된 게임 데이터로부터 맵의 변경 사항에 대한 정보를 얻는 키");
}
