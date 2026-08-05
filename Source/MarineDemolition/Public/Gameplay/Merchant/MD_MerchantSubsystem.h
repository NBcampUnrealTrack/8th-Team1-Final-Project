// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MD_MerchantSubsystem.generated.h"

/**
 * DayPersistenceSubsystem과 같은 패턴으로, Day전환해도 계약3일동안 크레딧과 퀘스트가 유지되어야하므로
 * Gameinstance에 수명을 둠. 값 변경 처리는 서버에서 하고 클라 복제는 GameState에서 담당
 */
UCLASS()
class MARINEDEMOLITION_API UMD_MerchantSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
	
public:
	int32 GetCredits() const { return Credits; }

	void AddCredits(int32 Amount);
private:
	int32 Credits = 0;
};
