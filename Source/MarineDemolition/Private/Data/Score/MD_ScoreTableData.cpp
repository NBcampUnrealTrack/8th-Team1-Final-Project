// Fill out your copyright notice in the Description page of Project Settings.


#include "Data/Score/MD_ScoreTableData.h"

#include "GameplayTags/MD_GameplayTags.h"

UMD_ScoreTableData::UMD_ScoreTableData()
{
	// 점수 가산
	EventValues.Add(MDGameplayTags::Score_Sorting_Metal, 20);
	EventValues.Add(MDGameplayTags::Score_Sorting_General, 12);
	EventValues.Add(MDGameplayTags::Score_Sorting_Plastic, 16);
	EventValues.Add(MDGameplayTags::Score_Sorting_Contaminated, 32);
	EventValues.Add(MDGameplayTags::Score_Sorting_NonBurnable, 16);
	EventValues.Add(MDGameplayTags::Score_Sorting_Bonus_Tool, 5);
	EventValues.Add(MDGameplayTags::Score_Sorting_Bonus_Workwear, 4);
	EventValues.Add(MDGameplayTags::Score_Sorting_Bonus_Glass, 4);
	EventValues.Add(MDGameplayTags::Score_Sorting_Bonus_Battery, 8);
	EventValues.Add(MDGameplayTags::Score_Demolition_Partition, 15);
	EventValues.Add(MDGameplayTags::Score_Daily_SectorClosed, 40);
	EventValues.Add(MDGameplayTags::Score_Daily_Quest, 80);
	EventValues.Add(MDGameplayTags::Score_Daily_QuestS, 160);
	EventValues.Add(MDGameplayTags::Score_Daily_AllReturned, 25);
	
	// 점수 차감
	EventValues.Add(MDGameplayTags::Score_Penalty_Missort, -12);
	EventValues.Add(MDGameplayTags::Score_Penalty_MissortContaminated, -24);
	EventValues.Add(MDGameplayTags::Score_Penalty_UnsortedRemain, -6);
	EventValues.Add(MDGameplayTags::Score_Penalty_UncollectedRemain, -6);
	EventValues.Add(MDGameplayTags::Score_Penalty_NotReturned, -35);
	EventValues.Add(MDGameplayTags::Score_Penalty_Death, -30);
	EventValues.Add(MDGameplayTags::Score_Penalty_CableCut, -25);
	EventValues.Add(MDGameplayTags::Score_Penalty_PipeBurst, -20);
	EventValues.Add(MDGameplayTags::Score_Penalty_PipeLeakTick, -8);
	
	// 샘플
	SampleValues.Add(EMD_SampleGrade::C, {70, 28, -20, -45});
	SampleValues.Add(EMD_SampleGrade::B, {140, 56, -35, -85});
	SampleValues.Add(EMD_SampleGrade::A, {220, 88, -55, -130});
	SampleValues.Add(EMD_SampleGrade::S, {360, 144, -90, -220});
	}

int32 UMD_ScoreTableData::GetEventValue(FGameplayTag EventTag) const
{
	if (const int32* Value = EventValues.Find(EventTag))
	{
		return *Value;
	}

	UE_LOG(LogTemp, Warning,
			TEXT("MD_ScoreTableData::GetEventValue - 단가 미등록 태그: %s"), *EventTag.ToString());
	return 0;
}

int32 UMD_ScoreTableData::GetSampleValue(EMD_SampleGrade Grade, EMD_SampleScoreType Type) const
{
	const FMD_SampleScoreRow* Row = SampleValues.Find(Grade);
	if (!Row)
	{
		return 0;
	}

	switch (Type)
	{
	case EMD_SampleScoreType::Intact: return Row->Intact;
	case EMD_SampleScoreType::Damaged: return Row->Damaged;
	case EMD_SampleScoreType::Unrecovered: return Row->Unrecovered;
	case EMD_SampleScoreType::Destroyed: return Row->Destroyed;
	default: return 0;
	}
}
