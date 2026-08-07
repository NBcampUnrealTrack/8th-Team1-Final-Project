// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SessionMapConfigDataAsset.generated.h"

/**
 * MultiplayManager에서 Create Session 시 지정된 맵이 존재하지 않는 경우 기본 맵을 설정하기 위한 DataAsset 클래스
 */
UCLASS()
class MARINEDEMOLITION_API USessionMapConfigDataAsset : public UDataAsset
{
	GENERATED_BODY()
	
public:
    // Multiplay Manager에서 Create Session 시 기본으로 사용하는 로비 맵
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Default Map Settings")
	TSoftObjectPtr<UWorld> DefaultLobbyMap;
};
