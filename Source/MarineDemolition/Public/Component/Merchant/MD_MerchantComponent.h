// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Types/Merchant/MD_MerchantTypes.h"
#include "MD_MerchantComponent.generated.h"

class UMD_MerchantSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMD_OnMerchantMirrorUpdated);

/**
 * Subsystem의 상인 상태를 클라이언트에 전달하는 복제 미러. GameState에 부착한다.
 *
 * 값을 직접 바꾸지 않는다. 변경은 항상 Subsystem에서 일어나고 여기로 복사만 된다.
 * Day 트래블로 GameState가 새로 생성되면 BeginPlay에서 Subsystem 값을 다시 읽어 복원한다.
 */

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class MARINEDEMOLITION_API UMD_MerchantComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	UMD_MerchantComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:

	/** 미러 값이 갱신되면 방송. UI가 구독한다. */
	UPROPERTY(BlueprintAssignable, Category = "MD|Merchant")
	FMD_OnMerchantMirrorUpdated OnMerchantStateChanged;

	UFUNCTION(BlueprintPure, Category = "MD|Merchant")
	int32 GetCredits() const { return Credits; }

	UFUNCTION(BlueprintPure, Category = "MD|Merchant")
	int32 GetCurrentDay() const { return CurrentDay; }

	UFUNCTION(BlueprintPure, Category = "MD|Merchant")
	int32 GetQuestCompletions() const { return QuestCompletions; }

	UFUNCTION(BlueprintPure, Category = "MD|Merchant")
	int32 GetActiveQuestReward() const { return ActiveQuestReward; }

	UFUNCTION(BlueprintPure, Category = "MD|Merchant")
	TArray<FMD_MainQuestState> GetMainQuests() const { return MainQuests; }

	UFUNCTION(BlueprintPure, Category = "MD|Merchant")
	TArray<FMD_RepeatQuestState> GetRepeatQuests() const { return RepeatQuests; }

private:
	/** Subsystem 값을 복제 변수로 복사한다. 서버에서만 호출된다. */
	void RefreshFromSubsystem();

	UMD_MerchantSubsystem* GetMerchantSubsystem() const;

	UFUNCTION()
	void OnRep_MerchantState();

	UPROPERTY(ReplicatedUsing = OnRep_MerchantState)
	int32 Credits = 0;

	UPROPERTY(ReplicatedUsing = OnRep_MerchantState)
	int32 CurrentDay = 0;

	UPROPERTY(ReplicatedUsing = OnRep_MerchantState)
	int32 QuestCompletions = 0;

	UPROPERTY(ReplicatedUsing = OnRep_MerchantState)
	int32 ActiveQuestReward = 0;

	UPROPERTY(ReplicatedUsing = OnRep_MerchantState)
	TArray<FMD_MainQuestState> MainQuests;

	UPROPERTY(ReplicatedUsing = OnRep_MerchantState)
	TArray<FMD_RepeatQuestState> RepeatQuests;

	FDelegateHandle SubsystemChangedHandle;
		
};
