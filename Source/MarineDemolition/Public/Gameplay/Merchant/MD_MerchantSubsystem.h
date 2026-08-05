// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Types/Merchant/MD_MerchantTypes.h"
#include "MD_MerchantSubsystem.generated.h"

/**
 * DayPersistenceSubsystem과 같은 패턴으로, Day전환해도 계약3일동안 크레딧과 퀘스트가 유지되어야하므로
 * Gameinstance에 수명을 둠. 값 변경 처리는 서버에서 하고 클라 복제는 GameState에서 담당
 */

class UMD_SiteDefinition;

UCLASS()
class MARINEDEMOLITION_API UMD_MerchantSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
	
public:
	int32 GetCredits() const { return Credits; }
	int32 GetCurrentDay() const { return CurrentDay; }

	void AddCredits(int32 Amount);

	//계약 시작, 착수금 지급
	void BeginContract(UMD_SiteDefinition* Site, int32 AdvancePayment);
private:

	/** 현재 진행 중인 현장. 반복 퀘스트 풀과 클리어 이력 판정에 쓴다. */
	UPROPERTY()
	TObjectPtr<UMD_SiteDefinition> CurrentSite;

	/** 메인 퀘스트 상태. UPROPERTY라 GC가 Definition 참조를 추적한다. */
	UPROPERTY()
	TArray<FMD_MainQuestState> MainQuests;

	int32 Credits = 0;

	int32 CurrentDay = 0;
};
