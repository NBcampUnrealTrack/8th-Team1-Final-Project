// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MD_SessionTypes.generated.h"

USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_SessionInfo
{
	GENERATED_BODY()

public:
	// Listen Server 이름
	UPROPERTY(BlueprintReadOnly, Category = "Marine Demolition|Session")
	FString ServerName;

	// Server Host 이름
	UPROPERTY(BlueprintReadOnly, Category = "Marine Demolition|Session")
	FString OwnerName;

	// Map 이름
	UPROPERTY(BlueprintReadOnly, Category = "Marine Demolition|Session")
	FString MapName;

	// 현재 참가 인원 수
	UPROPERTY(BlueprintReadOnly, Category = "Marine Demolition|Session")
	int32 CurrentPlayers = 0;

	// 최대 참가 인원 수
	UPROPERTY(BlueprintReadOnly, Category = "Marine Demolition|Session")
	int32 MaxPlayers = 0;

	// Server Ping[ms]
	UPROPERTY(BlueprintReadOnly, Category = "Marine Demolition|Session")
	int32 PingInMs = 0;

	// 내부 검색 결과를 나타내는 배열에서의 인덱스
	UPROPERTY(BlueprintReadOnly, Category = "Marine Demolition|Session")
	int32 SearchResultIndex = INDEX_NONE;

public:
	// FOnlineSession 구조체에서 정보를 추출하는 함수
	void ExportSessionInfo(const class FOnlineSession& Session);
};