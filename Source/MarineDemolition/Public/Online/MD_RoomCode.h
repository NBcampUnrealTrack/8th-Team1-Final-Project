// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MD_RoomCode.generated.h"

/**
 * 
 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_RoomCode
{
	GENERATED_BODY()
	
public:
	FMD_RoomCode();

public:
	static const FMD_RoomCode EmptyRoomCode;

private:
	// 방 코드의 길이
	static constexpr uint8 CodeLengths[] = { 4, 4, 4 };

	// 방 코드를 분할하여 저장하는 변수
	TArray<FString> RoomCodes;

public:
	// 무작위 방 코드를 생성하는 함수
	static FMD_RoomCode Random();

	// 임의 문자열으로부터 방 코드를 추출하는 함수
	static FMD_RoomCode Parse(FString StringRoomCode);

	// 방 코드를 문자열로 변환하는 함수
	FString ToString() const;

	// 방 코드가 유효한 형식인지 확인하는 함수
	bool IsValid() const;

private:
	// 방 코드에 사용되는 후보 문자 집합을 반환하는 함수
	static const FString& GetCharacterCandidates();
};
