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


DECLARE_MULTICAST_DELEGATE(FMD_OnMerchantStateChanged);

class UMD_SiteDefinition;
class UMD_QuestDefinition;
struct FGameplayTag;

UCLASS()
class MARINEDEMOLITION_API UMD_MerchantSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
	
public:

	FMD_OnMerchantStateChanged OnMerchantStateChanged;

public:

	int32 GetCredits() const { return Credits; }
	int32 GetCurrentDay() const { return CurrentDay; }

	//상점 해금 판정에 넘길 값
	int32 GetQuestCompletions() const { return QuestCompletions; }
	//수락 퀘스트 보수
	int32 GetActiveQuestReward() const;

	/** 크레딧 증감. 단독 호출용이며 변경 후 델리게이트 알림. */
	void AddCredits(int32 Amount);

	//계약 시작, 착수금 지급
	void BeginContract(UMD_SiteDefinition* Site, int32 AdvancePayment);

	const TArray<FMD_MainQuestState>& GetMainQuests() const { return MainQuests; }
	const TArray<FMD_RepeatQuestState>& GetRepeatQuests() const { return RepeatQuests; }

	/** 반복 퀘스트 노출 조건. 현장을 한 번 클리어해야 열린다. (기획 09 §4) */
	bool HasClearedCurrentSite() const;

	/** Day 시작 시 호출. 메인 언락 판정 + 반복 카드 재롤. */
	void BeginDay(int32 NewDay);

	/** 계약 클리어 시 호출. 반복 퀘스트 게이트를 여는 이력을 남긴다. */
	void MarkSiteCleared();

	/** 반복 퀘스트 수락. Offered 상태에서만 가능하다. */
	bool TryAcceptRepeatQuest(int32 QuestIndex);

	/** 거절. 페널티 없이 다음 Day 재롤 대상이 된다. */
	bool TryDeclineRepeatQuest(int32 QuestIndex);

	/** 샘플이 모선에 납품될 때 호출한다. 메인·반복 활성 퀘스트를 함께 검사한다. */
	void NotifySampleDelivered(const FGameplayTag& GradeTag, bool bIntact);

	/** 계약 종료 시 남은 활성 반복 퀘스트를 실패로 정리한다. */
	void FinalizeContract();
private:

	/** 현재 진행 중인 현장. 반복 퀘스트 풀과 클리어 이력 판정에 쓴다. */
	UPROPERTY()
	TObjectPtr<UMD_SiteDefinition> CurrentSite;

	/** 메인 퀘스트 상태. UPROPERTY라 GC가 Definition 참조를 추적한다. */
	UPROPERTY()
	TArray<FMD_MainQuestState> MainQuests;

	/** 반복 퀘스트 상태. Day마다 재롤된다. */
	UPROPERTY()
	TArray<FMD_RepeatQuestState> RepeatQuests;

	UPROPERTY()
	TSet<TObjectPtr<UMD_SiteDefinition>> ClearedSites;

	void RerollRepeatQuests();

	/** 방송하지 않는 내부용. 여러 변경을 한 번에 묶을 때 쓴다. */
	void AddCreditsInternal(int32 Amount);

	/** 목표 등급·Intact 조건이 이 퀘스트에 맞는지 판정한다. */
	bool DoesDeliveryMatch(const UMD_QuestDefinition* Definition,const FGameplayTag& GradeTag, bool bIntact) const;

	/** 완료 보상을 공용으로 지급한다. */
	void ApplyQuestReward(const UMD_QuestDefinition* Definition);

	int32 Credits = 0;

	int32 CurrentDay = 0;

	/** 누적 퀘스트 성공 횟수. 계약을 넘어 유지되며 상점 해금 조건에 쓴다. */
	int32 QuestCompletions = 0;
};
