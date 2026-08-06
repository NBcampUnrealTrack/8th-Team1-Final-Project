// Fill out your copyright notice in the Description page of Project Settings.


#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "Data/Merchant/MD_QuestDefinition.h"
#include "Data/Merchant/MD_SiteDefinition.h"

void UMD_MerchantSubsystem::AddCredits(int32 Amount)
{
	//크레딧은 최소 0
	if (Amount == 0)
	{
		return;
	}

	Credits = FMath::Max(0, Credits + Amount);
}

void UMD_MerchantSubsystem::BeginContract(UMD_SiteDefinition* Site, int32 AdvancePayment)
{
	if (!IsValid(Site))
	{
		return;
	}

	CurrentSite = Site;
	CurrentDay = 0;

	//메인퀘스트 반복 방지
	MainQuests.Reset();

	for (int32 Index = 0; Index < Site->MainQuestLine.Num(); ++Index)
	{
		UMD_QuestDefinition* Definition = Site->MainQuestLine[Index];
		if (!IsValid(Definition))
		{
			// 애셋 배열에 빈 슬롯이 있으면 건너뛴다. 다만 Day 번호가 밀리므로 경고를 남긴다.
			UE_LOG(LogTemp, Warning, TEXT("[Merchant] MainQuestLine[%d]가 비어 있다"), Index);
			continue;
		}

		FMD_MainQuestState NewMain;
		NewMain.Definition = Definition;
		NewMain.UnlockDay = Index + 1;   // 인덱스 0 → Day 1
		NewMain.Status = EMD_MainQuestStatus::Locked;

		MainQuests.Add(NewMain);

	}

	AddCredits(AdvancePayment);

	UE_LOG(LogTemp, Log, TEXT("[Merchant] 계약 시작: %s (메인 %d건, 착수금 %d)"), *Site->SiteName.ToString(), MainQuests.Num(), AdvancePayment);

}
