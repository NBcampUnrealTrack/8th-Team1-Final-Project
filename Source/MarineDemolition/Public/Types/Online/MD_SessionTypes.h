// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Types/Online/MD_RoomCode.h"
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

	// 방 참여 코드
	UPROPERTY(BlueprintReadOnly, Category = "Marine Demolition|Session")
	FString RoomCode;

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

/**
 * Online Session 생성 설정 값
 */
USTRUCT(BlueprintType, meta = (HasNativeMake = "/Script/MarineDemolition.MD_BlueprintFunctionLibrary.MakeMD_SessionCreateParams"))
struct MARINEDEMOLITION_API FMD_SessionCreateParams
{
	GENERATED_BODY()

public:
	FMD_SessionCreateParams()
	{
		SessionMapPackagePath = TEXT("");
		bPrivateSession = false;
		bAllowJoinByFollow = true;
	}

	FMD_SessionCreateParams(TSoftObjectPtr<UWorld> SessionMap, bool bPrivateSession, bool bAllowJoinByFollow)
		: bPrivateSession(bPrivateSession), bAllowJoinByFollow(bAllowJoinByFollow)
	{
		SessionMapPackagePath = SessionMap.ToSoftObjectPath().GetLongPackageName();
	}

	FMD_SessionCreateParams(const FString& SessionMapPackagePath, bool bPrivateSession, bool bAllowJoinByFollow)
		: SessionMapPackagePath(SessionMapPackagePath), bPrivateSession(bPrivateSession), bAllowJoinByFollow(bAllowJoinByFollow)
	{
	}

public:
	// Session 호스팅 맵의 패키지 경로
	// Ex) /Game/Maps/L_Lobby
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Online Session")
	FString SessionMapPackagePath;

	// 비공개 Session 여부
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Online Session")
	bool bPrivateSession;

	// 친구 목록을 통한 참여하기 허용 여부
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Online Session")
	bool bAllowJoinByFollow;

public:
	inline const bool IsValid() const { return SessionMapPackagePath.IsEmpty() == false; }
};

/**
 * Online Session 검색 설정 값
 */
USTRUCT(BlueprintType, meta = (HasNativeMake = "/Script/MarineDemolition.MD_BlueprintFunctionLibrary.MakeMD_SessionFindParams"))
struct MARINEDEMOLITION_API FMD_SessionFindParams
{
	GENERATED_BODY()

public:
	FMD_SessionFindParams()
	{
		ServerName = TEXT("");
		RoomCode = FMD_RoomCode::EmptyRoomCode;
	}

	FMD_SessionFindParams(const FString& ServerName, const FString& RoomCodeString)
		: ServerName(ServerName)
	{
		RoomCode = FMD_RoomCode::Parse(RoomCodeString);
	}

	FMD_SessionFindParams(const FString& ServerName, const FMD_RoomCode& RoomCode)
		: ServerName(ServerName), RoomCode(RoomCode)
	{
	}

public:
	// 찾고자 하는 서버 이름
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Online Session")
	FString ServerName;

	// 찾고자 하는 방 코드
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Online Session")
	struct FMD_RoomCode RoomCode;
};
