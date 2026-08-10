// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MD_BlueprintFunctionLibrary.generated.h"

// 새 게임 생성 노드가 반환하는 확장 게임 데이터 구조 전방 선언
struct FMD_GameDataVer2;

/**
 * 
 */
UCLASS()
class MARINEDEMOLITION_API UMD_BlueprintFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:
	// Clipboard에 임의 문자열을 복사하는 함수
	UFUNCTION(BlueprintCallable, Category = "System|Clipboard")
	static void CopyTextToClipboard(const FString& CopyString);

	// Clipboard로부터 복사된 문자열을 얻는 함수
	UFUNCTION(BlueprintCallable, Category = "System|Clipboard")
	static FString PasteTextFromClipboard();

	UFUNCTION(BlueprintPure, Category = "Online Session", meta = (NativeMakeFunc, DisplayName = "Make MD_Session Create Params"))
	static FMD_SessionCreateParams MakeMD_SessionCreateParams(TSoftObjectPtr<UWorld> SessionMap, bool bPrivateSession, bool bAllowJoinByFollow);

	UFUNCTION(BlueprintPure, Category = "Online Session", meta = (NativeMakeFunc, DisplayName = "Make MD_Session Find Params"))
	static FMD_SessionFindParams MakeMD_SessionFindParams(FString ServerName, FString RoomCodeString);

	UFUNCTION(BlueprintPure, Category = "Game Data", meta = (NativeMakeFunc, DisplayName = "Make MD_GameData"))
	static struct FMD_GameDataVer2 MakeMD_GameData(TSoftObjectPtr<UWorld> GameMap);
};
