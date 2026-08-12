// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Types/Merchant/MD_MerchantTypes.h"
#include "UI/Merchant/MD_MerchantUITypes.h"
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

	// 서버 원장에서 복제된 호감도를 UI와 상점 해금 판정에 제공하는 함수
	UFUNCTION(BlueprintPure, Category = "MD|Merchant")
	float GetAffinity() const { return Affinity; }

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

	// 모든 클라이언트가 같은 상점 해금 상태를 사용하기 위한 복제 호감도
	UPROPERTY(ReplicatedUsing = OnRep_MerchantState)
	float Affinity = 0.0f;

	UPROPERTY(ReplicatedUsing = OnRep_MerchantState)
	int32 QuestCompletions = 0;

	UPROPERTY(ReplicatedUsing = OnRep_MerchantState)
	int32 ActiveQuestReward = 0;

	UPROPERTY(ReplicatedUsing = OnRep_MerchantState)
	TArray<FMD_MainQuestState> MainQuests;

	UPROPERTY(ReplicatedUsing = OnRep_MerchantState)
	TArray<FMD_RepeatQuestState> RepeatQuests;

	FDelegateHandle SubsystemChangedHandle;
		

public:
	/**
	 * 상인 메뉴 위젯에 넘길 정보를 만든다.
	 * MD_MerchantMenuWidget::SetMenuInfo()에 그대로 전달하면 된다.
	 *
	 * 아직 시스템이 없는 항목(대출)은 구조체 기본값을 그대로 둔다.
	 * 기본값이 기획 07 §5 확정값이라 0으로 덮으면 화면이 더 이상해진다.
	 */
	UFUNCTION(BlueprintPure, Category = "MD|Merchant")
	FMD_MerchantMenuInfo BuildMenuInfo() const;
};
